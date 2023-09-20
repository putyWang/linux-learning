/*
 *  linux/lib/write.c
 *
 *  (C) 1991  Linus Torvalds
 */

#define __LIBRARY__
#include <unistd.h>

/**
 * 写文件系统调用函数
 * 函数实际为 int write(int fd, char * buf, off_t count)
 * 也是通过调用系统中断 80 对其进行调用
 * @param fd 文件描述符
 * @param buf 写缓冲区指针
 * @param off_t 写字节数
 * @return 成功 - 写入字节数；出错 - -1，并设置对应的出错号
*/
_syscall3(int,write,int,fd,const char *,buf,off_t,count)
