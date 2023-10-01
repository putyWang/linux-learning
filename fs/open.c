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

/**
 * 取文件系统信息调用函数（未实现）
 * @param dev 设备号
 * @param ubuf 保存文件系统信息数据结构指针
*/
int sys_ustat(int dev, struct ustat * ubuf)
{
	return -ENOSYS;
}

/**
 * 设置指定文件时间信息
 * @param filename 指定文件全限定文件名
 * @param times 文件相关时间信息数据结构指针
 * @return 成功 - 0，失败 - 错误号 * -1
*/
int sys_utime(char * filename, struct utimbuf * times)
{
	struct m_inode * inode;
	long actime,modtime;

	if (!(inode=namei(filename)))	// 获取指定文件 i 节点
		return -ENOENT;
	if (times) {					// 指定时间信息指针不为空，则从其中获取文件访问时间和修改时间字段
		actime = get_fs_long((unsigned long *) &times->actime);		// 获取访问时间
		modtime = get_fs_long((unsigned long *) &times->modtime);	// 获取修改时间
	} else
		actime = modtime = CURRENT_TIME;	// 否则访问时间与修改时间都设置为当前时间
	inode->i_atime = actime;				// 设置 i 节点访问时间
	inode->i_mtime = modtime;				// 设置 i 节点更新时间
	inode->i_dirt = 1;						// 置脏标志位
	iput(inode);							// 更新磁盘上 i 节点信息
	return 0;
}

/**
 * 检查文件访问权限
 * @param filename 指定文件全限定文件名
 * @param mode 文件权限屏蔽码
 * @return 拥有权限 - 0，没有权限 - 错误码 * -1
*/
int sys_access(const char * filename,int mode)
{
	struct m_inode * inode;
	int res, i_mode;

	mode &= 0007;						// 获取权限屏蔽位
	if (!(inode=namei(filename)))		// 获取指定文件 i 节点
		return -EACCES;
	i_mode = res = inode->i_mode & 0777;// 获取指定文件的所有权限位数据
	iput(inode);						// 释放 i 节点
	if (current->uid == inode->i_uid)	// 设置有效的有效用户权限
		res >>= 6;
	else if (current->gid == inode->i_gid)	// 设置用户组相关属性
		res >>= 6;
	if ((res & 0007 & mode) == mode)	// 如果对应属性包含 mode 需要的所有属性时返回拥有权限
		return 0;
	/*
	 * XXX we are doing this test last because we really should be
	 * swapping the effective with the real user id (temporarily),
	 * and then calling suser() routine.  If we do call the
	 * suser() routine, it needs to be called last. 
	 */
	if ((!current->uid) &&
	    (!(mode & 1) || (i_mode & 0111)))	// 但如果当前进程用户 id 为 0（超级管理员用户）并且 mode 执行为是 0，或文件可以被任何人访问，则返回 0
		return 0;
	return -EACCES;
}

/**
 * 改变当前工作目录系统调用函数
 * @param filename 目录
 * @return 成功 - 0，失败 - 错误码 * -1
*/
int sys_chdir(const char * filename)
{
	struct m_inode * inode;

	if (!(inode = namei(filename)))	// 获取指定文件 i 节点
		return -ENOENT;
	if (!S_ISDIR(inode->i_mode)) {	// 指定文件必须为文件夹
		iput(inode);
		return -ENOTDIR;
	}
	iput(current->pwd);				// 释放当前工作目录 i 节点
	current->pwd = inode;			// 将当前工作目录指针指向指定文件夹的 i 节点
	return (0);
}

/**
 * 改变当前根目录系统调用函数
 * @param filename 目录
 * @return 成功 - 0，失败 - 错误码 * -1
*/
int sys_chroot(const char * filename)
{
	struct m_inode * inode;

	if (!(inode=namei(filename)))	// 获取指定文件 i 节点
		return -ENOENT;
	if (!S_ISDIR(inode->i_mode)) {	// 指定文件必须为文件夹
		iput(inode);
		return -ENOTDIR;
	}
	iput(current->root);			// 释放根目录 i 节点
	current->root = inode;			// 将当前根目录指针指向指定文件夹的 i 节点
	return (0);
}

/**
 * 改变指定文件的权限位
 * @param filename 指定文件全限定文件名
 * @param mode 文件权限屏蔽码
 * @return 成功 - 0，失败 - 错误码 * -1
*/
int sys_chmod(const char * filename,int mode)
{
	struct m_inode * inode;

	if (!(inode=namei(filename)))						// 获取指定文件 i 节点
		return -ENOENT;
	if ((current->euid != inode->i_uid) && !suser()) {	// 只有超级管理员以及文件的所有者才能修改文件文件属性
		iput(inode);
		return -EACCES;
	}
	inode->i_mode = (mode & 07777) | (inode->i_mode & ~07777);	// 设置对应 i 节点文件属性
	inode->i_dirt = 1;											// i 节点脏标志置位
	iput(inode);
	return 0;
}

