/*
 *  linux/kernel/system_call.s
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 *  system_call.s  contains the system-call low-level handling routines.
 * This also contains the timer-interrupt handler, as some of the code is
 * the same. The hd- and flopppy-interrupts are also here.
 *
 * NOTE: This code handles signal-recognition, which happens every time
 * after a timer-interrupt and after each system call. Ordinary interrupts
 * don't handle signal-recognition, as that would clutter them up totally
 * unnecessarily.
 *
 * Stack layout in 'ret_from_system_call':
 *
 *	 0(%esp) - %eax
 *	 4(%esp) - %ebx
 *	 8(%esp) - %ecx
 *	 C(%esp) - %edx
 *	10(%esp) - %fs
 *	14(%esp) - %es
 *	18(%esp) - %ds
 *	1C(%esp) - %eip
 *	20(%esp) - %cs
 *	24(%esp) - %eflags
 *	28(%esp) - %oldesp
 *	2C(%esp) - %oldss
 */

SIG_CHLD	= 17 # 定义 SIG_CHLD 信号（子进程停止或结束）

EAX		= 0x00   # 各个寄存器再堆栈上的存储位置
EBX		= 0x04
ECX		= 0x08
EDX		= 0x0C
FS		= 0x10
ES		= 0x14
DS		= 0x18
EIP		= 0x1C
CS		= 0x20
EFLAGS		= 0x24
OLDESP		= 0x28
OLDSS		= 0x2C

# 任务机构（task_struct）中变量的偏移值
state	= 0		    # 任务状态
counter	= 4			# 剩余时间片			
priority = 8		# 优先权
signal	= 12		# 型号位图
sigaction = 16		# sigaction 结构长度必须是 16 字节
blocked = (33*16)   # 受阻塞信号位图的偏移量

# sigaction 结构中的偏移量 
sa_handler = 0		# 信号处理过程的句柄
sa_mask = 4   		# 信号量屏蔽码
sa_flags = 8		# 信号集
sa_restorer = 12	# 恢复函数指针

nr_system_calls = 72 # 系统调用总数

/*
 * Ok, I get parallel printer interrupts while using the floppy for some
 * strange reason. Urgel. Now I just ignore them.
 */
.globl _system_call,_sys_fork,_timer_interrupt,_sys_execve
.globl _hd_interrupt,_floppy_interrupt,_parallel_interrupt
.globl _device_not_available, _coprocessor_error

.align 2
bad_sys_call:			# 错误的系统调用号从这里返回
	movl $-1,%eax		# eax 置为 -1，退出中断
	iret
.align 2
/*重新执行调度程序入口*/
reschedule:
	pushl $ret_from_sys_call # 将 ret_from_sys_call 函数指针入栈
	jmp _schedule # 跳转到 schedule 函数处执行
.align 2
# system_call 中断调用函数入口
_system_call:
	cmpl $nr_system_calls-1,%eax 	# 需要调用的函数未在表中时，跳转到 bad_sys_call
	ja bad_sys_call
	push %ds
	push %es
	push %fs
	pushl %edx
	pushl %ecx		# push %ebx,%ecx,%edx as parameters
	pushl %ebx		# to the system call
	movl $0x10,%edx		# set up ds,es to kernel space
	mov %dx,%ds
	mov %dx,%es
	movl $0x17,%edx		# fs points to local data space
	mov %dx,%fs
	call _sys_call_table(,%eax,4)	# 调用 sys_call_table 对应函数 （AT&T寻址格式 immed32(basepointer, indexpointer, indexscale) = immed32 + basepointer + indexpointer * indexscale）
	pushl %eax						# 系统调用返回值压栈
	movl _current,%eax				# 将当前进程参数复制到 eax 中
	cmpl $0,state(%eax)				# 如果当前进程状态不是就绪态则重新调度进程
	jne reschedule
	cmpl $0,counter(%eax)			# 当前进程可使用时间片为 0 时，也对程序重新调度
	je reschedule
/*ret_from_sys_call 程序在从系统调用 c 函数后，对信号量进行识别处理*/
ret_from_sys_call:
	movl _current,%eax				# 进程 0 时，不需要对信号进行处理
	cmpl _task,%eax					
	je 3f
	# 对原调用程序代码选择符进行检查来判断源程序是否为内核中的进程
	# 不是才需要处理信号量（0x0f为普通用户代码段的选择符） 
	cmpw $0x0f,CS(%esp)		# was old code segment supervisor ?
	jne 3f
	# 原堆栈段选择符不为 0x17 （不在用户数据段中）也退出
	cmpw $0x17,OLDSS(%esp)		# was stack segment = 0x17 ?
	jne 3f
	# 下面 3 之前的代对当前进程信号量进行处理
	movl signal(%eax),%ebx 			# ebx -> 信号量
	movl blocked(%eax),%ecx 		# ecx -> 屏蔽量
	notl %ecx						# 逆转 ecx 中的值
	andl %ebx,%ecx 					# 叠加获取当信号中未被屏蔽的
	bsfl %ecx,%ecx 					# 从低位扫描位图，查看是否有 1 的位
	je 3f 							# 没有退出
	btrl %ecx,%ebx 					# 复位该位
	movl %ebx,signal(%eax) 			# 重新保存位图信息到本进程信号位图
	incl %ecx 						# 将 信号调整为从1 开始的数
	pushl %ecx 						# 将信号量参数 signr 入栈
	call _do_signal 				# 调用 do_signal 函数
	popl %eax
3:	popl %eax
	popl %ebx
	popl %ecx
	popl %edx
	pop %fs
	pop %es
	pop %ds
	iret

