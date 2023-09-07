/*
 *  linux/fs/open.c
 *
 *  (C) 1991  Linus Torvalds
 */

#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <utime.h>
#include <sys/stat.h>

#include <linux/sched.h>
#include <linux/tty.h>
#include <linux/kernel.h>
#include <asm/segment.h>

int sys_ustat(int dev, struct ustat * ubuf)
{
	return -ENOSYS;
}

int sys_utime(char * filename, struct utimbuf * times)
{
	struct m_inode * inode;
	long actime,modtime;

	if (!(inode=namei(filename)))
		return -ENOENT;
	if (times) {
		actime = get_fs_long((unsigned long *) &times->actime);
		modtime = get_fs_long((unsigned long *) &times->modtime);
	} else
		actime = modtime = CURRENT_TIME;
	inode->i_atime = actime;
	inode->i_mtime = modtime;
	inode->i_dirt = 1;
	iput(inode);
	return 0;
}

/*
 * XXX should we use the real or effective uid?  BSD uses the real uid,
 * so as to make this call useful to setuid programs.
 */
int sys_access(const char * filename,int mode)
{
	struct m_inode * inode;
	int res, i_mode;

	mode &= 0007;
	if (!(inode=namei(filename)))
		return -EACCES;
	i_mode = res = inode->i_mode & 0777;
	iput(inode);
	if (current->uid == inode->i_uid)
		res >>= 6;
	else if (current->gid == inode->i_gid)
		res >>= 6;
	if ((res & 0007 & mode) == mode)
		return 0;
	/*
	 * XXX we are doing this test last because we really should be
	 * swapping the effective with the real user id (temporarily),
	 * and then calling suser() routine.  If we do call the
	 * suser() routine, it needs to be called last. 
	 */
	if ((!current->uid) &&
	    (!(mode & 1) || (i_mode & 0111)))
		return 0;
	return -EACCES;
}

int sys_chdir(const char * filename)
{
	struct m_inode * inode;

	if (!(inode = namei(filename)))
		return -ENOENT;
	if (!S_ISDIR(inode->i_mode)) {
		iput(inode);
		return -ENOTDIR;
	}
	iput(current->pwd);
	current->pwd = inode;
	return (0);
}

int sys_chroot(const char * filename)
{
	struct m_inode * inode;

	if (!(inode=namei(filename)))
		return -ENOENT;
	if (!S_ISDIR(inode->i_mode)) {
		iput(inode);
		return -ENOTDIR;
	}
	iput(current->root);
	current->root = inode;
	return (0);
}

int sys_chmod(const char * filename,int mode)
{
	struct m_inode * inode;

	if (!(inode=namei(filename)))
		return -ENOENT;
	if ((current->euid != inode->i_uid) && !suser()) {
		iput(inode);
		return -EACCES;
	}
	inode->i_mode = (mode & 07777) | (inode->i_mode & ~07777);
	inode->i_dirt = 1;
	iput(inode);
	return 0;
}

int sys_chown(const char * filename,int uid,int gid)
{
	struct m_inode * inode;

	if (!(inode=namei(filename)))
		return -ENOENT;
	if (!suser()) {
		iput(inode);
		return -EACCES;
	}
	inode->i_uid=uid;
	inode->i_gid=gid;
	inode->i_dirt=1;
	iput(inode);
	return 0;
}

/**
 * 打开（创建）指定文件函数
 * @param filename 文件名
 * @param flag 打开文件标志（只读 O_RDONLY, 只写 O_WRONLY, 读写 O_RDWR）
 * @param mode 模式（创建文件时指定文件使用的许可属性）
*/
int sys_open(const char * filename,int flag,int mode)
{
	struct m_inode * inode;
	struct file * f;
	int i,fd;

	// 将用户设置的模式与进程的模式屏蔽码相与获取创建文件的模式码
	mode &= 0777 & ~current->umask;
	// 查找进程打开文件中的空闲项
	for(fd=0 ; fd<NR_OPEN ; fd++)
		if (!current->filp[fd])
			break;
	// 进程已无打开文件空闲项时，返回错误
	if (fd>=NR_OPEN)
		return -EINVAL;
	// 设置执行时关闭文件句柄位图，复位对应比特位
	current->close_on_exec &= ~(1<<fd);
	// 查询文件表中空闲项，没有空闲项时报错
	f=0+file_table;
	for (i=0 ; i<NR_FILE ; i++,f++)
		if (!f->f_count) break;
	if (i>=NR_FILE)
		return -EINVAL;
	// 让进程对应打开文件指向指定空闲文件
	(current->filp[fd]=f)->f_count++;
	// 打开指定文件
	if ((i=open_namei(filename,flag,mode,&inode))<0) {
		current->filp[fd]=NULL; // 将指定句柄设置为 Null
		f->f_count=0;
		return i; // 返回异常
	}
/* ttys are somewhat special (ttyxx major==4, tty major==5) */
	// 如果是字符设备文件
	if (S_ISCHR(inode->i_mode))
		// 设备号为 4 
		if (MAJOR(inode->i_zone[0])==4) {
			if (current->leader && current->tty<0) {
				current->tty = MINOR(inode->i_zone[0]); // 设置当前进程的 tty 号为该 i 节点的子设备号
				tty_table[current->tty].pgrp = current->pgrp; // tty 设备表项的进程组号对应 当前进程的进程组号
			}
		// 设备号为 5 且 没有tty 则出错
		} else if (MAJOR(inode->i_zone[0])==5)
			if (current->tty<0) {
				iput(inode); // 释放该 i 节点
				current->filp[fd]=NULL; // 当前设备打开文件设置为空
				f->f_count=0; //文件引用数清空
				return -EPERM;
			}
/* Likewise with block-devices: check for floppy_change */
	// 块设备检查盘片是否更换
	if (S_ISBLK(inode->i_mode))
		check_disk_change(inode->i_zone[0]);
	// 初始化文件结构，返回文件句柄
	f->f_mode = inode->i_mode;
	f->f_flags = flag;
	f->f_count = 1;
	f->f_inode = inode;
	f->f_pos = 0;
	return (fd);
}

int sys_creat(const char * pathname, int mode)
{
	return sys_open(pathname, O_CREAT | O_TRUNC, mode);
}

/**
 * 关闭指定句柄文件
 * @param fd 将要关闭的句柄
 * @return 成功 - 0，或错误号
*/
int sys_close(unsigned int fd)
{	
	struct file * filp;

	if (fd >= NR_OPEN)
		return -EINVAL;
	// 将本进程对应句柄需要关闭文件位图置 0
	current->close_on_exec &= ~(1<<fd);
	// 获取将要关闭的文件
	if (!(filp = current->filp[fd]))
		return -EINVAL;
	current->filp[fd] = NULL; // 清空进程对应文件使用项
	if (filp->f_count == 0)
		panic("Close: file count is 0");
	if (--filp->f_count) // 文件引用计数 -1
		return (0);
	iput(filp->f_inode); // 文件引用计数为 0 时，释放对应 i 节点
	return (0);
}
