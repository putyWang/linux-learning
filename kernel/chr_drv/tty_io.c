/*
 *  linux/kernel/tty_io.c
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 * 'tty_io.c' gives an orthogonal feeling to tty's, be they consoles
 * or rs-channels. It also implements echoing, cooked mode etc.
 *
 * Kill-line thanks to John T Kohl.
 */
#include <ctype.h>
#include <errno.h>
#include <signal.h>

#define ALRMMASK (1<<(SIGALRM-1))
#define KILLMASK (1<<(SIGKILL-1))
#define INTMASK (1<<(SIGINT-1))
#define QUITMASK (1<<(SIGQUIT-1))
#define TSTPMASK (1<<(SIGTSTP-1))

#include <linux/sched.h>
#include <linux/tty.h>
#include <asm/segment.h>
#include <asm/system.h>

#define _L_FLAG(tty,f)	((tty)->termios.c_lflag & f) // 取termios中本地模式标志位
#define _I_FLAG(tty,f)	((tty)->termios.c_iflag & f) // 取termios中输入模式标志位
#define _O_FLAG(tty,f)	((tty)->termios.c_oflag & f) // 取termios中输出模式标志位

#define L_CANON(tty)	_L_FLAG((tty),ICANON) // 标志集中规范(熟)模式标志位
#define L_ISIG(tty)	_L_FLAG((tty),ISIG) // 取信号标志位
#define L_ECHO(tty)	_L_FLAG((tty),ECHO) // 取回显字符标志位
#define L_ECHOE(tty)	_L_FLAG((tty),ECHOE) // 规范模式时取回显擦除标志位
#define L_ECHOK(tty)	_L_FLAG((tty),ECHOK) // 规范模式时，取KILL擦除当前行标志位
#define L_ECHOCTL(tty)	_L_FLAG((tty),ECHOCTL) // 取回显控制字符标志位
#define L_ECHOKE(tty)	_L_FLAG((tty),ECHOKE) // 规范模式时，取KILL擦除行并回显标志位

#define I_UCLC(tty)	_I_FLAG((tty),IUCLC) // 取输入模式标志集中大写到小写转换标志位
#define I_NLCR(tty)	_I_FLAG((tty),INLCR) // 取换行符 NL 转回车符 CR 标志位
#define I_CRNL(tty)	_I_FLAG((tty),ICRNL) // 取回车符 CR 转换行符 NL 标志位
#define I_NOCR(tty)	_I_FLAG((tty),IGNCR) // 取忽略回车符 CR 标志位

#define O_POST(tty)	_O_FLAG((tty),OPOST) // 取输出模式标志集中处理标志
#define O_NLCR(tty)	_O_FLAG((tty),ONLCR) // 取换行符 NL 转回车换行符 CR-NL 标志位
#define O_CRNL(tty)	_O_FLAG((tty),OCRNL) // 取回车符 CR 转换行符 NL 标志位
#define O_NLRET(tty)	_O_FLAG((tty),ONLRET) // 取换行符 NL 执行回车功能标志位
#define O_LCUC(tty)	_O_FLAG((tty),OLCUC) // 取小写转大写字符标志

