/*
 *  linux/kernel/asm.s
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 * asm.s contains the low-level code for most hardware faults.
 * page_exception is handled by the mm, so that isn't here. This
 * file also handles (hopefully) fpu-exceptions due to TS-bit, as
 * the fpu must be properly saved/resored. This hasn't been tested.
 */

.globl _divide_error,_debug,_nmi,_int3,_overflow,_bounds,_invalid_op
.globl _double_fault,_coprocessor_segment_overrun
.globl _invalid_TSS,_segment_not_present,_stack_segment
.globl _general_protection,_coprocessor_error,_irq13,_reserved

# 被 0 除出错（divide_error）中断处理代码入口
_divide_error:
	pushl $_do_divide_error # 首先将要调用的函数地址入栈，程序出错号为 0
no_error_code:
	xchgl %eax,(%esp) # 提取 do_divide_error 函数地址复制到 eax 中
	pushl %ebx
	pushl %ecx
	pushl %edx
	pushl %edi
	pushl %esi
	pushl %ebp
	push %ds
	push %es
	push %fs
	pushl $0		   # 将出错码 0 压栈
	lea 44(%esp),%edx  # 取原调用返回地址处堆栈指针位置，并压入堆栈
	pushl %edx
	movl $0x10,%edx    # 设置内核代码段选择符
	mov %dx,%ds
	mov %dx,%es
	mov %dx,%fs
	call *%eax         # 间接调用
	addl $8,%esp       # 让堆栈指针重新指向寄存器 fs 入栈处
	pop %fs
	pop %es
	pop %ds
	popl %ebp
	popl %esi
	popl %edi
	popl %edx
	popl %ecx
	popl %ebx
	popl %eax
	iret

# int 1 --debug测试中断入口点
_debug:
	pushl $_do_int3		# do_debug c函数指针入栈
	jmp no_error_code

# int 2 --非屏蔽中断调用入口点
_nmi:
	pushl $_do_nmi	    # do_nmi c函数指针入栈
	jmp no_error_code

# int 3 --断点指令引起的中断调用入口点
_int3:
	pushl $_do_int3
	jmp no_error_code

# int 4 --溢出出错处理中断调用入口点
_overflow:
	pushl $_do_overflow
	jmp no_error_code

# int 5 --边界检查（有效地址之外）出错中断调用入口点
_bounds:
	pushl $_do_bounds
	jmp no_error_code

# int 6 --无效操作指令出错中断调用入口点
_invalid_op:
	pushl $_do_invalid_op
	jmp no_error_code

# int 9 --协处理器段超出出错中断调用入口点
_coprocessor_segment_overrun:
	pushl $_do_coprocessor_segment_overrun
	jmp no_error_code

# int 15 --保留
_reserved:
	pushl $_do_reserved
	jmp no_error_code

# 下面用于当协处理器执行完一个操作时就会发出 IRQ13 中断信号，已通知 CPU 操作完成
_irq13:
	pushl %eax
	xorb %al,%al    # 80387 在执行计算时，CPU 回等待其操作的完成
	outb %al,$0xF0  # 通过写 0xF0 端口，本中断消除 CPU 的 BUSY 延续信号，并重新激活 387 的处理器扩展请求引脚 PEREQ，该操作主要是为了确保在继续执行 387 的任何指令之前，响应此中断
	movb $0x20,%al
	outb %al,$0x20  # 向 8259 主中断控制芯片发送 BOI（中断结束信号）
	jmp 1f
1:	jmp 1f
1:	outb %al,$0xA0  # 再向 8259 从中断控制芯片发送 BOI（中断结束信号）
	popl %eax
	jmp _coprocessor_error

# 以下中断在调用时会在中断返回地址之后将出错号压入堆栈，因此返回时也需要将出错号弹出
# int 8  --双出错故障调用入口点
_double_fault:
	pushl $_do_double_fault # c 函数地址入栈
error_code:
	xchgl %eax,4(%esp)		# 将出错码提取到 eax 中
	xchgl %ebx,(%esp)		# 将 c 函数地址提取到 eax 中
	pushl %ecx
	pushl %edx
	pushl %edi
	pushl %esi
	pushl %ebp
	push %ds
	push %es
	push %fs
	pushl %eax			     # 出错码入栈 
	lea 44(%esp),%eax		 # 程序返回地址处堆栈指针位置值入栈
	pushl %eax
	movl $0x10,%eax
	mov %ax,%ds
	mov %ax,%es
	mov %ax,%fs
	call *%ebx               # 调用中断处理程序 c 函数
	addl $8,%esp
	pop %fs
	pop %es
	pop %ds
	popl %ebp
	popl %esi
	popl %edi
	popl %edx
	popl %ecx
	popl %ebx
	popl %eax
	iret

# int 10 --无效的任务状态段（TSS）
_invalid_TSS:
	pushl $_do_invalid_TSS
	jmp error_code

# int 11 --段不存在
_segment_not_present:
	pushl $_do_segment_not_present
	jmp error_code

# int 12 --堆栈段错误
_stack_segment:
	pushl $_do_stack_segment
	jmp error_code

# int 13 -- 一般保护性出错
_general_protection:
	pushl $_do_general_protection
	jmp error_code

