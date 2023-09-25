/*
 *  linux/kernel/sys.c
 *
 *  (C) 1991  Linus Torvalds
 */

#include <errno.h>

#include <linux/sched.h>
#include <linux/tty.h>
#include <linux/kernel.h>
#include <asm/segment.h>
#include <sys/times.h>
#include <sys/utsname.h>

// 返回日期和时间（未实现）
int sys_ftime()
{
	return -ENOSYS;
}

int sys_break()
{
	return -ENOSYS;
}

// 用于当前进程对子进程进行调试（未实现）
int sys_ptrace()
{
	return -ENOSYS;
}

// 改变并打印终端行设置（未实现）
int sys_stty()
{
	return -ENOSYS;
}

// 取终端行设置信息（未实现）
int sys_gtty()
{
	return -ENOSYS;
}

// 修改文件名（未实现）
int sys_rename()
{
	return -ENOSYS;
}

int sys_prof()
{
	return -ENOSYS;
}

/**
 * 设置当前任务的实际以及有效组 id
 * 如果任务没有超级用户特权，那么只能互换其实际组 ID 与有效组 ID，如果任务具有超级用户特权，就能任意设置有效和实际的组 ID
 * @param rgid 实际组 id
 * @param egid 有效组 id
*/
int sys_setregid(int rgid, int egid)
{
	if (rgid>0) {
		if ((current->gid == rgid) || 
		    suser())
			current->gid = rgid;
		else
			return(-EPERM);
	}
	if (egid>0) {
		if ((current->gid == egid) ||
		    (current->egid == egid) ||
		    (current->sgid == egid) ||
		    suser())
			current->egid = egid;
		else
			return(-EPERM);
	}
	return 0;
}

/**
 * 设置进程组号
 * @param gid 组 id
 * @return 0-成功，<0-错误码
*/
int sys_setgid(int gid)
{
	return(sys_setregid(gid, gid));
}

int sys_acct()
{
	return -ENOSYS;
}

int sys_phys()
{
	return -ENOSYS;
}

int sys_lock()
{
	return -ENOSYS;
}

int sys_mpx()
{
	return -ENOSYS;
}

int sys_ulimit()
{
	return -ENOSYS;
}

/**
 * 获取当前从 1970 到现在的时间（秒 ）
*/
int sys_time(long * tloc)
{
	int i;

	i = CURRENT_TIME;
	if (tloc) {
		verify_area(tloc,4);
		put_fs_long(i,(unsigned long *)tloc);
	}
	return i;
}

/**
 * 设置当前任务的实际以及有效用户 id
 * 如果任务没有超级用户特权，那么只能互换其实际用户 ID 与有效用户 ID，如果任务具有超级用户特权，就能任意设置有效和实际的用户 ID
 * @param ruid 实际用户 id
 * @param euid 有效用户 id
*/
int sys_setreuid(int ruid, int euid)
{
	int old_ruid = current->uid;
	
	if (ruid>0) {
		if ((current->euid==ruid) ||
                    (old_ruid == ruid) ||
		    suser())
			current->uid = ruid;
		else
			return(-EPERM);
	}
	if (euid>0) {
		if ((old_ruid == euid) ||
                    (current->euid == euid) ||
		    suser())
			current->euid = euid;
		else {
			current->uid = old_ruid;
			return(-EPERM);
		}
	}
	return 0;
}

/**
 * 设置用户号
 * @param uid 用户号
 * @return 0-成功，<0-错误码
*/
int sys_setuid(int uid)
{
	return(sys_setreuid(uid, uid));
}

/**
 * 设置系统时间与日期
 * @param tptr 从 1970 到现在的时间（秒 ）
 * @return 成功 0，失败 错误号
*/
int sys_stime(long * tptr)
{	// 必须具有超级用户权限
	if (!suser())
		return -EPERM;
	startup_time = get_fs_long((unsigned long *)tptr) - jiffies/HZ;
	return 0;
}

/**
 * 将当前任务时间保存到 tbuf 指向的指针
 * @param tbuf 时间保存地址地址
 * @return 当前时钟滴答数
*/
int sys_times(struct tms * tbuf)
{
	if (tbuf) {
		verify_area(tbuf,sizeof *tbuf);
		put_fs_long(current->utime,(unsigned long *)&tbuf->tms_utime);
		put_fs_long(current->stime,(unsigned long *)&tbuf->tms_stime);
		put_fs_long(current->cutime,(unsigned long *)&tbuf->tms_cutime);
		put_fs_long(current->cstime,(unsigned long *)&tbuf->tms_cstime);
	}
	return jiffies;
}

/**
 * 设置数据段末尾的值
 * @param end_data_seg 设置的数据段末尾值
 * @return 设置后的数据段末尾值
*/
int sys_brk(unsigned long end_data_seg)
{
	// 数据段末尾的值必须大于代码末尾的值
	// 数据段末尾的值与堆栈段之间的空闲空间必须大于 16 kb
	if (end_data_seg >= current->end_code &&
	    end_data_seg < current->start_stack - 16384)
		current->brk = end_data_seg;
	return current->brk;
}

/**
 * 将参数 pid 指定进程的进程组 ID 设置成 pgid
 * @param pid 进程 id
 * @param pgid 设置的进程组 id 
 * @return 0-成功，<0-错误码
*/
int sys_setpgid(int pid, int pgid)
{
	int i;

	// pid 为 0 时，表明设置的目标进程为 当前进程
	if (!pid)
		pid = current->pid;
	// pgid 为 0时，表示设置的进程组 id 为 本进程id
	if (!pgid)
		pgid = current->pid;
	for (i=0 ; i<NR_TASKS ; i++)
		if (task[i] && task[i]->pid==pid) {
			// 目标进程不能是首领
			if (task[i]->leader)
				return -EPERM;
			// 目标进程的会话必须与当前进程会话一致
			if (task[i]->session != current->session)
				return -EPERM;
			task[i]->pgrp = pgid;
			return 0;
		}
	return -ESRCH;
}

/**
 * 获取当前进程的进程组号
 * @return 当前进程的组号
*/
int sys_getpgrp(void)
{
	return current->pgrp;
}

/**
 * 创建一个会话（session），并且设置其会话 = 其组号 = 其进程号
 * @return 会话 id
*/
int sys_setsid(void)
{
	// 当前进程不能是 leader 
	// 且当前必须是超级用户
	if (current->leader && !suser())
		return -EPERM;
	current->leader = 1; // 将当前进程设置为新会话 leader
	current->session = current->pgrp = current->pid; // 设置当前会话 = 当前组号 = 当前进程号
	current->tty = -1; // 表示当前进程没有控制终端
	return current->pgrp;
}

/**
 * 获取系统信息
 * @param name 存储系统信息指针
*/
int sys_uname(struct utsname * name)
{
	static struct utsname thisname = {
		"linux .0","nodename","release ","version ","machine "
	};
	int i;

	if (!name) return -ERROR;
	verify_area(name,sizeof *name); // 验证 name 缓冲区是否超限
	for(i=0;i<sizeof *name;i++) // 逐字节复制系统信息
		put_fs_byte(((char *) &thisname)[i],i+(char *) name);
	return 0;
}

/**
 * 设置当前进程的文件创建属性屏蔽位
 * @param mask 设置的文件属性屏蔽码
 * @return 旧的文件屏蔽码
*/
int sys_umask(int mask)
{
	int old = current->umask;

	current->umask = mask & 0777;
	return (old);
}
