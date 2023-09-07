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

SIG_CHLD	= 17

EAX		= 0x00
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

state	= 0		# these are offsets into the task-struct.
counter	= 4
priority = 8
signal	= 12
sigaction = 16		# MUST be 16 (=len of sigaction)
blocked = (33*16)

# offsets within sigaction
sa_handler = 0
sa_mask = 4
sa_flags = 8
sa_restorer = 12

nr_system_calls = 72

/*
 * Ok, I get parallel printer interrupts while using the floppy for some
 * strange reason. Urgel. Now I just ignore them.
 */
.globl _system_call,_sys_fork,_timer_interrupt,_sys_execve
.globl _hd_interrupt,_floppy_interrupt,_parallel_interrupt
.globl _device_not_available, _coprocessor_error

.align 2
bad_sys_call:
	movl $-1,%eax
	iret
.align 2
/*重新执行调度程序入口*/
reschedule:
	pushl $ret_from_sys_call # 将 ret_from_sys_call 函数入栈
	jmp _schedule # 跳转到 schedule 函数处执行
.align 2
/*system_call 中断调用函数入口*/
_system_call:
	# 需要调用的函数未在表中时，跳转到 bad_sys_call
	cmpl $nr_system_calls-1,%eax
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
	# 调用 sys_call_table 对应函数
	call _sys_call_table(,%eax,4)
	# 下面 4 行代码查看当前进程的运行状态
	pushl %eax
	movl _current,%eax
	cmpl $0,state(%eax)		# state
	jne reschedule # 不是就绪态则重新调度进程
	# 当前进程时间片为 0 时，也对程序重新调度
	cmpl $0,counter(%eax)		# counter
	je reschedule
/*ret_from_sys_call 程序在从系统调用 c 函数后，对信号量进行识别处理*/
ret_from_sys_call:
	# 当前为进程 0 时，不需要进行处理
	movl _current,%eax		# task[0] cannot have signals
	cmpl _task,%eax
	je 3f
	# 对原调用程序代码选择符进行检查来判断源程序是否为内核中的进程
	# 不是才需要处理信号量（0x0f为普通用户代码段的选择符） 
	cmpw $0x0f,CS(%esp)		# was old code segment supervisor ?
	jne 3f
	# 原堆栈段选择符不为 0x17 （不在用户数据段中）也退出
	cmpw $0x17,OLDSS(%esp)		# was stack segment = 0x17 ?
	jne 3f
	/*下面 3 之前的代对当前进程信号量进行处理*/
	movl signal(%eax),%ebx # ebx -> 信号量
	movl blocked(%eax),%ecx # ecx -> 屏蔽量
	notl %ecx
	andl %ebx,%ecx # 叠加获取当信号中未被屏蔽的
	bsfl %ecx,%ecx # 从低位扫描位图，ecx 保留置 1 的位的偏移量
	je 3f # 没有退出
	btrl %ecx,%ebx # 复位该位
	movl %ebx,signal(%eax) # 保存复位后的信号量
	incl %ecx # 将 信号调整为从1 开始的数
	pushl %ecx # 将参数 signr 入栈
	call _do_signal # 调用 do_signal 函数
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
/*定时器中断调用函数入口*/
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
	incl _jiffies
	# 发送指令结束硬件中断
	movb $0x20,%al		# EOI to interrupt controller #1
	outb %al,$0x20
	# 下面 3 句从选择符中获取当前特权等级，然后压入堆栈做为 do_timer 函数的参数
	movl CS(%esp),%eax
	andl $3,%eax		# %eax is CPL (0 or 3, 0=supervisor)
	pushl %eax
	# 调用 do_timer 函数（操作计时器，切换进程等）
	call _do_timer		# 'do_timer(long CPL)' does everything from
	addl $4,%esp		# task switching to accounting ...
	jmp ret_from_sys_call

/*sys_execve 系统调用函数入口*/
.align 2
_sys_execve:
	lea EIP(%esp),%eax
	pushl %eax
	call _do_execve // 调用 do_execve 函数
	addl $4,%esp
	ret

.align 2
# _sys_fork 系统调用函数入口
_sys_fork:
	call _find_empty_process # 先找到个空闲进程项 
	testl %eax,%eax
	js 1f
	push %gs
	pushl %esi
	pushl %edi
	pushl %ebp
	pushl %eax
	call _copy_process
	addl $20,%esp # 丢弃所有的压栈内容
1:	ret

/*硬盘中断调用程序入口*/
_hd_interrupt:
	pushl %eax
	pushl %ecx
	pushl %edx
	push %ds
	push %es
	push %fs
	movl $0x10,%eax // 将 ds es 置为 内核数据段与代码段
	mov %ax,%ds
	mov %ax,%es
	movl $0x17,%eax // fs 置为调用程序的局部数据段
	mov %ax,%fs
	movb $0x20,%al
	outb %al,$0xA0		# EOI to interrupt controller #1 # EOF 命令禁止中断
	jmp 1f			# give port chance to breathe # 等待命令执行
1:	jmp 1f
1:	xorl %edx,%edx 
	xchgl _do_hd,%edx # 获取当前的 do_hd 指向的函数（read_intr() 或者 write_intr()）赋值给 edx
	testl %edx,%edx # 测试函数指针是否为空
	jne 1f
	movl $_unexpected_hd_interrupt,%edx # 若空则指向 unexpected_hd_interrupt 函数
1:	outb %al,$0x20
	call *%edx		# "interesting" way of handling intr. // 执行读 或者 写操作
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
	outb %al,$0x20		# EOI to interrupt controller #1 #EOF 命令禁止中断
	xorl %eax,%eax
	xchgl _do_floppy,%eax
	testl %eax,%eax
	jne 1f // do_floppy 不为空时执行
	movl $_unexpected_floppy_interrupt,%eax # 否则执行 unexpected_floppy_interrupt 函数
1:	call *%eax		# "interesting" way of handling intr.
	pop %fs
	pop %es
	pop %ds
	popl %edx
	popl %ecx
	popl %eax
	iret

_parallel_interrupt:
	pushl %eax
	movb $0x20,%al
	outb %al,$0x20
	popl %eax
	iret