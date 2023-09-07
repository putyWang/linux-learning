/*
 *  linux/boot/head.s
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 *  head.s contains the 32-bit startup code.
 *
 * NOTE!!! Startup happens at absolute address 0x00000000, which is also where
 * the page directory will exist. The startup code will be overwritten by
 * the page directory.
 */

/*
 * head.s 包含 32 位启动代码，
 *
 * 启动代码是从 0x00000000 开始，在运行中是保存页目录的地方，因此此处代码将会被 页表覆盖
 */

.text
.globl _idt,_gdt,_pg_dir,_tmp_floppy_area 
_pg_dir: # 页目录将会保存在此处

startup_32:
# 下面 5 行代码设置 各个数据段寄存器
	movl $0x10,%eax # GNU 汇编每个直接数需要使用 $ 前缀进行标识，否则表示地址 
	# 每个寄存器名都要以 % 前缀进行标识，eax 则是表示 32 位的 ax 寄存器
	# 由于此处代码已处于 32 位运行模式，因此 $0x10 并不是把绝对地址装入各个段寄存器，它存储的其实是指向全局段描述符表中的偏移值；
	# 此处 0x10 表示 请求特权级0 （位 0～1）、选择全局描述符表 0（位2 = 0）、以及表中第二项（位3～15 = 2）；
	# 代码实际含义为：置 ds, es, fs, gs 中的选择符为 setup.s 中构造的数据段 = 0x10, 并将堆栈描述符放置在 _stack_start 所指向的 user_stack 数组内，然后使用心得中断描述符与全局段描述符
	mov %ax,%ds # 
	mov %ax,%es
	mov %ax,%fs
	mov %ax,%gs
	lss _stack_start,%esp # 表示 _stack_start -> ss::esp, 设置系统堆栈
	call setup_idt # 调用 setup_idt 函数 初始化并加载中断描述符表，每项指向空门
	call setup_gdt # 调用 setup_gdt 函数 加载全局描述符表
	# 由于 gdt 已经被重新设置，因此需要重新装载所有段寄存器
	movl $0x10,%eax		# reload all the segment registers
	mov %ax,%ds		# after changing gdt. CS was already
	mov %ax,%es		# reloaded in 'setup_gdt'
	mov %ax,%fs
	mov %ax,%gs
	lss _stack_start,%esp
	# 下面 5 行代码用于测试 A20 地址线是否已经开启；
	# 先往内存地址 0x000000 处写入一个数值，然后查看 0x100000 （1Mb）处是否也是这个数值；
	# 相同的话，就说明地址线还未选通（无法使用 1Mb 以上的内存），然后一直尝试，导致死机
	xorl %eax,%eax
1:	incl %eax		# check that A20 really IS enabled
	movl %eax,0x000000	# loop forever if it isn't
	cmpl %eax,0x100000
	je 1b # 1b 表示向后跳转到标记1去 5f 则是表示向前跳转到标记5处
/*
 * NOTE! 486 should set bit 16, to check for write-protect in supervisor
 * mode. Then it would be unnecessary with the "verify_area()"-calls.
 * 486 users probably want to set the NE (#5) bit also, so as to use
 * int 16 for math errors.
 */
 /*
  * 下面一段代码用于检查数学处理芯片是否存在；
  * 方法为：修改控制寄存器 CR0，在假设存在协处理器的情况下执行一个协处理器指令，出错表明不存在协处理芯片，
  * 需设置 CR0 中的协处理仿真位 EM（位2），并复位协处理器存在标志 MP 位1
  */
	movl %cr0,%eax		# check math chip
	andl $0x80000011,%eax	# Save PG,PE,ET
/* "orl $0x10020,%eax" here for 486 might be good */
	orl $2,%eax		# set MP
	movl %eax,%cr0
	call check_x87
	jmp after_page_tables # 跳转到 after_page_tables 标志处

/*
 * We depend on ET to be correct. This checks for 287/387.
 */
 # 该子函数依赖与 ET 标志的正确性来检测 287/387 是否存在
