/*
 *  linux/fs/file_dev.c
 *
 *  (C) 1991  Linus Torvalds
 */

#include <errno.h>
#include <fcntl.h>

#include <linux/sched.h>
#include <linux/kernel.h>
#include <asm/segment.h>

#define MIN(a,b) (((a)<(b))?(a):(b))
#define MAX(a,b) (((a)>(b))?(a):(b))

/**
 * 文件读函数-根据 i 节点和文件结构，读设备数据
 * @param inode 指定文件 i 节点
 * @param filp 文件对象
 * @param buf 读取到缓冲区
 * @param count 读取的字符数
 * @return 读取的字节数
*/
int file_read(struct m_inode * inode, struct file * filp, char * buf, int count)
{
	int left,chars,nr;
	struct buffer_head * bh;

	if ((left=count)<=0)									// 将 left 值设置为将要读取的字符数
		return 0;
	while (left) {											
		if (nr = bmap(inode,(filp->f_pos)/BLOCK_SIZE)) {	// 获取文件偏移指针所在逻辑块
			if (!(bh=bread(inode->i_dev,nr)))
				break;
		} else
			bh = NULL;
		nr = filp->f_pos % BLOCK_SIZE;						// 将 nr 设置为文件相对于当前块的偏移量
		chars = MIN( BLOCK_SIZE-nr , left );				// 将 chars 设置为当前块剩余值与未读取字符数中的小值
		filp->f_pos += chars;								// 更新文件偏移位置指针
		left -= chars;										// 更新还未读取的字符数
		if (bh) {											// bh 不为空时，将缓冲区 bh 中指定数据复制到 buf 缓冲区中
			char * p = nr + bh->b_data;
			while (chars-->0)
				put_fs_byte(*(p++),buf++);
			brelse(bh);
		} else {											// bh 为空时，则在 buf 指定位置指定长度置为 0
			while (chars-->0)
				put_fs_byte(0,buf++);
		}
	}
	inode->i_atime = CURRENT_TIME;							// 设置 i 节点访问时间
	return (count-left)?(count-left):-ERROR;				// 返回读取字符数，读取字符数为 0 时，返回错误码
}

/**
 * 文件写函数-根据 i 节点和文件结构，将数据写到设备中
 * @param inode 指定文件 i 节点
 * @param filp 文件对象
 * @param buf 写缓冲区
 * @param count 写的字符数
 * @return 写的字节数
*/
int file_write(struct m_inode * inode, struct file * filp, char * buf, int count)
{
	off_t pos;
	int block,c;
	struct buffer_head * bh;
	char * p;
	int i=0;

	if (filp->f_flags & O_APPEND)							// 以添加的方式打开文件，pos 指向文件尾偏移量
		pos = inode->i_size;
	else
		pos = filp->f_pos;									// 其他情况，pos指向文件当前偏移量
	while (i<count) {
		if (!(block = create_block(inode,pos/BLOCK_SIZE)))	// 获取当前偏移量所在块号
			break;
		if (!(bh=bread(inode->i_dev,block)))				// 将偏移量所在块号读取到 bh 缓冲区中
			break;
		c = pos % BLOCK_SIZE;								// 将 c 置为偏移量在当前块中的偏移值
		p = c + bh->b_data;									// 将 p 指向 bh 中写的当前指针
		bh->b_dirt = 1;										// 更新 bh 缓冲区脏标志
		c = BLOCK_SIZE-c;									// 计算当前块中还能写的字符数
		if (c > count-i) c = count-i;						// 将 c 置为当前块中还需写的字符数
		pos += c;											// 更新 pos 偏移位到写字符的尾部
		if (pos > inode->i_size) {							// 如果当前 pos 大于指定文件大小时，更新指定文件 i 节点的文件大小
			inode->i_size = pos;
			inode->i_dirt = 1;
		}
		i += c;												// 更新 i 为写的字符数
		while (c-->0)
			*(p++) = get_fs_byte(buf++);					// 循环将 buf 中的字符写到 p 指针所指向的位置
		brelse(bh);
	}
	inode->i_mtime = CURRENT_TIME;							// 设置 i 节点修改时间
	if (!(filp->f_flags & O_APPEND)) {						// 不是以添加的方式打开文件，更新文件当前指针指向位置
		filp->f_pos = pos;
		inode->i_ctime = CURRENT_TIME;
	}
	return (i?i:-1);										// 返回写的字节数，否则出错号为 -1
}
