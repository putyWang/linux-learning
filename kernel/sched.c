/*
 *  linux/kernel/sched.c
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 * 'sched.c' is the main kernel file. It contains scheduling primitives
 * (sleep_on, wakeup, schedule etc) as well as a number of simple system
 * call functions (type getpid(), which just extracts a field from
 * current-task
 */
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/sys.h>
#include <linux/fdreg.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/segment.h>

#include <signal.h>

#define _S(nr) (1<<((nr)-1))	// 获取信号 nr 在信号位图中对应位的二进制数值，信号编码 1～32
#define _BLOCKABLE (~(_S(SIGKILL) | _S(SIGSTOP))) // 除了 SIGKILL 与 SIGSTOP 其他型号都是可阻塞的

/**
 * 显示任务号 nr 的进程号、进程状态和内核堆栈空闲字节数
 * @param nr 任务号
 * @param p 进程数据结构
*/
void show_task(int nr,struct task_struct * p)
{
	int i,j = 4096-sizeof(struct task_struct);

	printk("%d: pid=%d, state=%d, ",nr,p->pid,p->state);
	i=0;
	while (i<j && !((char *)(p+1))[i]) // 检测指定任务数据结构以后等于 0 的字节数
		i++;
	printk("%d (of %d) chars free in kernel stack\n\r",i,j);
}

/**
 * 展示当前所有任务的进程信息
*/
void show_stat(void)
{
	int i;

	for (i=0;i<NR_TASKS;i++)
		if (task[i])
			show_task(i,task[i]);
}

#define LATCH (1193180/HZ) // 设置定时芯片 8253 的计数初值

extern void mem_use(void);

extern int timer_interrupt(void);
extern int system_call(void);

/**
 * 定义任务联合体
*/
union task_union {
	struct task_struct task; /*因为一个任务数据结构与其堆栈放在同一内存页中，因此从堆栈段寄存器 ss 可以获取其数据段选择符*/
	char stack[PAGE_SIZE];
};

static union task_union init_task = {INIT_TASK,}; //定义第一个进程（0）的初始化数据

long volatile jiffies=0; 	// 从电脑开机时到现在的所有时钟滴答总数
long startup_time=0; 		// 从 1970:0:0:0 到开机时间计时的秒数
struct task_struct *current = &(init_task.task); 	// 当前进程
struct task_struct *last_task_used_math = NULL; 	// 使用过协处理器的进程指针

struct task_struct * task[NR_TASKS] = {&(init_task.task), };	// 进程指针数组

long user_stack [ PAGE_SIZE>>2 ] ; // 定义堆栈 任务 0 的用户态，4K

/**
 * 设置堆栈 ss:esp(数据段选择符，指针)，指针指在最后一项
*/
struct {
	long * a;
	short b;
	} stack_start = { & user_stack [PAGE_SIZE>>2] , 0x10 };

/**
 * 当任务被调度交换过以后，
 * 该函数用以保存原任务的协处理器状态（上下文）并恢复新调度进来的当前任务的协处理器的执行状态
*/
void math_state_restore()
{
	if (last_task_used_math == current) // 任务没变则返回
		return;
	__asm__("fwait"); // 发送协处理器命令之前要先发 WAIT 命令 
	if (last_task_used_math) { // 若果上个任务使用了协处理器，则保存其状态
		__asm__("fnsave %0"::"m" (last_task_used_math->tss.i387));
	}
	last_task_used_math=current; // 然后将 last_task_used_math 指向当前任务
	// 当前用过协处理器，则恢复其状态
	if (current->used_math) {
		__asm__("frstor %0"::"m" (current->tss.i387));
	} else {
		__asm__("fninit"::); // 否则第一次使用，初始化
		current->used_math=1; // 将当前是否使用协处理器位置位
	}
}

