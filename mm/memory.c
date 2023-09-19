/*
 *  linux/mm/memory.c
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 * demand-loading started 01.12.91 - seems it is high on the list of
 * things wanted, and it should be easy to implement. - Linus
 */

/*
 * Ok, demand-loading was easy, shared pages a little bit tricker. Shared
 * pages started 02.12.91, seems to work. - Linus.
 *
 * Tested sharing by executing about 30 /bin/sh: under the old kernel it
 * would have taken more than the 6M I have free, but it worked well as
 * far as I could see.
 *
 * Also corrected some "invalidate()"s - I wasn't doing enough of them.
 */

#include <signal.h>

#include <asm/system.h>

#include <linux/sched.h>
#include <linux/head.h>
#include <linux/kernel.h>

volatile void do_exit(long code);

static inline volatile void oom(void)
{
	printk("out of memory\n\r");
	do_exit(SIGSEGV);
}

/**
 * 刷新页变换高速缓冲函数
 * 为了提高地址转换的效率，cpu将最近使用的页表数据存放在芯片中高速缓冲中；
 * 在修改过页表信息之后，就需要刷新该缓冲区，这里使用重新加载目录基址寄存器 cr3 的方式进行刷新 （0 为页目录基址）
*/
#define invalidate() \
__asm__("movl %%eax,%%cr3"::"a" (0))

/* these are not to be changed without changing head.s etc */
#define LOW_MEM 0x100000 // 内存低端 默认为 1 MB
#define PAGING_MEMORY (15*1024*1024) // 分页内存 15 MB，主内存区最多 15 MB
#define PAGING_PAGES (PAGING_MEMORY>>12) // 分页后的物理内存页数 （一页 4 KB）
#define MAP_NR(addr) (((addr)-LOW_MEM)>>12) // 获取指定内存所在页
#define USED 100 // 页面被占用标志

#define CODE_SPACE(addr) ((((addr)+4095)&~4095) < \
current->start_code + current->end_code)

static long HIGH_MEMORY = 0; // 存储物理内存最高端地址

#define copy_page(from,to) \
__asm__("cld ; rep ; movsl"::"S" (from),"D" (to),"c" (1024):"cx","di","si")

static unsigned char mem_map [ PAGING_PAGES ] = {0,}; // 内存映射字节图（1B代表 1 页），每个页面对应字节代表被引用（占用）次数

/*
 * Get physical address of first (actually last :-) free page, and mark it
 * used. If no free pages left, return 0.
 */
/**
 * 获取第一个（实际上是最后一个）空闲页面，标记为一使用；
 * 当没有空闲页面页面时返回0；
*/
unsigned long get_free_page(void)
{
register unsigned long __res asm("ax");

__asm__("std ; repne ; scasb\n\t" // 方向位置位，将 al(0) 与对应每个页面(di)内容进行比较
	"jne 1f\n\t" // 如果没有等于 0 的字节，则跳转结束
	"movb $1,1(%%edi)\n\t" // 将对应页面的内存映像位置 1
	"sall $12,%%ecx\n\t" // 页面数 * 4Kb = 相对页面起始地址
	"addl %2,%%ecx\n\t" //加上内存低端地址 = 实际页面起始物理地址
	"movl %%ecx,%%edx\n\t" // 保存实际起始物理地址 -> edx
	"movl $1024,%%ecx\n\t" // ecx 置计数值 1024
	"leal 4092(%%edx),%%edi\n\t" // 将 4092 + 实际内存物理地址（即当前页面末端地址）保存到 edi 中
	"rep ; stosl\n\t" // 将 edi 所指内存清 0
	"movl %%edx,%%eax\n" // 将页面起始物理地址保存到 ax 中
	"1:"
	:"=a" (__res)
	:"0" (0),"i" (LOW_MEM),"c" (PAGING_PAGES),
	"D" (mem_map+PAGING_PAGES-1)
	:"di","cx","dx");
return __res;
}

/*
 * Free a page of memory at physical address 'addr'. Used by
 * 'free_page_tables()'
 */
/**
 * 释放指定物理地址开始处的一页内存
 * @param addr 需要释放内存的页的起始物理内存
*/
void free_page(unsigned long addr)
{
	// 不允许释放 内核相关内存（小于 1MB）
	if (addr < LOW_MEM) return;
	// 物理地址必须小于最高内存
	if (addr >= HIGH_MEMORY)
		panic("trying to free nonexistent page");
	addr -= LOW_MEM; // 获取相对地址
	addr >>= 12; // 获取指定内存地址
	if (mem_map[addr]--) return; // 若对应内存页面映射字节不等于 0，则减一返回
	mem_map[addr]=0; // 否则置对应映射字节为 0 死机
	panic("trying to free free page");
}

