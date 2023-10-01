/*
 *  linux/fs/buffer.c
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 *  'buffer.c' implements the buffer-cache functions. Race-conditions have
 * been avoided by NEVER letting a interrupt change a buffer (except for the
 * data, of course), but instead letting the caller do it. NOTE! As interrupts
 * can wake up a caller, some cli-sti sequences are needed to check for
 * sleep-on-calls. These should be extremely quick, though (I hope).
 */

/*
 * NOTE! There is one discordant note here: checking floppies for
 * disk change. This is where it fits best, I think, as it should
 * invalidate changed floppy-disk-caches.
 */

#include <stdarg.h>
 
#include <linux/config.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <asm/system.h>
#include <asm/io.h>

extern int end; 													// 由链接程序 ld 生成的指向程序末端的变量-
struct buffer_head * start_buffer = (struct buffer_head *) &end;	
struct buffer_head * hash_table[NR_HASH]; 							// 存储已使用的块的hash表
static struct buffer_head * free_list; 								// 空闲链表头指针
static struct task_struct * buffer_wait = NULL;						// 等待缓冲区使用完的解锁的指针
int NR_BUFFERS = 0;													// 高速缓冲区中，缓冲块个数

/**
 * 等待指定缓冲区解锁
 * @param bh 需要被等待缓冲区的头指针
*/
static inline void wait_on_buffer(struct buffer_head * bh)
{
	cli(); 						// 关闭中断
	while (bh->b_lock) 			// 如果已被上锁，进程进行睡眠，等待其解锁
		sleep_on(&bh->b_wait);
	sti(); 						// 开启中断
}

/**
 * 将所有i节点与缓冲同步到存储设备
*/
int sys_sync(void)
{
	int i;
	struct buffer_head * bh;

	sync_inodes();		/* write out inodes into buffers */
	bh = start_buffer;
	// 将所有脏块写回块设备
	for (i=0 ; i<NR_BUFFERS ; i++,bh++) {
		wait_on_buffer(bh);
		if (bh->b_dirt)
			ll_rw_block(WRITE,bh);
	}
	return 0;
}

/**
 * 将属于指定设备的缓冲区同步回块设备
 * @param dev 设备号
 * @return 未出错返回0
*/
int sync_dev(int dev)
{
	int i;
	struct buffer_head * bh;

	bh = start_buffer;
	// 遍历所有缓冲，写回所有与指定设备号匹配的缓冲
	for (i=0 ; i<NR_BUFFERS ; i++,bh++) {
		if (bh->b_dev != dev)
			continue;
		wait_on_buffer(bh);		// 等待匹配缓冲区解锁
		// 将脏块写回块设备
		if (bh->b_dev == dev && bh->b_dirt)
			ll_rw_block(WRITE,bh);
	}
	sync_inodes();				// 将 i 节点数据同步回块设备
	bh = start_buffer;			// 重新遍历写回脏块
	for (i=0 ; i<NR_BUFFERS ; i++,bh++) {
		if (bh->b_dev != dev)
			continue;
		wait_on_buffer(bh);
		if (bh->b_dev == dev && bh->b_dirt)
			ll_rw_block(WRITE,bh);
	}
	return 0;
}

/**
 * 让指定设备在高速缓冲区中失效
 * @param dev 设备号
*/
void inline invalidate_buffers(int dev)
{
	int i;
	struct buffer_head * bh;

	bh = start_buffer;
	// 遍历缓冲区头节点数组，将所有相关缓冲 更新标志与已修改标志置为 0 
	for (i=0 ; i<NR_BUFFERS ; i++,bh++) {
		if (bh->b_dev != dev)
			continue;
		wait_on_buffer(bh);
		if (bh->b_dev == dev)
			bh->b_uptodate = bh->b_dirt = 0;
	}
}

/*
 * This routine checks whether a floppy has been changed, and
 * invalidates all buffer-cache-entries in that case. This
 * is a relatively slow routine, so we have to try to minimize using
 * it. Thus it is called only upon a 'mount' or 'open'. This
 * is the best way of combining speed and utility, I think.
 * People changing diskettes in the middle of an operation deserve
 * to loose :-)
 *
 * NOTE! Although currently this is only for floppies, the idea is
 * that any additional removable block-device will use this routine,
 * and that mount/open needn't know that floppies/whatever are
 * special.
 */