.align 2
# int 16 协处理器错误信号中断函数入口
_coprocessor_error:
	push %ds
	push %es
	push %fs
	pushl %edx
	pushl %ecx
	pushl %ebx
	pushl %eax
	movl $0x10,%eax
	mov %ax,%ds
	mov %ax,%es
	movl $0x17,%eax
	mov %ax,%fs
	pushl $ret_from_sys_call
	jmp _math_error

.align 2
# int 7 设备不存在后者协处理器不存在中断调用函数入口
_device_not_available:
	push %ds
	push %es
	push %fs
	pushl %edx
	pushl %ecx
	pushl %ebx
	pushl %eax
	movl $0x10,%eax
	mov %ax,%ds
	mov %ax,%es
	movl $0x17,%eax
	mov %ax,%fs
	pushl $ret_from_sys_call
	clts				# clear TS so that we can use math
	movl %cr0,%eax
	testl $0x4,%eax			# EM (math emulation bit)
	je _math_state_restore
	pushl %ebp
	pushl %esi
	pushl %edi
	call _math_emulate
	popl %edi
	popl %esi
	popl %ebp
	ret

.align 2
# int32 时钟中断处理程序入口，中断频率设置为 100HZ
_timer_interrupt:
	push %ds		# save ds,es and put kernel data space
	push %es		# into them. %fs is used by _system_call
	push %fs
	pushl %edx		# we save %eax,%ecx,%edx as gcc doesn't
	pushl %ecx		# save those across function calls. %ebx
	pushl %ebx		# is saved as we use that in ret_sys_call
	pushl %eax
	movl $0x10,%eax
	mov %ax,%ds
	mov %ax,%es
	movl $0x17,%eax
	mov %ax,%fs
	incl _jiffies		# jiffes 嘀嗒数 +1
	movb $0x20,%al		# 发送指令结束硬件中断
	outb %al,$0x20
	# 下面 3 句从选择符中获取当前特权等级（0-内核，3-用户态），然后压入堆栈做为 do_timer 函数的参数
	movl CS(%esp),%eax
	andl $3,%eax		# %eax is CPL (0 or 3, 0=supervisor)
	pushl %eax
	call _do_timer		# 调用 do_timer 函数（操作计时器，切换进程等）
	addl $4,%esp		# 丢弃压入堆栈中的 do_timer 函数的参数
	jmp ret_from_sys_call

.align 2
# sys_execve 系统调用函数入口
_sys_execve:
	lea EIP(%esp),%eax	# 获取中断调用程序的代码指针作为参数压栈
	pushl %eax
	call _do_execve 	# 调用 do_execve 函数
	addl $4,%esp 		# 丢弃刚压栈参数
	ret

.align 2
# _sys_fork 系统调用函数入口
_sys_fork:
	call _find_empty_process 	# 先找到个空闲进程项 
	testl %eax,%eax				# 未找到直接退出
	js 1f
	push %gs
	pushl %esi
	pushl %edi
	pushl %ebp
	pushl %eax
	call _copy_process			# 调用复制进程函数
	addl $20,%esp 				# 丢弃所有 copy_process 函数的参数
1:	ret

/*硬盘中断调用程序入口*/
_hd_interrupt:
	pushl %eax
	pushl %ecx
	pushl %edx
	push %ds
	push %es
	push %fs
	movl $0x10,%eax 	# 将 ds es 置为 内核数据段与代码段
	mov %ax,%ds
	mov %ax,%es
	movl $0x17,%eax 	# fs 置为调用程序的局部数据段
	mov %ax,%fs
	movb $0x20,%al
	outb %al,$0xA0		# 结束硬件中断
	jmp 1f			# give port chance to breathe # 等待命令执行
1:	jmp 1f
1:	xorl %edx,%edx 
	xchgl _do_hd,%edx 	# 获取当前的 do_hd 指向的函数（read_intr() 或者 write_intr()）赋值给 edx
	testl %edx,%edx 	# 测试函数指针是否为空
	jne 1f
	movl $_unexpected_hd_interrupt,%edx # 若空则指向 unexpected_hd_interrupt 函数
1:	outb %al,$0x20		# 结束硬件中断
	call *%edx			# 执行读 或者 写操作
	pop %fs
	pop %es
	pop %ds
	popl %edx
	popl %ecx
	popl %eax
	iret

/*软盘中断函数调用程序入口*/
_floppy_interrupt:
	pushl %eax
	pushl %ecx
	pushl %edx
	push %ds
	push %es
	push %fs
	movl $0x10,%eax
	mov %ax,%ds
	mov %ax,%es
	movl $0x17,%eax
	mov %ax,%fs
	movb $0x20,%al
	outb %al,$0x20			# 结束硬件中断
	xorl %eax,%eax			# eax 值清0
	xchgl _do_floppy,%eax	# 将 do_floppy 函数指针复制到 eax 之中
	testl %eax,%eax
	jne 1f 					# do_floppy 不为空时执行
	movl $_unexpected_floppy_interrupt,%eax # 否则执行 unexpected_floppy_interrupt 函数
1:	call *%eax				# 调用 do_floppy 函数
	pop %fs
	pop %es
	pop %ds
	popl %edx
	popl %ecx
	popl %eax
	iret

# int 39 并行口中断处理程序，对应硬件中断请求信号 IRQ7（未实现，直接发送结束中断命令）
_parallel_interrupt:
	pushl %eax
	movb $0x20,%al
	outb %al,$0x20
	popl %eax
	iret