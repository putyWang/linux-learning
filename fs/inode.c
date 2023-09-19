/*
 *  linux/fs/inode.c
 *
 *  (C) 1991  Linus Torvalds
 */

#include <string.h>
#include <sys/stat.h>

#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <asm/system.h>

struct m_inode inode_table[NR_INODE]={{0,},}; // i 节点数组

static void read_inode(struct m_inode * inode);
static void write_inode(struct m_inode * inode);

/**
 * 等待指定 i 节点解锁
 * @param inode i 节点指针
*/
static inline void wait_on_inode(struct m_inode * inode)
{
	cli();
	while (inode->i_lock)
		sleep_on(&inode->i_wait);
	sti();
}

/**
 * 为指定 i 节点上锁
 * @param inode 需要上锁的 i 节点指针
*/
static inline void lock_inode(struct m_inode * inode)
{
	cli();
	while (inode->i_lock)
		sleep_on(&inode->i_wait);
	inode->i_lock=1;
	sti();
}

static inline void unlock_inode(struct m_inode * inode)
{
	inode->i_lock=0;
	wake_up(&inode->i_wait);
}

/**
 * 使 i 节点占用的缓冲区失效
 * @param dev 设备号
*/
void invalidate_inodes(int dev)
{
	int i;
	struct m_inode * inode;

	inode = 0+inode_table;
	// 遍历 i 节点数组，将对应设备号与修改标志清空
	for(i=0 ; i<NR_INODE ; i++,inode++) {
		wait_on_inode(inode);
		if (inode->i_dev == dev) {
			// 该i节点还在使用提示一下
			if (inode->i_count)
				printk("inode in use on removed disk\n\r");
			inode->i_dev = inode->i_dirt = 0;
		}
	}
}

/**
 * 将所有 i 节点同步到存储器
*/
void sync_inodes(void)
{
	int i;
	struct m_inode * inode;

	inode = 0+inode_table;
	for(i=0 ; i<NR_INODE ; i++,inode++) {
		wait_on_inode(inode);
		if (inode->i_dirt && !inode->i_pipe)
			write_inode(inode);
	}
}

/**
 * 文件数据块映射到盘块的操作
 * @param inode 文件的i节点
 * @param block 文件中的数据块号
 * @param create 创建标志
 * @return 指定块号
*/
static int _bmap(struct m_inode * inode,int block,int create)
{
	struct buffer_head * bh;
	int i;

	// 块号 < 0 或者 大于 直接块数 + 加间接块数 + 两次间接块数 则直接死机
	if (block<0)
		panic("_bmap: block<0");
	if (block >= 7+512+512*512)
		panic("_bmap: block>big");
	// 如果是直接块
	if (block<7) {
		// 创建标志置位且对应数据块为 0，则向磁盘申请一磁盘块，并将其填入磁盘块中
		if (create && !inode->i_zone[block])
			if (inode->i_zone[block]=new_block(inode->i_dev)) {
				// 更新i节点参数
				inode->i_ctime=CURRENT_TIME;
				inode->i_dirt=1;
			}
		// 返回新创建的或者已存在的块
		return inode->i_zone[block];
	}
	block -= 7;
	// 间接块
	if (block<512) {
		// 创建标志置位且间接数据块 7 为 0，则向磁盘申请一磁盘块，并将其填入磁盘块中
		if (create && !inode->i_zone[7])
			if (inode->i_zone[7]=new_block(inode->i_dev)) {
				inode->i_dirt=1;
				inode->i_ctime=CURRENT_TIME;
			}
		if (!inode->i_zone[7])
			return 0;
		// 将间接块 7 数据读取到缓冲中
		if (!(bh = bread(inode->i_dev,inode->i_zone[7])))
			return 0;
		// 获取间接块 指向数据的指针
		i = ((unsigned short *) (bh->b_data))[block];
		// 与一次块处理方式一致
		if (create && !i)
			if (i=new_block(inode->i_dev)) {
				((unsigned short *) (bh->b_data))[block]=i;
				bh->b_dirt=1;
			}
		brelse(bh); // 释放指定缓冲
		return i;
	}
	// 获取二次间接块
	block -= 512;
	if (create && !inode->i_zone[8])
		if (inode->i_zone[8]=new_block(inode->i_dev)) {
			inode->i_dirt=1;
			inode->i_ctime=CURRENT_TIME;
		}
	if (!inode->i_zone[8])
		return 0;
	if (!(bh=bread(inode->i_dev,inode->i_zone[8])))
		return 0;
	i = ((unsigned short *)bh->b_data)[block>>9];
	if (create && !i)
		if (i=new_block(inode->i_dev)) {
			((unsigned short *) (bh->b_data))[block>>9]=i;
			bh->b_dirt=1;
		}
	brelse(bh);
	if (!i)
		return 0;
	if (!(bh=bread(inode->i_dev,i)))
		return 0;
	i = ((unsigned short *)bh->b_data)[block&511];
	if (create && !i)
		if (i=new_block(inode->i_dev)) {
			((unsigned short *) (bh->b_data))[block&511]=i;
			bh->b_dirt=1;
		}
	brelse(bh);
	return i;
}

