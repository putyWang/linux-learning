/*
 *  linux/fs/truncate.c
 *
 *  (C) 1991  Linus Torvalds
 */

#include <linux/sched.h>

#include <sys/stat.h>

/**
 * 释放一次间接块
 * @param dev 设备号
 * @param block 块号
*/
static void free_ind(int dev,int block)
{
	struct buffer_head * bh;
	unsigned short * p;
	int i;

	if (!block)								// 逻辑块号为 0，返回
		return;
	if (bh=bread(dev,block)) {				// 将指定设备上的指定块读到缓冲区
		p = (unsigned short *) bh->b_data;	// p 指向实际存储该块数据的位置
		for (i=0;i<512;i++,p++)				// 遍历清空间接块中所有块
			if (*p)
				free_block(dev,*p);
		brelse(bh);							// 更新缓冲区
	}
	free_block(dev,block);					// 清空该间接块的数据
}

/**
 * 释放二次间接块
 * @param dev 设备号
 * @param block 块号
*/
static void free_dind(int dev,int block)
{
	struct buffer_head * bh;
	unsigned short * p;
	int i;

	if (!block)
		return;
	if (bh=bread(dev,block)) {
		p = (unsigned short *) bh->b_data;
		for (i=0;i<512;i++,p++)
			if (*p)
				free_ind(dev,*p);	// 遍历二次间接块中保存的一次间接块
		brelse(bh);
	}
	free_block(dev,block);			// 释放本二次间接块
}

/**
 * 清空指定 i 节点所有相关数据
 * @param inode i 节点结构指针
*/
void truncate(struct m_inode * inode)
{
	int i;

	if (!(S_ISREG(inode->i_mode) || S_ISDIR(inode->i_mode)))	// 只有普通文件和文件夹可以执行清空操作
		return;
	for (i=0;i<7;i++)											// 清空 0 - 6 七个直接块数据
		if (inode->i_zone[i]) {
			free_block(inode->i_dev,inode->i_zone[i]);
			inode->i_zone[i]=0;
		}
	free_ind(inode->i_dev,inode->i_zone[7]);					// 清空 7 一次间接块数据
	free_dind(inode->i_dev,inode->i_zone[8]);					// 清空 8 二次间接块数据
	inode->i_zone[7] = inode->i_zone[8] = 0;					// 将 7 ，8 两个块块号设置为 0
	inode->i_size = 0;											// 将 i 节点大小设置为 0
	inode->i_dirt = 1;											// 设置 i 节点更新标志
	inode->i_mtime = inode->i_ctime = CURRENT_TIME;				// 设置 i 节点更新时间
}

