/*
 * malloc.c --- a general purpose kernel memory allocator for Linux.
 * 
 * Written by Theodore Ts'o (tytso@mit.edu), 11/29/91
 *
 * This routine is written to be as fast as possible, so that it
 * can be called from the interrupt level.
 *
 * Limitations: maximum size of memory we can allocate using this routine
 *	is 4k, the size of a page in Linux.
 *
 * The general game plan is that each page (called a bucket) will only hold
 * objects of a given size.  When all of the object on a page are released,
 * the page can be returned to the general free pool.  When malloc() is
 * called, it looks for the smallest bucket size which will fulfill its
 * request, and allocate a piece of memory from that bucket pool.
 *
 * Each bucket has as its control block a bucket descriptor which keeps 
 * track of how many objects are in use on that page, and the free list
 * for that page.  Like the buckets themselves, bucket descriptors are
 * stored on pages requested from get_free_page().  However, unlike buckets,
 * pages devoted to bucket descriptor pages are never released back to the
 * system.  Fortunately, a system should probably only need 1 or 2 bucket
 * descriptor pages, since a page can hold 256 bucket descriptors (which
 * corresponds to 1 megabyte worth of bucket pages.)  If the kernel is using 
 * that much allocated memory, it's probably doing something wrong.  :-)
 *
 * Note: malloc() and free() both call get_free_page() and free_page()
 *	in sections of code where interrupts are turned off, to allow
 *	malloc() and free() to be safely called from an interrupt routine.
 *	(We will probably need this functionality when networking code,
 *	particularily things like NFS, is added to Linux.)  However, this
 *	presumes that get_free_page() and free_page() are interrupt-level
 *	safe, which they may not be once paging is added.  If this is the
 *	case, we will need to modify malloc() to keep a few unused pages
 *	"pre-allocated" so that it can safely draw upon those pages if
 * 	it is called from an interrupt routine.
 *
 * 	Another concern is that get_free_page() should not sleep; if it 
 *	does, the code is carefully ordered so as to avoid any race 
 *	conditions.  The catch is that if malloc() is called re-entrantly, 
 *	there is a chance that unecessary pages will be grabbed from the 
 *	system.  Except for the pages for the bucket descriptor page, the 
 *	extra pages will eventually get released back to the system, though,
 *	so it isn't all that bad.
 */

#include <linux/kernel.h>
#include <linux/mm.h>
#include <asm/system.h>

/**
 * 桶描述结构
*/
struct bucket_desc {	/* 16 bytes */
	void			    *page;       // 桶描述符对应的内存页面指针 
	struct bucket_desc	*next;       // 下一个描述符指针
	void			    *freeptr;    // 指向本桶中空闲内存位置指针
	unsigned short		refcnt;      // 引用计数
	unsigned short		bucket_size; // 本描述符对应桶的大小
};

/**
 * 桶描述符目录结构
*/
struct _bucket_dir {	/* 8 bytes */
	int			        size;     // 存储桶的大小（字节数）     
	struct bucket_desc	*chain;   // 该存储桶目录项的桶描述符链表指针
};

/*
 * The following is the where we store a pointer to the first bucket
 * descriptor for a given size.  
 *
 * If it turns out that the Linux kernel allocates a lot of objects of a
 * specific size, then we may want to add that specific size to this list,
 * since that will allow the memory to be allocated more efficiently.
 * However, since an entire page must be dedicated to each specific size
 * on this list, some amount of temperance must be exercised here.
 *
 * Note that this list *must* be kept in order.
 */
/**
 * 存储桶目录列表
*/
struct _bucket_dir bucket_dir[] = {
	{ 16,	(struct bucket_desc *) 0}, // 16B 长度内存块
	{ 32,	(struct bucket_desc *) 0}, // 32B 长度内存块
	{ 64,	(struct bucket_desc *) 0}, // 64B 长度内存块
	{ 128,	(struct bucket_desc *) 0}, // 128B 长度内存块
	{ 256,	(struct bucket_desc *) 0}, // 256B 长度内存块
	{ 512,	(struct bucket_desc *) 0}, // 512B 长度内存块
	{ 1024,	(struct bucket_desc *) 0}, // 1024B 长度内存块
	{ 2048, (struct bucket_desc *) 0}, // 2048B 长度内存块
	{ 4096, (struct bucket_desc *) 0}, // 4096B 长度内存块（一页内存）
	{ 0,    (struct bucket_desc *) 0}};   /* End of list marker */

/*
 * This contains a linked list of free bucket descriptor blocks
 */
struct bucket_desc *free_bucket_desc = (struct bucket_desc *) 0; // 空闲桶描述符内存块链表

/**
 * 初始化桶描述符
 * 建立空闲描述符列表，并让 free_bucket_desc 指向第一个空闲桶描述符
*/ 
static inline void init_bucket_desc()
{
	struct bucket_desc *bdesc, *first;
	int	i;
	
	// first 与 bdesc 指向新申请的空闲页面
	first = bdesc = (struct bucket_desc *) get_free_page();
	if (!bdesc)
		panic("Out of memory in init_bucket_desc()"); // 申请失败死机
	// 计算一页中能存放桶描述符个数，然后将其使用链表链接
	for (i = PAGE_SIZE/sizeof(struct bucket_desc); i > 1; i--) {
		bdesc->next = bdesc+1;
		bdesc++;
	}
	/*
	 * This is done last, to avoid race conditions in case 
	 * get_free_page() sleeps and this routine gets called again....
	 */
	// 将空闲桶描述符最后一项的下一项置空
	bdesc->next = free_bucket_desc;
	// free_bucket_desc 指向第一项
	free_bucket_desc = first;
}