check_x87:
	fninit
	fstsw %ax
	cmpb $0,%al
	je 1f			/* no coprocessor: have to set bits */ # 存在则向前跳转转到标号 1 处，否则修改 CR0
	movl %cr0,%eax
	xorl $6,%eax		/* reset MP, set EM */
	movl %eax,%cr0
	ret
.align 2 # 该行代码表示存储边界对齐调整，2 表示调整到地址最后两位为0，即按照 4 字节的方式对齐内存地址，以提高cpu 寻址效率
1:	.byte 0xDB,0xE4		/* fsetpm for 287, ignored by 387 */
	ret

/*
 *  setup_idt
 *
 *  sets up a idt with 256 entries pointing to
 *  ignore_int, interrupt gates. It then loads
 *  idt. Everything that wants to install itself
 *  in the idt-table may do so themselves. Interrupts
 *  are enabled elsewhere, when we can be relatively
 *  sure everything is ok. This routine will be over-
 *  written by the page tables.
 */
 # 下面这段代码为 setup_idt 子程序 主要功能是重新设置 中断描述符表
 # 首先将 中断描述符表 idt 设置为 256 个项，将其全部指向 ignore_int 中断门
 # 然后使用 lidt 指令加载中断描述符表寄存器。真正使用的中断门之后再安装，在其他地方一切正常时再开启中断
 # 中断描述符表中的项 0～1、6～7字节是偏移量，2～3B 为选择符，4～5B 为一些标识位
setup_idt:
	lea ignore_int,%edx # 将 edx寄存器 指向 ignore_int 的有效地址
	movl $0x00080000,%eax # 将 选择符 0x0008 置入 eax 高 16 位中
	movw %dx,%ax		/* selector = 0x0008 = cs */ # 将 ignore_int 的有效地址低 16 位置于 ax 的低 16 位中， 此时 eax 含有门低 16 位地址
	movw $0x8E00,%dx	/* interrupt gate - dpl=0, present */ # 将 0x8E00 存入 dx 的低16位中 此时 edx 含有门 高 16 位地址

	lea _idt,%edi # 将 edi 寄存器指向 中断描述符表地址 （_idt）
	mov $256,%ecx # 设置表项数量进行计数
rp_sidt:
	movl %eax,(%edi) # 将 _idt 地址低 16 位存储 ignore_int 的有效地址低16位
	movl %edx,4(%edi) # 将 _idt 地址高 16 位存储 ignore_int 的有效地址高16位
	addl $8,%edi # edi 每次偏移 8 字节，指向下一项
	dec %ecx
	jne rp_sidt
	lidt idt_descr # 加载中断描述符表寄存器值
	ret

/*
 *  setup_gdt
 *
 *  This routines sets up a new gdt and loads it.
 *  Only two entries are currently built, the same
 *  ones that were built in init.s. The routine
 *  is VERY complicated at two whole lines, so this
 *  rather long comment is certainly needed :-).
 *  This routine will beoverwritten by the page tables.
 */
 /*
 * setup_gdt 子函数设置一个全新的描述符表，并加载；
 * 和 setup.s 中一致，只创建了两个表项
 */
setup_gdt:
	lgdt gdt_descr # 加载全局描述符表项（具体内容见程序最后 _gdt 的声明）
	ret

/*
 * I put the kernel page tables right after the page directory,
 * using 4 of them to span 16 Mb of physical memory. People with
 * more than 16MB will have to expand this.
 */
/ *
  * Linux 将内核页表直接放在页目录之后，使用 4 个 表来寻址 16 Mb 物理内存；
  * 若使用多于 16MB 的内存时需要对此处进行扩充修改
  */
  # 每隔页表长为 4 个字节，而每个表项需要4个字，因此一个页面可以存放 1024 个表项；
  # 如果一个表项寻址 4kb 的地址空间，则一个页表就可以寻址 4MB 的物理内存；
  # 页表项的格式为：项前 0～11 位存放一些标志，如是否在内存中（位0）、读写许可（位1）、普通用户还是超级用户使用（位2）、是否修改过（位6）等，表项的位 12～31 是页框地址，用于指出一页内存的起始地址
