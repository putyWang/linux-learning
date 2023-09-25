/*
 *  linux/kernel/traps.c
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 * 'Traps.c' handles hardware traps and faults after we have saved some
 * state in 'asm.s'. Currently mostly a debugging-aid, will be extended
 * to mainly kill the offending process (probably by giving it a signal,
 * but possibly by killing it outright if necessary).
 */
#include <string.h>

#include <linux/head.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <asm/system.h>
#include <asm/segment.h>
#include <asm/io.h>

/**
 * 取段中指定地址处的一个字节
 * @param seg 段描述符
 * @param addr 指定地址
 * @return 指定字节
*/
#define get_seg_byte(seg,addr) ({ \
register char __res; \
__asm__("push %%fs;mov %%ax,%%fs;movb %%fs:%2,%%al;pop %%fs" \
	:"=a" (__res):"0" (seg),"m" (*(addr))); \
__res;})

/**
 * 取段中指定地址处的一个长字（4字节）
 * @param seg 段描述符
 * @param addr 指定地址
 * @return 指定字节
*/
#define get_seg_long(seg,addr) ({ \
register unsigned long __res; \
__asm__("push %%fs;mov %%ax,%%fs;movl %%fs:%2,%%eax;pop %%fs" \
	:"=a" (__res):"0" (seg),"m" (*(addr))); \
__res;})

/**
 * 获取段寄存器 fs 的值（描述符）
*/
#define _fs() ({ \
register unsigned short __res; \
__asm__("mov %%fs,%%ax":"=a" (__res):); \
__res;})

int do_exit(long code);

void page_exception(void);

void divide_error(void);
void debug(void);
void nmi(void);
void int3(void);
void overflow(void);
void bounds(void);
void invalid_op(void);
void device_not_available(void);
void double_fault(void);
void coprocessor_segment_overrun(void);
void invalid_TSS(void);
void segment_not_present(void);
void stack_segment(void);
void general_protection(void);
void page_fault(void);
void coprocessor_error(void);
void reserved(void);
void parallel_interrupt(void);
void irq13(void);

/**
 * 打印出错中断的名称、出错号、调用程序的的 EIP、EFLAGGS、ESP、fs 段寄存器值、段的基址、段的长度、进程号 pid、任务号、10字节指令码；
 * 如果堆栈在用户段，则还打印 16 字节的堆栈内容
 * @param str 出错中断名称
 * @param esp_ptr 中断调用程序返回地址
 * @param nr 进程号
*/
static void die(char * str,long esp_ptr,long nr)
{
	long * esp = (long *) esp_ptr;
	int i;

	printk("%s: %04x\n\r",str,nr&0xffff);
	printk("EIP:\t%04x:%p\nEFLAGS:\t%p\nESP:\t%04x:%p\n",
		esp[1],esp[0],esp[2],esp[4],esp[3]);
	printk("fs: %04x\n",_fs());
	printk("base: %p, limit: %p\n",get_base(current->ldt[1]),get_limit(0x17));
	if (esp[4] == 0x17) {
		printk("Stack: ");
		for (i=0;i<4;i++)
			printk("%p ",get_seg_long(0x17,i+(long *)esp[3]));
		printk("\n");
	}
	str(i); // 取当前进程号
	printk("Pid: %d, process nr: %d\n\r",current->pid,0xffff & i);
	for(i=0;i<10;i++)
		printk("%02x ",0xff & get_seg_byte(esp[1],(i+(char *)esp[0])));
	printk("\n\r");
	do_exit(11);		/* play segment exception */
}

/**
 * 双错误中断调用 c 函数
 * @param esp 调用中断处理程序返回指针
 * @param error_code 错误码
*/
void do_double_fault(long esp, long error_code)
{
	die("double fault",esp,error_code);
}

/**
 * 一般保护性出错中断调用 c 函数
 * @param esp 调用中断处理程序返回指针
 * @param error_code 错误码
*/
void do_general_protection(long esp, long error_code)
{
	die("general protection",esp,error_code);
}

/**
 * 被 0 除出错中断调用 c 函数
 * @param esp 调用中断处理程序返回指针
 * @param error_code 错误码
*/
void do_divide_error(long esp, long error_code)
{
	die("divide error",esp,error_code);
}

/**
 * 断点指令引起的中断调用 c 函数
 * @param esp 调用中断处理程序返回指针
 * @param error_code 错误码
*/
void do_int3(long * esp, long error_code,
		long fs,long es,long ds,
		long ebp,long esi,long edi,
		long edx,long ecx,long ebx,long eax)
{
	int tr;

	__asm__("str %%ax":"=a" (tr):"0" (0)); // 取任务寄存器段地址
	printk("eax\t\tebx\t\tecx\t\tedx\n\r%8x\t%8x\t%8x\t%8x\n\r",
		eax,ebx,ecx,edx);
	printk("esi\t\tedi\t\tebp\t\tesp\n\r%8x\t%8x\t%8x\t%8x\n\r",
		esi,edi,ebp,(long) esp);
	printk("\n\rds\tes\tfs\ttr\n\r%4x\t%4x\t%4x\t%4x\n\r",
		ds,es,fs,tr);
	printk("EIP: %8x   CS: %4x  EFLAGS: %8x\n\r",esp[0],esp[1],esp[2]);
}

/**
 * 非屏蔽中断调用c 函数
 * @param esp 调用中断处理程序返回指针
 * @param error_code 错误码
*/
void do_nmi(long esp, long error_code)
{
	die("nmi",esp,error_code);
}

