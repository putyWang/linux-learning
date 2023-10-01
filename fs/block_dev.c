/*
 *  linux/fs/block_dev.c
 *
 *  (C) 1991  Linus Torvalds
 */

#include <errno.h>

#include <linux/sched.h>
#include <linux/kernel.h>
#include <asm/segment.h>
#include <asm/system.h>

/**
 * 数据块写函数-向指定设备从指定偏移处写入指定长度数组
 * @param dev 设备号
 * @param pos 设备文件中的偏移指针
 * @param buf 用户地址空间中缓冲区地址
 * @param count 要传送的字节数
 * @return 写入的字节数
*/
int block_write(int dev, long * pos, char * buf, int count)
{
	int block = *pos >> BLOCK_SIZE_BITS; 				// 获取 pos 所在块号
	int offset = *pos & (BLOCK_SIZE-1);					// 获取 pos 在 block 上的偏移值
	int chars;
	int written = 0;									// 初始化 writen 为 0
	struct buffer_head * bh;
	register char * p;

	while (count>0) {
		chars = BLOCK_SIZE - offset;					// chars 表示当前块剩余可写的空间数
		if (chars > count)								// 剩余空间大于 count 时，表示当前块可以写完，将 chars 设置为剩余未写字节数
			chars=count;
		if (chars == BLOCK_SIZE)						// 正好需要写一块数据，则直接申请一块高速缓冲区
			bh = getblk(dev,block);
		else
			bh = breada(dev,block,block+1,block+2,-1);	// 否则则需要读入将被修改的数据块，并预读两块数据块
		block++;										// 块号 +1
		if (!bh)										// 缓冲区读取失败，返回已写的字符数
			return written?written:-EIO;
		p = offset + bh->b_data;						// p 指针指向要写的开始位置
		offset = 0;
		*pos += chars;									// 偏移量加上将要写的字符数
		written += chars;								// 写字符数加上将要写的字符数
		count -= chars;									// 需要写的字符数减去将要写的字符数
		while (chars-->0)								// 将字符复制到缓冲区中指定位置
			*(p++) = get_fs_byte(buf++);
		bh->b_dirt = 1;									// 将更新后的缓冲区写回盘
		brelse(bh);
	}
	return written;
}

/**
 * 数据块读函数-读取指定设备的指定偏移处指定长度字符
 * @param dev 设备号
 * @param pos 设备文件中的偏移指针
 * @param buf 用户地址空间中缓冲区地址
 * @param count 要传送的字节数
 * @return 读取的字节数
*/
int block_read(int dev, unsigned long * pos, char * buf, int count)
{
	int block = *pos >> BLOCK_SIZE_BITS;					// 计算 pos 所在块号
	int offset = *pos & (BLOCK_SIZE-1);						// 计算 pos 在 block 上的偏移值
	int chars;
	int read = 0;											// 初始化 read 为 0
	struct buffer_head * bh;
	register char * p;

	while (count>0) {
		chars = BLOCK_SIZE-offset;							// chars 表示当前块剩余可读字符数
		if (chars > count)									// 剩余空间大于 count 时，表示会将当前块读完，将 chars 设置为剩余未读字节数
			chars = count;
		if (!(bh = breada(dev,block,block+1,block+2,-1)))	// 读入将被读取的数据块，并预读两块数据块
			return read?read:-EIO;
		block++;											// 块号 +1
		p = offset + bh->b_data;							// p 指针指向要读的开始位置
		offset = 0;
		*pos += chars;										// 偏移量加上将要读取的字符数
		read += chars;										// 读取字符数加上将要读取的字符数
		count -= chars;										// 需要读的字符数减去将要读取的字符数
		while (chars-->0)									// 将读取数据从缓冲区复制到指定指针处
			put_fs_byte(*(p++),buf++);
		brelse(bh);											// 释放缓冲区
	}
	return read;
}