/*
 * This function frees a continuos block of page tables, as needed
 * by 'exit()'. As does copy_page_tables(), this handles only 4Mb blocks.
 */
/**
 * 根据指定线性地址和限长(页面个数)，释放内存页表所对应的内存块，并置表项空闲
 * @param from 起始线性地址
 * @param size 释放的页面长度大小
*/
int free_page_tables(unsigned long from,unsigned long size)
{
	unsigned long *pg_table;
	unsigned long * dir, nr;

	if (from & 0x3fffff) // 需要释放内存需要以 4 MB 为边界
		panic("free_page_tables called with wrong alignment");
	if (!from) // 试图释放内核所占用空间出错
		panic("Trying to free up swapper memory space");
	size = (size + 0x3fffff) >> 22; // 计算所占页目录项数（4 MB 的进位的整数位），即所占页表数
	dir = (unsigned long *) ((from>>20) & 0xffc); /* _pg_dir = 0 */ // 计算起始目录项
	// 遍历需要释放内存的目录项数
	for ( ; size-->0 ; dir++) {
		// 该目录项无效（p 位 = 0）时，跳过本目录项
		if (!(1 & *dir))
			continue;
		// 获取目录项中的页表地址
		pg_table = (unsigned long *) (0xfffff000 & *dir);
		// 遍历页表中的页
		for (nr=0 ; nr<1024 ; nr++) {
			// p 位 = 1 时，则释放该页内存
			if (1 & *pg_table)
				free_page(0xfffff000 & *pg_table);
			*pg_table = 0; // 页表项内容清零
			pg_table++; // 页表下一项
		}
		free_page(0xfffff000 & *dir); // 释放该页表所占内存空间
		*dir = 0; // 清空对应页表目录项
	}
	invalidate(); // 刷新页交换高速缓冲
	return 0;
}

/*
 *  Well, here is one of the most complicated functions in mm. It
 * copies a range of linerar addresses by copying only the pages.
 * Let's hope this is bug-free, 'cause this one I don't want to debug :-)
 *
 * Note! We don't copy just any chunks of memory - addresses have to
 * be divisible by 4Mb (one page-directory entry), as this makes the
 * function easier. It's used only by fork anyway.
 *
 * NOTE 2!! When from==0 we are copying kernel space for the first
 * fork(). Then we DONT want to copy a full page-directory entry, as
 * that would lead to some serious memory waste - we just copy the
 * first 160 pages - 640kB. Even that is more than we need, but it
 * doesn't take any more memory - we don't copy-on-write in the low
 * 1 Mb-range, so the pages can be shared with the kernel. Thus the
 * special case for nr=xxxx.
 */
/**
 * 复制指定线性地址和长度（页表个数）内存对应的页目录和页表，从而被复制的页目录对应的原物理内存区被共享使用；
 * 此后两个进程共享该物理内存，直到有个进程对共享数据进行了更改
 * @param from 复制原进程起始线性地址
 * @param to 复制目标进程起始线性地址
 * @param size 释放的页面长度大小
*/
int copy_page_tables(unsigned long from,unsigned long to,long size)
{
	unsigned long * from_page_table;
	unsigned long * to_page_table;
	unsigned long this_page;
	unsigned long * from_dir, * to_dir;
	unsigned long nr;

	// 内存需要以 4 MB（页目录项大小） 为边界
	if ((from&0x3fffff) || (to&0x3fffff))
		panic("copy_page_tables called with wrong alignment");
	from_dir = (unsigned long *) ((from>>20) & 0xffc); /* _pg_dir = 0 */ // 获取源目录指针
	to_dir = (unsigned long *) ((to>>20) & 0xffc); // 获取目标目录指针
	size = ((unsigned) (size+0x3fffff)) >> 22; // 复制的目录项数
	for( ; size-->0 ; from_dir++,to_dir++) {
		// 目标目录也有数据时，输出被占用，死机
		if (1 & *to_dir)
			panic("copy_page_tables: already exist");
		// 源目录为空时跳过该项
		if (!(1 & *from_dir))
			continue;
		// 获取当前源目录项中的页表地址
		from_page_table = (unsigned long *) (0xfffff000 & *from_dir);
		// 为目的页表取一块空闲内存
		if (!(to_page_table = (unsigned long *) get_free_page()))
			return -1;	/* Out of memory, see freeing */
		// 设置目标目录项内容（7 为标志信息，表示 Usr、R/W，Present）
		*to_dir = ((unsigned long) to_page_table) | 7;
		// 设置需要复制的页面数（为0时只能复制 160 页，否则需要复制完整的 1024 个页表项）
		nr = (from==0)?0xA0:1024;
		// 遍历复制页项信息
		for ( ; nr-- > 0 ; from_page_table++,to_page_table++) {
			this_page = *from_page_table;
			// 当前页面未使用时，跳过
			if (!(1 & this_page))
				continue;
			// 复位页表项所指页面的 R/W 标志位
			// 如果 U/S位是0，则 R/W 没有作用，U/S位是 1 ，则 R/W 为 0 时，运行在用户层面的代码只有读功能，否则可以写
			this_page &= ~2;
			*to_page_table = this_page;
			// 该内存在 1 Mb 以上(非内核代码页面)时，需要设置 men_map
			if (this_page > LOW_MEM) {
				*from_page_table = this_page; // 令源代码也为只读，只有写时才会分配新的内存页面，进行写时复制 
				this_page -= LOW_MEM;
				this_page >>= 12;
				mem_map[this_page]++; // 内存使用位 +1
			}
		}
	}
	invalidate();
	return 0;
}

