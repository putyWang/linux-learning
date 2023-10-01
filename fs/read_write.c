/*
 *  linux/fs/read_write.c
 *
 *  (C) 1991  Linus Torvalds
 */

#include <sys/stat.h>
#include <errno.h>
#include <sys/types.h>

#include <linux/kernel.h>
#include <linux/sched.h>
#include <asm/segment.h>

extern int rw_char(int rw,int dev, char * buf, int count, off_t * pos);
extern int read_pipe(struct m_inode * inode, char * buf, int count);
extern int write_pipe(struct m_inode * inode, char * buf, int count);
extern int block_read(int dev, off_t * pos, char * buf, int count);
extern int block_write(int dev, off_t * pos, char * buf, int count);
extern int file_read(struct m_inode * inode, struct file * filp,
		char * buf, int count);
extern int file_write(struct m_inode * inode, struct file * filp,
		char * buf, int count);

/**
 * 重定义文件读写指针调用函数
 * @param fd 文件句柄
 * @param offset 新的文件读取指针偏移值
 * @param orgin 偏移起始位置(SEEK_SET-文件起始处，SEEK_CUR-当前指针位置，SEEK_END-文件结尾处)
*/
int sys_lseek(unsigned int fd,off_t offset, int origin)
{
	struct file * file;
	int tmp;

	// fd 不合法、句柄对应文件为空、文件对应 i 节点为空或文件所属设备不能定义文件指针时返回错误
	if (fd >= NR_OPEN || !(file=current->filp[fd]) || !(file->f_inode)
	   || !IS_SEEKABLE(MAJOR(file->f_inode->i_dev)))
		return -EBADF;
	if (file->f_inode->i_pipe)							// 管道文件也不能定义指针
		return -ESPIPE;
	switch (origin) {
		case 0:											// SEEK_SET-指针 = 文件起始处（0） + offset
			if (offset<0) return -EINVAL;
			file->f_pos=offset;
			break;
		case 1:
			if (file->f_pos+offset<0) return -EINVAL;	// SEEK_CUR-指针 = 文件当前指针（f_pos） + offset
			file->f_pos += offset;
			break;
		case 2:
			if ((tmp=file->f_inode->i_size+offset) < 0)	// SEEK_END-指针 = 文件结尾处（i_size） + offset
				return -EINVAL;
			file->f_pos = tmp;
			break;
		default:										// 其余情况说明模式有误
			return -EINVAL;
	}
	return file->f_pos;									// 返回文件指针
}

/**
 * 读文件系统调用
 * @param fd 文件句柄
 * @param buf 读写缓冲区
 * @param count 读取字节数
 * @return 读取字节数
*/
int sys_read(unsigned int fd,char * buf,int count)
{
	struct file * file;
	struct m_inode * inode;

	// fd 不合法、读写字节数小于 0 或指定句柄对应的文件不存在时返回错误码
	if (fd>=NR_OPEN || count<0 || !(file=current->filp[fd]))
		return -EINVAL;
	if (!count)				// count = 0 时，直接返回 0
		return 0;
	verify_area(buf,count);	// 验证缓冲区是否有足够的空间存储读取的字节
	inode = file->f_inode;	// 获取指定文件 i 节点
	if (inode->i_pipe)		// 调用管道文件读取函数
		return (file->f_mode&1)?read_pipe(inode,buf,count):-EIO;
	if (S_ISCHR(inode->i_mode))	// 调用字节文件读取函数
		return rw_char(READ,inode->i_zone[0],buf,count,&file->f_pos);
	if (S_ISBLK(inode->i_mode))	// 调用块文件读取函数
		return block_read(inode->i_zone[0],&file->f_pos,buf,count);
	if (S_ISDIR(inode->i_mode) || S_ISREG(inode->i_mode)) {	// 目录文件或者常规文件
		if (count+file->f_pos > inode->i_size)				// 将读取的字节数设置成指定读取字节数与剩余字节数之中的最小值
			count = inode->i_size - file->f_pos;
		if (count<=0)										// 当前文件已无数据可读时，直接返回 0
			return 0;
		return file_read(inode,file,buf,count);				// 调用文件读取函数
	}
	printk("(Read)inode->i_mode=%06o\n\r",inode->i_mode);	// 如果不是上述文件，则打印对应文件节点 mode 属性值，然后返回错误码
	return -EINVAL;
}

/**
 * 写文件系统调用
 * @param fd 文件句柄
 * @param buf 读写缓冲区
 * @param count 读取字节数
 * @return 读取字节数
*/
int sys_write(unsigned int fd,char * buf,int count)
{
	struct file * file;
	struct m_inode * inode;
	
	if (fd>=NR_OPEN || count <0 || !(file=current->filp[fd]))			// fd 不合法、读写字节数小于 0 或指定句柄对应的文件不存在时返回错误码
		return -EINVAL;
	if (!count)															// count = 0 时，直接返回 0
		return 0;
	inode=file->f_inode;												// 获取指定文件 i 节点
	if (inode->i_pipe)
		return (file->f_mode&2)?write_pipe(inode,buf,count):-EIO;		// 调用管道文件写函数
	if (S_ISCHR(inode->i_mode))
		return rw_char(WRITE,inode->i_zone[0],buf,count,&file->f_pos);	// 调用字节文件写函数
	if (S_ISBLK(inode->i_mode))
		return block_write(inode->i_zone[0],&file->f_pos,buf,count);	// 调用块文件写函数
	if (S_ISREG(inode->i_mode))
		return file_write(inode,file,buf,count);						// 调用普通文件写函数
	printk("(Write)inode->i_mode=%06o\n\r",inode->i_mode);				// 不是上述文件，则打印对应文件节点 mode 属性值，然后返回错误码
	return -EINVAL;
}
