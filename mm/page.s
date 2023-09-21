/*
 *  linux/mm/page.s
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 * page.s contains the low-level page-exception code.
 * the real work is done in mm.c
 */

.globl _page_fault

// 缺页异常调用函数 page_fault() 函数入口
_page_fault:
	xchgl %eax,(%esp) # 取出错码到 eax
	pushl %ecx
	pushl %edx
	push %ds
	push %es
	push %fs
	movl $0x10,%edx   # 置内核数据段选择符
	mov %dx,%ds
	mov %dx,%es
	mov %dx,%fs
	movl %cr2,%edx    # 取引起页面异常的线性地址
	pushl %edx        # 线性地址压栈
	pushl %eax        # 出错码压栈
	testl $1,%eax     # 测试出错码值是否为 1 
	jne 1f            # 为 1 时是缺页异常
	call _do_no_page  # 调用缺页处理函数
	jmp 2f
1:	call _do_wp_page  # 调用写保护处理函数
2:	addl $8,%esp      # 丢弃线性地址与出错码参数
	pop %fs
	pop %es
	pop %ds
	popl %edx
	popl %ecx
	popl %eax
	iret
