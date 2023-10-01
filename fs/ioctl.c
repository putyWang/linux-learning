/*
 *  linux/fs/ioctl.c
 *
 *  (C) 1991  Linus Torvalds
 */

#include <string.h>
#include <errno.h>
#include <sys/stat.h>

#include <linux/sched.h>

extern int tty_ioctl(int dev, int cmd, int arg);

typedef int (*ioctl_ptr)(int dev,int cmd,int arg);

#define NRDEVS ((sizeof (ioctl_table))/(sizeof (ioctl_ptr)))	// 系统中设备种数

/**
 * ioctl 操作函数指针表
*/
static ioctl_ptr ioctl_table[]={
	NULL,		/* nodev */
	NULL,		/* /dev/mem */
	NULL,		/* /dev/fd */
	NULL,		/* /dev/hd */
	tty_ioctl,	// 串行端口设备
	tty_ioctl,	// 终端设备
	NULL,		/* /dev/lp */
	NULL};		/* named pipes */
	
/**
 * 输入输出控制系统调用函数
 * @param fd 文件描述符句柄文件
 * @param cmd 命令
 * @param arg 参数
*/
int sys_ioctl(unsigned int fd, unsigned int cmd, unsigned long arg)
{	
	struct file * filp;
	int dev,mode;

	if (fd >= NR_OPEN || !(filp = current->filp[fd]))	// fd 描述符合法
		return -EBADF;
	mode=filp->f_inode->i_mode;							// 获取指定文件的类型和读写控制参数
	if (!S_ISCHR(mode) && !S_ISBLK(mode))				// 只能对字符设备文件与块文件设备
		return -EINVAL;
	dev = filp->f_inode->i_zone[0];						// 获取设备号
	if (MAJOR(dev) >= NRDEVS)							// 设备号合法
		return -ENODEV;
	if (!ioctl_table[MAJOR(dev)])						// ioctl 操作函数不能为空
		return -ENOTTY;
	return ioctl_table[MAJOR(dev)](dev,cmd,arg);		// 调用对应 ioctl 操作函数
}
