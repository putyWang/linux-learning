/*
 *  linux/kernel/rs_io.s
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 *	rs_io.s
 *
 * This module implements the rs232 io interrupts.
 */

.text
.globl _rs1_interrupt,_rs2_interrupt

size	= 1024
/* these are the offsets into the read/write buffer structures */
rs_addr = 0
head = 4
tail = 8
proc_list = 12
buf = 16

startup	= 256		# 当写队列中还剩 256 个字符空间时，就可以写

/*
 * These are the actual interrupt routines. They look where
 * the interrupt is coming from, and take appropriate action.
 */
.align 2
_rs1_interrupt: 			# 串行端口1 中断处理程序入口
	pushl $_table_list+8 	# 串行端口1 对应的读写缓冲指针入栈
	jmp rs_int
.align 2 
_rs2_interrupt: 			# 串行端口2 中断处理程序入口
	pushl $_table_list+16 	# 串行端口2 对应的读写缓冲指针入栈
rs_int:
	pushl %edx 
	pushl %ecx
	pushl %ebx
	pushl %eax
	push %es
	push %ds
	pushl $0x10
	pop %ds 				# 由于是中断程序， 因此不知道 ds 是否正确
	pushl $0x10 			# 所以重新加载他们（让 ds、es 指向内核数据段）
	pop %es
	movl 24(%esp),%edx 		# 将缓冲队列的指针 -> edx， 最先压入堆栈的地址
	movl (%edx),%edx 		# 取串口1读队列指针 -> edx
	movl rs_addr(%edx),%edx # 取串口1端口号 -> edx
	addl $2,%edx			# edx 指向中断标识符寄存器
rep_int:
	xorl %eax,%eax
	inb %dx,%al 			# 取中断标识符字节，用于判断中断来源（4种情况）
	testb $1,%al			# 首先判断有无待处理的中断（位 0=1 无中断；=0 有中断）
	jne end					# 若无中断则跳转至退出中断处理程序
	cmpb $6,%al
	ja end					# al 值大于 6 则跳转至 end （几乎不可能）
	# 下面四行指的是当有待处理中断时，al 中位0 = 0，位2-1是中断类型，因此相当于已经将中断类型乘了 2，这里再乘 2 ，得到跳转表的中断类型地址，并跳转到那里去做相应处理
	movl 24(%esp),%ecx
	pushl %edx				# 端口号 0x3fa 入栈
	subl $2,%edx
	call jmp_table(,%eax,2)	# 调用对应中断处理程序
	popl %edx				# 弹出中断标识寄存器端口号 0x3fa
	jmp rep_int 			# 重复处理中断，直到无待处理中断
end:	movb $0x20,%al 		# 向中断寄存器发送结束中断指令 EOF
	outb %al,$0x20		/* EOI */
	pop %ds
	pop %es
	popl %eax
	popl %ebx
	popl %ecx
	popl %edx
	addl $4,%esp		# jump over _table_list entry
	iret

/*各中断类型处理程序地址跳转表，共有四种中断来源：modem-状态变化中断,write_char-写字符中断,read_char-读字符中断,line_status-线路状态变化中断*/
jmp_table:
	.long modem_status,write_char,read_char,line_status

.align 2
modem_status:
	addl $6,%edx		/* clear intr by reading modem status reg */
	inb %dx,%al 		# 通过读 medem 状态寄存器进行复位
	ret

.align 2
line_status:
	addl $5,%edx		/* clear intr by reading line status reg. */
	inb %dx,%al 		# 通过读线路状态寄存器进行复位
	ret

.align 2
read_char:
	inb %dx,%al 			# 读取字符 -> al
	movl %ecx,%edx 			# 将串口缓冲队列的指针 -> edx
	subl $_table_list,%edx 	# 缓冲队列指针表首地址 - 当前串口指针地址 -> ecx
	shrl $3,%edx			# 差值除以 8，即可得到 串口1 还是 串口2
	movl (%ecx),%ecx		# read-queue # 取缓冲队列结构地址 —> ecx
	movl head(%ecx),%ebx 	# 缓冲头指针 -> ebx
	movb %al,buf(%ecx,%ebx) # 将字符放在缓冲区头指针所在位置
	incl %ebx 				# 将头指针向前移动一个字节
	andl $size-1,%ebx 		# 用缓冲区大小对指针进行模操作，指针不能超过缓冲区大小
	cmpl tail(%ecx),%ebx 	# 若相等表示缓冲区已满，跳转至标号 1 处
	je 1f
	movl %ebx,head(%ecx) 	# 保存修改过的头指针
1:	pushl %edx 				# 将串口号压入堆栈，作为参数
	call _do_tty_interrupt 	# 调用 tty 中断处理c 函数
	addl $4,%esp 			# 丢弃入栈参数 并返回
	ret

.align 2
write_char:
	movl 4(%ecx),%ecx		# write-queue # 获取写队列指针
	movl head(%ecx),%ebx 	# 获取缓冲头指针
	subl tail(%ecx),%ebx 	# 计算需要写的总字符数
	andl $size-1,%ebx		# nr chars in queue # 对指针取模运算
	je write_buffer_empty 	# 头指针与尾指针相同，队列为空，跳转处理
	cmpl $startup,%ebx 		# 队列中字符数超过 256 时
	ja 1f 					# 跳转到 1 
	movl proc_list(%ecx),%ebx	# 获取当前等待的进程
	testl %ebx,%ebx	
	je 1f
	movl $0,(%ebx) 			# 将等待的进程唤醒
1:	movl tail(%ecx),%ebx 	# 获取尾指针
	movb buf(%ecx,%ebx),%al # 获取缓冲尾指针处的一个字 -> al
	outb %al,%dx 			# 送出一个字到保存寄存器之中
	incl %ebx 				# 尾指针自减1
	andl $size-1,%ebx 
	movl %ebx,tail(%ecx) 	# 保存修改过的尾指针
	cmpl head(%ecx),%ebx 	# 判断缓冲区是否已空
	je write_buffer_empty
	ret
.align 2
write_buffer_empty:
	movl proc_list(%ecx),%ebx	# 唤醒 等待的进程
	testl %ebx,%ebx	
	je 1f 						# 无跳转到标号 1 处
	movl $0,(%ebx) 				# 有 唤醒进程
1:	incl %edx 
	inb %dx,%al 				# 读取中断允许寄存器 0x3f9 
	jmp 1f
1:	jmp 1f
1:	andb $0xd,%al				# 屏蔽发送 保持寄存器空中断
	outb %al,%dx				# 写入 0x3f9
	ret