.org 0x1000 # 从偏移 0x1000 处是第一个页表（偏移 0 开始处存放页表目录）
pg0:

.org 0x2000
pg1:

.org 0x3000
pg2:

.org 0x4000
pg3:

.org 0x5000 # 定义下面的内存块从 偏移 0x5000开始
/*
 * tmp_floppy_area is used by the floppy-driver when DMA cannot
 * reach to a buffer-block. It needs to be aligned, so that it isn't
 * on a 64kB border.
 */
/*
 * 当 DMA 不能访问缓冲块时，下面 tmp_floppy_area 内存块就可供软盘驱动程序使用；
 * 其地址需要对齐，就不会跨越64kb边界
 */
_tmp_floppy_area:
	.fill 1024,1,0 # 预留 1024 个 使用 0 填充的 1 kb 项

/*
 * 下面几个入栈操作为调用 /init/main.C 程序和返回做准备；
 * 第四个入栈操作是模拟调用 main 程序时首先将返回地址压入栈中的操作，因此如果 main 程序真的退出时，就会返回到这里的标号 L6 处继续执行下去。即死循环；
 * 第五个入栈操作则是将 main 的地址压入栈中，这样在设置分页处理程序结束后执行 ret 返回指令时就会将 main 程序的地址弹出堆栈并执行；
 */
after_page_tables:
	pushl $0		# These are the parameters to main :-)
	pushl $0        # 前三项是 main 函数的参数
	pushl $0
	pushl $L6		# return address for main, if it decides to. 
	pushl $_main    # _main 是对 main 函数的内部表示方法
	jmp setup_paging
L6:
	jmp L6			# main should never return here, but
				# just in case, we know what happens.

/* This is the default interrupt "handler" :-) */
int_msg:
	.asciz "Unknown interrupt\n\r" # 定义字符串 "未知中断（回车换行）"
.align 2 
# 打印 未知中断（回车换行）
ignore_int:
	pushl %eax
	pushl %ecx
	pushl %edx
	push %ds
	push %es
	push %fs
	movl $0x10,%eax # 将 ds es fs 指向 gdt 表中数据段
	mov %ax,%ds
	mov %ax,%es
	mov %ax,%fs
	pushl $int_msg
	call _printk
	popl %eax
	pop %fs
	pop %es
	pop %ds
	popl %edx
	popl %ecx
	popl %eax
	iret # 中断返回（把中断调用时压入栈的 CPU 标志寄存器（32位）值也弹出）


/*
 * Setup_paging
 *
 * This routine sets up paging by setting the page bit
 * in cr0. The page tables are set up, identity-mapping
 * the first 16MB. The pager assumes that no illegal
 * addresses are produced (ie >4Mb on a 4Mb machine).
 *
 * NOTE! Although all physical memory should be identity
 * mapped by this routine, only the kernel page functions
 * use the >1Mb addresses directly. All "normal" functions
 * use just the lower 1Mb, or the local data space, which
 * will be mapped to some other place - mm keeps track of
 * that.
 *
 * For those with more memory than 16 Mb - tough luck. I've
 * not got it, why should you :-) The source is here. Change
 * it. (Seriously - it shouldn't be too difficult. Mostly
 * change some constants etc. I left it at 16Mb, as my machine
 * even cannot be extended past that (ok, but it was cheap :-)
 * I've tried to show which constants to change by having
 * some kind of marker at them (search for "16Mb"), but I
 * won't guarantee that's all :-( )
 */

/*
 * 该子程序通过设置 CRO 标志（PG 位 31）来启用对内存的分页处理功能，并设置各个页表项的内容，以恒等映射前 16MB 的物理内存；
 * 分页器假定不会产生非法的地址映射，即在只有 4Mb 内存的机器上不会产生大于 4MB 的内存地址；
 * 尽管所有的物理地址都应由该子程序进行恒等映射，但只有内核页面管理函数能直接使用大于 1MB 的地址，其余函数仅使用低于 1MB 的地址空间，或局部数据空间，地址空间将被映射到其他一些地方去，内存管理程序（mm）来对内存映射进行管理；
 * 对于多于 16MB 内存的机器，可以通过修改扩充其页表设置
 */
