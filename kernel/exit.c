/*
 *  linux/kernel/exit.c
 *
 *  (C) 1991  Linus Torvalds
 */

#include <errno.h>
#include <signal.h>
#include <sys/wait.h>

#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/tty.h>
#include <asm/segment.h>

int sys_pause(void);
int sys_close(int fd);

/**
 * 释放指定进程
 * @param p 释放目标进程的数据结构
*/
void release(struct task_struct * p)
{
	int i;

	if (!p)
		return;
	for (i=1 ; i<NR_TASKS ; i++)
		if (task[i]==p) {
			task[i]=NULL; // 将指定进程表项设置为空
			free_page((long)p); // 释放指定进程所占用内存页
			schedule(); // 重新调度
			return;
		}
	panic("trying to release non-existent task");
}

/**
 * 向指定进程发送信号
 * @param sig 信号类型
 * @param p 目标进程结构
 * @param priv 权限码
*/
static inline int send_sig(long sig,struct task_struct * p,int priv)
{
	// 信号值必须在 1 到 32 之间
	if (!p || sig<1 || sig>32)
		return -EINVAL;
	// 只有 priv 标志置位、目标进程的有效用户与当前进程的有效用户一致或管理员用户才能发送信号
	if (priv || (current->euid==p->euid) || suser())
		p->signal |= (1<<(sig-1));
	else
		return -EPERM;
	return 0;
}

/**
 * 终止会话
*/
static void kill_session(void)
{
	struct task_struct **p = NR_TASKS + task;
	// 从后向前遍历所有进程，向所有与当前进程处于同一会话的进程发送挂断进程信号
	while (--p > &FIRST_TASK) {
		if (*p && (*p)->session == current->session)
			(*p)->signal |= 1<<(SIGHUP-1);
	}
}

/*
 * XXX need to check permissions needed to send signals to process
 * groups, etc. etc.  kill() permissions semantics are tricky!
 */
int sys_kill(int pid,int sig)
{
	struct task_struct **p = NR_TASKS + task;
	int err, retval = 0;

	if (!pid) while (--p > &FIRST_TASK) {
		if (*p && (*p)->pgrp == current->pid) 
			if (err=send_sig(sig,*p,1))
				retval = err;
	} else if (pid>0) while (--p > &FIRST_TASK) {
		if (*p && (*p)->pid == pid) 
			if (err=send_sig(sig,*p,0))
				retval = err;
	} else if (pid == -1) while (--p > &FIRST_TASK)
		if (err = send_sig(sig,*p,0))
			retval = err;
	else while (--p > &FIRST_TASK)
		if (*p && (*p)->pgrp == -pid)
			if (err = send_sig(sig,*p,0))
				retval = err;
	return retval;
}

/**
 * 通知父进程本进程即将结束
 * @param pid 父进程 id
*/
static void tell_father(int pid)
{
	int i;

	if (pid)
		for (i=0;i<NR_TASKS;i++) {
			if (!task[i])
				continue;
			if (task[i]->pid != pid)
				continue;
			task[i]->signal |= (1<<(SIGCHLD-1));
			return;
		}
/* if we don't find any fathers, we just release ourselves */
/* This is not really OK. Must change it to make father 1 */
	printk("BAD BAD - no father found\n\r");
	release(current);
}

