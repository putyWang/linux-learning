/*
 *  linux/kernel/floppy.c
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 * 02.12.91 - Changed to static variables to indicate need for reset
 * and recalibrate. This makes some things easier (output_byte reset
 * checking etc), and means less interrupt jumping in case of errors,
 * so the code is hopefully easier to understand.
 */

/*
 * This file is certainly a mess. I've tried my best to get it working,
 * but I don't like programming floppies, and I have only one anyway.
 * Urgel. I should check for more errors, and do more graceful error
 * recovery. Seems there are problems with several drives. I've tried to
 * correct them. No promises. 
 */

/*
 * As with hd.c, all routines within this file can (and will) be called
 * by interrupts, so extreme caution is needed. A hardware interrupt
 * handler may not sleep, or a kernel panic will happen. Thus I cannot
 * call "floppy-on" directly, but have to set a special timer interrupt
 * etc.
 *
 * Also, I'm not certain this works on more than 1 floppy. Bugs may
 * abund.
 */

#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/fdreg.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/segment.h>

#define MAJOR_NR 2 //软驱主设备号位 2
#include "blk.h"

static int recalibrate = 0; // 重新校正标志位
static int reset = 0; // 复位操作标志位
static int seek = 0; // 是否进行寻道操作位

extern unsigned char current_DOR;  //当前数字输出寄存器，低两位用于指定选择的软驱

/**
 * 直接输出字节
 * val 参数
 * port 端口
*/
#define immoutb_p(val,port) \
__asm__("outb %0,%1\n\tjmp 1f\n1:\tjmp 1f\n1:"::"a" ((char) (val)),"i" (port))

#define TYPE(x) ((x)>>2)
#define DRIVE(x) ((x)&0x03)
/*
 * Note that MAX_ERRORS=8 doesn't imply that we retry every bad read
 * max 8 times - some types of errors increase the errorcount by 2,
 * so we might actually retry only 5-6 times before giving up.
 */
#define MAX_ERRORS 8

/*
 * globals used by 'result()'
 */
/**
 * 函数 result() 使用的全局变量，用于存储软盘控制器 FDC 执行结果
*/
#define MAX_REPLIES 7 // FDC 最后返回 7B 的数据
static unsigned char reply_buffer[MAX_REPLIES]; // 存放 FDC 返回执行结果信息
#define ST0 (reply_buffer[0]) // 返回结果状态字节0
#define ST1 (reply_buffer[1]) // 返回结果状态字节1
#define ST2 (reply_buffer[2]) // 返回结果状态字节2
#define ST3 (reply_buffer[3]) // 返回结果状态字节3

/*
 * This struct defines the different floppy types. Unlike minix
 * linux doesn't have a "search for right type"-type, as the code
 * for that is convoluted and weird. I've got enough problems with
 * this driver as it is.
 *
 * The 'stretch' tells if the tracks need to be boubled for some
 * types (ie 360kB diskette in 1.2MB drive etc). Others should
 * be self-explanatory.
 */
