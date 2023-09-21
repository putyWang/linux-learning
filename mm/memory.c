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

volatile void do_exit(long code); // 进程退出函数

/**
 * 显示内存已用完出错信息，并退出
*/
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
#define LOW_MEM 0x100000                     // 内存低端 默认为 1 MB
#define PAGING_MEMORY (15*1024*1024)         // 分页内存 15 MB，主内存区最多 15 MB
#define PAGING_PAGES (PAGING_MEMORY>>12)     // 分页后的物理内存页数 （一页 4 KB）
#define MAP_NR(addr) (((addr)-LOW_MEM)>>12)  // 获取指定内存所在页
#define USED 100                             // 页面被占用标志

// 该宏用于判断给定地址是否位于当前进程的代码段中，参见 252 行
#define CODE_SPACE(addr) ((((addr)+4095)&~4095) < \
current->start_code + current->end_code)

static long HIGH_MEMORY = 0; // 存储实际物理内存最高端地址

// 从 from 地址复制 1页（4KB） 数据内容到 to 地址处
#define copy_page(from,to) \
__asm__("cld ; rep ; movsl"::"S" (from),"D" (to),"c" (1024):"cx","di","si")

static unsigned char mem_map [ PAGING_PAGES ] = {0,}; // 内存映射字节图（1B代表 1 页），每个页面对应字节代表被引用（占用）次数

/*
 * Get physical address of first (actually last :-) free page, and mark it
 * used. If no free pages left, return 0.
 */
