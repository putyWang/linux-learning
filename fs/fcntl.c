/*
 *  linux/fs/fcntl.c
 *
 *  (C) 1991  Linus Torvalds
 */

#include <string.h>
#include <errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <asm/segment.h>

#include <fcntl.h>
#include <sys/stat.h>

extern int sys_close(int fd);

/**
 * 复制文件句柄
 * @param fd 将被复制的文件句柄
 * @param arg 指定文件句柄的最小数值
 * @return 新文件句柄或出错码
*/
static int dupfd(unsigned int fd, unsigned int arg)
{
	// 旧文件不存在时，返回出错码
	if (fd >= NR_OPEN || !current->filp[fd])
		return -EBADF;
	// 新的最小句柄大于最大值时，返回出错码
	if (arg >= NR_OPEN)
		return -EINVAL;
	// 循环获取大于等于 arg 的最小空闲句柄
	while (arg < NR_OPEN)
		if (current->filp[arg])
			arg++;
		else
			break;
	if (arg >= NR_OPEN)
		return -EMFILE;
	// 将指定句柄可关闭标志置位
	current->close_on_exec &= ~(1<<arg);
	// 将文件引用计数 +1，且新文件项指向旧文件
	(current->filp[arg] = current->filp[fd])->f_count++;
	return arg;
}

int sys_dup2(unsigned int oldfd, unsigned int newfd)
{
	sys_close(newfd);
	return dupfd(oldfd,newfd);
}

/**
 * 复制文件句柄系统调用函数
 * @param fildes 需要复制的文件句柄
 * @return 当前最小的未用句柄
*/
int sys_dup(unsigned int fildes)
{
	return dupfd(fildes,0);
}

int sys_fcntl(unsigned int fd, unsigned int cmd, unsigned long arg)
{	
	struct file * filp;

	if (fd >= NR_OPEN || !(filp = current->filp[fd]))
		return -EBADF;
	switch (cmd) {
		case F_DUPFD:
			return dupfd(fd,arg);
		case F_GETFD:
			return (current->close_on_exec>>fd)&1;
		case F_SETFD:
			if (arg&1)
				current->close_on_exec |= (1<<fd);
			else
				current->close_on_exec &= ~(1<<fd);
			return 0;
		case F_GETFL:
			return filp->f_flags;
		case F_SETFL:
			filp->f_flags &= ~(O_APPEND | O_NONBLOCK);
			filp->f_flags |= arg & (O_APPEND | O_NONBLOCK);
			return 0;
		case F_GETLK:	case F_SETLK:	case F_SETLKW:
			return -1;
		default:
			return -1;
	}
}