/**
 * 定义了不同软盘的结构
 * size 大小（扇区数）
 * sect 每磁道扇区数
 * head 磁头数 
 * track 磁道数
 * stretch 对磁道是否特殊处理
 * gap 扇区间隙长度（字节数）
 * rate 数据传输速率
 * spec1 参数（高 4 位步进速率，低 4 位磁头卸载时间）
*/
static struct floppy_struct {
	unsigned int size, sect, head, track, stretch;
	unsigned char gap,rate,spec1;
} floppy_type[] = {
	{    0, 0,0, 0,0,0x00,0x00,0x00 },	/* no testing */
	{  720, 9,2,40,0,0x2A,0x02,0xDF },	/* 360kB PC diskettes */
	{ 2400,15,2,80,0,0x1B,0x00,0xDF },	/* 1.2 MB AT-diskettes */
	{  720, 9,2,40,1,0x2A,0x02,0xDF },	/* 360kB in 720kB drive */
	{ 1440, 9,2,80,0,0x2A,0x02,0xDF },	/* 3.5" 720kB diskette */
	{  720, 9,2,40,1,0x23,0x01,0xDF },	/* 360kB in 1.2MB drive */
	{ 1440, 9,2,80,0,0x23,0x01,0xDF },	/* 720kB in 1.2MB drive */
	{ 2880,18,2,80,0,0x1B,0x00,0xCF },	/* 1.44MB diskette */
};
/*
 * Rate is 0 for 500kb/s, 2 for 300kbps, 1 for 250kbps
 * Spec1 is 0xSH, where S is stepping rate (F=1ms, E=2ms, D=3ms etc),
 * H is head unload time (1=16ms, 2=32ms, etc)
 *
 * Spec2 is (HLD<<1 | ND), where HLD is head load time (1=2ms, 2=4 ms etc)
 * and ND is set means no DMA. Hardcoded to 6 (HLD=6ms, use DMA).
 */

extern void floppy_interrupt(void);
extern char tmp_floppy_area[1024]; //临时软盘内存缓冲区（head.s 文件中定义）

/*
 * These are global variables, as that's the easiest way to give
 * information to interrupts. They are the data used for the current
 * request.
 */
// 下面参数为传递给中断程序的全局参数
static int cur_spec1 = -1;
static int cur_rate = -1;
static struct floppy_struct * floppy = floppy_type;
static unsigned char current_drive = 0; // 当前软盘驱动器使用的驱动器
static unsigned char sector = 0; // 磁道上扇区号
static unsigned char head = 0; // 操作磁头号
static unsigned char track = 0; // 操作磁道号
static unsigned char seek_track = 0; // 寻道号
static unsigned char current_track = 255; // 当前磁道号
static unsigned char command = 0; // 发送给软盘控制器的命令
unsigned char selected = 0;
struct task_struct * wait_on_floppy_select = NULL; // 等待当前操作完成的进程

/**
 * 释放当前驱动器资源函数
*/
void floppy_deselect(unsigned int nr)
{
	// nr 不是当前软驱，打印对应错误
	if (nr != (current_DOR & 3))
		printk("floppy_deselect: drive not selected\n\r");
	selected = 0; // 重置选中的软驱
	wake_up(&wait_on_floppy_select); // 唤醒等待当前操作的进程
}

/*
 * floppy-change is never called from an interrupt, so we can relax a bit
 * here, sleep etc. Note that floppy-on tries to set current_DOR to point
 * to the desired drive, but it will probably not survive the sleep if
 * several floppies are used at the same time: thus the loop.
 */
/**
 * 检查软盘是否被更换
 * @return 0-未更换 1-已更换
*/
int floppy_change(unsigned int nr)
{
repeat:
	floppy_on(nr); //开启指定软驱
	while ((current_DOR & 3) != nr && selected) // 如果选择的软驱不是指定软驱，则将当前进程置为可中断等待状态
		interruptible_sleep_on(&wait_on_floppy_select);
	if ((current_DOR & 3) != nr) // 当前进程被唤醒后，依然不是指定软驱则循环直到时指定软驱
		goto repeat;
	// 取数字寄存器值，最高位（位7）置位则表示该软盘已被更换，关闭电动机返回1
	if (inb(FD_DIR) & 0x80) { 
		floppy_off(nr);
		return 1;
	}
	floppy_off(nr);
	return 0; // 未被更换返回 0
}

/**
 * 使用 movsl 命令复制内存数据
 * from 原地址
 * to 目标地址
*/
#define copy_buffer(from,to) \
__asm__("cld ; rep ; movsl" \
	::"c" (BLOCK_SIZE/4),"S" ((long)(from)),"D" ((long)(to)) \
	:"cx","di","si")