/*
 *  'schedule()' is the scheduler function. This is GOOD CODE! There
 * probably won't be any reason to change this, as it should work well
 * in all circumstances (ie gives IO-bound processes good response etc).
 * The one thing you might take a look at is the signal-handler code here.
 *
 *   NOTE!!  Task 0 is the 'idle' task, which gets called when no other
 * tasks can run. It can not be killed, and it cannot sleep. The 'state'
 * information in task[0] is never used.
 */
/**
 * 进程调度函数
*/
void schedule(void)
{
	int i,next,c;
	struct task_struct ** p;

/* check alarm, wake up any interruptible tasks that have got a signal */
	// 从进程数组中的最后一个开始向前遍历
	for(p = &LAST_TASK ; p > &FIRST_TASK ; --p)
		if (*p) {
			// 进程设置过 alarm（不为零） 且 已经过期
			if ((*p)->alarm && (*p)->alarm < jiffies) {
					(*p)->signal |= (1<<(SIGALRM-1)); // 在信号位图中置 SIGALRM 警告信号
					(*p)->alarm = 0; // 置 alram 为 0
				}
			// 信号图中除被阻塞信号以外还存在其他信号，且当前进程状态为可中断状态
			// ~(_BLOCKABLE & (*p)->blocked) 表达式用于忽略被阻塞的信号
			if (((*p)->signal & ~(_BLOCKABLE & (*p)->blocked)) &&
			(*p)->state==TASK_INTERRUPTIBLE)
				(*p)->state=TASK_RUNNING; // 置进程为就绪状态
		}

	// 调度程序的主要部分
	while (1) {
		c = -1; // 存储最大时间片
		next = 0; // 指向即将切换的进程索引
		i = NR_TASKS;
		p = &task[NR_TASKS];
		// 从进程表中最后一个进程向前遍历
		while (--i) {
			if (!*--p)
				continue;
			// 对比处于就绪态进程所拥有的剩余时间片（查找剩余时间片最大的进程）
			if ((*p)->state == TASK_RUNNING && (*p)->counter > c)
				// 将 next 指向剩余最多时间片表项 索引
				c = (*p)->counter, next = i;
		}
		if (c) break;
		// 从后向前遍历所有进程
		for(p = &LAST_TASK ; p > &FIRST_TASK ; --p)
			if (*p)
				// 重新计算进程剩余时间片值（不考虑进程状态） counter / 2 + priority(优先权) 
				(*p)->counter = ((*p)->counter >> 1) +
						(*p)->priority;
	}
	/*切换进程到当前选择的时间片最多的程序*/
	switch_to(next);
}

/**
 * 将当前进程转化为可中断的等待状态，并重新调度
 * 该系统调用将导致进程进入睡眠状态，直到收到一个信号，
 * 该信号用于终止进程或者使进程调用一个信号捕获函数，只有当捕获了一个信号并且信号捕获处理函数返回，pause() 才会返回
*/
int sys_pause(void)
{
	current->state = TASK_INTERRUPTIBLE; // 将当前进程状态设置为可中断
	schedule(); // 重新调度
	return 0;
}

/**
 * 把任务置为不可中断的等待状态，并让睡眠队列头指针指向当前进程
 * 只有在明确被唤醒时才会返回
 * @param *p 等待任务的队列头指针
*/
void sleep_on(struct task_struct **p)
{
	struct task_struct *tmp;
	// 指针无效直接返回
	if (!p)
		return;
	// 0进程不允许睡眠
	if (current == &(init_task.task))
		panic("task[0] trying to sleep");
	tmp = *p;
	*p = current; // 睡眠队列指向当前进程
	current->state = TASK_UNINTERRUPTIBLE; // 将当前进程状态修改为不可中断的等待状态
	schedule(); // 重新调度进程
	// 只有等待任务被唤醒后，程序才会重新执行到此处
	// 由于大家都是在等待同一个资源，在资源可用时，就有必要唤醒所有等待该进程的进程（该函数为嵌套使用）
	if (tmp)
		tmp->state=0;
}

