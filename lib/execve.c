/*
 *  linux/lib/execve.c
 *
 *  (C) 1991  Linus Torvalds
 */

#define __LIBRARY__
#include <unistd.h>

/**
 * 函数实际为 int execve(char * file, char ** argv, char ** envp)
 * 也是通过调用系统中断 80 对其进行调用
*/
_syscall3(int,execve,const char *,file,char **,argv,char **,envp)