/**
 * 设置（初始化）软盘 DMA 通道
*/
static void setup_DMA(void)
{
	// 获取当前缓冲区地址
	long addr = (long) CURRENT->buffer;

	cli(); // 禁止中断
	// 如果缓冲区处于 1MB 以上的地址，则将 DMA 设置在临时软盘缓冲区（因为 8237A 芯片只能在 1MB 的地址范围内寻址）
	if (addr >= 0x100000) {
		addr = (long) tmp_floppy_area; //指向零食缓冲区
		// 将要写入软盘的内容复制到临时缓冲区
		if (command == FD_WRITE)
			copy_buffer(CURRENT->buffer,tmp_floppy_area);
	}
/* mask DMA 2 */
	immoutb_p(4|2,10); //单通道屏蔽寄存器端口为 0x10，位 0-1 指定 DMA 通道（0-3），位 2 ：1 表示屏蔽，0 表示允许请求
/* output command byte. I don't know why, but everyone (minix, */
/* sanches & canton) output this twice, first to 12 then to 11 */
// 下面嵌入式汇编代码向 DMA 控制器端口 12 和 11 写方式字
 	__asm__("outb %%al,$12\n\tjmp 1f\n1:\tjmp 1f\n1:\t"
	"outb %%al,$11\n\tjmp 1f\n1:\tjmp 1f\n1:"::
	"a" ((char) ((command == FD_READ)?DMA_READ:DMA_WRITE)));
/* 8 low bits of addr */
	immoutb_p(addr,4); // 向 DMA 通道2中写入基/当前地址寄存器
	addr >>= 8;
/* bits 8-15 of addr */
	immoutb_p(addr,4);
	addr >>= 8;
/* bits 16-19 of addr */
	immoutb_p(addr,0x81); // 由于 DMA 寄存器只可以在 1MB，其高 16～19 位地址需放入页面寄存器（端口 81）
/* low 8 bits of count-1 (1024-1=0x3ff) */
	immoutb_p(0xff,5); // 向 DMA 通道 2 基/当前字节计数器值（端口 0x81）
/* high 8 bits of count-1 */
	immoutb_p(3,5); // 一共传输 2个扇区 (1024B)
/* activate DMA 2 */
	immoutb_p(0|2,10); //开启DMA 通道2 的请求
	sti(); //允许中断
}

/**
 * 向软盘控制器输入一个字节数据（命令或者参数）
*/
static void output_byte(char byte)
{
	int counter;
	unsigned char status;

	if (reset)
		return;
	for(counter = 0 ; counter < 10000 ; counter++) {
		status = inb_p(FD_STATUS) & (STATUS_READY | STATUS_DIR);
		if (status == STATUS_READY) {
			outb(byte,FD_DATA);
			return;
		}
	}
	reset = 1;
	printk("Unable to send byte to FDC\n\r");
}

/**
 * 读取 FDC 执行结果
 * 结果信息最多为 7B，存放在 reply_buffer[] 中;
 * 返回读入结果字节数，-1 时表示出错
*/
static int result(void)
{
	int i = 0, counter, status;

	if (reset)
		return -1;
	for (counter = 0 ; counter < 10000 ; counter++) {
		status = inb_p(FD_STATUS)&(STATUS_DIR|STATUS_READY|STATUS_BUSY);
		if (status == STATUS_READY)
			return i;
		if (status == (STATUS_DIR|STATUS_READY|STATUS_BUSY)) {
			if (i >= MAX_REPLIES)
				break;
			reply_buffer[i++] = inb_p(FD_DATA); // 保存读取参数
		}
	}
	// 超时重新设置reset 等待下次重试
	reset = 1;
	printk("Getstatus times out\n\r");
	return -1;
}