/**
 * 获取第一个（实际上是最后一个）空闲物理页面，标记为一使用；
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
 * 释放指定物理地址开始处的一页物理内存
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
 * 根据指定线性地址和限长(页面个数)，释放内存页表所对应的页表项，并置表项空闲
 * @param from 起始线性地址
 * @param size 释放的页面长度大小
*/
int free_page_tables(unsigned long from,unsigned long size)
{
	unsigned long *pg_table;
	unsigned long * dir, nr;

	if (from & 0x3fffff) // 需要释放内存需要以 4 MB 为边界（0-21位需要为空）
		panic("free_page_tables called with wrong alignment");
	if (!from) // 试图释放内核所占用空间出错
		panic("Trying to free up swapper memory space");
	size = (size + 0x3fffff) >> 22; // 计算所占页目录项数（4 MB 的进位的整数位），即所占页表数
	dir = (unsigned long *) ((from>>20) & 0xffc); /* _pg_dir = 0 */ // 计算起始目录项（最后两位为标志位）
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
 * 把一个物理页也射到指定的线性地址处
 * @param page 物理页地址
 * @param address 线性地址
 * @return 页面地址
*/
unsigned long put_page(unsigned long page,unsigned long address)
{
	unsigned long tmp, *page_table;

/* NOTE !!! This uses the fact that _pg_dir=0 */
	// 判断物理页地址是否为非法地址
	if (page < LOW_MEM || page >= HIGH_MEMORY)
		printk("Trying to put page %p at %p\n",page,address);
	// 判断需映射的物理页是否已经使用（未使用或共享页面不允许映射）
	if (mem_map[(page-LOW_MEM)>>12] != 1)
		printk("mem_map disagrees with %p at %p\n",page,address);
	// 查询页表地址
	page_table = (unsigned long *) ((address>>20) & 0xffc);
	// 目录项有效，获取页表地址
	if ((*page_table)&1)
		page_table = (unsigned long *) (0xfffff000 & *page_table);
	// 无效则重新申请一个新物理页存放该页表
	else {
		if (!(tmp=get_free_page()))
			return 0;
		// 置相应标志位（p位，u/s位以及r/w位）
		*page_table = tmp|7;
		// 将该页表地址保存到 page_table 之中
		page_table = (unsigned long *) tmp;
	}
	// 设置对应页表项地址
	page_table[(address>>12) & 0x3ff] = page | 7;
/* no need for invalidate */
	return page;
}

/**
 * 取消页面写保护
 * @param table_entry 页表项指针
*/
void un_wp_page(unsigned long * table_entry)
{
	unsigned long old_page,new_page;

	old_page = 0xfffff000 & *table_entry; // 获取需要取消写保护的页面
	// 如果页面仅使用了一次，直接将 R/W 读写位置位
	if (old_page >= LOW_MEM && mem_map[MAP_NR(old_page)]==1) {
		*table_entry |= 2;
		invalidate();
		return;
	}
	if (!(new_page=get_free_page())) //申请新的空闲内存页
		oom();
	if (old_page >= LOW_MEM)
		mem_map[MAP_NR(old_page)]--; // 并将旧共享页面的引用次数 -1
	*table_entry = new_page | 7; // 将页表项指针指向新申请的物理页面
	invalidate();
	copy_page(old_page,new_page); // 共享页面则需要将数据复制到新的物理页之中，
}	

/**
 * 页异常中断处理调用的 c 函数，写共享页面的处理函数，在 page.s 中被调用
 * @param error_code 错误码
 * @param address 页面线性地址
*/
void do_wp_page(unsigned long error_code,unsigned long address)
{
#if 0
/* we cannot do this yet: the estdio library writes to code space */
/* stupid, stupid. I really want the libc.a from GNU */
	if (CODE_SPACE(address))
		do_exit(SIGSEGV);
#endif
	// 取消指定页面的写保护
	// (unsigned long *) ((address>>20) &0xffc) 页目录项地址
	// (0xfffff000 & *((unsigned long *) ((address>>20) &0xffc)) 页表项地址
	// (address>>10) & 0xffc) 页面在页表项中的偏移值
	un_wp_page((unsigned long *)
		(((address>>10) & 0xffc) + (0xfffff000 &
		*((unsigned long *) ((address>>20) &0xffc)))));

}

/**
 * 写页面验证，若页面不可写，则复制页面
 * @param address 页面线性地址
*/
void write_verify(unsigned long address)
{
	unsigned long page;

	// page 指向对应页表项
	if (!( (page = *((unsigned long *) ((address>>20) & 0xffc)) )&1))
		return;
	// 对应页表地址
	page &= 0xfffff000;
	// 加上指定页偏移量，获取页表项中对应页项
	page += ((address>>10) & 0xffc);
	// 获取对应 r/w 位，查看是否置位以及P位是否置位
	if ((3 & *(unsigned long *) page) == 1)  /* non-writeable, present */
		// 取消对应页面的写保护
		un_wp_page((unsigned long *) page);
	return;
}

/**
 * 获取一个空闲物理页并映射到指定线性地址处
 * @param address 线性地址
*/
void get_empty_page(unsigned long address)
{
	unsigned long tmp;

	// 物理页全部都已经被使用或映射失败时。报错并释放申请到的物理内存
	if (!(tmp=get_free_page()) || !put_page(tmp,address)) {
		free_page(tmp);		/* 0 is ok - ignored */
		oom();
	}
}

/**
 * 尝试将指定进程指定地址处的页面进行共享到当前进程
 * @param address 线性地址
 * @param p 共享的源进程
 * @return 1-成功，0-失败
*/
static int try_to_share(unsigned long address, struct task_struct * p)
{
	unsigned long from;
	unsigned long to;
	unsigned long from_page;
	unsigned long to_page;
	unsigned long phys_addr;

	from_page = to_page = ((address>>20) & 0xffc);
	from_page += ((p->start_code>>20) & 0xffc); // 获取 p 进程指定页目录项地址
	to_page += ((current->start_code>>20) & 0xffc); // 获取当前进程指定页目录项地址
/* is there a page-directory at from? */
	from = *(unsigned long *) from_page; // 获取 p 进程指定页表项页面地址
	// 源目录项页面未映射（P = 0）直接返回
	if (!(from & 1))
		return 0;
	from &= 0xfffff000; // 获取源页表项地址
	from_page = from + ((address>>10) & 0xffc);
	phys_addr = *(unsigned long *) from_page; // 数据所在真实物理页地址
/* is the page clean and present? */
	// 该物理页面脏位未置位单p位置位的情况下，继续，其他情况返回
	if ((phys_addr & 0x41) != 0x01)
		return 0;
	// 获取物理页面起始地址
	phys_addr &= 0xfffff000;
	// 物理地址必须有效
	if (phys_addr >= HIGH_MEMORY || phys_addr < LOW_MEM)
		return 0;
	// 设置目标空闲页面
	to = *(unsigned long *) to_page;
	// 页表项未映射，映射新空白页
	if (!(to & 1))
		if (to = get_free_page())
			*(unsigned long *) to_page = to | 7;
		else
			oom();
	to &= 0xfffff000;
	to_page = to + ((address>>10) & 0xffc); // 目标线性地址所在页表项中的页框地址
	// 该页面已被映射，死机
	if (1 & *(unsigned long *) to_page)
		panic("try_to_share: to_page already exists");
/* share them: write-protect */
	// 设置物理页面的 R/W 位，写保护
	*(unsigned long *) from_page &= ~2;
	*(unsigned long *) to_page = *(unsigned long *) from_page; // 目标页框指向共享物理页
	invalidate();
	// 对应内存映射数组引用计数 + 1
	phys_addr -= LOW_MEM;
	phys_addr >>= 12;
	mem_map[phys_addr]++;
	return 1;
}

/**
 * 共享页面，在缺页处理时看看能否共享页面
 * @param address 线性地址
 * @return 1-成功，0-失败
*/
static int share_page(unsigned long address)
{
	struct task_struct ** p;
	// 当前是不可执行的，直接返回
	if (!current->executable)
		return 0;
	// 仅在单独执行时，也直接退出
	if (current->executable->i_count < 2)
		return 0;
	// 查询可以与当前进程共享页面的进程（正在执行同一个文件）
	for (p = &LAST_TASK ; p > &FIRST_TASK ; --p) {
		if (!*p)
			continue;
		if (current == *p)
			continue;
		if ((*p)->executable != current->executable)
			continue;
		// 只有在进程执行文件与本进程一致且不是本进程时才共享页面
		if (try_to_share(address,*p))
			return 1;
	}
	return 0;
}

/**
 * 页异常中断处理调用函数，处理缺页异常情况
 * @param error_code 错误码
 * @param address 页面线性地址
*/
void do_no_page(unsigned long error_code,unsigned long address)
{
	int nr[4];
	unsigned long tmp;
	unsigned long page;
	int block,i;

	address &= 0xfffff000; // 页面地址
	tmp = address - current->start_code; // 计算指定线性地址在进程空间中相对于进程基址的偏移长度值
	// 当前进程不是可执行的或指定地址已经超出进程的代码范围
	if (!current->executable || tmp >= current->end_data) {
		get_empty_page(address); // 申请映射一个空物理页面到指定线性地址
		return;
	}
	// 如果能够该页面能共享，直接返回
	if (share_page(tmp))
		return;
	// 否则从根文件设备读取指定块
	if (!(page = get_free_page()))
		oom();
/* remember that 1 block is used for header */
	block = 1 + tmp/BLOCK_SIZE;
	for (i=0 ; i<4 ; block++,i++)
		nr[i] = bmap(current->executable,block); // 获取设备上对应逻辑块
	bread_page(page,current->executable->i_dev,nr); // 读设备上一个页面的数据（4个逻辑块）到指定物理地址 page 处
	// 将超过 end_data 部分的空间清空
	i = tmp + 4096 - current->end_data;
	tmp = page + 4096;
	while (i-- > 0) {
		tmp--;
		*(char *)tmp = 0;
	}
	// 将page 与 address 进行映射
	if (put_page(page,address))
		return;
	// 失败 释放内存，报错
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

/**
 * 计算内存空闲页面数并展示
*/
void calc_mem(void)
{
	int i,j,k,free=0;
	long * pg_tbl;

	// 扫描内存页面映射数组 mem_map[]，获取空闲页面数并显示 
	for(i=0 ; i<PAGING_PAGES ; i++)
		if (!mem_map[i]) free++;
	printk("%d pages free (of %d)\n\r",free,PAGING_PAGES);
	// 统计页表中有效页面数
	for(i=2 ; i<1024 ; i++) {
		if (1&
		[i]) {
			pg_tbl=(long *) (0xfffff000 & pg_dir[i]);
			for(j=k=0 ; j<1024 ; j++)
				if (pg_tbl[j]&1)
					k++;
			printk("Pg-dir[%d] uses %d pages\n",i,k);
		}
	}
}