/*
 * This function puts a page in memory at the wanted address.
 * It returns the physical address of the page gotten, 0 if
 * out of memory (either when trying to access page-table or
 * page.)
 */
/**
 * 
 * 
*/
unsigned long put_page(unsigned long page,unsigned long address)
{
	unsigned long tmp, *page_table;

/* NOTE !!! This uses the fact that _pg_dir=0 */

	if (page < LOW_MEM || page >= HIGH_MEMORY)
		printk("Trying to put page %p at %p\n",page,address);
	if (mem_map[(page-LOW_MEM)>>12] != 1)
		printk("mem_map disagrees with %p at %p\n",page,address);
	page_table = (unsigned long *) ((address>>20) & 0xffc);
	if ((*page_table)&1)
		page_table = (unsigned long *) (0xfffff000 & *page_table);
	else {
		if (!(tmp=get_free_page()))
			return 0;
		*page_table = tmp|7;
		page_table = (unsigned long *) tmp;
	}
	page_table[(address>>12) & 0x3ff] = page | 7;
/* no need for invalidate */
	return page;
}

void un_wp_page(unsigned long * table_entry)
{
	unsigned long old_page,new_page;

	old_page = 0xfffff000 & *table_entry;
	if (old_page >= LOW_MEM && mem_map[MAP_NR(old_page)]==1) {
		*table_entry |= 2;
		invalidate();
		return;
	}
	if (!(new_page=get_free_page()))
		oom();
	if (old_page >= LOW_MEM)
		mem_map[MAP_NR(old_page)]--;
	*table_entry = new_page | 7;
	invalidate();
	copy_page(old_page,new_page);
}	

/*
 * This routine handles present pages, when users try to write
 * to a shared page. It is done by copying the page to a new address
 * and decrementing the shared-page counter for the old page.
 *
 * If it's in code space we exit with a segment error.
 */
void do_wp_page(unsigned long error_code,unsigned long address)
{
#if 0
/* we cannot do this yet: the estdio library writes to code space */
/* stupid, stupid. I really want the libc.a from GNU */
	if (CODE_SPACE(address))
		do_exit(SIGSEGV);
#endif
	un_wp_page((unsigned long *)
		(((address>>10) & 0xffc) + (0xfffff000 &
		*((unsigned long *) ((address>>20) &0xffc)))));

}

/**
 * 
*/
void write_verify(unsigned long address)
{
	unsigned long page;

	if (!( (page = *((unsigned long *) ((address>>20) & 0xffc)) )&1))
		return;
	page &= 0xfffff000;
	page += ((address>>10) & 0xffc);
	if ((3 & *(unsigned long *) page) == 1)  /* non-writeable, present */
		un_wp_page((unsigned long *) page);
	return;
}

void get_empty_page(unsigned long address)
{
	unsigned long tmp;

	if (!(tmp=get_free_page()) || !put_page(tmp,address)) {
		free_page(tmp);		/* 0 is ok - ignored */
		oom();
	}
}

/*
 * try_to_share() checks the page at address "address" in the task "p",
 * to see if it exists, and if it is clean. If so, share it with the current
 * task.
 *
 * NOTE! This assumes we have checked that p != current, and that they
 * share the same executable.
 */