/**
 * 将当前任务置为可中断的等待状态
 * @param *p 等待任务的队列头指针
*/
void interruptible_sleep_on(struct task_struct **p)
{
	struct task_struct *tmp;

	if (!p)
		return;
	if (current == &(init_task.task))
		panic("task[0] trying to sleep");
	tmp=*p;
	*p=current;
repeat:	current->state = TASK_INTERRUPTIBLE;
	schedule();
	// 如果等待队列中还有等待任务，并且队列头指针指向的任务不是当前任务时，则将该等待任务置为就绪状态并重新执行调度程序
	// 当指针 *p 所指向的不是当前任务时，表示在当前任务被放入队列后又有新的被插入队列之中，因此旧影将所有其他等待任务也只为可运行态
	if (*p && *p != current) {
		(**p).state=0;
		goto repeat;
	}
	// 该句代码应该为 *p=tmp，意在在队列头指针指向其余等待任务，否则在当前任务之前插入等待队列的任务全部被抹除掉了
	*p=NULL;
	if (tmp)
		tmp->state=0;
}

/**
 * 唤醒指定任务 *p
 * @param *p 为任务等待队列头指针
*/
void wake_up(struct task_struct **p)
{
	if (p && *p) {
		(**p).state=0;
		// 该句代码应该为 *p=tmp，意在在队列头指针指向其余等待任务，否则在当前任务之前插入等待队列的任务全部被抹除掉了
		*p=NULL;
	}
}

/*
 * OK, here are some floppy things that shouldn't be in the kernel
 * proper. They are here because the floppy needs a timer, and this
 * was the easiest way of doing it.
 */
static struct task_struct * wait_motor[4] = {NULL,NULL,NULL,NULL}; // 等待电动机加速进程的指针数组
static int  mon_timer[4]={0,0,0,0}; // 软驱启动加速所需时间值数组
static int moff_timer[4]={0,0,0,0}; // 软驱电动机停止前需维持L时间数组
unsigned char current_DOR = 0x0C; // 数字输出寄存器（初值）

/**
 * 指定软盘到正常运行状态所需延时时间
 * @param nr 软驱号（0-3）
 * @return 滴答数
*/
int ticks_to_floppy_on(unsigned int nr)
{
	extern unsigned char selected; // 当前选中的软盘号
	unsigned char mask = 0x10 << nr; // 所选软驱对应数字输出寄存器中启动马达比特位

	// 软驱最多有四个
	if (nr>3)
		panic("floppy_on: nr>3");
	moff_timer[nr]=10000;		/* 100 s = very big :-) */ // 默认设置停止前维持时间位 10000
	cli();				/* use floppy_off to turn it off */ // 禁止中断
	mask |= current_DOR;
	if (!selected) { // 若非当前软驱，则先复位其他软驱选择位，然后置对应的软驱选择位
		mask &= 0xFC;
		mask |= nr;
	}
	//数字输出寄存器的当前值与要求的值不同时
	if (mask != current_DOR) {
		outb(mask,FD_DOR); // 向 FDC 数字输出端口输出新值
		if ((mask ^ current_DOR) & 0xf0) // 需要启动的软驱电动机未启动
			mon_timer[nr] = HZ/2; //设置对应的启动器时间值（HZ/2）
		else if (mon_timer[nr] < 2) //
			mon_timer[nr] = 2;
		current_DOR = mask; // 更新当前软驱数字驱动器值
	}
	sti(); // 开启中断
	return mon_timer[nr]; // 返回设置的启动器启动对应时间（最小为 2）
}

/**
 * 开启指定软驱电动机
 * @param nr 指定软驱句柄
*/
void floppy_on(unsigned int nr)
{
	cli(); // 禁止中断
	// 等待软驱启动
	while (ticks_to_floppy_on(nr))
		sleep_on(nr+wait_motor);
	sti(); // 开启中断
}