/**
 * 修改文件宿主系统调用函数
 * @param filename 指定文件全限定文件名
 * @param uid 用户标识符
 * @param gid 组 id 
 * @return 成功 - 0，失败 - 错误码 * -1
*/
int sys_chown(const char * filename,int uid,int gid)
{
	struct m_inode * inode;

	if (!(inode=namei(filename)))	// 获取指定文件 i 节点
		return -ENOENT;
	if (!suser()) {					// 只有超级管理员能设置
		iput(inode);
		return -EACCES;
	}
	inode->i_uid=uid;				// 设置文件所属用户
	inode->i_gid=gid;				// 设置文件所属组
	inode->i_dirt=1;				// 文件脏位置位
	iput(inode);					// 释放 i 节点
	return 0;
}

/**
 * 打开（创建）指定文件函数
 * @param filename 文件名
 * @param flag 打开文件标志（只读 O_RDONLY, 只写 O_WRONLY, 读写 O_RDWR）
 * @param mode 模式（创建文件时指定文件使用的许可属性）
 * @return 文件句柄
*/
int sys_open(const char * filename,int flag,int mode)
{
	struct m_inode * inode;
	struct file * f;
	int i,fd;

	mode &= 0777 & ~current->umask;		// 将用户设置的模式与进程的模式屏蔽码相与获取创建文件的模式码
	for(fd=0 ; fd<NR_OPEN ; fd++)		// 查找进程打开文件中的空闲项
		if (!current->filp[fd])
			break;
	if (fd>=NR_OPEN)					// 进程已无打开文件空闲项时，返回错误
		return -EINVAL;
	current->close_on_exec &= ~(1<<fd);	// 设置执行时关闭文件句柄位图，复位对应比特位
	f=0+file_table;
	for (i=0 ; i<NR_FILE ; i++,f++)		// 查询系统文件表中空闲项，没有空闲项时报错
		if (!f->f_count) break;
	if (i>=NR_FILE)
		return -EINVAL;
	(current->filp[fd]=f)->f_count++;	// 获取到的空闲文件项引用计数 +1
	if ((i=open_namei(filename,flag,mode,&inode))<0) {	// 打开（创建）指定目录项
		current->filp[fd]=NULL; 		// 将指定句柄设置为 Null
		f->f_count=0;
		return i; 						// 返回异常
	}
	if (S_ISCHR(inode->i_mode))			// 字符设备文件
		if (MAJOR(inode->i_zone[0])==4) {						// 设备号为 4 (终端设备)
			if (current->leader && current->tty<0) {
				current->tty = MINOR(inode->i_zone[0]); 		// 设置当前进程的 tty 号为该 i 节点的子设备号
				tty_table[current->tty].pgrp = current->pgrp; 	// tty 设备表项的进程组号对应 当前进程的进程组号
			}
		} else if (MAJOR(inode->i_zone[0])==5)					// 设备号为 5 且 没有 tty 则出错
			if (current->tty<0) {
				iput(inode); 									// 释放该 i 节点
				current->filp[fd]=NULL; 						// 当前设备打开文件设置为空
				f->f_count=0; 									//文件引用数清空
				return -EPERM;
			}
	if (S_ISBLK(inode->i_mode))	// 块设备需要检查盘片是否更换
		check_disk_change(inode->i_zone[0]);
	f->f_mode = inode->i_mode;	// 设置文件类型信息
	f->f_flags = flag;			// 设置文件打开标志
	f->f_count = 1;				// 文件引用计数置为 1
	f->f_inode = inode;			// 指向指定 i 节点
	f->f_pos = 0;				// 初始化文件指针偏移量
	return (fd);				// 返回文件句柄
}

/**
 * 创建文件系统调用函数
 * @param filename 文件名
 * @param mode 模式（创建文件时指定文件使用的许可属性）
 * @return 文件句柄
*/
int sys_creat(const char * pathname, int mode)
{
	return sys_open(pathname, O_CREAT | O_TRUNC, mode);	// 使用 O_CREAT 与 O_TRUNC 创建指定文件
}

/**
 * 根据文件句柄关闭指定文件
 * @param fd 将要关闭的句柄
 * @return 成功 - 0，或错误号
*/
int sys_close(unsigned int fd)
{	
	struct file * filp;

	if (fd >= NR_OPEN)
		return -EINVAL;
	current->close_on_exec &= ~(1<<fd);	// 将本进程对应句柄需要关闭文件位图置 0
	if (!(filp = current->filp[fd]))	// 获取将要关闭的文件
		return -EINVAL;
	current->filp[fd] = NULL; 			// 清空进程对应文件使用项
	if (filp->f_count == 0)				// 文件引用计数为 0 时，系统出错死机
		panic("Close: file count is 0");
	if (--filp->f_count) 				// 文件引用计数 -1
		return (0);
	iput(filp->f_inode); 				// 文件引用计数为 0 时，释放对应 i 节点
	return (0);
}
