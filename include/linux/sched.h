#ifndef _SCHED_H
#define _SCHED_H


#define NR_TASKS 64 // 系统最大进程数量
#define HZ 100 // 定义时钟嘀嗒频率

#define FIRST_TASK task[0] // 进程数组第一个进程
#define LAST_TASK task[NR_TASKS-1] // 进程数组最后一个进程

#include <linux/head.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <signal.h>

// 最大允许的文件开始数量为 32
#if (NR_OPEN > 32)
#error "Currently the close-on-exec-flags are in one word, max 32 files/proc"
#endif

/**
 * 定义进程可能所处状态
*/
#define TASK_RUNNING		0 // 正在运行或者准备就绪
#define TASK_INTERRUPTIBLE	1 // 进程处于可中断等待状态
#define TASK_UNINTERRUPTIBLE	2 // 不可中断等待状态，主要用于 I/O 操作等待
#define TASK_ZOMBIE		3 // 僵死状态，已经停止运行，但父进程还未发信号
#define TASK_STOPPED		4 // 进程已停止

// 定义空指针
#ifndef NULL
#define NULL ((void *) 0)
#endif

extern int copy_page_tables(unsigned long from, unsigned long to, long size);
extern int free_page_tables(unsigned long from, unsigned long size);

extern void sched_init(void);
extern void schedule(void);
extern void trap_init(void);
extern void panic(const char * str);
extern int tty_write(unsigned minor,char * buf,int count);

typedef int (*fn_ptr)(); // 定义函数指针类型

// 数字协处理器使用的结构，主要用于保存进程切换时 i387 的执行状态信息
struct i387_struct {
	long	cwd; // 控制字
	long	swd; // 状态字
	long	twd; // 标记字
	long	fip; // 协处理器代码指针
	long	fcs; // 协处理器代码段寄存器
	long	foo; // 内存操作数偏移值
	long	fos; // 内存操作数段值
	long	st_space[20];	/* 8*10 bytes for each FP-reg = 80 bytes */ // 8 个 10 字节的协处理器累加器
};

/**
 * 进程状态段的数据结构（进程描述符）
 */
struct tss_struct {
	long	back_link;	/* 16 high bits zero */ // 前一执行任务的 TSS 选择符
	// 特权级 0～2 的堆栈指针
	long	esp0;
	long	ss0;		/* 16 high bits zero */
	long	esp1;	
	long	ss1;		/* 16 high bits zero */
	long	esp2;
	long	ss2;		/* 16 high bits zero */
	// 页目录基地址的寄存器（CDBR）
	long	cr3; 
	long	eip;        // 指定代码指针
	long	eflags;     // 标志寄存器（任务进行切换时导致 CPU 产生一个调试异常的 T-比特位）
	// 通用寄存器
	long	eax,ecx,edx,ebx; 
	long	esp;
	long	ebp;
	long	esi;
	long	edi;
	// 段寄存器
	long	es;		/* 16 high bits zero */
	long	cs;		/* 16 high bits zero */
	long	ss;		/* 16 high bits zero */
	long	ds;		/* 16 high bits zero */
	long	fs;		/* 16 high bits zero */
	long	gs;		/* 16 high bits zero */
	// 任务的 LDT 的选择符
	long	ldt;		/* 16 high bits zero */
	// I/O 比特位位图基地址
	long	trace_bitmap;	/* bits: trace 0, bitmap 16-31 */
	struct i387_struct i387;
};

/**
 * 进程的数据结构
 */
struct task_struct {
/* these are hardcoded - don't touch */
	long state;	/* 进程状态 -1 未运行, 0 运行 (正在运行或就绪), 1 可中断睡眠态, 2 不可中断睡眠状态, 3 僵死状态, 4 暂停状态*/
	long counter; /* 当前程序剩余时间片 长度 */
	long priority; // 进程优先权值，任务开始时 counter = priority
	long signal; /*信号，位图，每个比特位代表一种型号，信号值 = 位偏移值 + 1*/
	struct sigaction sigaction[32]; /*信号执行数据结构，对应信号将要执行的操作和标志信息*/
	long blocked;	/* bitmap of masked signals */ /*进程信号屏蔽码，对应信号位图*/
/* various fields */
	int exit_code; /*进程执行停止的退出码，父进程获取*/
	unsigned long start_code,end_code,end_data,brk,start_stack; /*代码段地址、代码段长度、代码段+数据段长度、总长度以及堆栈地址*/
	long pid,father,pgrp,session,leader; /*进程识别号、父进程id、进程组号、会话号以及会话首领*/
	unsigned short uid,euid,suid; /*用户标识号、有效用户(0时表示为管理员)与保存的用户*/
	unsigned short gid,egid,sgid; /*组标识号、有效组与保存的组*/
	long alarm; /*报警定时器*/
	long utime,stime,cutime,cstime,start_time; /*用户态运行时间、系统态运行时间、子进程用户态运行时间、子进程系统态运行时间与开始时间*/
	unsigned short used_math; /*是否运行协处理器标识符*/
/* file system info */
	int tty;		/* -1 if no tty, so it must be signed */ /*进程使用 tty 子设备号，-1表示未使用*/
	unsigned short umask; /*文件创建属性屏蔽位*/
	struct m_inode * pwd; /*当前工作目录 i 节点结构*/
	struct m_inode * root; /*根目录 i 节点结构*/
	struct m_inode * executable; /*执行文件 i 节点结构*/
	unsigned long close_on_exec; /*执行时关闭的文件句柄位图标志*/
	struct file * filp[NR_OPEN]; /*进程使用的文件表结构*/
/* ldt for this task 0 - zero 1 - cs 2 - ds&ss */
	struct desc_struct ldt[3]; /*本任务的局部表描述符；0-空，1-代码段，2-数据和堆栈段*/
/* tss for this task */
	struct tss_struct tss; /*进程任务状态段信息结构*/
};

