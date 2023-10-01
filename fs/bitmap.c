/*
 *  linux/fs/bitmap.c
 *
 *  (C) 1991  Linus Torvalds
 */

/* bitmap.c contains the code that handles the inode and block bitmaps */
#include <string.h>

#include <linux/sched.h>
#include <linux/kernel.h>

/**
 * 清空指定地址处的一块内存
 * @param addr 清除地址
*/
#define clear_block(addr) \
__asm__("cld\n\t" \
	"rep\n\t" \
	"stosl" \
	::"a" (0),"c" (BLOCK_SIZE/4),"D" ((long) (addr)):"cx","di")

/**
 * 将指定地址的指定偏移处置位
 * @param nr 地址偏移量
 * @param addr 基地址
*/
#define set_bit(nr,addr) ({\
register int res __asm__("ax"); \
__asm__ __volatile__("btsl %2,%3\n\tsetb %%al": \
"=a" (res):"0" (0),"r" (nr),"m" (*(addr))); \
res;})

/**
 * 将指定地址的指定偏移处清 0 
 * @param nr 地址偏移量
 * @param addr 基地址
*/
#define clear_bit(nr,addr) ({\
register int res __asm__("ax"); \
__asm__ __volatile__("btrl %2,%3\n\tsetnb %%al": \
"=a" (res):"0" (0),"r" (nr),"m" (*(addr))); \
res;})

/**
 * 寻址从指定地址开始第一个为 0 的位
*/
#define find_first_zero(addr) ({ \
int __res; \
__asm__("cld\n" \
	"1:\tlodsl\n\t" \
	"notl %%eax\n\t" \
	"bsfl %%eax,%%edx\n\t" \
	"je 2f\n\t" \
	"addl %%edx,%%ecx\n\t" \
	"jmp 3f\n" \
	"2:\taddl $32,%%ecx\n\t" \
	"cmpl $8192,%%ecx\n\t" \
	"jl 1b\n" \
	"3:" \
	:"=c" (__res):"c" (0),"S" (addr):"ax","dx","si"); \
__res;})

/**
 * 释放指定设备的指定块
 * @param dev 设备号
 * @param block 块号
*/
void free_block(int dev, int block)
{
	struct super_block * sb;
	struct buffer_head * bh;

	// 获取对应设备号的超级块
	if (!(sb = get_super(dev)))
		panic("trying to free block on nonexistent device");
	// 逻辑块号小于第一个逻辑块号或大于逻辑块数，说明块不合法
	if (block < sb->s_firstdatazone || block >= sb->s_nzones)
		panic("trying to free block not in datazone");
	// 确定将要释放的块没有程序使用
	bh = get_hash_table(dev,block);									// 获取指定块上数据
	if (bh) {
		if (bh->b_count != 1) {										// 不允许释放正在被其他进程引用的块
			printk("trying to free block (%04x:%d), count=%d\n",
				dev,block,bh->b_count);
			return;
		}
		bh->b_dirt=0;												// 将 dirt 设置为0
		bh->b_uptodate=0;											// 将 更新标志置为0
		brelse(bh);													// 释放指定缓冲区大小
	}
	block -= sb->s_firstdatazone - 1 ;								// 获取实际块号
	if (clear_bit(block&8191,sb->s_zmap[block/8192]->b_data)) {		// 清除块在块位图中位置
		printk("block (%04x:%d) ",dev,block+sb->s_firstdatazone-1);
		panic("free_block: bit already cleared");
	}
	sb->s_zmap[block/8192]->b_dirt = 1;								// 将该逻辑位图所在块设置为脏
}

