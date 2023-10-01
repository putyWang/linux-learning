/*
 *  linux/fs/super.c
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 * super.c contains code to handle the super-block tables.
 */
#include <linux/config.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <asm/system.h>

#include <errno.h>
#include <sys/stat.h>

int sync_dev(int dev);
void wait_for_keypress(void);

/* set_bit uses setb, as gas doesn't recognize setc */
/**
 * 测试定义位偏移处比特位的值（0 或 1），并返回该比特位值
 * @param bitnr 比特位偏移值
 * @param addr 测试比特位操作的起始地址
*/
#define set_bit(bitnr,addr) ({ \
register int __res __asm__("ax"); \
__asm__("bt %2,%3;setb %%al":"=a" (__res):"a" (0),"r" (bitnr),"m" (*(addr))); \
__res; })

struct super_block super_block[NR_SUPER]; 	// 超级块数组
/* this is initialized in init/main.c */
int ROOT_DEV = 0;

/**
 * 为指定超级块上锁
 * @param sb 超级块结构指针
*/
static void lock_super(struct super_block * sb)
{
	cli();
	while (sb->s_lock)
		sleep_on(&(sb->s_wait));
	sb->s_lock = 1;
	sti();
}

/**
 * 解锁指定超级块
 * @param sb 超级块结构指针
*/
static void free_super(struct super_block * sb)
{
	cli();
	sb->s_lock = 0;
	wake_up(&(sb->s_wait));
	sti();
}

/**
 * 等待指定超级块解锁
 * @param sb 超级块结构指针
*/
static void wait_on_super(struct super_block * sb)
{
	cli();
	while (sb->s_lock)
		sleep_on(&(sb->s_wait));
	sti();
}

/**
 * 获取指定设备的超级块
 * @param dev 设备号
 * @return 不在缓冲返回空
*/
struct super_block * get_super(int dev)
{
	struct super_block * s;

	if (!dev)
		return NULL;
	s = 0+super_block;
	// 遍历超级块数组，获取指定超级块
	while (s < NR_SUPER+super_block)
		if (s->s_dev == dev) {
			wait_on_super(s); 		// 等待指定超级块解锁
			if (s->s_dev == dev) 	// 该超级块设备未被修改，直接返回
				return s;
			s = 0+super_block; 		// 否则重新遍历查找
		} else
			s++;
	return NULL;
}

/**
 * 释放指定设备的超级块
 * @param dev 设备号
*/
void put_super(int dev)
{
	struct super_block * sb;
	struct m_inode * inode;
	int i;

	// 根设备文件超级不允许释放
	if (dev == ROOT_DEV) {
		printk("root diskette changed: prepare for armageddon\n\r");
		return;
	}
	// 缓冲区中未保存当前设备超级块时，不需要释放直接返回
	if (!(sb = get_super(dev)))
		return;
	// 不允许释放已经安装到 i 节点的超级块
	if (sb->s_imount) {
		printk("Mounted disk changed - tssk, tssk\n\r");
		return;
	}
	lock_super(sb);				// 为指定块上锁
	sb->s_dev = 0;				// 清空设备号
	for(i=0;i<I_MAP_SLOTS;i++)	// 释放该设备 i 节点位图和逻辑块位图所占用缓冲区
		brelse(sb->s_imap[i]);
	for(i=0;i<Z_MAP_SLOTS;i++)
		brelse(sb->s_zmap[i]);
	free_super(sb); 			// 解锁指定超级块
	return;
}

/**
 * 读取指定设备超级块
 * @param dev 设备号
 * @return 指向指定设备超级块指针
*/
static struct super_block * read_super(int dev)
{
	struct super_block * s;
	struct buffer_head * bh;
	int i,block;

