/*
 *  linux/lib/dup.c
 *
 *  (C) 1991  Linus Torvalds
 */

#define __LIBRARY__
#include <unistd.h>

/**
 * 函数实际为 int dup(int fd)
 * 也是通过调用系统中断 80 对其进行调用
*/
_syscall1(int,dup,int,fd)
