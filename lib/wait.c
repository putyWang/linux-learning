/*
 *  linux/lib/wait.c
 *
 *  (C) 1991  Linus Torvalds
 */

#define __LIBRARY__
#include <unistd.h>
#include <sys/wait.h>

/**
 * 函数实际为 pid_t waitpid(pid_t pid, int * wait_stat, int options)
 * 也是通过调用系统中断 80 对其进行调用
 * @param pid 等待被终止进程的进程
 * @param wait_stat 用于存放状态信息
 * @param options 选项信息，WN
*/
_syscall3(pid_t,waitpid,pid_t,pid,int *,wait_stat,int,options)

/**
 * wait 函数的系统调用，直接调用 waitpid 函数
*/
pid_t wait(int * wait_stat)
{
	return waitpid(-1,wait_stat,0);
}