static int try_to_share(unsigned long address, struct task_struct * p)
{
	unsigned long from;
	unsigned long to;
	unsigned long from_page;
	unsigned long to_page;
	unsigned long phys_addr;

	from_page = to_page = ((address>>20) & 0xffc);
	from_page += ((p->start_code>>20) & 0xffc);
	to_page += ((current->start_code>>20) & 0xffc);
/* is there a page-directory at from? */
	from = *(unsigned long *) from_page;
	if (!(from & 1))
		return 0;
	from &= 0xfffff000;
	from_page = from + ((address>>10) & 0xffc);
	phys_addr = *(unsigned long *) from_page;
/* is the page clean and present? */
	if ((phys_addr & 0x41) != 0x01)
		return 0;
	phys_addr &= 0xfffff000;
	if (phys_addr >= HIGH_MEMORY || phys_addr < LOW_MEM)
		return 0;
	to = *(unsigned long *) to_page;
	if (!(to & 1))
		if (to = get_free_page())
			*(unsigned long *) to_page = to | 7;
		else
			oom();
	to &= 0xfffff000;
	to_page = to + ((address>>10) & 0xffc);
	if (1 & *(unsigned long *) to_page)
		panic("try_to_share: to_page already exists");
/* share them: write-protect */
	*(unsigned long *) from_page &= ~2;
	*(unsigned long *) to_page = *(unsigned long *) from_page;
	invalidate();
	phys_addr -= LOW_MEM;
	phys_addr >>= 12;
	mem_map[phys_addr]++;
	return 1;
}

/*
 * share_page() tries to find a process that could share a page with
 * the current one. Address is the address of the wanted page relative
 * to the current data space.
 *
 * We first check if it is at all feasible by checking executable->i_count.
 * It should be >1 if there are other tasks sharing this inode.
 */
static int share_page(unsigned long address)
{
	struct task_struct ** p;

	if (!current->executable)
		return 0;
	if (current->executable->i_count < 2)
		return 0;
	for (p = &LAST_TASK ; p > &FIRST_TASK ; --p) {
		if (!*p)
			continue;
		if (current == *p)
			continue;
		if ((*p)->executable != current->executable)
			continue;
		if (try_to_share(address,*p))
			return 1;
	}
	return 0;
}

void do_no_page(unsigned long error_code,unsigned long address)
{
	int nr[4];
	unsigned long tmp;
	unsigned long page;
	int block,i;

	address &= 0xfffff000;
	tmp = address - current->start_code;
	if (!current->executable || tmp >= current->end_data) {
		get_empty_page(address);
		return;
	}
	if (share_page(tmp))
		return;
	if (!(page = get_free_page()))
		oom();
/* remember that 1 block is used for header */
	block = 1 + tmp/BLOCK_SIZE;
	for (i=0 ; i<4 ; block++,i++)
		nr[i] = bmap(current->executable,block);
	bread_page(page,current->executable->i_dev,nr);
	i = tmp + 4096 - current->end_data;
	tmp = page + 4096;
	while (i-- > 0) {
		tmp--;
		*(char *)tmp = 0;
	}
	if (put_page(page,address))
		return;
	free_page(page);
	oom();
}

/**
 * 初始化主内存空间
 * @param start_mem 主内存起始地址
 * @param end_mem 主内存末端地址
*/
void mem_init(long start_mem, long end_mem)
{
	int i;

	HIGH_MEMORY = end_mem; // 更新实际内存末端地址
	// 首先设置内存中所有页面都为已占用
	for (i=0 ; i<PAGING_PAGES ; i++)
		mem_map[i] = USED;
	// 减去内核已占用内存，其余全部设置为未使用（0） 
	i = MAP_NR(start_mem);
	end_mem -= start_mem;
	end_mem >>= 12;
	while (end_mem-->0)
		mem_map[i++]=0;
}

void calc_mem(void)
{
	int i,j,k,free=0;
	long * pg_tbl;

	for(i=0 ; i<PAGING_PAGES ; i++)
		if (!mem_map[i]) free++;
	printk("%d pages free (of %d)\n\r",free,PAGING_PAGES);
	for(i=2 ; i<1024 ; i++) {
		if (1&pg_dir[i]) {
			pg_tbl=(long *) (0xfffff000 & pg_dir[i]);
			for(j=k=0 ; j<1024 ; j++)
				if (pg_tbl[j]&1)
					k++;
			printk("Pg-dir[%d] uses %d pages\n",i,k);
		}
	}
}