/**
 * 关闭指定软驱电动机
 * @param nr 指定软驱句柄
*/
void floppy_off(unsigned int nr)
{
	moff_timer[nr]=3*HZ; // 3 秒
}

/**
 * 软盘定时处理子程序
 * 在每次时钟中断调用 do_timer 时被调用
*/
void do_floppy_timer(void)
{
	int i;
	unsigned char mask = 0x10;

	// 将四个软驱定时器全部处理完
	for (i=0 ; i<4 ; i++,mask <<= 1) {
		if (!(mask & current_DOR)) // 如果不是 DOR 指定的电动机则跳过
			continue;
		if (mon_timer[i]) { // 启动器定时不为0时
			if (!--mon_timer[i])
				wake_up(i+wait_motor); // 电动机启动时间到了唤醒对应进程
		} else if (!moff_timer[i]) { // 电动停转计时已到
			current_DOR &= ~mask; // 复位对应的电动机启动位
			outb(current_DOR,FD_DOR); // 更新数字输出寄存器
		} else
			moff_timer[i]--; // 停转计时 -1
	}
}

#define TIME_REQUESTS 64 // 默认定时器数组长度

/**
 * 定时器数组和定时器链表结构
 * next_timer 定时器链表头指针
*/
static struct timer_list {
	long jiffies; // 时钟滴答数
	void (*fn)(); // 定时处理程序
	struct timer_list * next; //定时器链表中的下一项
} timer_list[TIME_REQUESTS], * next_timer = NULL;

/**
 * 新增定时器
 * @param jiffies 初始时间嘀嗒数
 * @param fn 定时结束后调用函数
*/
void add_timer(long jiffies, void (*fn)(void))
{
	struct timer_list * p;

	// 执行函数为空时，返回
	if (!fn)
		return;
	// 禁止中断
	cli();
	// 时间计数小于0表示立即执行
	if (jiffies <= 0)
		(fn)();
	else {
		// 获取列表中空项
		for (p = timer_list ; p < timer_list + TIME_REQUESTS ; p++)
			if (!p->fn)
				break;
		if (p >= timer_list + TIME_REQUESTS)
			panic("No more time requests free");
		// 设置定时器参数
		p->fn = fn;
		p->jiffies = jiffies;
		p->next = next_timer;
		next_timer = p; // 将当前计数器添加到表头
		// 调整链表顺序，按照时间计数 从 小到大排列
		while (p->next && p->next->jiffies < p->jiffies) {
			p->jiffies -= p->next->jiffies;
			fn = p->fn;
			p->fn = p->next->fn;
			p->next->fn = fn;
			jiffies = p->jiffies;
			p->jiffies = p->next->jiffies;
			p->next->jiffies = jiffies;
			p = p->next;
		}
	}
	sti(); // 开启中断
}

/**
 * 每次时钟中断时进行调用
 * 对于一个进程由于执行时间片用完时，则进行任务切换，并进行计时更新
 * @param cpl 指的是当前调用程序系统特权级, 0 表示在内核态运行
 */ 
void do_timer(long cpl)
{
	extern int beepcount; // 扬声器发声时间嘀嗒数
	extern void sysbeepstop(void); // 关闭扬声器

	// 发声计数次数到，则关闭发声
	if (beepcount)
		if (!--beepcount)
			sysbeepstop();

	// 更新内核与用户运行时间片参数
	if (cpl)
		current->utime++;
	else
		current->stime++;

	// 如果有软驱操作定时器指针存在，则将第一个定时器的值减一
	// 如果已经等于 0，则调相应的处理程序，并将处理程序指针置空，然后去掉该项定时器
	if (next_timer) {
		next_timer->jiffies--;
		// 循环直到链表中定时器当前项 滴答数不为 0
		while (next_timer && next_timer->jiffies <= 0) {
			void (*fn)(void);
			
			fn = next_timer->fn;
			next_timer->fn = NULL;
			next_timer = next_timer->next;
			(fn)(); // 执行处理函数
		}
	}
	// 如果当前软盘控制器 FDC 的数字输出寄存器中电动机启动位有置位的，则执行软盘定时程序
	if (current_DOR & 0xf0)
		do_floppy_timer();
	if ((--current->counter)>0) return; // 如果进程运行时间还未用完则，继续执行
	current->counter=0;
	if (!cpl) return; // 对于内核程序，不依赖 Counter 值进行调度
	schedule();
}