/**
 * 向指定设备申请空闲块
 * @param dev 申请空闲块的设备号
 * @return 空闲块号
*/
int new_block(int dev)
{
	struct buffer_head * bh;
	struct super_block * sb;
	int i,j;

	// 获取指定设备的超级块
	if (!(sb = get_super(dev)))
		panic("trying to get new block from nonexistant device");
	j = 8192;
	// 查询逻辑块位图中第一个空块，并使 bh 指向该空闲块所在位图块
	for (i=0 ; i<8 ; i++)
		if (bh=sb->s_zmap[i])
			if ((j=find_first_zero(bh->b_data))<8192)
				break;
	if (i>=8 || !bh || j>=8192)
		return 0;
	// 将该空闲块所对应的位图位置位
	if (set_bit(j,bh->b_data))
		panic("new_block: bit already set");
	// 将该空闲块所在位图块置位脏，等待同步回块设备
	bh->b_dirt = 1;
	// 更新块号
	j += i*8192 + sb->s_firstdatazone-1;
	if (j >= sb->s_nzones)
		return 0;
	// 获取指定空闲块
	if (!(bh=getblk(dev,j)))
		panic("new_block: cannot get block");
	if (bh->b_count != 1)
		panic("new block: count is != 1");
	// 清空指定块数据
	clear_block(bh->b_data);  
	bh->b_uptodate = 1;
	bh->b_dirt = 1;
	// 释放该缓冲区
	brelse(bh);
	return j;
}

/**
 * 释放指定 i 节点
 * @param inode 将要释放的 i 节点结构
*/
void free_inode(struct m_inode * inode)
{
	struct super_block * sb;
	struct buffer_head * bh;

	if (!inode)
		return;
	if (!inode->i_dev) {
		memset(inode,0,sizeof(*inode));
		return;
	}
	// 存在除本进程外还有其他进程对该节点引用，则死机
	if (inode->i_count>1) {
		printk("trying to free inode with count=%d\n",inode->i_count);
		panic("free_inode");
	}
	// 不允许释放链接数不为 0 的文件目录项
	if (inode->i_nlinks)
		panic("trying to free inode with links");
	// 获取 inode 对应设备的超级块
	if (!(sb = get_super(inode->i_dev)))
		panic("trying to free inode on nonexistent device");
	// 判断 i 节点号是否有效
	if (inode->i_num < 1 || inode->i_num > sb->s_ninodes)
		panic("trying to free inode 0 or nonexistant inode");
	// 获取指定i节点位图所在块数据
	if (!(bh=sb->s_imap[inode->i_num>>13]))
		panic("nonexistent imap in superblock");
	// 将位图中指定 i 节点的位清 0
	if (clear_bit(inode->i_num&8191,bh->b_data))
		printk("free_inode: bit already cleared.\n\r");
	bh->b_dirt = 1;
	// 清空 inode 所指向的内存
	memset(inode,0,sizeof(*inode));
}

/**
 * 在指定设备上创建新的 i 节点
 * @param dev 设备号
 * @return 创建好的 i 节点指针
*/
struct m_inode * new_inode(int dev)
{
	struct m_inode * inode;
	struct super_block * sb;
	struct buffer_head * bh;
	int i,j;

	// 获取一个空的j节点
	if (!(inode=get_empty_inode()))
		return NULL;
	// 获取该设备的超级块
	if (!(sb = get_super(dev)))
		panic("new_inode with unknown device");
	j = 8192;
	for (i=0 ; i<8 ; i++)
		// 寻找拥有空闲i节点的设备 i 节点位图块
		if (bh=sb->s_imap[i])
			if ((j=find_first_zero(bh->b_data))<8192)
				break;
	// 如果获取块为空或者j大于块的位数，或者所在位的位置和磁盘i节点个数对不上时
	if (!bh || j >= 8192 || j+i*8192 > sb->s_ninodes) {
		iput(inode); // 释放申请的空闲 i 节点
		return NULL;
	}
	// 将i节点位图上指定位置置位，表明该i节点已被使用
	if (set_bit(j,bh->b_data))
		panic("new_inode: bit already set");
	bh->b_dirt = 1; // 修改位图所在缓冲区置位已修改，等待同步写回设备
	// 设置新建 i 节点参数
	inode->i_count=1;
	inode->i_nlinks=1;
	inode->i_dev=dev;
	inode->i_uid=current->euid;
	inode->i_gid=current->egid;
	inode->i_dirt=1;
	inode->i_num = j + i*8192;
	inode->i_mtime = inode->i_atime = inode->i_ctime = CURRENT_TIME;
	return inode;
}