.align 2
setup_paging: 
   	# 下面三行对 页表目录与页表项的内存qing0 
	movl $1024*5,%ecx		/* 5 pages - pg_dir+4 page tables */
	xorl %eax,%eax
	xorl %edi,%edi			/* pg_dir is at 0x000 */
	cld;rep;stosl
/*
 * 下面四行设置页目录中的项，共有4个页表所以只需设置4项；
 * 页目录项的结构与页表中项的结构一样，4个字节为1项，$pg0+7 表示：0x00001007,是页目录表中的第一项
 * 第一个页表所在的地址 = 0x00001007 & 0xfffff000 = 0x1000, 第一个页表的属性标志为 = 0x00001007 & 0x00000fff = 0x0007，表示该页存在、用户可读写
 */	
	movl $pg0+7,_pg_dir		/* set present bit/user r/w */
	movl $pg1+7,_pg_dir+4		/*  --------- " " --------- */
	movl $pg2+7,_pg_dir+8		/*  --------- " " --------- */
	movl $pg3+7,_pg_dir+12		/*  --------- " " --------- */
/*
 * 下面四行填写 4 个页表中的所有项内容，共有：4（页表）* 1024（项/页表）= 4096 项（0-0xffff），既能映射物理内存 4096 * 4KB = 16MB ；
 * 每项内容是：当前项所映射的物理内存地址 + 该页的标志位（这里均为7）；
 * 使用方法：从最后一个页表的最后一项开始按倒退顺序填写，一个页表的最后一项在页表中的位置为 1023 * 4 = 4092，因此最后一页的最后一项的位置就是为 $pg3+4092
 */	
	movl $pg3+4092,%edi
# 最后一项对应的物理内存页面地址为 $0xfff000，加上属性标志位 7，即 $0xfff007
	movl $0xfff007,%eax		/*  16Mb - 4096 + 7 (r/w user,p) */
	std # 方向位置位，edi 值递减 每次 4B
1:	stosl			/* fill pages backwards - more efficient :-) */
	subl $0x1000,%eax # 没填写一项，物理地址 - 0x1000
	jge 1b # 当小于0时 说明已经设置完成
# 下面两行设置页目录基址寄存器 CR3 的值，指向页目录表（0x0000）
	xorl %eax,%eax		/* pg_dir is at 0x0000 */
	movl %eax,%cr3		/* cr3 - page directory start */ 
# 下面两行设置 CR0 分页标志位，启用分页
	movl %cr0,%eax
	orl $0x80000000,%eax
	movl %eax,%cr0		/* set paging (PG) bit */
	ret			/* this also flushes prefetch-queue */ // 返回开始执行 main程序

.align 2
.word 0
# idt_descr 为 lidt 指令 6B 操作数（长度，基址）
idt_descr:
	.word 256*8-1		# idt contains 256 entries
	.long _idt
.align 2
.word 0
# gdt_descr 为 lgdt 指令 6B 操作数（长度，基址）
gdt_descr:
	.word 256*8-1		# so does gdt (not that that's any
	.long _gdt		# magic number, but it works for me :^)

	.align 3

# 中断描述符表，共256项 每项 8 kb 全部使用 0 进行填充
_idt:	.fill 256,8,0		# idt is uninitialized

/*
* 全局表，前四项分别是空项、代码段描述符、数据段描述符以及系统段描述符，
* 其中系统段描述符 Linux 中没有派用场；然后使用 .fill 预留了 252项空间用于放置所创建任务的局部描述符（LDT）和 对应的任务状态段 TSS 描述符
*/
_gdt:	.quad 0x0000000000000000	/* NULL descriptor */
	.quad 0x00c09a0000000fff	/* 16Mb */ # 代码段最大长度为 16 Mb
	.quad 0x00c0920000000fff	/* 16Mb */ # 数据段最大长度也为 16 Mb
	.quad 0x0000000000000000	/* TEMPORARY - don't use */
	.fill 252,8,0			/* space for LDT's and TSS's etc */
