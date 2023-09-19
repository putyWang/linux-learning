/*
 * 'tty.h' defines some structures used by tty_io.c and some defines.
 *
 * NOTE! Don't touch this without checking that nothing in rs_io.s or
 * con_io.s breaks. Some constants are hardwired into the system (mainly
 * offsets into 'tty_queue'
 */

#ifndef _TTY_H
#define _TTY_H

#include <termios.h>

#define TTY_BUF_SIZE 1024 // tty 缓冲区大小

/**
 * tty 等待队列数据结构
*/
struct tty_queue {
	unsigned long data; // 等待队列缓冲区中当前字符行数，串行通信时存放端口地址
	unsigned long head; // 缓冲区中数据头指针
	unsigned long tail; // 缓冲区中数据尾指针
	struct task_struct * proc_list; // 等待进程列表
	char buf[TTY_BUF_SIZE]; // 队列的缓冲区
};

#define INC(a) ((a) = ((a)+1) & (TTY_BUF_SIZE-1))        // 缓冲区指针向前移动一个字节
#define DEC(a) ((a) = ((a)-1) & (TTY_BUF_SIZE-1))        // 缓冲区指针向后移动一个字节
#define EMPTY(a) ((a).head == (a).tail)                  // 判断缓冲区是否为空
#define LEFT(a) (((a).tail-(a).head-1)&(TTY_BUF_SIZE-1)) //缓冲区还可存放字节长度
#define LAST(a) ((a).buf[(TTY_BUF_SIZE-1)&((a).head-1)]) // 缓冲区中最后一个位置
#define FULL(a) (!LEFT(a))                               // 判断指定队列缓冲区是否已满
#define CHARS(a) (((a).head-(a).tail)&(TTY_BUF_SIZE-1))  // 缓冲区中存放字节长度
#define GETCH(queue,c) \
(void)({c=(queue).buf[(queue).tail];INC((queue).tail);}) // 从缓冲区尾部取一字节
#define PUTCH(c,queue) \
(void)({(queue).buf[(queue).head]=(c);INC((queue).head);}) // 向缓冲区头部放一字节

// 判断终端键盘字符类型
#define INTR_CHAR(tty) ((tty)->termios.c_cc[VINTR])        // 中断符
#define QUIT_CHAR(tty) ((tty)->termios.c_cc[VQUIT])        // 退出符
#define ERASE_CHAR(tty) ((tty)->termios.c_cc[VERASE]) 	   // 擦除符
#define KILL_CHAR(tty) ((tty)->termios.c_cc[VKILL])        // 终止符
#define EOF_CHAR(tty) ((tty)->termios.c_cc[VEOF])          // 文件结束符
#define START_CHAR(tty) ((tty)->termios.c_cc[VSTART])      // 开始符
#define STOP_CHAR(tty) ((tty)->termios.c_cc[VSTOP])        // 结束符
#define SUSPEND_CHAR(tty) ((tty)->termios.c_cc[VSUSP])     // 挂起符

/**
 * tty 数据结构
*/
struct tty_struct {
	struct termios termios; // 终端 io 属性和控制字符数据结构
	int pgrp; // 所属进程组
	int stopped; // 停止标志
	void (*write)(struct tty_struct * tty); // tty 写函数指针
	struct tty_queue read_q; // tty 读队列
	struct tty_queue write_q; // tty 写队列
	struct tty_queue secondary; // tty 辅助队列 （存放规范模式字符序列），也可称为规范（熟）模式队列
	};

extern struct tty_struct tty_table[]; // tty 结构数组

/*	intr=^C		quit=^|		erase=del	kill=^U
	eof=^D		vtime=\0	vmin=\1		sxtc=\0
	start=^Q	stop=^S		susp=^Z		eol=\0
	reprint=^R	discard=^U	werase=^W	lnext=^V
	eol2=\0
*/ // 控制字符对应的 ASCII 码值，[8 进制]
#define INIT_C_CC "\003\034\177\025\004\0\1\0\021\023\032\0\022\017\027\026\0" // 

void rs_init(void);
void con_init(void);
void tty_init(void);

int tty_read(unsigned c, char * buf, int n);
int tty_write(unsigned c, char * buf, int n);

void rs_write(struct tty_struct * tty);
void con_write(struct tty_struct * tty);

void copy_to_cooked(struct tty_struct * tty);

#endif
