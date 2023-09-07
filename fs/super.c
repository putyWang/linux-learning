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

struct super_block super_block[NR_SUPER]; // 超级块数组
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
			wait_on_super(s); // 等待指定超级块解锁
			if (s->s_dev == dev) // 该超级块设备未被修改，直接返回
				return s;
			s = 0+super_block; // 否则重新遍历查找
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

	// 根系统盘被改变时，提示然后直接返回
	if (dev == ROOT_DEV) {
		printk("root diskette changed: prepare for armageddon\n\r");
		return;
	}
	// 获取指定超级块
	if (!(sb = get_super(dev)))
		return;
	// 该超级块系统安装了其他文件系统，显示警告然后返回
	if (sb->s_imount) {
		printk("Mounted disk changed - tssk, tssk\n\r");
		return;
	}
	// 为指定块上锁
	lock_super(sb);
	// 清空设备号
	sb->s_dev = 0;
	// 释放该设备 i 节点位图和逻辑块位图所占用缓冲区
	for(i=0;i<I_MAP_SLOTS;i++)
		brelse(sb->s_imap[i]);
	for(i=0;i<Z_MAP_SLOTS;i++)
		brelse(sb->s_zmap[i]);
	free_super(sb); // 解锁指定超级块
	return;
}

/**
 * 读取指定设备超级块
 * @param dev 设备号
*/
static struct super_block * read_super(int dev)
{
	struct super_block * s;
	struct buffer_head * bh;
	int i,block;

	if (!dev)
		return NULL;
	check_disk_change(dev); // 检查软盘是否更换
	// 获取指定的超级块
	if (s = get_super(dev))
		return s;
	// 遍历查找一个未绑定设备的超级块
	for (s = 0+super_block ;; s++) {
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
	// 锁定超级块
	lock_super(s);
	// 读取该磁盘 1 扇区，没有数据则解锁指定超级块
	if (!(bh = bread(dev,1))) {
		s->s_dev=0;
		free_super(s);
		return NULL;
	}
	// 将主设备超级块信息复制到 s 中去
	*((struct d_super_block *) s) =
		*((struct d_super_block *) bh->b_data);
	brelse(bh); // 释放 bh 指定内存
	// 根文件只能是 minix 文件系统
	if (s->s_magic != SUPER_MAGIC) {
		s->s_dev = 0;
		free_super(s);
		return NULL;
	}
	// 初始化 i 节点位图
	for (i=0;i<I_MAP_SLOTS;i++)
		s->s_imap[i] = NULL;
	// 初始化逻辑位图
	for (i=0;i<Z_MAP_SLOTS;i++)
		s->s_zmap[i] = NULL;
	block=2;
	// 读取根文件系统，i 节点位图与逻辑块位图，并存储在指定操作块中
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

int sys_umount(char * dev_name)
{
	struct m_inode * inode;
	struct super_block * sb;
	int dev;

	if (!(inode=namei(dev_name)))
		return -ENOENT;
	dev = inode->i_zone[0];
	if (!S_ISBLK(inode->i_mode)) {
		iput(inode);
		return -ENOTBLK;
	}
	iput(inode);
	if (dev==ROOT_DEV)
		return -EBUSY;
	if (!(sb=get_super(dev)) || !(sb->s_imount))
		return -ENOENT;
	if (!sb->s_imount->i_mount)
		printk("Mounted inode has i_mount=0\n");
	for (inode=inode_table+0 ; inode<inode_table+NR_INODE ; inode++)
		if (inode->i_dev==dev && inode->i_count)
				return -EBUSY;
	sb->s_imount->i_mount=0;
	iput(sb->s_imount);
	sb->s_imount = NULL;
	iput(sb->s_isup);
	sb->s_isup = NULL;
	put_super(dev);
	sync_dev(dev);
	return 0;
}

int sys_mount(char * dev_name, char * dir_name, int rw_flag)
{
	struct m_inode * dev_i, * dir_i;
	struct super_block * sb;
	int dev;

	if (!(dev_i=namei(dev_name)))
		return -ENOENT;
	dev = dev_i->i_zone[0];
	if (!S_ISBLK(dev_i->i_mode)) {
		iput(dev_i);
		return -EPERM;
	}
	iput(dev_i);
	if (!(dir_i=namei(dir_name)))
		return -ENOENT;
	if (dir_i->i_count != 1 || dir_i->i_num == ROOT_INO) {
		iput(dir_i);
		return -EBUSY;
	}
	if (!S_ISDIR(dir_i->i_mode)) {
		iput(dir_i);
		return -EPERM;
	}
	if (!(sb=read_super(dev))) {
		iput(dir_i);
		return -EBUSY;
	}
	if (sb->s_imount) {
		iput(dir_i);
		return -EBUSY;
	}
	if (dir_i->i_mount) {
		iput(dir_i);
		return -EPERM;
	}
	sb->s_imount=dir_i;
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

	// 判断磁盘 i 节点大小是否为 32 字节，用来防止修改源代码时的不一致性
	if (32 != sizeof (struct d_inode))
		panic("bad i-node size");
	// 初始化文件数组总所有项引用数位 0 
	for(i=0;i<NR_FILE;i++)
		file_table[i].f_count=0;
	// 根文件所在设备位软盘时，提示插入软盘
	if (MAJOR(ROOT_DEV) == 2) {
		printk("Insert root floppy and press ENTER");
		wait_for_keypress(); // 等待按下 enter 键
	}
	// 初始化超级块数组
	for(p = &super_block[0] ; p < &super_block[NR_SUPER] ; p++) {
		p->s_dev = 0; // 所在设备为 0
		p->s_lock = 0; // 未锁定
		p->s_wait = NULL; // 没有等待进程
	}
	if (!(p=read_super(ROOT_DEV))) // 获取指定根目录超级块
		panic("Unable to mount root");
	if (!(mi=iget(ROOT_DEV,ROOT_INO))) // 获取指定 i 节点信息
		panic("Unable to read root i-node");
	mi->i_count += 3 ;	/* NOTE! it is logically used 4 times, not 1 */ // 将 i 节点引用数 +3
	p->s_isup = p->s_imount = mi; // 设置根目录超级块安装 i 节点
	current->pwd = mi; // 设置当前进程pwd 
	current->root = mi; // 设置当前进程根目录 i 节点
	// 统计该设备上的空闲块数
	free=0; 
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