	if (!dev)
		return NULL;
	check_disk_change(dev); 			// 检查软盘是否更换
	if (s = get_super(dev))				// 获取指定的超级块
		return s;
	for (s = 0+super_block ;; s++) {	// 遍历查找超级块数组中的空闲项
		if (s >= NR_SUPER+super_block)
			return NULL;
		if (!s->s_dev)
			break;
	}
	// 设置超级块初始参数
	s->s_dev = dev;
	s->s_isup = NULL;
	s->s_imount = NULL;
	s->s_time = 0;
	s->s_rd_only = 0;
	s->s_dirt = 0;
	lock_super(s);						// 锁定超级块
	// 读取该磁盘 1 扇区中存储的超级块数据
	if (!(bh = bread(dev,1))) {
		s->s_dev=0;
		free_super(s);
		return NULL;
	}
	*((struct d_super_block *) s) = *((struct d_super_block *) bh->b_data);	// 将指定超级块信息保存到 s 指针指向的内存中
	brelse(bh); 						// 释放 bh 指定内存
	if (s->s_magic != SUPER_MAGIC) {	// Linux 0.11 只支持 minix 文件系统
		s->s_dev = 0;
		free_super(s);
		return NULL;
	}
	for (i=0;i<I_MAP_SLOTS;i++)			// 清空 i 节点位图指向的内存
		s->s_imap[i] = NULL;
	for (i=0;i<Z_MAP_SLOTS;i++)			// 清空逻辑位图指向的内存
		s->s_zmap[i] = NULL;
	block=2;
	// 设备从第 2 扇区开始依次存储 i 节点位图、逻辑块位图，然后分别读取该超级块的逻辑位图信息，保存到相应的变量中
	for (i=0 ; i < s->s_imap_blocks ; i++)
		if (s->s_imap[i]=bread(dev,block))
			block++;
		else
			break;
	for (i=0 ; i < s->s_zmap_blocks ; i++)
		if (s->s_zmap[i]=bread(dev,block))
			block++;
		else
			break;
	// block 与 i 节点位图和 逻辑块位图不一致时，说明读取过程有误，
	// 释放对应缓冲
	// 解锁对应超级块
	// 读取失败 返回 NULL
	if (block != 2+s->s_imap_blocks+s->s_zmap_blocks) {
		for(i=0;i<I_MAP_SLOTS;i++)
			brelse(s->s_imap[i]);
		for(i=0;i<Z_MAP_SLOTS;i++)
			brelse(s->s_zmap[i]);
		s->s_dev=0;
		free_super(s);
		return NULL;
	}
	/*由于申请空闲 i 节点的函数，在所有 i 节点都已经被使用时返回 0，
	如果0 号节点可以使用的话，会产生歧义，因此无法使用，所以将位图的最低位设置为 1，以保证文件系统不会分配 0 号 i 节点
	对于逻辑位图也是同样的道理*/
	s->s_imap[0]->b_data[0] |= 1;
	s->s_zmap[0]->b_data[0] |= 1;
	free_super(s); // 初始化完成，解锁超级块
	return s;
}

/**
 * 卸载指定文件系统
 * @param dev_name 设备文件名
 * @return 成功 0 ，负值-错误号
*/
int sys_umount(char * dev_name)
{
	struct m_inode * inode;
	struct super_block * sb;
	int dev;

	// 获取设备文件名对应的 i 节点
	if (!(inode=namei(dev_name)))
		return -ENOENT;
	dev = inode->i_zone[0];			// 设备文件名 i 节点第一个直接块中保存着设备号
	if (!S_ISBLK(inode->i_mode)) {	// 不是块设备文件 i 节点，返回出错码
		iput(inode);
		return -ENOTBLK;
	}
	iput(inode);					// 释放指定 i 节点
	if (dev==ROOT_DEV)				// 根文件系统不允许释放
		return -EBUSY;
	// 内存中没有对应超级块或超级块未被安装时直接返回
	if (!(sb=get_super(dev)) || !(sb->s_imount))
		return -ENOENT;
	if (!sb->s_imount->i_mount)		// 对应设备文件 i 节点，安装位未置位。则打印警告信息
		printk("Mounted inode has i_mount=0\n");
	// 被卸载的文件系统不允许存在正在被其他进程使用该设备上的文件
	for (inode=inode_table+0 ; inode<inode_table+NR_INODE ; inode++)
		if (inode->i_dev==dev && inode->i_count)
				return -EBUSY;
	sb->s_imount->i_mount=0;		// 将对应超级块对应 i 节点的安装位置位
	iput(sb->s_imount);				// 释放超级块对应 i 节点
	sb->s_imount = NULL;			// 置空超级块上被安装的 i 节点项
	iput(sb->s_isup);				// 释放超级块对应根文件 i 节点
	sb->s_isup = NULL;				// 清空根文件 i 节点项
	put_super(dev);					// 释放指定超级块
	sync_dev(dev);					// 将指定设备上所占用的内存写回盘
	return 0;
}