/**
 * 动态分配内存函数
 * @param len 需要的内存块长度
 * @return 指向被分配内存指针，如果失败返回 NULL
*/
void *malloc(unsigned int len)
{
	struct _bucket_dir	*bdir;
	struct bucket_desc	*bdesc;
	void			*retval;

	// 查询与需要内存块匹配的桶描述符目录项
	for (bdir = bucket_dir; bdir->size; bdir++)
		if (bdir->size >= len)
			break;
	if (!bdir->size) {
		printk("malloc called with impossibly large argument (%d)\n",
			len);
		panic("malloc: bad arg");
	}
	/*
	 * Now we search for a bucket descriptor which has free space
	 */
	cli();	//禁止中断
	// 查找对应桶描述符中空闲 桶描述符链表
	for (bdesc = bdir->chain; bdesc; bdesc = bdesc->next) 
		if (bdesc->freeptr)
			break;
	// 如果没有空闲的桶描述符，需要先申请建立
	if (!bdesc) {
		char		*cp;
		int		i;

		// 空闲了列表指针为空，先对桶描述符进行初始化
		if (!free_bucket_desc)	
			init_bucket_desc();
		// 获取 free_bucket_desc 指向的第一个空闲桶描述符，并让 free_bucket_desc 指向下一个空闲桶描述符
		bdesc = free_bucket_desc;
		free_bucket_desc = bdesc->next;
		// 初始化桶描述符引用计数为 0
		bdesc->refcnt = 0;
		// 初始化桶大小为桶目录大小
		bdesc->bucket_size = bdir->size;
		// 为空闲桶描述符申请新的一页空间
		bdesc->page = bdesc->freeptr = (void *) cp = get_free_page();
		if (!cp)
			panic("Out of memory in kernel malloc()");
		// 以该桶的目录项指定的桶大小为对象长度，对该页内存进行划分，并将每个对象的开始 4 字节设置成指向下一对象的指针
		for (i=PAGE_SIZE/bdir->size; i > 1; i--) {
			*((char **) cp) = cp + bdir->size;
			cp += bdir->size;
		}
		// 清空最后一个对象开始处指针
		*((char **) cp) = 0;
		bdesc->next = bdir->chain; // 桶描述符下一项指向对应桶目录项指针所指向的描述符
		bdir->chain = bdesc; // 桶目录的 chain 则指向该桶描述符，即将该描述符插入到描述符链表头部
	}
	// 返回所申请的空闲页面
	retval = (void *) bdesc->freeptr;
	// bdesc->freeptr  指向下一个空闲空间
	bdesc->freeptr = *((void **) retval);
	// 引用计数 +1
	bdesc->refcnt++;
	sti();	// 重新开启中断
	return(retval);
}

/**
 * 释放存储桶对象
 * @param obj 释放的对象指针
 * @param size 对象大小
*/
void free_s(void *obj, int size)
{
	void		*page;
	struct _bucket_dir	*bdir;
	struct bucket_desc	*bdesc, *prev;

	// 获取指针所在页
	page = (void *)  ((unsigned long) obj & 0xfffff000);
	// 查找指向该页的桶描述符
	for (bdir = bucket_dir; bdir->size; bdir++) {
		prev = 0;
		/* If size is zero then this conditional is always false */
		if (bdir->size < size)
			continue;
		for (bdesc = bdir->chain; bdesc; bdesc = bdesc->next) {
			if (bdesc->page == page) 
				goto found;
			prev = bdesc;
		}
	}
	panic("Bad address passed to kernel free_s()");
found:
	cli(); // 关闭中断
	*((void **)obj) = bdesc->freeptr; // 将对象开始位置设置为指向空闲列表的指针
	bdesc->freeptr = obj; // 桶描述符的空闲空间指针指向 obj
	bdesc->refcnt--; // 引用计数 -1
	// 桶描述符引用计数为 0 
	if (bdesc->refcnt == 0) {
		// 如果 prev 已经不是搜索到的描述符的前一个描述符，则重新搜索当前描述符的前一描述符
		if ((prev && (prev->next != bdesc)) ||
		    (!prev && (bdir->chain != bdesc)))
			for (prev = bdir->chain; prev; prev = prev->next)
				if (prev->next == bdesc)
					break;
		// 找到前一描述符后，将当前描述符从列表中清除
		if (prev)
			prev->next = bdesc->next;
		else {
			// 否则当前描述符应该为 bdesc，否则则表示链表有问题显示错误信息，死机
			if (bdir->chain != bdesc)
				panic("malloc bucket chains corrupted");
			// 说明 bdesc 为描述符链表中的头节点，直接使描述符目录指向 bdesc 的下一节点
			bdir->chain = bdesc->next;
		}
		// 清空桶描述符所在页面
		free_page((unsigned long) bdesc->page);
		// 将桶描述符插入空闲描述符链表的头部
		bdesc->next = free_bucket_desc;
		free_bucket_desc = bdesc;
	}
	sti(); // 重新开启中断 返回
	return;
}