/**
 * tty 数据结构的 tty_table 数组
 * 包含三个初始化项数据，分别对应控制台，串行终端1，串行终端2的初始化数据
*/
struct tty_struct tty_table[] = {
	{
		{ICRNL,		/* change incoming CR to NL */ // 终端输入模式标志，将输入的 CR 转化为 NL
		OPOST|ONLCR,	/* change outgoing NL to CRNL */ // 终端输出模式标志，将输入的 NL 转化为 CRNL
		0, // 控制模式标志 初始化为 0
		ISIG | ICANON | ECHO | ECHOCTL | ECHOKE, // 本地模式标志
		0,		/* console termio */ // 控制台 termio
		INIT_C_CC}, // 控制字符数组
		0,			/* initial pgrp */ // 所属初始进程组
		0,			/* initial stopped */ // 初始停止标志
		con_write, // tty 写函数指针
		{0,0,0,0,""},		/* console read-queue */ // tty 控制台读队列
		{0,0,0,0,""},		/* console write-queue */ // tty 控制台写队列
		{0,0,0,0,""}		/* console secondary queue */ // tty 控制台辅助（第二）队列
	},{
		{0, /* no translation */ // 输入模式标志，0-无须转换
		0,  /* no translation */ // 输出模式标志，0-无须转换
		B2400 | CS8, // 控制模式标志 波特率 2400bps 8位数据位
		0, // 本地模式标志 0
		0, // 行规程 0
		INIT_C_CC}, // 控制字符数组
		0, // 所属初始进程
		0, // 初始停止标志
		rs_write, // 串口 1 tty 写函数指针
		{0x3f8,0,0,0,""},		/* rs 1 */ // 串行终端1读缓冲队列 
		{0x3f8,0,0,0,""}, // 串行终端1写缓冲队列
		{0,0,0,0,""} // 串行终端1辅助缓冲队列
	},{
		{0, /* no translation */ // 输入模式标志，0-无须转换
		0,  /* no translation */ // 输出模式标志，0-无须转换
		B2400 | CS8, // 控制模式标志 波特率 2400bps 8位数据位
		0, // 本地模式标志 0
		0, // 行规程 0
		INIT_C_CC}, // 控制字符数组
		0, // 所属初始进程
		0, // 初始停止标志
		rs_write, // 串口 2 tty 写函数指针
		{0x2f8,0,0,0,""},		/* rs 2 */ // 串行终端2读缓冲队列 
		{0x2f8,0,0,0,""}, // 串行终端2写缓冲队列
		{0,0,0,0,""} // 串行终端2辅助缓冲队列
	}
};

/*
 * these are the tables used by the machine code handlers.
 * you can implement pseudo-tty's or something by changing
 * them. Currently not done.
 */
/**
 * tty 缓冲队列地址表，用于获取读写缓冲地址
*/
struct tty_queue * table_list[]={
	&tty_table[0].read_q, &tty_table[0].write_q,
	&tty_table[1].read_q, &tty_table[1].write_q,
	&tty_table[2].read_q, &tty_table[2].write_q
	};

/**
 * 终端初始化函数
*/
void tty_init(void)
{
	rs_init(); // 初始化串行中断程序和串行接口 1 和 2
	con_init(); // 初始化控制台终端
}

/***
 * tty 键盘终端处理函数
 * tty 相应 tty 终端结构指针
 * mask 信号屏蔽位
*/
void tty_intr(struct tty_struct * tty, int mask)
{
	int i;
	// 如果 tty 所属组号小于等于 0 则退出
	if (tty->pgrp <= 0)
		return;
	// 扫描所有任务组，向tty 所属进程组的所有进程发送指定信号
	for (i=0;i<NR_TASKS;i++)
		if (task[i] && task[i]->pgrp==tty->pgrp)
			task[i]->signal |= mask;
}

static void sleep_if_empty(struct tty_queue * queue)
{
	cli();
	while (!current->signal && EMPTY(*queue))
		interruptible_sleep_on(&queue->proc_list);
	sti();
}

static void sleep_if_full(struct tty_queue * queue)
{
	if (!FULL(*queue))
		return;
	cli();
	while (!current->signal && LEFT(*queue)<128)
		interruptible_sleep_on(&queue->proc_list);
	sti();
}

/**
 * 等待按下enter键
*/
void wait_for_keypress(void)
{
	sleep_if_empty(&tty_table[0].secondary);
}

