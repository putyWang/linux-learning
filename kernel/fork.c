/*
 *  linux/kernel/fork.c
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 *  'fork.c' contains the help-routines for the 'fork' system call
 * (see also system_call.s), and some misc functions ('verify_area').
 * Fork is rather simple, once you get the hang of it, but the memory
 * management can be a bitch. See 'mm/mm.c': 'copy_page_tables()'
 */
#include <errno.h>

#include <linux/sched.h>
#include <linux/kernel.h>
#include <asm/segment.h>
#include <asm/system.h>

extern void write_verify(unsigned long address);

long last_pid=0;

/**
 * 进程空间区写前验证函数
 * @param addr 当前进程地址
 * @param size 与 addr 相加这一段进程空间以页为单位执行写操作的检测操作
*/
void verify_area(void * addr,int size)
{
	unsigned long start;
	// 将起始地址 start 调整为其所在页的左边界开始位置，同时相应地调整验证区域大小
	start = (unsigned long) addr;
	size += start & 0xfff;
	// 将 start 位置设置为进程线性空间地址
	start &= 0xfffff000;
	// 将其转化为整个内存空间中的线性地址位置
	start += get_base(current->ldt[2]);
	while (size>0) {
		size -= 4096;
		write_verify(start); // 页面写验证，不可写时进行复制
		start += 4096;
	}
}

/**
 * 设置新进程的代码和数据段基址、限长并复制页；
 * @param nr 当前进程号
 * @param p 新生成进程结构指针
*/
int copy_mem(int nr,struct task_struct * p)
{
	unsigned long old_data_base,new_data_base,data_limit;
	unsigned long old_code_base,new_code_base,code_limit;

	code_limit=get_limit(0x0f); // 取局部描述符表中代码段描述符项中的段限长
	data_limit=get_limit(0x17); // 取局部描述符表中数据段描述符项中的段限长
	old_code_base = get_base(current->ldt[1]); // 取原代码段基址
	old_data_base = get_base(current->ldt[2]); // 取原数据段基址
	// 0.11 版本不支持代码段与数据段分离
	if (old_data_base != old_code_base)
		panic("We don't support separate I&D");
	// 数据段限长不能大于代码段
	if (data_limit < code_limit)
		panic("Bad data_limit");
	// 新基址 = 任务号 * 64MB（任务大小）
	new_data_base = new_code_base = nr * 0x4000000;
	p->start_code = new_code_base;
	// 设置新进程 代码基址与数据基址域
	set_base(p->ldt[1],new_code_base);
	set_base(p->ldt[2],new_data_base);
	// 把新进程的线性地址内存页对应到实际物理地址内存页面上
	if (copy_page_tables(old_data_base,new_data_base,data_limit)) {
		free_page_tables(new_data_base,data_limit);
		return -ENOMEM;
	}
	return 0;
}

/*
 *  Ok, this is the main fork-routine. It copies the system process
 * information (task[nr]) and sets up the necessary registers. It
 * also copies the data segment in it's entirety.
 */
/**
 * 进程复制函数
 * 复制系统进程信息(task[n])并且设置必要的寄存器，同时整个复制数据段
 * @param nr 分配的空闲进程 index
*/
int copy_process(int nr,long ebp,long edi,long esi,long gs,long none,
		long ebx,long ecx,long edx,
		long fs,long es,long ds,
		long eip,long cs,long eflags,long esp,long ss)
{
	struct task_struct *p;
	int i;
	struct file *f;

	p = (struct task_struct *) get_free_page(); // 为新进程数据结构分配进程
	if (!p) // 分配出错 返回错误码
		return -EAGAIN;
	task[nr] = p;
	*p = *current;	/* NOTE! this doesn't copy the supervisor stack */ // 复制当前进程内容
	p->state = TASK_UNINTERRUPTIBLE; // 进程状态设置为不可中断等待状态
	p->pid = last_pid; // 设置进程号
	p->father = current->pid; // 父进程为当前进程
	p->counter = p->priority;
	p->signal = 0; // 初始化信号量为 0
	p->alarm = 0; // 初始化 alarm 为 0
	p->leader = 0;		/* process leadership doesn't inherit */
	p->utime = p->stime = 0; // 初始化系统态运行时间为 0
	p->cutime = p->cstime = 0; // 初始化系统态运行时间为 0
	p->start_time = jiffies; // 设置进程创建时间为当前时间
	p->tss.back_link = 0;
	p->tss.esp0 = PAGE_SIZE + (long) p;
	p->tss.ss0 = 0x10;
	p->tss.eip = eip;
	p->tss.eflags = eflags;
	p->tss.eax = 0; // 新进程返回 0 的原因
	p->tss.ecx = ecx;
	p->tss.edx = edx;
	p->tss.ebx = ebx;
	p->tss.esp = esp;
	p->tss.ebp = ebp;
	p->tss.esi = esi;
	p->tss.edi = edi;
	p->tss.es = es & 0xffff; // 段寄存器仅 16位 有效
	p->tss.cs = cs & 0xffff;
	p->tss.ss = ss & 0xffff;
	p->tss.ds = ds & 0xffff;
	p->tss.fs = fs & 0xffff;
	p->tss.gs = gs & 0xffff;
	p->tss.ldt = _LDT(nr); // 新进程的局部描述表选择符
	p->tss.trace_bitmap = 0x80000000; // 高 15 位 有效
	// 当前进程使用了协处理器，保存其上下文
	if (last_task_used_math == current)
		__asm__("clts ; fnsave %0"::"m" (p->tss.i387));
	// 设置新任务的代码和数据段基址、限长并复制页表
	// 出错时，复位任务数组中相应项并释放位改新任务分配的内存页
	if (copy_mem(nr,p)) {
		task[nr] = NULL;
		free_page((long) p);
		return -EAGAIN;
	}
	// 父进程有文件是打开的，则文件打开次数 +1 
	for (i=0; i<NR_OPEN;i++)
		if (f=p->filp[i])
			f->f_count++;
	if (current->pwd)
		current->pwd->i_count++;
	if (current->root)
		current->root->i_count++;
	if (current->executable)
		current->executable->i_count++;
	// 在 GDT 中设置新任务的 TSS 与 LDT 描述符项
	// 任务切换时，任务寄存器 tr 由 cpu 自动加载
	set_tss_desc(gdt+(nr<<1)+FIRST_TSS_ENTRY,&(p->tss));
	set_ldt_desc(gdt+(nr<<1)+FIRST_LDT_ENTRY,&(p->ldt));
	p->state = TASK_RUNNING;	/* do this last, just in case */
	return last_pid;
}

/**
 * 为新进程生成不重复的进程号 pid， 并返回在任务数组中的任务号（ 数组 index）
*/
int find_empty_process(void)
{
	int i;

	// 下面三行代码获取唯一的进程号
	repeat:
		if ((++last_pid)<0) last_pid=1;
		for(i=0 ; i<NR_TASKS ; i++)
			if (task[i] && task[i]->pid == last_pid) goto repeat;
	// 便利获取进程数组中空闲项
	for(i=1 ; i<NR_TASKS ; i++)
		if (!task[i])
			return i;
	return -EAGAIN;
}