/*
 *  INIT_TASK is used to set up the first task table, touch at
 * your own risk!. Base=0, limit=0x9ffff (=640kB)
 */
/**
 * 用于对第一个任务表进行初始化，基址=0，段限长=0x9ffff(640Kb)
*/
#define INIT_TASK \
/* state etc */	{ 0,15,15, \
/* signals */	0,{{},},0, \
/* ec,brk... */	0,0,0,0,0,0, \
/* pid etc.. */	0,-1,0,0,0, \
/* uid etc */	0,0,0,0,0,0, \
/* alarm */	0,0,0,0,0,0, \
/* math */	0, \
/* fs info */	-1,0022,NULL,NULL,NULL,0, \
/* filp */	{NULL,}, \
	{ \
		{0,0}, \
/* ldt */	{0x9f,0xc0fa00}, \
		{0x9f,0xc0f200}, \
	}, \
/*tss*/	{0,PAGE_SIZE+(long)&init_task,0x10,0,0,0,0,(long)&pg_dir,\
	 0,0,0,0,0,0,0,0, \
	 0,0,0x17,0x17,0x17,0x17,0x17,0x17, \
	 _LDT(0),0x80000000, \
		{} \
	}, \
}

extern struct task_struct *task[NR_TASKS];      // 任务数组
extern struct task_struct *last_task_used_math; // 上一个使用过协处理器的进程
extern struct task_struct *current;             // 当前进程结构指针变量
extern long volatile jiffies;                   // 从开机开始算起的嘀嗒数（10 ms/滴答）
extern long startup_time;                       // 开机时间，从 1970:0:0:0 开始计时的秒数

#define CURRENT_TIME (startup_time+jiffies/HZ)  // 当前时间（秒数）

extern void add_timer(long jiffies, void (*fn)(void));
extern void sleep_on(struct task_struct ** p);
extern void interruptible_sleep_on(struct task_struct ** p);
extern void wake_up(struct task_struct ** p);

/**
 * 寻找第一个 TSS 在全局表中的入口，0-null，1-代码段 cs，2-数据段 ds，3-系统段 syscall，4-任务状态段 TSS0，5-局部表 LTD0，6-任务状态段 TSS1，等
 */
#define FIRST_TSS_ENTRY 4                                       // 全局表中第一个任务状态段（TSS）描述符的选择符索引号 
#define FIRST_LDT_ENTRY (FIRST_TSS_ENTRY+1)                     // 全局表中第一个局部描述符表（LDT）描述符的选择符索引号 
#define _TSS(n) ((((unsigned long) n)<<4)+(FIRST_TSS_ENTRY<<3)) // 宏定义，计算在全局表中第 n 个任务的 TSS 描述符的索引号
#define _LDT(n) ((((unsigned long) n)<<4)+(FIRST_LDT_ENTRY<<3)) // 宏定义，计算在全局表中第 n 个任务的 LDT 描述符的索引号
// 使用 ltr 命令，将 _TSS 任务表中对应加载到任务寄存器
#define ltr(n) __asm__("ltr %%ax"::"a" (_TSS(n)))
// 使用 lldt 命令，将 _LDT 局部描述符表中对应项加载到 局部描述符寄存器
#define lldt(n) __asm__("lldt %%ax"::"a" (_LDT(n)))
/**
 * 获取挡墙允许任务的任务号
 * @param n 当前任务号
*/
#define str(n) \
__asm__("str %%ax\n\t" \   // 将任务寄存器中 TSS 段选择符 => ax
	"subl %2,%%eax\n\t" \
	"shrl $4,%%eax" \
	:"=a" (n) \
	:"a" (0),"i" (FIRST_TSS_ENTRY<<3))