/**
 * debug测试中断调用c 函数
 * @param esp 调用中断处理程序返回指针
 * @param error_code 错误码
*/
void do_debug(long esp, long error_code)
{
	die("debug",esp,error_code);
}

/**
 * 溢出出错处理中断调用c 函数
 * @param esp 调用中断处理程序返回指针
 * @param error_code 错误码
*/
void do_overflow(long esp, long error_code)
{
	die("overflow",esp,error_code);
}

/**
 * 边界检查（有效地址之外）出错中断调用c 函数
 * @param esp 调用中断处理程序返回指针
 * @param error_code 错误码
*/
void do_bounds(long esp, long error_code)
{
	die("bounds",esp,error_code);
}

/**
 * 无效操作指令出错中断调用c 函数
 * @param esp 调用中断处理程序返回指针
 * @param error_code 错误码
*/
void do_invalid_op(long esp, long error_code)
{
	die("invalid operand",esp,error_code);
}

/**
 * 协处理器不可用处理中断调用c 函数
 * @param esp 调用中断处理程序返回指针
 * @param error_code 错误码
*/
void do_device_not_available(long esp, long error_code)
{
	die("device not available",esp,error_code);
}

/**
 * 协处理器段超出出错中断调用c 函数
 * @param esp 调用中断处理程序返回指针
 * @param error_code 错误码
*/
void do_coprocessor_segment_overrun(long esp, long error_code)
{
	die("coprocessor segment overrun",esp,error_code);
}

/**
 * 无效的任务状态段出错处理中断调用c 函数
 * @param esp 调用中断处理程序返回指针
 * @param error_code 错误码
*/
void do_invalid_TSS(long esp,long error_code)
{
	die("invalid TSS",esp,error_code);
}

/**
 * 段不存在出错处理中断调用c 函数
 * @param esp 调用中断处理程序返回指针
 * @param error_code 错误码
*/
void do_segment_not_present(long esp,long error_code)
{
	die("segment not present",esp,error_code);
}

/**
 * 堆栈段错误处理中断调用c 函数
 * @param esp 调用中断处理程序返回指针
 * @param error_code 错误码
*/
void do_stack_segment(long esp,long error_code)
{
	die("stack segment",esp,error_code);
}

/**
 * 协处理中断调用c 函数
 * @param esp 调用中断处理程序返回指针
 * @param error_code 错误码
*/
void do_coprocessor_error(long esp, long error_code)
{
	if (last_task_used_math != current)
		return;
	die("coprocessor error",esp,error_code);
}

/**
 * 保留中断调用
 * @param esp 调用中断处理程序返回指针
 * @param error_code 错误码
*/
void do_reserved(long esp, long error_code)
{
	die("reserved (15,17-47) error",esp,error_code);
}

/*
	对陷阱门进行初始化（中断向量）
	设置 0～16 系统中断向量；
	15,17-47 预留给其他程序使用

	set_trap_gate 与 set_system_gate 主要区别是前者设置的特权级为 0，后者 3；
	断点 in3、溢出 overflow 与 边界出错中断 bounds 可由任何程序产生；
*/
void trap_init(void)
{
	int i;

	set_trap_gate(0,&divide_error); // 进行除零操作时 故障 产生的中断
	set_trap_gate(1,&debug); // 当进行程序单步调试时，设置标志寄存器 eflags 的T标志时产生的中断 （陷阱）
	set_trap_gate(2,&nmi); // 由不可屏蔽中断 NMI 产生
	set_system_gate(3,&int3);	// 由断点指令 int3 产生，与 debug 处理相同 （陷阱）
	set_system_gate(4,&overflow); // eflags 的溢出标志位 OF 引起的 （陷阱）
	set_system_gate(5,&bounds); // 寻址到有效地址之外引起的
	set_trap_gate(6,&invalid_op); // cpu 发现一个无效指定操作码时产生的
	set_trap_gate(7,&device_not_available); // 协处理器不存在时，cpu遇到一个转义指令并且EM置位或 MP 和 TS 都在置位状态时，CPU 遇到 WAIT 或 一个转义指令时产生的
	set_trap_gate(8,&double_fault); // 双故障出错时产生
	set_trap_gate(9,&coprocessor_segment_overrun); // 协处理器段超出时产生
	set_trap_gate(10,&invalid_TSS); // CPU 切换后发现 TSS 无效时引起
	set_trap_gate(11,&segment_not_present); //描述符所指的段不存在时产生
	set_trap_gate(12,&stack_segment); // 堆栈段不存在或寻址超过堆栈段产生
	set_trap_gate(13,&general_protection); // 操作不符合 80386 保护机制（特权级）时产生
	set_trap_gate(14,&page_fault); // 页不在内存
	set_trap_gate(15,&reserved); // 15 预留程序使用中断
	set_trap_gate(16,&coprocessor_error); // 协处理器发出的出错信号引起
	for (i=17;i<48;i++) // 预留程序使用中断 17 - 47
		set_trap_gate(i,&reserved);
	set_trap_gate(45,&irq13); //协处理器中断处理
	outb_p(inb_p(0x21)&0xfb,0x21); // 允许主 8259A 芯片 IRQ2 中断请求
	outb(inb_p(0xA1)&0xdf,0xA1); // 允许从 8259A 芯片 IRQ13 中断请求
	set_trap_gate(39,&parallel_interrupt); // 设置并行口的陷阱门
}