/**
 * 将读缓冲区文字复制为规范模式字符序列
 * tty 指定终端的tty结构
*/
void copy_to_cooked(struct tty_struct * tty)
{
	signed char c;

	// 循环直到 辅助队列满，或者去缓冲为空
	while (!EMPTY(tty->read_q) && !FULL(tty->secondary)) {
		GETCH(tty->read_q,c); // 每次从读缓冲中读取一个字符
		// 下面对于输入字符，利用输入模式标志集进行处理
		// 如果是回车符 CR(13);
		if (c==13)
			// 回车换行标志 CRNL 置位，则将其转换为换行符 NL(10)
			if (I_CRNL(tty))
				c=10;
			// 忽略回车标志位 NOCR 置位，则忽略该字符
			else if (I_NOCR(tty))
				continue;
			else ;
		// 如果字符为换行符 NL(10) 且换行转回车标志 NLCR 则将其转化为回车符 CR(13)
		else if (c==10 && I_NLCR(tty))
			c=13;
		// 大写转小写标志 UCLC 置位，则将该字符转换为小写字符
		if (I_UCLC(tty))
			c=tolower(c);
		// 本地模式标志集中规范（熟）模式标志 CANON 置位，则进入下列分支
		if (L_CANON(tty)) {
			// 键盘终止控制符 KILL(^U)，则进行输入行删除处理
			if (c==KILL_CHAR(tty)) {
				/* deal with killing the input line */
				// 执行循环直到辅助队列为空、辅助队列最后一个字符为换行符或文件结束字符时结束
				while(!(EMPTY(tty->secondary) ||
				        (c=LAST(tty->secondary))==10 ||
				        c==EOF_CHAR(tty))) {
					// 若本地回显标志（ECHO）置位
					if (L_ECHO(tty)) {
						// 当字符为控制字符 (c<32)，则往写队列中添加两个擦除符 (ERASE)，其余情况只放入一个插入符 
						if (c<32)
							PUTCH(127,tty->write_q);
						PUTCH(127,tty->write_q);
						tty->write(tty);
					}
					// 删除一个缓冲队列中字符
					DEC(tty->secondary.head);
				}
				continue;
			}
			// 该字符为删除控制字符 ERASE(^H)
			if (c==ERASE_CHAR(tty)) {
				// 与行删除逻辑一致，但只删除一个字符
				if (EMPTY(tty->secondary) ||
				   (c=LAST(tty->secondary))==10 ||
				   c==EOF_CHAR(tty))
					continue;
				if (L_ECHO(tty)) {
					if (c<32)
						PUTCH(127,tty->write_q);
					PUTCH(127,tty->write_q);
					tty->write(tty);
				}
				DEC(tty->secondary.head);
				continue;
			}
			// 停止字符（^S）时，则将 tty 停止位置位 1
			if (c==STOP_CHAR(tty)) {
				tty->stopped=1;
				continue;
			}
			// 停止字符(^Q)时，则将 tty 停止位复位 0
			if (c==START_CHAR(tty)) {
				tty->stopped=0;
				continue;
			}
		}
		// 输入模式标志集中 ISIG 标志置位，则在收到 INTR、QUIT、SUSP 或 DSUSP 字符时，需要位进程产生相应的信号
		if (L_ISIG(tty)) {
			// 键盘中断符（^C）则向当前进程发送键盘中断信号
			if (c==INTR_CHAR(tty)) {
				tty_intr(tty,INTMASK);
				continue;
			}
			// 键盘中断符（^ ），则向当前进程发送键盘退出信号
			if (c==QUIT_CHAR(tty)) {
				tty_intr(tty,QUITMASK);
				continue;
			}
		}
		// 换行符(NL),或文件结束符（EOF（^D））辅助缓冲队列字符行数加 1
		if (c==10 || c==EOF_CHAR(tty))
			tty->secondary.data++;
		// 本地模式集中回显标志 ECHO 置位
		if (L_ECHO(tty)) {
			// 换行符，向写队列中添加换行符 NL（10）与回车符 CR（13）
			if (c==10) {
				PUTCH(10,tty->write_q);
				PUTCH(13,tty->write_q);
			// 控制字符且回显控制字符标志 ECHOCTL 置位，将字符 ^ 与字符 c+64 添加到写队列中
			} else if (c<32) {
				if (L_ECHOCTL(tty)) {
					PUTCH('^',tty->write_q);
					PUTCH(c+64,tty->write_q);
				}
			// 其他情况下，直接将字符放入写队列中
			} else
				PUTCH(c,tty->write_q);
			tty->write(tty);
		}
		// 将字符添加至 辅助队列之中
		PUTCH(c,tty->secondary);
	}
	// 唤醒等待该辅助队列的进程
	wake_up(&tty->secondary.proc_list);
}

