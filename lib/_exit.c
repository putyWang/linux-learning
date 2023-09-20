/*
 *  linux/lib/_exit.c
 *
 *  (C) 1991  Linus Torvalds
 */

#define __LIBRARY__
#include <unistd.h>

/**
 * 内核中使用的退出终止函数
 * @param exit_code 退出码
*/
volatile void _exit(int exit_code)
{
	__asm__("int $0x80"::"a" (__NR_exit),"b" (exit_code)); // 使用系统中断 80，中断调用 sys_exit 函数
}