/**
 * 软盘操作出错时调用的中断函数，由软驱中中断处理程序调用
*/
static void bad_flp_intr(void)
{
	// 当前错误次数 +1
	CURRENT->errors++;
	// 大于最大了允许出错次数，结束当前请求，返回出错
	if (CURRENT->errors > MAX_ERRORS) {
		floppy_deselect(current_drive);
		end_request(0);
	}
	// 大于最大出错次数一半时，进行复位操作
	if (CURRENT->errors > MAX_ERRORS/2)
		reset = 1;
	else
	// 未大于一半则只进行复位操作
		recalibrate = 1;
}	

/*
 * Ok, this interrupt is called after a DMA read/write has succeeded,
 * so we check the results, and copy any buffers.
 */
/**
 * 读写操作中断 DMA 读取操作成功后调用的函数
*/
static void rw_interrupt(void)
{
	// 当放回值结果字节数不为 7 或状态字节 0、1、2 中存在错误标志时进程错误处理
	if (result() != 7 || (ST0 & 0xf8) || (ST1 & 0xbf) || (ST2 & 0x73)) {
		if (ST1 & 0x02) { // 写保护错误，打印错误，释放当前驱动器，然后退出
			printk("Drive %d is write protected\n\r",current_drive);
			floppy_deselect(current_drive);
			end_request(0);
		} else //其余情况，则是执行出错计数处理
			bad_flp_intr();
		do_fd_request();
		return;
	}
	// 读操作且使用临时内存时，将临时内存中读出的数据复制到指定软驱读取的实际缓冲内存中去
	if (command == FD_READ && (unsigned long)(CURRENT->buffer) >= 0x100000)
		copy_buffer(tmp_floppy_area,CURRENT->buffer);
	floppy_deselect(current_drive); // 释放当前软驱
	end_request(1);
	do_fd_request();
}

/**
 * 设置 DMA 并输出软盘操作命令和参数（输出1字节命令 + 0～7字节参数）
*/
inline void setup_rw_floppy(void)
{
	setup_DMA(); // 初始化软盘 DMA 通道
	do_floppy = rw_interrupt; // 设置读写中断执行函数
	output_byte(command); // 发送命令字节
	output_byte(head<<2 | current_drive); // 发送 磁头与当前驱动器参数
	output_byte(track); // 发送磁道号
	output_byte(head); // 发送 磁头号
	output_byte(sector); // 发送起始扇区号
	output_byte(2);		/* sector size = 512 */ // 发送操作扇区数
	output_byte(floppy->sect); // 设置每磁道扇区数
	output_byte(floppy->gap); // 发送 扇区间隔长度参数
	output_byte(0xFF);	/* sector size (0xff when n!=0 ?) */ // 发送 当 N=0 时，扇区定义的字节长度
	if (reset)
		do_fd_request();
}

/*
 * This is the routine called after every seek (or recalibrate) interrupt
 * from the floppy controller. Note that the "unexpected interrupt" routine
 * also does a recalibrate, but doesn't come here.
 */
/**
 * 寻道中断处理函数，在每次软盘控制器寻道（或重新校正）中断后被调用
*/
static void seek_interrupt(void)
{
/* sense drive status */
	output_byte(FD_SENSEI); //发送检测中断状态命令
	// 结果字节数不为 2，ST0 不为寻道结束或磁头所在磁道不等于设定磁道，说明发生错误
	if (result() != 2 || (ST0 & 0xF8) != 0x20 || ST1 != seek_track) {
		bad_flp_intr(); //执行错误处理逻辑
		do_fd_request();
		return;
	}
	// 将 ST1 设置为当前磁道
	current_track = ST1;
	// 设置DMA 并 输出软盘操作命令和参数
	setup_rw_floppy();
}

/*
 * This routine is called when everything should be correctly set up
 * for the transfer (ie floppy motor is on and the correct floppy is
 * selected).
 */