/**
 * 检查一个软盘是否已经被更换，如果已经被更换的话就使高速缓冲中与该软驱对应的缓冲区无效
 * @param dev 设备号
*/
void check_disk_change(int dev)
{
	int i;

	if (MAJOR(dev) != 2) 					// 不是软盘设备不需要检测
		return;
	if (!floppy_change(dev & 0x03)) 		// 软盘未更换直接返回
		return;
	// 软盘更换后，遍历超级块数组，释放设本设备的超级块所占用的相关内存
	for (i=0 ; i<NR_SUPER ; i++)
		if (super_block[i].s_dev == dev)
			put_super(super_block[i].s_dev);
	invalidate_inodes(dev); 				// 使i节点占用的缓冲区失效
	invalidate_buffers(dev); 				// 使数据占用的缓冲区失效
}

#define _hashfn(dev,block) (((unsigned)(dev^block))%NR_HASH) 	// 对指定 dev 与 block 进行hash
#define hash(dev,block) hash_table[_hashfn(dev,block)] 			// 查找指定 dev 与 block 在hash表中所在的项

/**
 * 从 hash 队列和空闲缓冲队列中移走指定缓冲块
 * @param bh 将要移除的缓冲块指针
*/
static inline void remove_from_queues(struct buffer_head * bh)
{
/* remove from hash-queue */
	// 将该缓冲块从哈西表中移除
	if (bh->b_next)
		bh->b_next->b_prev = bh->b_prev;
	if (bh->b_prev)
		bh->b_prev->b_next = bh->b_next;
	if (hash(bh->b_dev,bh->b_blocknr) == bh)
		hash(bh->b_dev,bh->b_blocknr) = bh->b_next;
/* remove from free list */
	// 将该缓冲块从空闲列表中移除
	if (!(bh->b_prev_free) || !(bh->b_next_free))
		panic("Free block list corrupted");
	bh->b_prev_free->b_next_free = bh->b_next_free;
	bh->b_next_free->b_prev_free = bh->b_prev_free;
	if (free_list == bh)
		free_list = bh->b_next_free;
}

/**
 * 将指定缓冲区放到空闲缓冲区尾并放入hash_table中
 * @param bh 将要插入的缓冲块指针
*/
static inline void insert_into_queues(struct buffer_head * bh)
{
/* put at end of free list */
	// 将该缓冲块插入到空闲列表中末尾
	bh->b_next_free = free_list;
	bh->b_prev_free = free_list->b_prev_free;
	free_list->b_prev_free->b_next_free = bh;
	free_list->b_prev_free = bh;
/* put the buffer in new hash-queue if it has a device */
	// 将该缓冲块加入到哈西表
	bh->b_prev = NULL;
	bh->b_next = NULL;
	if (!bh->b_dev)
		return;
	bh->b_next = hash(bh->b_dev,bh->b_blocknr);
	hash(bh->b_dev,bh->b_blocknr) = bh;
	bh->b_next->b_prev = bh;
}

/**
 * 在高速缓存中寻找对应的缓冲区块，没找到返回 null
 * @param dev 对应驱动器号
 * @param block 对应的分区号
 * @return 相应缓冲区的头指针
*/
static struct buffer_head * find_buffer(int dev, int block)
{		
	struct buffer_head * tmp;

	// 遍历hash 表中指定项的列表，查找指定头节点指针
	for (tmp = hash(dev,block) ; tmp != NULL ; tmp = tmp->b_next)
		if (tmp->b_dev==dev && tmp->b_blocknr==block)
			return tmp;
	// 没找到返回 NULL
	return NULL;
}

/*
 * Why like this, I hear you say... The reason is race-conditions.
 * As we don't lock buffers (unless we are readint them, that is),
 * something might happen to it while we sleep (ie a read-error
 * will force it bad). This shouldn't really happen currently, but
 * the code is ready.
 */
/**
 * 查询 hashTable 中是否存在指定缓冲，存在查看是否上锁，等待解锁
 * @param dev 对应驱动器号
 * @param block 对应的分区号
 * @return 相应缓冲区的头指针
*/
struct buffer_head * get_hash_table(int dev, int block)
{
	struct buffer_head * bh;

	for (;;) {
		if (!(bh=find_buffer(dev,block)))
			return NULL; 					// 不存在返回 NULL
		bh->b_count++; 						// 指向该缓冲区数 +1
		wait_on_buffer(bh); 				// 等待该块解锁
											// 判断该缓冲区是否还是为指定设备与快的
		if (bh->b_dev == dev && bh->b_blocknr == block)
			return bh;
		bh->b_count--;
	}
}

