/*
 *  linux/fs/stat.c
 *
 *  (C) 1991  Linus Torvalds
 */

#include <errno.h>
#include <sys/stat.h>

#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <asm/segment.h>

/**
 * 复制文件信息
 * @param inode 文件对应的 i 节点
 * @param statbuf 存储 stat 文件状态结构指针
*/
static void cp_stat(struct m_inode * inode, struct stat * statbuf)
{
	struct stat tmp;
	int i;

	verify_area(statbuf,sizeof (* statbuf));	// 验证文件信息缓冲区内存状态
	tmp.st_dev = inode->i_dev;
	tmp.st_ino = inode->i_num;
	tmp.st_mode = inode->i_mode;
	tmp.st_nlink = inode->i_nlinks;
	tmp.st_uid = inode->i_uid;
	tmp.st_gid = inode->i_gid;
	tmp.st_rdev = inode->i_zone[0];
	tmp.st_size = inode->i_size;
	tmp.st_atime = inode->i_atime;
	tmp.st_mtime = inode->i_mtime;
	tmp.st_ctime = inode->i_ctime;
	for (i=0 ; i<sizeof (tmp) ; i++)
		put_fs_byte(((char *) &tmp)[i],&((char *) statbuf)[i]);
}

/**
 * 获取指定文件信息
 * @param filename 文件全限定文件名
 * @param statbuf 存储 stat 文件状态结构指针
 * @return 成功 - 0，失败 - 错误号 * -1
*/
int sys_stat(char * filename, struct stat * statbuf)
{
	struct m_inode * inode;

	if (!(inode=namei(filename)))	// 获取文件名对应 i 节点
		return -ENOENT;
	cp_stat(inode,statbuf);			// 将指定文件信息存储到 statbuf 中
	iput(inode);					// 释放指定 i 节点
	return 0;
}

/**
 * 获取指定文件句柄对应的文件信息
 * @param fd 文件句柄
 * @param statbuf 存储 stat 文件状态结构指针
 * @return 成功 - 0，失败 - 错误号 * -1
*/
int sys_fstat(unsigned int fd, struct stat * statbuf)
{
	struct file * f;
	struct m_inode * inode;

	if (fd >= NR_OPEN || !(f=current->filp[fd]) || !(inode=f->f_inode))
		return -EBADF;
	cp_stat(inode,statbuf);
	return 0;
}