/**
 * 该函数是在传输操作的所有信息都正确设置（软驱电动机已开启并且已选择了正确的软盘）好后被调用的
*/
static void transfer(void)
{
	// 当前驱动器参数不是指定驱动器参数时，重新设置驱动器参数
	if (cur_spec1 != floppy->spec1) {
		cur_spec1 = floppy->spec1;
		output_byte(FD_SPECIFY); //发送设置参数命令
		output_byte(cur_spec1);		/* hut etc */ // 发送参数
		output_byte(6);			/* Head load time =6ms, DMA */
	}
	// 当前数据传输速率与指定传输速率不一致时，重新设置驱动器传输速率
	if (cur_rate != floppy->rate)
		outb_p(cur_rate = floppy->rate,FD_DCR);
	if (reset) { // 若出错，则调用请求处理函数然后返回
		do_fd_request();
		return;
	}
	// 寻道标志为 0 时，设置 DMA 并发送对应的读写炒作命令和参数，然后返回
	if (!seek) {
		setup_rw_floppy();
		return;
	}
	// 否则，则设置中断函数为寻道中断
	do_floppy = seek_interrupt;
	// 起始磁道号不为零时
	if (seek_track) {
		output_byte(FD_SEEK); // 发送磁头寻道命令和参数
		output_byte(head<<2 | current_drive); // 发送参数，磁道号与当前驱动器号
		output_byte(seek_track); // 发送磁道号
	// 起始磁道号为 0 时，只需要设置 磁道号与当前驱动器号
	} else {
		output_byte(FD_RECALIBRATE); // 发送磁头寻道命令和参数
		output_byte(head<<2 | current_drive); // 发送参数，磁道号与当前驱动器号
	}
	// 出错，置重置位，并重试
	if (reset)
		do_fd_request();
}

/*
 * Special case - used after a unexpected interrupt (or reset)
 */
/**
 * 软盘控制器校正中断中调用的校正中断函数
*/
static void recal_interrupt(void)
{
	output_byte(FD_SENSEI); // 发送检测中断状态指令
	if (result()!=2 || (ST0 & 0xE0) == 0x60) // 如果返回结果字节数不等于2，或命令异常结束，则置复位标志，重新进行复位
		reset = 1;
	else
		recalibrate = 0; // 否则校正成功 复位校正标志位
	do_fd_request();
}

/**
 * 意外软盘中断请求中断调用函数
 * 在中断处理函数 do_floppy 为空时调用
*/
void unexpected_floppy_interrupt(void)
{
	output_byte(FD_SENSEI); // 发送检测中断状态命令
	// 返回结果字节数不为 2 或 命令异常结束时复位
	if (result()!=2 || (ST0 & 0xE0) == 0x60) 
		reset = 1;
	else
		// 其他情况重新校正
		recalibrate = 1;
}

/**
 * 校正处理函数
*/
static void recalibrate_floppy(void)
{
	recalibrate = 0; // 重置校正位
	current_track = 0; // 当前磁道号归 0
	do_floppy = recal_interrupt; // 中断执行程序指向 recal_interrupt
	output_byte(FD_RECALIBRATE); // 发送 重新校正命令
	output_byte(head<<2 | current_drive); // 发送参数（磁头号加）当前驱动器号
	if (reset) // 如果出错（复位标志位被置位），则继续执行 请求命令
	
		do_fd_request();
}

/**
 * 软盘控制器 FDC 调用的复位中断函数，在软盘中断程序中调用
*/
static void reset_interrupt(void)
{
	output_byte(FD_SENSEI); // 发送检测中断状态命令
	(void) result(); // 设置命令读取字节
	output_byte(FD_SPECIFY); //发送设置软驱参数命令
	output_byte(cur_spec1);		/* hut etc */ //发送参数
	output_byte(6);			/* Head load time =6ms, DMA */
	do_fd_request(); //调用执行软盘请求
}

/*
 * reset is done by pulling bit 2 of DOR low for a while.
 */