/**
 * 安装文件系统调用的函数
 * @param dev_name 设备文件名
 * @param dir_name 安装到的目录名
 * @param rw_flag 被安装文件的读写标志
*/
int sys_mount(char * dev_name, char * dir_name, int rw_flag)
{
	struct m_inode * dev_i, * dir_i;
	struct super_block * sb;
	int dev;

	if (!(dev_i=namei(dev_name)))	// 根据设备文件名找到对应的 i 节点，并获取其中的设备号
		return -ENOENT;
	dev = dev_i->i_zone[0];
	if (!S_ISBLK(dev_i->i_mode)) {	// 不是块设备，直接返回出错码
		iput(dev_i);
		return -EPERM;
	}
	iput(dev_i);					// 释放指定 i 节点
	if (!(dir_i=namei(dir_name)))	// 获取指定目录 i 节点
		return -ENOENT;
	// 指定目录不应该有其他进程的使用且不应该是根目录
	if (dir_i->i_count != 1 || dir_i->i_num == ROOT_INO) {
		iput(dir_i);
		return -EBUSY;
	}
	// 也必须是个文件夹
	if (!S_ISDIR(dir_i->i_mode)) {
		iput(dir_i);
		return -EPERM;
	}
	// 从磁盘上读取将要安装的设备的超级块
	if (!(sb=read_super(dev))) {
		iput(dir_i);
		return -EBUSY;
	}
	// 已被安装后直接返回出错码
	if (sb->s_imount) {
		iput(dir_i);
		return -EBUSY;
	}
	// 如果该目录已经安装了其他文件系统，直接返回
	if (dir_i->i_mount) {
		iput(dir_i);
		return -EPERM;
	}
	sb->s_imount=dir_i;				// 超级块的安装 i 节点指向文件夹 i 节点
	dir_i->i_mount=1;
	dir_i->i_dirt=1;		/* NOTE! we don't iput(dir_i) */
	return 0;			/* we do that in umount */
}

/**
 * 安装根文件系统
 * 系统开机初始化时调用
*/
void mount_root(void)
{
	int i,free;
	struct super_block * p;
	struct m_inode * mi;

	if (32 != sizeof (struct d_inode))	// 判断磁盘 i 节点大小是否为 32 字节，用来防止修改源代码时的不一致性
		panic("bad i-node size");
	for(i=0;i<NR_FILE;i++)				// 初始化文件数组总所有项引用数位 0 
		file_table[i].f_count=0;
	if (MAJOR(ROOT_DEV) == 2) {			// 根文件所在设备位软盘时，提示插入软盘
		printk("Insert root floppy and press ENTER");
		wait_for_keypress(); 			// 等待按下 enter 键
	}
	// 初始化超级块数组
	for(p = &super_block[0] ; p < &super_block[NR_SUPER] ; p++) {
		p->s_dev = 0; 					// 所在设备为 0
		p->s_lock = 0; 					// 未锁定
		p->s_wait = NULL; 				// 没有等待进程
	}
	if (!(p=read_super(ROOT_DEV))) 		// 获取根目录超级块
		panic("Unable to mount root");
	if (!(mi=iget(ROOT_DEV,ROOT_INO))) 	// 获取根文件系统的根文件目录
		panic("Unable to read root i-node");
	mi->i_count += 3 ;					// 将根文件目录 i 节点引用数 +3
	p->s_isup = p->s_imount = mi; 		// 设置根目录超级块安装 i 节点
	current->pwd = mi; 					// 设置当前进程pwd 
	current->root = mi; 				// 设置当前进程根目录 i 节点
	free=0; 							// 统计该设备上的空闲块数
	i=p->s_nzones;
	// 根据逻辑块位图中相应比特位的占用情况统计处空闲块数；
	// i&8191 用于取得 i 节点号在当前块中的偏移值
	// i>>13 是将 i 除以 8192，即除一个磁盘块包含的比特位数
	while (-- i >= 0)
		if (!set_bit(i&8191,p->s_zmap[i>>13]->b_data))
			free++;
	printk("%d/%d free blocks\n\r",free,p->s_nzones);
	// 统计该设备上的空闲 i 节点数
	free=0;
	i=p->s_ninodes+1;
	while (-- i >= 0)
		if (!set_bit(i&8191,p->s_imap[i>>13]->b_data))
			free++;
	printk("%d/%d free inodes\n\r",free,p->s_ninodes);
}