int bmap(struct m_inode * inode,int block)
{
	return _bmap(inode,block,0);
}

/**
 * 创建文件数据块在设备上的对应逻辑块
 * @param inode 文件i节点指针
 * @param block 逻辑块
 * @return 设备上对应的逻辑块号
*/
int create_block(struct m_inode * inode, int block)
{
	return _bmap(inode,block,1);
}
/**
 * 释放指定 i 节点内存（回写入设备）
 * */	
void iput(struct m_inode * inode)
{
	if (!inode)
		return;
	wait_on_inode(inode);
	// 所要释放 i 节点引用计数不为零 死机
	if (!inode->i_count)
		panic("iput: trying to free free inode");
	// 如果是管道 i 节点
	if (inode->i_pipe) {
		wake_up(&inode->i_wait); // 唤醒等待该 i 节点的进程
		if (--inode->i_count) // 复位该节点的引入计数标志
			return;
		// 释放所占用的物理内存
		free_page(inode->i_size);
		// 将 i 节点所有参数都置为 0 
		inode->i_count=0;
		inode->i_dirt=0;
		inode->i_pipe=0;
		return;
	}
	// 设备号为 0 则只是将引用计数 - 1 然后返回
	if (!inode->i_dev) {
		inode->i_count--;
		return;
	}
	// 如果是块设备的 i 节点，则是将 逻辑块字段 0 中的设备号更新，然后等待其解锁
	if (S_ISBLK(inode->i_mode)) {
		sync_dev(inode->i_zone[0]);
		wait_on_inode(inode);
	}
repeat:
	// 引用计数 大于 1 时 ，将引用计数 -1
	if (inode->i_count>1) {
		inode->i_count--;
		return;
	}
	// 指向该 i 节点的目录数为 0 时
	if (!inode->i_nlinks) {
		truncate(inode); // 释放 i 节点所有逻辑块
		free_inode(inode); // 释放该 i 节点
		return;
	}
	// i 节点已修改后，将其写回磁盘，等待写回完成，然后重复
	if (inode->i_dirt) {
		write_inode(inode);	/* we can sleep - so do again */
		wait_on_inode(inode);
		goto repeat;
	}
	inode->i_count--; // i 节点引用计数 -1
	return;
}

/**
 * 从 i 节点表中获取一个空闲 i 节点项
 * 寻找引用计数 count 为 0 的 i 节点，并将其写盘后清零
*/
struct m_inode * get_empty_inode(void)
{
	struct m_inode * inode;
	static struct m_inode * last_inode = inode_table;
	int i;

	do {
		inode = NULL;
		// 从后向前遍历 i 节点数组，寻找引用计数为 0 的空闲 i 节点
		for (i = NR_INODE; i ; i--) {
			if (++last_inode >= inode_table + NR_INODE)
				last_inode = inode_table; // 遍历完从头开始寻找
			// 获取引用计数为 0 未上锁 且 脏标记为 0 的 i 节点
			if (!last_inode->i_count) {
				inode = last_inode;
				if (!inode->i_dirt && !inode->i_lock)
					break;
			}
		}
		// 没有空闲节点时 打印每个 i 节点信息，死机
		if (!inode) {
			for (i=0 ; i<NR_INODE ; i++)
				printk("%04x: %6d\t",inode_table[i].i_dev,
					inode_table[i].i_num);
			panic("No free inodes in mem");
		}
		// 等待指定i节点解锁
		wait_on_inode(inode);
		// 如果 i 节点是脏的，将其写回磁盘，直到i节点变干净为止
		while (inode->i_dirt) {
			write_inode(inode);
			wait_on_inode(inode);
		}
	} while (inode->i_count); // 只有获取到的i 节点 引用计数为 0 时，才获取
	// 使用 0 填充 inode 所占用的内存，来将 i 节点清空
	memset(inode,0,sizeof(*inode));
	inode->i_count = 1; // i 节点引用计数为 1
	return inode;
}

struct m_inode * get_pipe_inode(void)
{
	struct m_inode * inode;

	if (!(inode = get_empty_inode()))
		return NULL;
	if (!(inode->i_size=get_free_page())) {
		inode->i_count = 0;
		return NULL;
	}
	inode->i_count = 2;	/* sum of readers/writers */
	PIPE_HEAD(*inode) = PIPE_TAIL(*inode) = 0;
	inode->i_pipe = 1;
	return inode;
}

/**
 * 从设备上读取指定的 i 节点
 * @param dev 当前设备号
 * @param nr i 节点号
*/
struct m_inode * iget(int dev,int nr)
{
	struct m_inode * inode, * empty;

