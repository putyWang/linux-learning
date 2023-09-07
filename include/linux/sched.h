#ifndef _SCHED_H
#define _SCHED_H

// 最大进程数量
#define NR_TASKS 64
#define HZ 100

#define FIRST_TASK task[0] // 进程数组第一个进程
#define LAST_TASK task[NR_TASKS-1] // 进程数组最后一个进程

#include <linux/head.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <signal.h>

#if (NR_OPEN > 32)
#error "Currently the close-on-exec-flags are in one word, max 32 files/proc"
#endif

#define TASK_RUNNING		0
#define TASK_INTERRUPTIBLE	1
#define TASK_UNINTERRUPTIBLE	2
#define TASK_ZOMBIE		3
#define TASK_STOPPED		4

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

typedef int (*fn_ptr)();

struct i387_struct {
	long	cwd;
	long	swd;
	long	twd;
	long	fip;
	long	fcs;
	long	foo;
	long	fos;
	long	st_space[20];	/* 8*10 bytes for each FP-reg = 80 bytes */
};

/*
* 进程的数据结构
*/
struct tss_struct {
	long	back_link;	/* 16 high bits zero */
	long	esp0;		// 内核堆栈指针值
	long	ss0;		// 内核堆栈边界值 /* 16 high bits zero */
	long	esp1;		
	long	ss1;		/* 16 high bits zero */
	long	esp2;
	long	ss2;		/* 16 high bits zero */
	long	cr3;
	long	eip;
	long	eflags;
	long	eax,ecx,edx,ebx;
	long	esp;
	long	ebp;
	long	esi;
	long	edi;
	long	es;		/* 16 high bits zero */
	long	cs;		/* 16 high bits zero */
	long	ss;		/* 16 high bits zero */
	long	ds;		/* 16 high bits zero */
	long	fs;		/* 16 high bits zero */
	long	gs;		/* 16 high bits zero */
	long	ldt;		/* 16 high bits zero */
	long	trace_bitmap;	/* bits: trace 0, bitmap 16-31 */
	struct i387_struct i387;
};

/*
* 进程的数据结构
*/
struct task_struct {
/* these are hardcoded - don't touch */
	long state;	/* 进程状态 -1 未运行, 0 运行 (正在运行或就绪), 1 可中断睡眠态, 2 不可中断睡眠状态, 3 僵死状态, 4 暂停状态*/
	long counter; /* 当前程序剩余时间片 长度 */
	long priority; /* 进程优先权值*/
	long signal; /*信号，位图，每个比特位代表一种型号，信号值 = 位偏移值 + 1*/
	struct sigaction sigaction[32]; /*信号执行数据结构，对应信号将要执行的操作和标志信息*/
	long blocked;	/* bitmap of masked signals */ /*进程信号屏蔽码，对应信号位图*/
/* various fields */
	int exit_code; /*进程执行停止的退出码，父进程获取*/
	unsigned long start_code,end_code,end_data,brk,start_stack; /*代码段地址、代码段长度、代码段+数据段长度、总长度以及堆栈地址*/
	long pid,father,pgrp,session,leader; /*进程识别号、父进程id、进程组号、会话号以及会话首领*/
	unsigned short uid,euid,suid; /*用户标识号、有效用户与保存的用户*/
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

extern struct task_struct *task[NR_TASKS];
extern struct task_struct *last_task_used_math;
extern struct task_struct *current;
extern long volatile jiffies;
extern long startup_time;

#define CURRENT_TIME (startup_time+jiffies/HZ)

extern void add_timer(long jiffies, void (*fn)(void));
extern void sleep_on(struct task_struct ** p);
extern void interruptible_sleep_on(struct task_struct ** p);
extern void wake_up(struct task_struct ** p);

/*
 * Entry into gdt where to find first TSS. 0-nul, 1-cs, 2-ds, 3-syscall
 * 4-TSS0, 5-LDT0, 6-TSS1 etc ...
 */
#define FIRST_TSS_ENTRY 4
#define FIRST_LDT_ENTRY (FIRST_TSS_ENTRY+1)
#define _TSS(n) ((((unsigned long) n)<<4)+(FIRST_TSS_ENTRY<<3))
#define _LDT(n) ((((unsigned long) n)<<4)+(FIRST_LDT_ENTRY<<3))
// 使用 ltr 命令，将 _TSS 任务表中对应加载到任务寄存器
#define ltr(n) __asm__("ltr %%ax"::"a" (_TSS(n)))
// 使用 lldt 命令，将 _LDT 局部描述符表中对应项加载到 局部描述符寄存器
#define lldt(n) __asm__("lldt %%ax"::"a" (_LDT(n)))
#define str(n) \
__asm__("str %%ax\n\t" \
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
#define switch_to(n) {\
struct {long a,b;} __tmp; \
__asm__("cmpl %%ecx,_current\n\t" \
	"je 1f\n\t" \
	"movw %%dx,%1\n\t" \
	"xchgl %%ecx,_current\n\t" \
	"ljmp %0\n\t" \
	"cmpl %%ecx,_last_task_used_math\n\t" \
	"jne 1f\n\t" \
	"clts\n" \
	"1:" \
	::"m" (*&__tmp.a),"m" (*&__tmp.b), \
	"d" (_TSS(n)),"c" ((long) task[n])); \
}

#define PAGE_ALIGN(n) (((n)+0xfff)&0xfffff000)

#define _set_base(addr,base) \
__asm__("movw %%dx,%0\n\t" \
	"rorl $16,%%edx\n\t" \
	"movb %%dl,%1\n\t" \
	"movb %%dh,%2" \
	::"m" (*((addr)+2)), \
	  "m" (*((addr)+4)), \
	  "m" (*((addr)+7)), \
	  "d" (base) \
	:"dx")

#define _set_limit(addr,limit) \
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

#define set_base(ldt,base) _set_base( ((char *)&(ldt)) , base )
#define set_limit(ldt,limit) _set_limit( ((char *)&(ldt)) , (limit-1)>>12 )

#define _get_base(addr) ({\
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

#define get_base(ldt) _get_base( ((char *)&(ldt)) )

#define get_limit(segment) ({ \
unsigned long __limit; \
__asm__("lsll %1,%0\n\tincl %0":"=r" (__limit):"r" (segment)); \
__limit;})

#endif