/*
 *	switch_to(n) should switch tasks to task nr n, first
 * checking that n isn't the current task, in which case it does nothing.
 * This also clears the TS-flag if the task we switched to has used
 * tha math co-processor latest.
 */
/**
 * 将当前任务切换到第 n 个进程
 * @param n 目标进程在进程表中对应索引
*/
#define switch_to(n) {\
/**
 * %0-新 TSS 的偏移地址（*&__tmp.a）
 * %1-存放新 TSS 的选择符值（*&__tmp.b）
 * %2（dx）-目标进程 n 的选择符
 * %3（cx）-目标进程结构属性
*/
struct {long a,b;} __tmp; \
__asm__("cmpl %%ecx,_current\n\t" \ // 检测目标进程是否为当前进程，是的话不用切换直接结束
	"je 1f\n\t" \
	"movw %%dx,%1\n\t" \ // __tmp.b 指向目标进程的段选择符
	"xchgl %%ecx,_current\n\t" \ // 将 current 指向 task[n] 该表当前进程状态 
	"ljmp %0\n\t" \ // 执行长跳转至 *&__tmp，完成任务切换
	// 只有在任务切换回来之后才会继续执行下面的代码
	"cmpl %%ecx,_last_task_used_math\n\t" \ // 新进程使用过协处理器时需要清 cr0 的 TS 标志
	"jne 1f\n\t" \
	"clts\n" \
	"1:" \
	::"m" (*&__tmp.a),"m" (*&__tmp.b), \
	"d" (_TSS(n)),"c" ((long) task[n])); \
}

#define PAGE_ALIGN(n) (((n)+0xfff)&0xfffff000) // 页面地址对准（在内核代码中没有任何地方引用）

/**
 * 设置位于 addr 地址处描述符中的各基地址字段
 * @param addr 地址
 * @param base 基地址
*/
#define _set_base(addr,base) \
/**
 * %0-addr 偏移 2
 * %1-addr 偏移 4
 * %2-addr 偏移 7
 * %3（dx）-基地址
*/
__asm__("movw %%dx,%0\n\t" \
	"rorl $16,%%edx\n\t" \
	"movb %%dl,%1\n\t" \
	"movb %%dh,%2" \
	::"m" (*((addr)+2)), \
	  "m" (*((addr)+4)), \
	  "m" (*((addr)+7)), \
	  "d" (base) \
	:"dx")

/**
 * 设置位于 addr 地址处描述符中的段限长字段
 * @param addr 地址
 * @param limit 段长值
*/
#define _set_limit(addr,limit) \
/**
 * %0-addr 偏移 0
 * %1-addr 偏移 6
 * %2（dx）-段长值
*/
__asm__("movw %%dx,%0\n\t" \
	"rorl $16,%%edx\n\t" \
	"movb %1,%%dh\n\t" \
	"andb $0xf0,%%dh\n\t" \
	"orb %%dh,%%dl\n\t" \
	"movb %%dl,%1" \
	::"m" (*(addr)), \
	  "m" (*((addr)+6)), \
	  "d" (limit) \
	:"dx")

#define set_base(ldt,base) _set_base( ((char *)&(ldt)) , base ) // 设置局部描述符表中 ldt 描述符的基地址字段
#define set_limit(ldt,limit) _set_limit( ((char *)&(ldt)) , (limit-1)>>12 ) // 设置局部描述符表中 ldt 描述符的段长字段

/**
 * 获取位于 addr 地址处描述符中的基地址字段
 * @param addr 地址
*/
#define _get_base(addr) ({\
/**
 * %0-addr 偏移 2
 * %1-addr 偏移 4
 * %2-addr 偏移 7
 * %3（dx）-__base 返回的基地址
*/
unsigned long __base; \
__asm__("movb %3,%%dh\n\t" \
	"movb %2,%%dl\n\t" \
	"shll $16,%%edx\n\t" \
	"movw %1,%%dx" \
	:"=d" (__base) \
	:"m" (*((addr)+2)), \
	 "m" (*((addr)+4)), \
	 "m" (*((addr)+7))); \
__base;})

#define get_base(ldt) _get_base( ((char *)&(ldt)) ) // 获取局部描述符表中 ldt 描述符的基地址字段 

/**
 * 取指定段选择符处的段长值
 * @param segment 地址
*/
#define get_limit(segment) ({ \
unsigned long __limit; \
__asm__("lsll %1,%0\n\tincl %0":"=r" (__limit):"r" (segment)); \
__limit;})

#endif