	if (!dev)
		panic("iget with dev==0");
	// 获取空 i 节点
	empty = get_empty_inode();
	inode = inode_table;
	while (inode < NR_INODE+inode_table) {
		// 查找未上锁的指定 i 节点
		if (inode->i_dev != dev || inode->i_num != nr) {
			inode++;
			continue;
		}
		wait_on_inode(inode);
		// 在进程睡眠过程中 此 i 节点被修改了 则从头开始查找
		if (inode->i_dev != dev || inode->i_num != nr) {
			inode = inode_table;
			continue;
		}
		// 指定 i 节点引用计数 + 1
		inode->i_count++;
		// 如果该 i 节点为 其他系统的安装点
		if (inode->i_mount) {
			int i;
			// 在超级块表上搜索安装在此 i 节点上的超级块
			for (i = 0 ; i<NR_SUPER ; i++)
				if (super_block[i].s_imount==inode)
					break;
			// 没找到直接打印日志
			if (i >= NR_SUPER) {
				printk("Mounted inode hasn't got sb\n");
				if (empty)
					iput(empty); // 释放获取到的 i 节点内存
				return inode;
			}
			iput(inode); // 释放该 i 节点。并将该 i 节点写盘
			dev = super_block[i].s_dev; // 获取安装i节点的设备号
			nr = ROOT_INO;
			inode = inode_table; //继续获取安装该文件系统的i节点
			continue;
		}
		// 已经找到相应 i 节点，则放弃申请的临时 i 节点
		if (empty)
			iput(empty);
		return inode; // 返回找到的 i 节点
	}
	// 如果在 i 节点表中 没找到相应节点信息，则利用所申请的空节点在 i 节点表中建立新节点
	if (!empty)
		return (NULL);
	inode=empty;
	inode->i_dev = dev;
	inode->i_num = nr;
	read_inode(inode); // 将指定设备上的指定 i 节点读取到内存
	return inode;
}

/**
 * 读取设备上指定i节点数据
*/
static void read_inode(struct m_inode * inode)
{
	struct super_block * sb;
	struct buffer_head * bh;
	int block;

	lock_inode(inode); // 给指定 i 节点加锁
	// 获取该 i 节点的超级块
	if (!(sb=get_super(inode->i_dev)))
		panic("trying to read inode without dev");
	// 该 i 节点所在的逻辑块号 = 2（启动块 + 超级块） + i 节点位图占用块数 + 逻辑块位图所占块数 + （i 节点号 - 1）/每块所含有 i 节点数 
	block = 2 + sb->s_imap_blocks + sb->s_zmap_blocks +
		(inode->i_num-1)/INODES_PER_BLOCK;
	// 读取 i 节点所在块的信息到缓冲区
	if (!(bh=bread(inode->i_dev,block)))
		panic("unable to read i-node block");
	// 将对应 i 节点信息复制到 inode 中
	*(struct d_inode *)inode =
		((struct d_inode *)bh->b_data)
			[(inode->i_num-1)%INODES_PER_BLOCK];
	brelse(bh); // 释放 缓冲区内存
	unlock_inode(inode); // 解锁指定 i 节点
}

/**
 * 将指定 i 节点数据写回磁盘
 * @param inode 指定 i 节点指针
*/
static void write_inode(struct m_inode * inode)
{
	struct super_block * sb;
	struct buffer_head * bh;
	int block;

	lock_inode(inode); // 为指定 i 节点上锁
	// 该 i 节点不是脏的且其为设置设备，直接解锁返回
	if (!inode->i_dirt || !inode->i_dev) {
		unlock_inode(inode);
		return;
	}
	// 获取该 i 节点所在驱动设备的超级块，不存在时死机
	if (!(sb=get_super(inode->i_dev)))
		panic("trying to write inode without device");
	// 该 i 节点所在的逻辑块号 = 2（启动块 + 超级块） + i 节点位图占用块数 + 逻辑块位图所占块数 + （i 节点号 - 1）/每块所含有 i 节点数 
	block = 2 + sb->s_imap_blocks + sb->s_zmap_blocks +
		(inode->i_num-1)/INODES_PER_BLOCK;
	// 读取指定 i 节点所在块
	if (!(bh=bread(inode->i_dev,block)))
		panic("unable to read i-node block");
	// 将指定 i 节点信息复制到逻辑块对应该 i 节点的项中
	((struct d_inode *)bh->b_data)
		[(inode->i_num-1)%INODES_PER_BLOCK] =
			*(struct d_inode *)inode;
	// 置缓冲区脏标志，而 i 节点未被修改，所以 i 节点脏标志置 0
	bh->b_dirt=1;
	inode->i_dirt=0;
	// 释放缓冲区内存
	brelse(bh);
	// 解锁 i
	unlock_inode(inode);
}
