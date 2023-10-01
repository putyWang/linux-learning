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
	if (fd >= NR_OPEN || !current->filp[fd])	// 旧文件不存在时，返回出错码
		return -EBADF;
	if (arg >= NR_OPEN)							// 新的最小句柄大于最大值时，返回出错码
		return -EINVAL;
	while (arg < NR_OPEN)						// 循环获取大于等于 arg 的最小空闲句柄
		i·f (current->filp[arg])
			arg++;
		else
			break;
	if (arg >= NR_OPEN)
		return -EMFILE;
	current->close_on_exec &= ~(1<<arg);		// 将指定句柄可关闭标志置位
	(current->filp[arg] = current->filp[fd])->f_count++;	// 将文件引用计数 +1，且新文件项指向旧文件
	return arg;
}

/**
 * 复制文件句柄系统调用函数
 * @param oldfd 旧句柄值
 * @param newfd 新句柄值
 * @return 新文件句柄或出错码
*/
int sys_dup2(unsigned int oldfd, unsigned int newfd)
{
	sys_close(newfd);			// 关闭新句柄文件
	return dupfd(oldfd,newfd);	// 将旧句柄文件复制到新句柄中
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

/**
 * 文件系统调用操作函数
 * @param fd 文件句柄
 * @param cmd 操作命令
 * @param arg 文件标志设置参数，0-关闭，1-设置
*/
int sys_fcntl(unsigned int fd, unsigned int cmd, unsigned long arg)
{	
	struct file * filp;

	if (fd >= NR_OPEN || !(filp = current->filp[fd]))	// 文件句柄必须合法
		return -EBADF;
	switch (cmd) {
		case F_DUPFD:									// 复制句柄命令
			return dupfd(fd,arg);
		case F_GETFD:									// 获取文件句柄的执行时关闭标志
			return (current->close_on_exec>>fd)&1;
		case F_SETFD:									// 设置句柄执行时关闭标志
			if (arg&1)
				current->close_on_exec |= (1<<fd);
			else
				current->close_on_exec &= ~(1<<fd);
			return 0;
		case F_GETFL:									// 获取文件标志和访问模式
			return filp->f_flags;
		case F_SETFL:									// 设置文件标志和访问模式（根据 arg 设置添加、非阻塞标志）
			filp->f_flags &= ~(O_APPEND | O_NONBLOCK);
			filp->f_flags |= arg & (O_APPEND | O_NONBLOCK);
			return 0;
		case F_GETLK:	case F_SETLK:	case F_SETLKW:	// 未实现
			return -1;
		default:
			return -1;
	}
}
