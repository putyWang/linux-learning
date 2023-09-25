/*
 *  linux/kernel/chr_drv/tty_ioctl.c
 *
 *  (C) 1991  Linus Torvalds
 */

#include <errno.h>
#include <termios.h>

#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/tty.h>

#include <asm/io.h>
#include <asm/segment.h>
#include <asm/system.h>

// 波特率因子数组（或称为除数数组）
static unsigned short quotient[] = {
	0, 2304, 1536, 1047, 857,
	768, 576, 384, 192, 96,
	64, 48, 24, 12, 6, 3
};

/**
 * 修改传输数率
 * @param tty 终端对应的 tty 数据结构
*/
static void change_speed(struct tty_struct * tty)
{
	unsigned short port,quot;
	// 串口终端，其 tty 结构的读缓冲队列 data 字段存放的串行端口号（0x3f8 或 0x2f8）
	if (!(port = tty->read_q.data))
		return;
	quot = quotient[tty->termios.c_cflag & CBAUD];	// 从波特率因子数组中取得对应的波特率因子值
	cli();											// 关中断
	outb_p(0x80,port+3);							// 设置除数锁定标志
	outb_p(quot & 0xff,port);						// 输出因子低字节
	outb_p(quot >> 8,port+1);						// 输出因子高字节
	outb(0x03,port+3);								// 复位 DLAB
	sti();											// 开中断
}

/**
 * 刷新 tty 缓冲队列
 * @param queue 要刷新的缓冲队列
*/
static void flush(struct tty_queue * queue)
{
	cli();						// 关中断
	queue->head = queue->tail;	// 头指针指向尾指针
	sti();						// 开中断
}

/**
 * 等待字符发送出去
 * @param tty 终端对应的 tty 数据结构
*/
static void wait_until_sent(struct tty_struct * tty)
{
	/* do nothing - not implemented */
}

/**
 * 发送 BREAK 控制符
 * @param tty 终端对应的 tty 数据结构
*/
static void send_break(struct tty_struct * tty)
{
	/* do nothing - not implemented */
}

/**
 * 取终端 termios 结构信息
 * @param tty 终端对应的 tty 数据结构
 * @param termios 用户数据区 termios 结构缓冲区指针
 * @return 0
*/
static int get_termios(struct tty_struct * tty, struct termios * termios)
{
	int i;

	verify_area(termios, sizeof (*termios));							// 验证指定缓冲区内存是否能使用
	for (i=0 ; i< (sizeof (*termios)) ; i++)
		put_fs_byte( ((char *)&tty->termios)[i] , i+(char *)termios );	// 将指定 tty 信息复制到 termios 所指向位置
	return 0;
}

/**
 * 设置终端 termios 结构信息
 * @param tty 终端对应的 tty 数据结构
 * @param termios 用户数据区 termios 结构缓冲区指针
 * @return 0
*/
static int set_termios(struct tty_struct * tty, struct termios * termios)
{
	int i;

	for (i=0 ; i< (sizeof (*termios)) ; i++)
		((char *)&tty->termios)[i]=get_fs_byte(i+(char *)termios);	// 将termios 所指向位置复制到 tty 指向位置
	change_speed(tty);												// 修改传输数率
	return 0;
}

/**
 * 取终端 termio 结构信息
 * @param tty 终端对应的 tty 数据结构
 * @param termio 用户数据区 termio 结构缓冲区指针
 * @return 0
*/
static int get_termio(struct tty_struct * tty, struct termio * termio)
{
	int i;
	struct termio tmp_termio;

	verify_area(termio, sizeof (*termio));			// 验证指定缓冲区内存是否能使用
	tmp_termio.c_iflag = tty->termios.c_iflag;		// tmp_termio 存储 tty 中指定参数
	tmp_termio.c_oflag = tty->termios.c_oflag;
	tmp_termio.c_cflag = tty->termios.c_cflag;
	tmp_termio.c_lflag = tty->termios.c_lflag;
	tmp_termio.c_line = tty->termios.c_line;
	for(i=0 ; i < NCC ; i++)
		tmp_termio.c_cc[i] = tty->termios.c_cc[i];
	for (i=0 ; i< (sizeof (*termio)) ; i++)
		put_fs_byte( ((char *)&tmp_termio)[i] , i+(char *)termio ); // 将 tmp_termio 保存的信息复制到 termio 指向位置
	return 0;
}

/**
 * 设置终端 termio 结构信息
 * @param tty 终端对应的 tty 数据结构
 * @param termio 用户数据区 termio 结构缓冲区指针
 * @return 0
*/
static int set_termio(struct tty_struct * tty, struct termio * termio)
{
	int i;
	struct termio tmp_termio;

	for (i=0 ; i< (sizeof (*termio)) ; i++)
		((char *)&tmp_termio)[i]=get_fs_byte(i+(char *)termio);		// 将 termio 指向位置信息保存到 tmp_termio 中
	*(unsigned short *)&tty->termios.c_iflag = tmp_termio.c_iflag;	// tmp_termio 中暂存的信息保存到 tty 数据结构中
	*(unsigned short *)&tty->termios.c_oflag = tmp_termio.c_oflag;
	*(unsigned short *)&tty->termios.c_cflag = tmp_termio.c_cflag;
	*(unsigned short *)&tty->termios.c_lflag = tmp_termio.c_lflag;
	tty->termios.c_line = tmp_termio.c_line;
	for(i=0 ; i < NCC ; i++)
		tty->termios.c_cc[i] = tmp_termio.c_cc[i];
	change_speed(tty);												// 修改传输数率
	return 0;
}

