/*
 *  linux/kernel/serial.c
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 *	serial.c
 *
 * This module implements the rs232 io functions
 *	void rs_write(struct tty_struct * queue);
 *	void rs_init(void);
 * and all interrupts pertaining to serial IO.
 */

#include <linux/tty.h>
#include <linux/sched.h>
#include <asm/system.h>
#include <asm/io.h>

#define WAKEUP_CHARS (TTY_BUF_SIZE/4)	// 当写队列中含有 WAKEUP_CHARS 个字符时，就开始发送

extern void rs1_interrupt(void);
extern void rs2_interrupt(void);

/**
 * 初始化串行端口
*/
static void init(int port)
{
	outb_p(0x80,port+3);				// 设置线路控制寄存器的 DLAB 位
	outb_p(0x30,port);					// 发送波特率因子低字节，0x30 -> 2400bps
	outb_p(0x00,port+1);				// 发送波特率因子高字节，0x00
	outb_p(0x03,port+3);				// 复位线路控制寄存器的 DLAB 位，数据位为 8 位
	outb_p(0x0b,port+4);				// 设置 DTR、RTS、辅助用户输出2
	outb_p(0x0d,port+1);				// 除了写（写保持空）外，允许所有中断源中断
	(void)inb(port);					// 读数据口，以进行复位操作 
}

/**
 * 初始化串行中断程序与串行接口
*/
void rs_init(void)
{
	set_intr_gate(0x24,rs1_interrupt);	// 设置串行口 1 的中断门向量（硬件 IRQ4 信号）
	set_intr_gate(0x23,rs2_interrupt);	// 设置串行口 2 的中断门向量（硬件 IRQ3 信号）
	init(tty_table[1].read_q.data); 	// 初始化串行端口 1 （.data 为 端口号）
	init(tty_table[2].read_q.data); 	// 初始化串行端口 2
	outb(inb_p(0x21)&0xE7,0x21); 		// 允许主 8259 芯片的 IRQ3， IRQ4 中断信号请求
}

/**
 * 串行数据发送输出
 * 实际上只是开启串行发送保持寄存器已空中断标志，在 UART 将数据发送出去后允许中断信号
*/
void rs_write(struct tty_struct * tty)
{
	cli();
	if (!EMPTY(tty->write_q))
		outb(inb_p(tty->write_q.data+1)|0x02,tty->write_q.data+1);
	sti();
}