/**
 * 设置报警定时系统调用
 * @param seconds 报警时间（秒数）
 * @return 设置过返回旧值，否则返回0
*/
int sys_alarm(long seconds)
{
	int old = current->alarm;

	if (old)
		old = (old - jiffies) / HZ;
	current->alarm = (seconds>0)?(jiffies+HZ*seconds):0;
	return (old);
}

/**
 * 获取当前进程号
*/
int sys_getpid(void)
{
	return current->pid;
}

/**
 * 获取当前进程父进程进程号
*/
int sys_getppid(void)
{
	return current->father;
}

/**
 * 获取当前进程用户号id
*/
int sys_getuid(void)
{
	return current->uid;
}

/**
 * 获取当前进程有效用户id
*/
int sys_geteuid(void)
{
	return current->euid;
}

/**
 * 获取当前进程组号
*/
int sys_getgid(void)
{
	return current->gid;
}

/**
 * 获取当前有效组号
*/
int sys_getegid(void)
{
	return current->egid;
}

/**
 * 降低当前优先权值
 * @param increment 降低的数
 * @return 0
*/
int sys_nice(long increment)
{
	if (current->priority-increment>0)
		current->priority -= increment;
	return 0;
}

/**
 * 调度程序初始化子程序
*/
void sched_init(void)
{
	int i;
	struct desc_struct * p; // 描述符表指针

	// 存放有关信号状态结构
	if (sizeof(struct sigaction) != 16)
		panic("Struct sigaction MUST be 16 bytes");
	set_tss_desc(gdt+FIRST_TSS_ENTRY,&(init_task.task.tss)); // 设置初始进程 0 相关 状态段
	set_ldt_desc(gdt+FIRST_LDT_ENTRY,&(init_task.task.ldt)); // 设置初始进程 0 相关 数据与代码段
	p = gdt+2+FIRST_TSS_ENTRY; // 清理进程数组及描述符表项
	for(i=1;i<NR_TASKS;i++) {
		task[i] = NULL; // 清除除第一项以外的所有进程数据
		p->a=p->b=0;
		p++;
		p->a=p->b=0;
		p++;
	}
/* Clear NT, so that we won't have troubles with that later on */
	/**
	 * 首先清除标志寄存器中的位 NT，
	 * NT 标志用于控制程序的递归调用，当 NT 位置位时，那么当前中断任务执行 iret 指令时就会引起任务切换；
	 * NT 指出 TSS 中的 back_link 字段是否有效
	*/
	__asm__("pushfl ; andl $0xffffbfff,(%esp) ; popfl");
	ltr(0); // 将进程 0 的 TSS 加载到任务寄存器 tr
	lldt(0); // 将局部描述符表加载到局部描述符寄存器，置手动加载这一次，之后是 cpu 根据 TSS 中的 LDT 项自动加载
	/**
	 * 然后对 8253 定时器进行初始化
	*/
	outb_p(0x36,0x43);		/* binary, mode 3, LSB/MSB, ch 0 */
	outb_p(LATCH & 0xff , 0x40);	/* LSB */ // 定时值低字节初始化
	outb(LATCH >> 8 , 0x40);	/* MSB */ // 定时器高字节初始化
	set_intr_gate(0x20,&timer_interrupt); // 设置定时器中断执行函数
	outb(inb_p(0x21)&~0x01,0x21); // 修改中断控制器屏蔽吗，允许时钟中断
	// 设置系统调用中断门，
	set_system_gate(0x80,&system_call);
}