int tty_read(unsigned channel, char * buf, int nr)
{
	struct tty_struct * tty;
	char c, * b=buf;
	int minimum,time,flag=0;
	long oldalarm;

	if (channel>2 || nr<0) return -1;
	tty = &tty_table[channel];
	oldalarm = current->alarm;
	time = 10L*tty->termios.c_cc[VTIME];
	minimum = tty->termios.c_cc[VMIN];
	if (time && !minimum) {
		minimum=1;
		if (flag=(!oldalarm || time+jiffies<oldalarm))
			current->alarm = time+jiffies;
	}
	if (minimum>nr)
		minimum=nr;
	while (nr>0) {
		if (flag && (current->signal & ALRMMASK)) {
			current->signal &= ~ALRMMASK;
			break;
		}
		if (current->signal)
			break;
		if (EMPTY(tty->secondary) || (L_CANON(tty) &&
		!tty->secondary.data && LEFT(tty->secondary)>20)) {
			sleep_if_empty(&tty->secondary);
			continue;
		}
		do {
			GETCH(tty->secondary,c);
			if (c==EOF_CHAR(tty) || c==10)
				tty->secondary.data--;
			if (c==EOF_CHAR(tty) && L_CANON(tty))
				return (b-buf);
			else {
				put_fs_byte(c,b++);
				if (!--nr)
					break;
			}
		} while (nr>0 && !EMPTY(tty->secondary));
		if (time && !L_CANON(tty))
			if (flag=(!oldalarm || time+jiffies<oldalarm))
				current->alarm = time+jiffies;
			else
				current->alarm = oldalarm;
		if (L_CANON(tty)) {
			if (b-buf)
				break;
		} else if (b-buf >= minimum)
			break;
	}
	current->alarm = oldalarm;
	if (current->signal && !(b-buf))
		return -EINTR;
	return (b-buf);
}

int tty_write(unsigned channel, char * buf, int nr)
{
	static cr_flag=0;
	struct tty_struct * tty;
	char c, *b=buf;

	if (channel>2 || nr<0) return -1;
	tty = channel + tty_table;
	while (nr>0) {
		sleep_if_full(&tty->write_q);
		if (current->signal)
			break;
		while (nr>0 && !FULL(tty->write_q)) {
			c=get_fs_byte(b);
			if (O_POST(tty)) {
				if (c=='\r' && O_CRNL(tty))
					c='\n';
				else if (c=='\n' && O_NLRET(tty))
					c='\r';
				if (c=='\n' && !cr_flag && O_NLCR(tty)) {
					cr_flag = 1;
					PUTCH(13,tty->write_q);
					continue;
				}
				if (O_LCUC(tty))
					c=toupper(c);
			}
			b++; nr--;
			cr_flag = 0;
			PUTCH(c,tty->write_q);
		}
		tty->write(tty);
		if (nr>0)
			schedule();
	}
	return (b-buf);
}

/*
 * Jeh, sometimes I really like the 386.
 * This routine is called from an interrupt,
 * and there should be absolutely no problem
 * with sleeping even in an interrupt (I hope).
 * Of course, if somebody proves me wrong, I'll
 * hate intel for all time :-). We'll have to
 * be careful and see to reinstating the interrupt
 * chips before calling this, though.
 *
 * I don't think we sleep here under normal circumstances
 * anyway, which is good, as the task sleeping might be
 * totally innocent.
 */
/**
 * tty中断处理调用函数-执行 tty 中断处理，
 * tty 指定 tty 终端号（0，1，2）
 * 将指定 tty 终端队列缓冲区的字符复制成规范模式字符并存放在辅助队列中；
 * 在串口读字符中断以及键盘中断中调用
*/
void do_tty_interrupt(int tty)
{
	copy_to_cooked(tty_table+tty);
}

/*
	预留字符输入设备初始化
*/
void chr_dev_init(void)
{
}