/**
 * 程序退出处理程序
 * @param code 错误码
 * @return 
*/
int do_exit(long code)
{
	int i;

	// 释放当前进程代码段和数据段所占用的内存页
	free_page_tables(get_base(current->ldt[1]),get_limit(0x0f));
	free_page_tables(get_base(current->ldt[2]),get_limit(0x17));
	// 将本进程所有子进程父进程设置为 1，
	// 当子进程已经僵死时，则向该子进程发送终止信号
	for (i=0 ; i<NR_TASKS ; i++)
		if (task[i] && task[i]->father == current->pid) {
			task[i]->father = 1;
			if (task[i]->state == TASK_ZOMBIE)
				/* assumption task[1] is always init */
				(void) send_sig(SIGCHLD, task[1], 1);
		}
	// 关闭当前进程所有打开文件
	for (i=0 ; i<NR_OPEN ; i++)
		if (current->filp[i])
			sys_close(i);
	// 释放当前工作目录
	iput(current->pwd);
	current->pwd=NULL;
	// 释放当前进程根目录
	iput(current->root);
	current->root=NULL;
	// 释放当前执行 i 节点
	iput(current->executable);
	current->executable=NULL;
	// 当前进程为领头进程且其有控制的终端则释放该终端
	if (current->leader && current->tty >= 0)
		tty_table[current->tty].pgrp = 0;
	// 进程上次使用过协处理器，则置空
	if (last_task_used_math == current)
		last_task_used_math = NULL;
	// 当前进程为领头进程时，发送终止会话信号
	if (current->leader)
		kill_session();
	// 将当前进程设置为僵死状态
	current->state = TASK_ZOMBIE;
	// 设置退出代码
	current->exit_code = code;
	// 向父进程发送即将停止信号
	tell_father(current->father);
	// 重新执行调度程序
	schedule();
	return (-1);	/* just to suppress warnings */
}

int sys_exit(int error_code)
{
	return do_exit((error_code&0xff)<<8);
}

/**
 * 挂起当前进程指定指定子进程退出、收到要求终止该进程的信号或是需要调用一个信号句柄为止
 * @param pid 需要等待的指定子进程号或者进程组号等
 * @param stat_addr 返回的状态信息地址
 * @return 匹配的进程号
*/
int sys_waitpid(pid_t pid,unsigned long * stat_addr, int options)
{
	int flag, code;
	struct task_struct ** p;

	verify_area(stat_addr,4);
repeat:
	flag=0;
	// 从后向前遍历所有进程
	for(p = &LAST_TASK ; p > &FIRST_TASK ; --p) {
		if (!*p || *p == current) // 跳过空进程与本进程项
			continue;
		if ((*p)->father != current->pid) // 父进程不是当前进程跳过
			continue;
		if (pid>0) {
			if ((*p)->pid != pid) // 当前 pid > 0 且 不等于指定 pid 跳过
				continue;
		} else if (!pid) { // pid == 0 但进程组号与当前进程组号不一致则跳过
			if ((*p)->pgrp != current->pgrp)
				continue;
		} else if (pid != -1) { // pid < -1 但进程组号不等于pid绝对值时跳过
			if ((*p)->pgrp != -pid)
				continue;
		}
		switch ((*p)->state) {
			// 进程停止时，置状态信息为 0x7f，返回选择的进程号
			case TASK_STOPPED:
				if (!(options & WUNTRACED))
					continue;
				put_fs_long(0x7f,stat_addr);
				return (*p)->pid;
			// 进程僵死时
			case TASK_ZOMBIE:
				current->cutime += (*p)->utime; // 子进程用户态运行时间加到本进程
				current->cstime += (*p)->stime; // 子进程系统态运行时间加到本进程
				flag = (*p)->pid; // 
				code = (*p)->exit_code;
				release(*p); // 释放对应僵死进程
				put_fs_long(code,stat_addr); // 将状态信息设置为子进程退出信息
				return flag;
			// 其余情况继续
			default:
				flag=1;
				continue;
		}
	}
	// 存在指定子进程，但没有停止或者僵死
	if (flag) {
		// options = WNOHANG 直接返回
		if (options & WNOHANG)
			return 0;
		// 当前进程状态设置为可中断等待状态
		current->state=TASK_INTERRUPTIBLE;
		// 重新调度进程
		schedule();
		// 收到除 SIGCHLD 的信号，重复处理，否则返回错误码
		if (!(current->signal &= ~(1<<(SIGCHLD-1))))
			goto repeat;
		else
			return -EINTR;
	}
	return -ECHILD;
}