/*
 * Ok, this is getblk, and it isn't very clear, again to hinder
 * race-conditions. Most of the code is seldom used, (ie repeating),
 * so it should be much more efficient than it looks.
 *
 * The algoritm is changed: hopefully better, and an elusive bug removed.
 */
#define BADNESS(bh) (((bh)->b_dirt<<1)+(bh)->b_lock)	// 同时判断缓冲区的脏与锁定标志
/**
 * 取高速缓冲区中指定的缓冲区
 * 检查指定的缓冲区是否在高速缓冲区中。如果不在，就需要在高速缓冲区中建立一个对应的新项
 * @param dev 对应驱动器号
 * @param block 对应的分区号
 * @return 相应缓冲区的头指针
*/
struct buffer_head * getblk(int dev,int block)
{
	struct buffer_head * tmp, * bh;

repeat:
	if (bh = get_hash_table(dev,block))			// 存在该高速缓冲时，直接返回该缓冲头
		return bh;
	tmp = free_list;							// 为指定程序获取空缓冲区块
	do {
		if (tmp->b_count) 						// 临时缓冲头正在被使用时跳过
			continue;
		// bh为空或者tmp所指缓冲头的标志（修改、锁定）权重小于bh头标志位，则让 bh 指向 tmp 缓冲区头；
		// 该 tmp 缓冲区表明既没有被修改过，也没有被锁定，则说明已为指定设备上的块以获取到对应的高速缓冲区，退出循环
		if (!bh || BADNESS(tmp)<BADNESS(bh)) {
			bh = tmp;
			if (!BADNESS(tmp))
				break;
		}
/* and repeat until we find something good */
	} while ((tmp = tmp->b_next_free) != free_list);
	// 所有缓冲区都正在被使用，则睡眠，等待有空闲的缓冲区使用
	if (!bh) {
		sleep_on(&buffer_wait);
		goto repeat;
	}
	wait_on_buffer(bh); 							// 等待该缓冲区解锁
	if (bh->b_count)								// 该缓冲区还有程序使用，重新寻找可用
		goto repeat;
	while (bh->b_dirt) {							// 该缓冲区被修改，需要将数据写回盘，并在此等待解锁
		sync_dev(bh->b_dev);
		wait_on_buffer(bh);
		if (bh->b_count)
			goto repeat;
	}
/* NOTE!! While we slept waiting for this block, somebody else might */
/* already have added "this" block to the cache. check it */
	if (find_buffer(dev,block))
		goto repeat;
/* OK, FINALLY we know that this buffer is the only one of it's kind, */
/* and that it's unused (b_count=0), unlocked (b_lock=0), and clean */
	// 先将寻找的缓冲区移除出旧hash表中
	bh->b_count=1;
	bh->b_dirt=0;
	bh->b_uptodate=0;
	remove_from_queues(bh);
	// 然后将寻找的缓冲区插入到新 hashtable 项中
	bh->b_dev=dev;
	bh->b_blocknr=block;
	insert_into_queues(bh);
	return bh;
}

/**
 * 释放指定缓存块
 * @param buf 缓存块头
*/
void brelse(struct buffer_head * buf)
{
	if (!buf)
		return;
	wait_on_buffer(buf); 						// 等待缓存块解锁
	if (!(buf->b_count--)) 						// 缓冲块引用计数减一
		panic("Trying to free free buffer");
	wake_up(&buffer_wait); 						// 唤醒等待缓冲区空闲的缓存
}

/*
 * bread() reads a specified block and returns the buffer that contains
 * it. It returns NULL if the block was unreadable.
 */
/**
 * 从设备上读取指定数据块并返回含有数据的缓冲区，如果指定的块不为空则返回NULL
 * @param dev 指定驱动设备
 * @param block 指定分区
*/
struct buffer_head * bread(int dev,int block)
{
	struct buffer_head * bh;

	if (!(bh=getblk(dev,block))) 				// 获取缓冲区中指定块
		panic("bread: getblk returned NULL\n");
	if (bh->b_uptodate) 						// 数据已经更新过，直接返回
		return bh;
	ll_rw_block(READ,bh); 						// 未更新过时，添加读取请求
	wait_on_buffer(bh); 						// 等待缓冲区解锁
	if (bh->b_uptodate) 						// 更新成功后返回
		return bh;
	brelse(bh); 								// 否则表明操作失败，然后释放缓存
	return NULL;
}

