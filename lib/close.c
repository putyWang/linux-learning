/*
 *  linux/lib/close.c
 *
 *  (C) 1991  Linus Torvalds
 */

#define __LIBRARY__
#include <unistd.h>

/**
 * 关闭文件函数·
 * 该调用函数，对应 int close(int fd)
 * 直接通过调用系统中断 80 来对 sys_close 函数进行调用
*/
_syscall1(int,close,int,fd)
