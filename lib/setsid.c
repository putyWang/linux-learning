/*
 *  linux/lib/setsid.c
 *
 *  (C) 1991  Linus Torvalds
 */

#define __LIBRARY__
#include <unistd.h>

/**
 * 创建一个会话并设置进程组号
 * 函数实际为 pid_t setsid()
 * 也是通过调用系统中断 80 对其进行调用
 * @return 调用进程的会话标识符
*/
_syscall0(pid_t,setsid)