/**
 * 复位软盘控制器
*/
static void reset_floppy(void)
{
	int i;

	reset = 0; // 复位软盘控制器复位标志位
	cur_spec1 = -1;
	cur_rate = -1;
	recalibrate = 1; // 设置重新校正标志位
	printk("Reset-floppy called\n\r");
	cli(); // 禁止中断
	do_floppy = reset_interrupt; // 将 do_floppy 指向 reset_interrupt 函数
	outb_p(current_DOR & ~0x04,FD_DOR); // 对软盘控制器 FDC 执行复位操作
	for (i=0 ; i<100 ; i++) // 等待
		__asm__("nop");
	outb(current_DOR,FD_DOR); // 在启动软盘控制器
	sti(); // 开启中断
}

/**
 * 软盘启动定时中断调用程序
*/
static void floppy_on_interrupt(void)
{
/* We cannot do a floppy-select, as that might sleep. We just force it */
	selected = 1; // 置已选择当前驱动器标志
	// 当当前驱动器号与数字输出寄存器 DOR 不同时，则将 DOR 重新设置为当前驱动器
	if (current_drive != (current_DOR & 3)) {
		current_DOR &= 0xFC;
		current_DOR |= current_drive;
		outb(current_DOR,FD_DOR); //向数字寄存器输出当前 DOR
		add_timer(2,&transfer); // 添加执行定时器并执行传输函数
	} else
		transfer(); // 相等条件下直接执行传输函数
}

/**
 * 软盘请求处理程序
*/
void do_fd_request(void)
{
	unsigned int block; // 操作扇区指针

	seek = 0;
	// 复位标志置位时，执行复位操作
	if (reset) {
		reset_floppy();
		return;
	}
	// 校正标志置位时，执行校正操作
	if (recalibrate) {
		recalibrate_floppy();
		return;
	}
	INIT_REQUEST; // 验证请求项合法性
	floppy = (MINOR(CURRENT->dev)>>2) + floppy_type; // 使用 MINOR(CURRENT->dev)>>2 作为索引获取当前所使用软盘参数
	if (current_drive != CURRENT_DEV) // 如果当前驱动器并非位请求项中指定的驱动器，则置标志位 seek 表示需要进行寻道操作
		seek = 1;
	current_drive = CURRENT_DEV; // 置驱动设备为当前请求设备
	block = CURRENT->sector; // block 指向请求起始扇区
	if (block+2 > floppy->size) { // 与硬盘一样，由于需要一次性读取两个扇区，当软盘扇区数小于 起始扇区 + 2 时 直接退出
		end_request(0);
		goto repeat;
	}
	sector = block % floppy->sect; // 对扇区数与没磁道扇区数求模，获取请求所在磁道扇区
	block /= floppy->sect; // 获取请求起始磁道号
	head = block % floppy->head; // 获取当前磁头号
	track = block / floppy->head; // 获取操作磁道号
	seek_track = track << floppy->stretch; // 磁道号对应于驱动器进行调整，得到寻道号
	if (seek_track != current_track) // 寻道号于当前磁头所在位置不一致时，则需要重新寻道
		seek = 1;
	sector++; // 实际扇区是从 1 开始计算
	if (CURRENT->cmd == READ) // 置读命令
		command = FD_READ;
	else if (CURRENT->cmd == WRITE) // 置写命令
		command = FD_WRITE;
	else
		panic("do_fd_request: unknown command");
	// 添加用于指定驱动器到能正常运行所需延迟时间的定时器（定时结束调用 floppy_on_interrupt 函数）
	add_timer(ticks_to_floppy_on(current_drive),&floppy_on_interrupt);
}

/**
 * 软盘初始化程序
*/
void floppy_init(void)
{
	blk_dev[MAJOR_NR].request_fn = DEVICE_REQUEST;
	// 设置中断表中软盘中断向量参数
	set_trap_gate(0x26,&floppy_interrupt);
	// 复位软盘控制器的中断屏蔽位，允许软盘控制器发送中断请求信号
	outb(inb_p(0x21)&~0x40,0x21);
}