/**
 * tty 终端设置的 ioctl 函数
 * @param dev 设备号
 * @param cmd ioctl 命令
 * @param arg 操作参数指针
*/
int tty_ioctl(int dev, int cmd, int arg)
{
	struct tty_struct * tty;
	// 主设备号 5（tty终端）
	if (MAJOR(dev) == 5) {
		dev=current->tty;				// 进程的 tty 设备即是子设备号
		if (dev<0)						// 子设备号不能小于 0
			panic("tty_ioctl: dev<0");
	} else
		dev=MINOR(dev);					// 否则直接从设备号中取子设备号
	tty = dev + tty_table;				// 获取对应 tty 数据
	switch (cmd) {
		case TCGETS:					// 取相应终端 termios 信息
			return get_termios(tty,(struct termios *) arg);
		case TCSETSF:					// 在设置终端信息之前，必须先等输出队列所有数据处理完，并且刷新输入队列
			flush(&tty->read_q); /* fallthrough */
		case TCSETSW:					// 在设置终端信息之前，需要先等待输出队列中的所有数据先处理完，对于修改参数会影响输出的情况，就使用这种形式
			wait_until_sent(tty); /* fallthrough */
		case TCSETS:					// 设置对应的 termio 终端信息
			return set_termios(tty,(struct termios *) arg);
		case TCGETA:					// 取相应终端 termio 信息
			return get_termio(tty,(struct termio *) arg);
		case TCSETAF:					// 在设置终端信息之前，必须先等输出队列所有数据处理完，并且刷新输入队列
			flush(&tty->read_q); /* fallthrough */
		case TCSETAW:					// 在设置终端信息之前，需要先等待输出队列中的所有数据先处理完，对于修改参数会影响输出的情况，就使用这种形式
			wait_until_sent(tty); /* fallthrough */
		case TCSETA:					// 设置对应的 termio 终端信息
			return set_termio(tty,(struct termio *) arg);
		case TCSBRK:					// 等待输出队列处理完毕，如果参数值为 0，则发送一个 break
			if (!arg) {
				wait_until_sent(tty);
				send_break(tty);
			}
			return 0;
		case TCXONC:					// 开始/停止控制，参数为 0 - 挂起输出，1-开启挂起的输出，2-挂起输入，3-重新开启挂起的输入
			return -EINVAL; /* not implemented */
		case TCFLSH:					// 刷新已写输出但还没发送或已收但还没有读数据（参数为 0-刷新输入队列，1-刷新输出队列，2-刷新输入输出队列）
			if (arg==0)
				flush(&tty->read_q);
			else if (arg==1)
				flush(&tty->write_q);
			else if (arg==2) {
				flush(&tty->read_q);
				flush(&tty->write_q);
			} else
				return -EINVAL;
			return 0;
		case TIOCEXCL:					// 设置串行线路专用模式
			return -EINVAL; /* not implemented */
		case TIOCNXCL:					// 复位终端串行线路专用模式
			return -EINVAL; /* not implemented */
		case TIOCSCTTY:					// 设置 tty 为控制终端
			return -EINVAL; /* set controlling term NI */
		case TIOCGPGRP:					// 取指定终端设备进程的组 id
			verify_area((void *) arg,4);
			put_fs_long(tty->pgrp,(unsigned long *) arg);
			return 0;
		case TIOCSPGRP:					// 设定指定终端设备进程的组 id
			tty->pgrp=get_fs_long((unsigned long *) arg);
			return 0;
		case TIOCOUTQ:					// 取输出队列中还未送出的字符数
			verify_area((void *) arg,4);
			put_fs_long(CHARS(tty->write_q),(unsigned long *) arg);
			return 0;
		case TIOCINQ:					// 取输入队列中还未读取的字符数
			verify_area((void *) arg,4);
			put_fs_long(CHARS(tty->secondary),
				(unsigned long *) arg);
			return 0;
		case TIOCSTI:					// 模拟终端输入
			return -EINVAL; /* not implemented */
		case TIOCGWINSZ:				// 读取终端设备串口大小信息
			return -EINVAL; /* not implemented */
		case TIOCSWINSZ:				// 读取终端设备串口大小信息
			return -EINVAL; /* not implemented */
		case TIOCMGET:					// 返回 modem 状态控制引线的当前状态比特位标志集
			return -EINVAL; /* not implemented */
		case TIOCMBIS:					// 设置单个 modem 状态控制引线的状态
			return -EINVAL; /* not implemented */
		case TIOCMBIC:					// 复位单个 modem 状态控制引线的状态
			return -EINVAL; /* not implemented */
		case TIOCMSET:					// 设置 modem 状态引线的状态
			return -EINVAL; /* not implemented */
		case TIOCGSOFTCAR:				// 读取软件载波检测标志
			return -EINVAL; /* not implemented */
		case TIOCSSOFTCAR:				// 设置软件载波检测标志
			return -EINVAL; /* not implemented */
		default:
			return -EINVAL;
	}
}
