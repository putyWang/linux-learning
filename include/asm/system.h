/**
 * 模拟中断调用返回过程 使用 iret 命令从内核模式移动到用户模式去运行 进程 0
*/
#define move_to_user_mode() \
__asm__ ("movl %%esp,%%eax\n\t" \ // 将堆栈指针保存到 eax 中 
	"pushl $0x17\n\t" \ // 将 TASK0 堆栈选择符（SS）入栈
	"pushl %%eax\n\t" \ // 将堆栈指针入栈
	"pushfl\n\t" \ // 将标志寄存器 （eflags）内容入栈
	"pushl $0x0f\n\t" \ // 将 TASK0 代码段选择符（eip）入栈
	"pushl $1f\n\t" \ // 将下面标号 1 处代码指针入栈
	"iret\n" \ // iret 执行后会执行入栈的下面标号1 处代码
	"1:\tmovl $0x17,%%eax\n\t" \ // 将所有段寄存器指向本局部表的数据段
	"movw %%ax,%%ds\n\t" \
	"movw %%ax,%%es\n\t" \
	"movw %%ax,%%fs\n\t" \
	"movw %%ax,%%gs" \
	:::"ax")

#define sti() __asm__ ("sti"::) // 开启中断子函数
#define cli() __asm__ ("cli"::) // 禁用中断子函数
#define nop() __asm__ ("nop"::) // 执行空语句只函数

#define iret() __asm__ ("iret"::) // 中断返回函数

/**
 * 设置中段描述符表参数
 * gate_addr 描述符地址
 * type 描述符种类型域值
 * dpl 描述符特权层值
 * addr 中断调用函数偏移地址
*/
#define _set_gate(gate_addr,type,dpl,addr) \。
/**
 * %0 (short) (0x8000+(dpl<<13)+(type<<8)) 类型标志字
 * %1 描述符低 4 字节地址
 * %2 描述符高 4 字节地址
 * %3 edx 中断处理程序偏移地址
 * %4 eax 高字中含有的段选择符
*/
__asm__ ("movw %%dx,%%ax\n\t" \ // 将偏移地址低字与选择符组合成描述符低 4 字节（eax）
	"movw %0,%%dx\n\t" \ // 将偏移地址高字与类型标志字组合成描述符高 4 字节（eax）
	"movl %%eax,%1\n\t" \ // 分别设置门描述符的低 4 字节与高 4 字节
	"movl %%edx,%2" \
	: \
	: "i" ((short) (0x8000+(dpl<<13)+(type<<8))), \
	"o" (*((char *) (gate_addr))), \
	"o" (*(4+(char *) (gate_addr))), \
	"d" ((char *) (addr)),"a" (0x00080000))

/**
 * 设置中断门调用宏函数；
 * n：中断号
 * addr 中断程序偏移地址
*/
#define set_intr_gate(n,addr) \
// &idt[n] 中断符描述表中的-偏移值，中断描述符类型 14， 特权级为 0， 中断程序偏移地址为 addr
	_set_gate(&idt[n],14,0,addr)

/**
 * 设置陷阱门调用宏函数；
 * n：中断号
 * addr 中断程序偏移地址
*/
#define set_trap_gate(n,addr) \
// &idt[n] 中断符描述表中的-偏移值，中断描述符类型 15， 特权级为 0， 中断程序偏移地址为 addr
	_set_gate(&idt[n],15,0,addr)

/**
 * 设置系统调用门宏函数；
 * n：中断号
 * addr 中断程序偏移地址
*/
#define set_system_gate(n,addr) \
// &idt[n] 中断符描述表中的-偏移值，中断描述符类型 15， 特权级为 3， 中断程序偏移地址为 addr
	_set_gate(&idt[n],15,3,addr)

/**
 * 设置段描述符函数
 * @param gate_addr 描述符地址
 * @param type 描述符中类型域值
 * @param dpl 描述符特权层级
 * @param base 段基址
 * @param limit 段限长
*/
#define _set_seg_desc(gate_addr,type,dpl,base,limit) {\
	*(gate_addr) = ((base) & 0xff000000) | \
		(((base) & 0x00ff0000)>>16) | \
		((limit) & 0xf0000) | \
		((dpl)<<13) | \
		(0x00408000) | \
		((type)<<8); \
	*((gate_addr)+1) = (((base) & 0x0000ffff)<<16) | \
		((limit) & 0x0ffff); }

/**
 * 在全表中设置任务状态段/局部表描述符
 * @param n 全局表中描述项 n 所对应地址
 * @param addr 状态段/局部表所在内存的基地址
 * @param type 描述中标志类型字节
*/
#define _set_tssldt_desc(n,addr,type) \
__asm__ ("movw $104,%1\n\t" \ // 将 TSS 长度（144）放入长度域
	"movw %%ax,%2\n\t" \ // 将基地址低字放入描述符第 2～3 字节
	"rorl $16,%%eax\n\t" \ // 将地址高字移入 eax 中
	"movb %%al,%3\n\t" \ // 将基地址高字中低字节移入 描述符 4 字节处
	"movb $" type ",%4\n\t" \ // 将描述符类型移入 描述符 5 字节处
	"movb $0x00,%5\n\t" \ // 将描述符第 6 字节置 0
	"movb %%ah,%6\n\t" \ // 将基地址最高字移入 描述符 7 字节处
	"rorl $16,%%eax" \ // eax 恢复原值
	::"a" (addr), "m" (*(n)), "m" (*(n+2)), "m" (*(n+4)), \
	 "m" (*(n+5)), "m" (*(n+6)), "m" (*(n+7)) \
	)

/**
 * 在全局表中设置任务状态段描述符
 * @param n 描述符指针
 * @param addr 描述符中的基地址值
 * @param 0x89 任务状态段描述符类型
*/
#define set_tss_desc(n,addr) _set_tssldt_desc(((char *) (n)),addr,"0x89")
/**
 * 在全局表中设置局部表描述符
 * @param n 描述符指针
 * @param addr 描述符中的基地址值
 * @param 0x82 局部表描述符类型
*/
#define set_ldt_desc(n,addr) _set_tssldt_desc(((char *) (n)),addr,"0x82")