/**
 * 复制内存块
 * @param from 原地址
 * @param to 目标地址
*/
#define COPYBLK(from,to) \
__asm__("cld\n\t" \
	"rep\n\t" \
	"movsl\n\t" \
	::"c" (BLOCK_SIZE/4),"S" (from),"D" (to) \
	:"cx","di","si")

/**
 * 一次将最多四个缓冲块内容读取到内存指定位置
 * @param address 保存数据内存地址
 * @param dev 设备号
 * @param b[4] 块号数组
*/
void bread_page(unsigned long address,int dev,int b[4])
{
	struct buffer_head * bh[4];
	int i;

	// 将 b[4] 中指定的四个块对应的数据保存到 bh[4] 对应的缓冲区中
	for (i=0 ; i<4 ; i++)
		if (b[i]) {
			if (bh[i] = getblk(dev,b[i]))
				if (!bh[i]->b_uptodate)
					ll_rw_block(READ,bh[i]);
		} else
			bh[i] = NULL;
	// 将 bh[i] 中的数据，按照顺序复制到指定内存地址处
	for (i=0 ; i<4 ; i++,address += BLOCK_SIZE)
		if (bh[i]) {
			wait_on_buffer(bh[i]);
			if (bh[i]->b_uptodate)
				COPYBLK((unsigned long) bh[i]->b_data,address);
			brelse(bh[i]);				// 释放缓冲区对应内存
		}
}

/*
 * Ok, breada can be used as bread, but additionally to mark other
 * blocks for reading as well. End the argument list with a negative
 * number.
 */
/**
 * 从指定设备读取一些指定块，成功时返回第一块的缓冲区头指针
 * @param dev 设备号
 * @param first 读取第一个块的编号
*/
struct buffer_head * breada(int dev,int first, ...)
{
	va_list args;
	struct buffer_head * bh, *tmp;

	va_start(args,first); // 取可变参数表中第一个参数
	// 获取第一个参数对应的缓冲块
	if (!(bh=getblk(dev,first)))
		panic("bread: getblk returned NULL\n");
	if (!bh->b_uptodate)
		ll_rw_block(READ,bh);
	// 循环将剩余所有块预读取到缓冲之中，但不引用
	while ((first=va_arg(args,int))>=0) {
		tmp=getblk(dev,first);
		if (tmp) {
			if (!tmp->b_uptodate) // 预读取
				ll_rw_block(READA,bh);
			tmp->b_count--; // 取消本次引用
		}
	}
	// 所有块都读取到缓冲中，等待第一块缓冲解锁
	va_end(args);
	wait_on_buffer(bh);
	if (bh->b_uptodate) // 该块更新后返回
		return bh;
	brelse(bh); // 否则释放该块内存，返回 NULL
	return (NULL);
}

/**
 * 初始化缓冲区函数
 * @param buffer_end 缓冲区尾地址
*/
void buffer_init(long buffer_end)
{
	// 第一个缓冲头指向主内存开始位置
	struct buffer_head * h = start_buffer;
	void * b;
	int i;
	// 当内存尾部小于 1 M 时，640～1Mb 为显示内存与 BIOS 使用，因此实际可使用内存高端为 640 KB
	if (buffer_end == 1<<20)
		b = (void *) (640*1024);
	else
		b = (void *) buffer_end;
	// 循环初始化所有内存，做到每个块都有对应的缓冲头
	while ( (b -= BLOCK_SIZE) >= ((void *) (h+1)) ) {
		h->b_dev = 0; // 设备号
		h->b_dirt = 0; // 脏标志
		h->b_count = 0; // 引用计数
		h->b_lock = 0; // 锁定标志
		h->b_uptodate = 0; // 更新标志
		h->b_wait = NULL; // 等待解锁使用进程
		h->b_next = NULL; // hash 表中下一项
		h->b_prev = NULL; // hash 表中上一项
		h->b_data = (char *) b; // data指针指向数据块 b
		h->b_prev_free = h-1; // 初始化时所有块都是空闲 因此直接
		h->b_next_free = h+1;
		h++;
		NR_BUFFERS++; //缓冲区块数累计
		// 当b 递减到 1 kb 时，跳过显示内存与 BIOS 所占内存
		if (b == (void *) 0x100000)
			b = (void *) 0xA0000;
	}
	h--;
	// 将空闲头第一项前一项空闲指向最后一项，最后一项下一项指向 第一项，形成空闲循环链表 
	free_list = start_buffer; 
	free_list->b_prev_free = h;
	h->b_next_free = free_list;
	// 初始化hash表所有表项为空
	for (i=0;i<NR_HASH;i++)
		hash_table[i]=NULL;
}	
