/*
 *  linux/kernel/keyboard.S
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 *	Thanks to Alfred Leung for US keyboard patches
 *		Wolfgang Thiel for German keyboard patches
 *		Marc Corsini for the French keyboard
 */

#include <linux/config.h>

.text
.globl _keyboard_interrupt

/*
 * these are for the keyboard read functions
 */
size	= 1024		# 键盘缓冲区大小的长度（字节）
head = 4			# 缓冲区中头指针字段的偏移
tail = 8			# 缓冲区中微指针字段的偏移
proc_list = 12		# 等待该缓冲队列的进程字段偏移
buf = 16			# 缓冲区字段偏移

# 键盘特殊键按下状态标志
# 表示大小写转换键（caps）、交换键（alt）、控制键（ctrl）和换挡键（shift）的状态
# 位7 caps 键按下，位6 caps 键的状态（应与 leds 中的对应标志位一样）
# 位 5 右alt 键按下，位4 左 alt 键按下，位3右 ctrl 键按下
# 位2 左ctrl 键按下，位1 右shift 键按下，位0 左shift 键按下
mode:	.byte 0	

# 数字锁定键（nums-lock）、大小写转换键（caps-lock）和滚动锁定键（scroll-lock）的 LED 发光管状态
# 位 7 -3 全 0 不用，位 2 caps-lock，位 1 nums-lock（初值为 1），位 0 scroll-lock
leds:	.byte 2

# 当扫描码为 0xe0 或 0xe1 时，置 e0 标志， 表示其后还跟随着 1 个或 2个字符扫描码
# e1 置位 1=1 e0（表示其后跟随 shift、ctrl 等键为右边） 置位 0=1
e0:	.byte 0

# 键盘中断处理程序入口
_keyboard_interrupt: 
	pushl %eax
	pushl %ebx
	pushl %ecx
	pushl %edx
	push %ds
	push %es
	movl $0x10,%eax 			# 将 ds、es 段寄存器置为内核数据段
	mov %ax,%ds
	mov %ax,%es
	xorl %al,%al		
	inb $0x60,%al 				# 读取扫描码
	cmpb $0xe0,%al 				# 扫描码为 0xe0 时，跳转到 e0 标志代码处
	je set_e0
	cmpb $0xe1,%al  			# 扫描码为 0xe1 时，跳转到 e1 标志代码处
	je set_e1
	call key_table(,%eax,4) 	# 其余情况调用键处理程序 key_table + eax * 4
	movb $0,e0 					# 重置 e0 位
e0_e1:	inb $0x61,%al 			# 取 PPI 端口 B 状态，位7用于允许/禁止（0/1）键盘
	jmp 1f
1:	jmp 1f
1:	orb $0x80,%al 				# al 位置位（禁止键盘工作）
	jmp 1f
1:	jmp 1f
1:	outb %al,$0x61 				# 使 PPI PB7 位置位
	jmp 1f
1:	jmp 1f
1:	andb $0x7F,%al 				# al 位7 复位
	outb %al,$0x61 				# 重新允许键盘工作
	movb $0x20,%al				# 向 8259 芯片发送 EOI 结束中断指令
	outb %al,$0x20
	pushl $0 					# 控制台 tty 号 = 0，作为参数入栈
	call _do_tty_interrupt 		# 调用 do_tty_interrupt 函数将收到的数据复制成规范数据存入规范字符缓冲队列中去
	addl $4,%esp 				# 丢弃入栈参数
	pop %es
	pop %ds
	popl %edx
	popl %ecx
	popl %ebx
	popl %eax
	iret
set_e0:	movb $1,e0 				# e0 置位
	jmp e0_e1
set_e1:	movb $2,e0 				# e1 置位
	jmp e0_e1

# 将 ebx:eax 中最多 8 个字符（一字节）添入缓冲队列中
put_queue:
	pushl %ecx
	pushl %edx
	movl _table_list,%edx		# 获取控制台读缓冲队列
	movl head(%edx),%ecx 		# 取读缓冲队列头指针偏移值 -> ecx
1:	movb %al,buf(%edx,%ecx) 	# 将 al 中的一字节字符放入缓冲队列头指针位置
	incl %ecx					# 将指针向前移动一字节
	andl $size-1,%ecx			# 使用缓冲区大小调整头指针
	cmpl tail(%edx),%ecx		# 当尾指针==头指针时，说明缓冲区已满，直接结束
	je 3f
	shrdl $8,%ebx,%eax 			# 将 ebx 中 8 位比特位右移 8 位到 eax 中，但 ebx 不变
	je 2f						# 没有字符的话跳转到 2 处
	shrl $8,%ebx				# 有的话继续复制字符
	jmp 1b
2:	movl %ecx,head(%edx)		# 重新设置缓冲区头指针位置，同时唤醒等待程序
	movl proc_list(%edx),%ecx
	testl %ecx,%ecx				# 如果有进程在等待该操作，则将该进程置为就绪态
	je 3f
	movl $0,(%ecx)
3:	popl %edx
	popl %ecx
	ret

# 根据 ctrl 与 alt 的扫描码，分别设置模式标志的相应位
# 若之前收到过 0xe0 扫描码，则说明是按下的右边的 ctrl 或者 atl 键，对应设置 ctrl 或者 alt 在模式标志 mode 中的比特位
ctrl:	movb $0x04,%al 	# 0x04 是模式标志 mode 中左 ctrl 键对应的比特位
	jmp 1f
alt:	movb $0x10,%al 	# 0x10 是模式标志 mode 中左 alt 键对应的比特位
1:	cmpb $0,e0 			# 设置过 eo 标志位
	je 2f
	addb %al,%al 		# 转换成右键
2:	orb %al,mode 		# 设置模式标志 mode 中对应的比特位
	ret
# 处理 ctrl 与 alt 松开时的扫描码，对应复位模式标志 mode 中的比特位；
# 处理时也是根据 es0 判断是否为 右边的按键
unctrl:	movb $0x04,%al 	# 0x04 是模式标志 mode 中左 ctrl 键对应的比特位
	jmp 1f
unalt:	movb $0x10,%al 	# 0x10 是模式标志 mode 中左 alt 键对应的比特位
1:	cmpb $0,e0
	je 2f
	addb %al,%al 		# 转换成右键
2:	notb %al 			# 复位模式标志 mode 中对应的比特位
	andb %al,mode
	ret

# 根据 shift 的扫描码，分别设置模式标志的相应位
lshift:
	orb $0x01,mode
	ret
unlshift:
	andb $0xfe,mode
	ret
rshift:
	orb $0x02,mode
	ret
unrshift:
	andb $0xfd,mode
	ret

# 按下 caps 键，分别设置模式标志与LEDs的相应位
caps:	testb $0x80,mode
	jne 1f
	xorb $4,leds
	xorb $0x40,mode
	orb $0x80,mode
# 根据 leds 相应位，开始或关闭对应 led 指示器
set_leds:
	call kb_wait		# 等待键盘控制器输入缓冲为空
	movb $0xed,%al		# 设置 LED 的命令
	outb %al,$0x60		# 发哦送键盘命令 0xed 到 0x60 端口，表明接下来要设置led
	call kb_wait		# 等待键盘控制器输入为空
	movb leds,%al		# 将 leds 标志发送到 0x60 端口以设置对应的 LED 显示
	outb %al,$0x60
	ret
uncaps:	andb $0x7f,mode	# 松开 caps 键，复位按下 caps 键模式标志位
	ret
scroll:					# scorll 键按下，对应 leds 置位，并设置对应的键盘 LED 显示
	xorb $1,leds
	jmp set_leds
num:	xorb $2,leds	# num 键按下，对应 leds 置位，并设置对应的键盘 LED 显示
	jmp set_leds

/*
 * 处理方向键/数字小键盘方向键，检测数字小键盘等
 */
cursor:
	subb $0x47,%al 		# > 0x47 表示时 小数字键盘上的键发出的扫描码
	jb 1f				# 不是则不处理
	cmpb $12,%al		# 小数字键盘扫描码的范围为 0x47 - 0x53，所以 大于 0x53 也退出
	ja 1f
	jne cur2			# 如果等于 12 则说明 del 键已被按下，则继续判断 ctrl 和 alt 是否也同时按下
	testb $0x0c,mode	# ctrl 键已经被按下吗
	je cur2
	testb $0x30,mode	# 同时按下 alt + ctrl + del 三键 重启
	jne reboot
cur2:	cmpb $0x01,e0	# e0 置位表示光标移动
	je cur				# 未置位表明是数字
	testb $0x02,leds	# nums-lock 按下时也是作为数字处理
	je cur
	testb $0x03,mode	# shift 按下时也按 数字处理
	jne cur
	xorl %ebx,%ebx		# 清空 bx 中数据
	movb num_table(%eax),%al # 以 eax 作为索引值，取对应字符 -> al
	jmp put_queue 		# 将对应字符加入缓冲队列中
1:	ret

# 对于光标移动的处理
cur:	movb cur_table(%eax),%al	# 以 eax 作为索引值，取对应光标移动数值 -> al
	cmpb $'9,%al		# 若该字符小于 9，表明为上一页、下一页、插入或删除键
	ja ok_cur			# 则功能字符序列中字符序列要添加 '～' 
	movb $'~,%ah
ok_cur:	shll $16,%eax	# 将 ax 中内容移到 eax 高字中
	movw $0x5b1b,%ax	# 在 ax 中放入 'esc[' 字符，与 eax 高字中字符组成移动序列
	xorl %ebx,%ebx
	jmp put_queue		# 将该字符放入缓冲队列

# num_table 数字小键盘上键对应的数字 ASCII 码
#if defined(KBD_FR)
num_table:
	.ascii "789 456 1230."
#else
num_table:
	.ascii "789 456 1230,"
#endif
# cur_table 数字小键盘上方向键或插入删除键对应的移动表示字符表
cur_table:
	.ascii "HA5 DGC YB623"

# 子程序处理功能键
func:
	pushl %eax
	pushl %ecx
	pushl %edx
	call _show_stat			# 调用显示各任务状态函数
	popl %edx
	popl %ecx
	popl %eax
	subb $0x3B,%al			# 功能键 F1 的扫描码为 0x3B，因此此时 al 中是功能键索引号
	jb end_func				# 小于 0x3B 时，则不处理直接返回
	cmpb $9,%al				# 是功能键 F1～F10
	jbe ok_func				# 是，则跳转
	subb $18,%al			# 是不是功能键 F11、F12 吗
	cmpb $10,%al			# 不是 F11 不处理返回
	jb end_func
	cmpb $11,%al			# 不是 F12 也不处理返回
	ja end_func
ok_func:
	cmpl $4,%ecx			# 空间不足时，结束
	jl end_func
	movl func_table(,%eax,4),%eax	# 取功能键对应字符序列
	xorl %ebx,%ebx
	jmp put_queue
end_func:
	ret

 # 功能键发送的扫描码，F1 为 'esc [ [ A' F2:'esc [ [ B' 等
func_table:
	.long 0x415b5b1b,0x425b5b1b,0x435b5b1b,0x445b5b1b
	.long 0x455b5b1b,0x465b5b1b,0x475b5b1b,0x485b5b1b
	.long 0x495b5b1b,0x4a5b5b1b,0x4b5b5b1b,0x4c5b5b1b

# 扫描码对应的字符映射表（根据 config.h 中定义的键盘类型获取对应的ASII表）
# 芬兰语键盘的扫描码映射表
#if	defined(KBD_FINNISH)
key_map:
	.byte 0,27 				# 扫描码 0x00 0x01 对应的 ASCII 码
	.ascii "1234567890+'" 	# 扫描码 0x02 ～ 0x0d 对应的 ASCII 码
	.byte 127,9
	.ascii "qwertyuiop}"
	.byte 0,13,0
	.ascii "asdfghjkl|{"
	.byte 0,0
	.ascii "'zxcvbnm,.-"
	.byte 0,'*,0,32		/* 36-39 */
	.fill 16,1,0		/* 3A-49 */
	.byte '-,0,0,0,'+	/* 4A-4E */
	.byte 0,0,0,0,0,0,0	/* 4F-55 */
	.byte '<
	.fill 10,1,0
# shift 同时按下时的映射表
shift_map:
	.byte 0,27
	.ascii "!\"#$%&/()=?`"
	.byte 127,9
	.ascii "QWERTYUIOP]^"
	.byte 13,0
	.ascii "ASDFGHJKL\\["
	.byte 0,0
	.ascii "*ZXCVBNM;:_"
	.byte 0,'*,0,32		/* 36-39 */
	.fill 16,1,0		/* 3A-49 */
	.byte '-,0,0,0,'+	/* 4A-4E */
	.byte 0,0,0,0,0,0,0	/* 4F-55 */
	.byte '>
	.fill 10,1,0

# alt 同时按下时的映射表
alt_map:
	.byte 0,0
	.ascii "\0@\0$\0\0{[]}\\\0"
	.byte 0,0
	.byte 0,0,0,0,0,0,0,0,0,0,0
	.byte '~,13,0
	.byte 0,0,0,0,0,0,0,0,0,0,0
	.byte 0,0
	.byte 0,0,0,0,0,0,0,0,0,0,0
	.byte 0,0,0,0		/* 36-39 */
	.fill 16,1,0		/* 3A-49 */
	.byte 0,0,0,0,0		/* 4A-4E */
	.byte 0,0,0,0,0,0,0	/* 4F-55 */
	.byte '|
	.fill 10,1,0

# 英语键盘的扫描码映射表
#elif defined(KBD_US)
key_map:
	.byte 0,27
	.ascii "1234567890-="
	.byte 127,9
	.ascii "qwertyuiop[]"
	.byte 13,0
	.ascii "asdfghjkl;'"
	.byte '`,0
	.ascii "\\zxcvbnm,./"
	.byte 0,'*,0,32		/* 36-39 */
	.fill 16,1,0		/* 3A-49 */
	.byte '-,0,0,0,'+	/* 4A-4E */
	.byte 0,0,0,0,0,0,0	/* 4F-55 */
	.byte '<
	.fill 10,1,0

shift_map:
	.byte 0,27
	.ascii "!@#$%^&*()_+"
	.byte 127,9
	.ascii "QWERTYUIOP{}"
	.byte 13,0
	.ascii "ASDFGHJKL:\""
	.byte '~,0
	.ascii "|ZXCVBNM<>?"
	.byte 0,'*,0,32		/* 36-39 */
	.fill 16,1,0		/* 3A-49 */
	.byte '-,0,0,0,'+	/* 4A-4E */
	.byte 0,0,0,0,0,0,0	/* 4F-55 */
	.byte '>
	.fill 10,1,0

alt_map:
	.byte 0,0
	.ascii "\0@\0$\0\0{[]}\\\0"
	.byte 0,0
	.byte 0,0,0,0,0,0,0,0,0,0,0
	.byte '~,13,0
	.byte 0,0,0,0,0,0,0,0,0,0,0
	.byte 0,0
	.byte 0,0,0,0,0,0,0,0,0,0,0
	.byte 0,0,0,0		/* 36-39 */
	.fill 16,1,0		/* 3A-49 */
	.byte 0,0,0,0,0		/* 4A-4E */
	.byte 0,0,0,0,0,0,0	/* 4F-55 */
	.byte '|
	.fill 10,1,0
# 德语键盘的扫描码映射表
#elif defined(KBD_GR)
key_map:
	.byte 0,27
	.ascii "1234567890\\'"
	.byte 127,9
	.ascii "qwertzuiop@+"
	.byte 13,0
	.ascii "asdfghjkl[]^"
	.byte 0,'#
	.ascii "yxcvbnm,.-"
	.byte 0,'*,0,32		/* 36-39 */
	.fill 16,1,0		/* 3A-49 */
	.byte '-,0,0,0,'+	/* 4A-4E */
	.byte 0,0,0,0,0,0,0	/* 4F-55 */
	.byte '<
	.fill 10,1,0


shift_map:
	.byte 0,27
	.ascii "!\"#$%&/()=?`"
	.byte 127,9
	.ascii "QWERTZUIOP\\*"
	.byte 13,0
	.ascii "ASDFGHJKL{}~"
	.byte 0,''
	.ascii "YXCVBNM;:_"
	.byte 0,'*,0,32		/* 36-39 */
	.fill 16,1,0		/* 3A-49 */
	.byte '-,0,0,0,'+	/* 4A-4E */
	.byte 0,0,0,0,0,0,0	/* 4F-55 */
	.byte '>
	.fill 10,1,0

alt_map:
	.byte 0,0
	.ascii "\0@\0$\0\0{[]}\\\0"
	.byte 0,0
	.byte '@,0,0,0,0,0,0,0,0,0,0
	.byte '~,13,0
	.byte 0,0,0,0,0,0,0,0,0,0,0
	.byte 0,0
	.byte 0,0,0,0,0,0,0,0,0,0,0
	.byte 0,0,0,0		/* 36-39 */
	.fill 16,1,0		/* 3A-49 */
	.byte 0,0,0,0,0		/* 4A-4E */
	.byte 0,0,0,0,0,0,0	/* 4F-55 */
	.byte '|
	.fill 10,1,0

# 法语键盘的扫描码映射表
#elif defined(KBD_FR)
key_map:
	.byte 0,27
	.ascii "&{\"'(-}_/@)="
	.byte 127,9
	.ascii "azertyuiop^$"
	.byte 13,0
	.ascii "qsdfghjklm|"
	.byte '`,0,42		/* coin sup gauche, don't know, [*|mu] */
	.ascii "wxcvbn,;:!"
	.byte 0,'*,0,32		/* 36-39 */
	.fill 16,1,0		/* 3A-49 */
	.byte '-,0,0,0,'+	/* 4A-4E */
	.byte 0,0,0,0,0,0,0	/* 4F-55 */
	.byte '<
	.fill 10,1,0

shift_map:
	.byte 0,27
	.ascii "1234567890]+"
	.byte 127,9
	.ascii "AZERTYUIOP<>"
	.byte 13,0
	.ascii "QSDFGHJKLM%"
	.byte '~,0,'#
	.ascii "WXCVBN?./\\"
	.byte 0,'*,0,32		/* 36-39 */
	.fill 16,1,0		/* 3A-49 */
	.byte '-,0,0,0,'+	/* 4A-4E */
	.byte 0,0,0,0,0,0,0	/* 4F-55 */
	.byte '>
	.fill 10,1,0

alt_map:
	.byte 0,0
	.ascii "\0~#{[|`\\^@]}"
	.byte 0,0
	.byte '@,0,0,0,0,0,0,0,0,0,0
	.byte '~,13,0
	.byte 0,0,0,0,0,0,0,0,0,0,0
	.byte 0,0
	.byte 0,0,0,0,0,0,0,0,0,0,0
	.byte 0,0,0,0		/* 36-39 */
	.fill 16,1,0		/* 3A-49 */
	.byte 0,0,0,0,0		/* 4A-4E */
	.byte 0,0,0,0,0,0,0	/* 4F-55 */
	.byte '|
	.fill 10,1,0

#else
#error "KBD-type not defined"
#endif

 /*
 * do_self 用于处理普通键，也即含义没有变化并且只有一个字符返回的键
 */
do_self:
	# 下面 7 行代码用于根据模式标志 mode 选择 alt_map、shift_map或key_map
	lea alt_map,%ebx
	testb $0x20,mode		# 同时按下 alter
	jne 1f
	lea shift_map,%ebx
	testb $0x03,mode 		# 同时按下 shift
	jne 1f
	lea key_map,%ebx		# 其他使用默认
# 将对应字符转化为大写字符
1:	movb (%ebx,%eax),%al	# 取 表中对应字符 
	orb %al,%al
	je none
	testb $0x4c,mode		# 控制键已按下或caps锁定
	je 2f
	cmpb $'a,%al 
	jb 2f
	cmpb $'},%al
	ja 2f
	subb $32,%al 			# 转化为大写字符
# 将对应字符转化为控制字符
2:	testb $0x0c,mode		/* ctrl */
	je 3f
	cmpb $64,%al
	jb 3f
	cmpb $64+32,%al
	jae 3f
	subb $64,%al
# 左 ctrl 键同时按下，则将字符位 7 置位
3:	testb $0x10,mode		/* left alt */
	je 4f
	orb $0x80,%al
# 将处理后的字符放入 缓冲队列之中
4:	andl $0xff,%eax
	xorl %ebx,%ebx
	call put_queue
none:	ret

/*
 * minus has a routine of it's own, as a 'E0h' before
 * the scan code for minus means that the numeric keypad
 * slash was pushed.
 */
minus:	cmpb $1,e0
	jne do_self
	movl $'/,%eax
	xorl %ebx,%ebx
	jmp put_queue

/*
 * This table decides which routine to call when a scan-code has been
 * gotten. Most routines just call do_self, or none, depending if
 * they are make or break.
 */
 /*
 * key_table 是一张子程序地址跳转表，当取得扫描码后就根据此表调用相应的扫描码处理子程序对其进行处理
 * 大多数调用的子程序时 do_self、none，取决于是按键还是释放键
 */
key_table:
	.long none,do_self,do_self,do_self	/* 00-03 s0 esc 1 2 */
	.long do_self,do_self,do_self,do_self	/* 04-07 3 4 5 6 */
	.long do_self,do_self,do_self,do_self	/* 08-0B 7 8 9 0 */
	.long do_self,do_self,do_self,do_self	/* 0C-0F + ' bs tab */
	.long do_self,do_self,do_self,do_self	/* 10-13 q w e r */
	.long do_self,do_self,do_self,do_self	/* 14-17 t y u i */
	.long do_self,do_self,do_self,do_self	/* 18-1B o p } ^ */
	.long do_self,ctrl,do_self,do_self	/* 1C-1F enter ctrl a s */
	.long do_self,do_self,do_self,do_self	/* 20-23 d f g h */
	.long do_self,do_self,do_self,do_self	/* 24-27 j k l | */
	.long do_self,do_self,lshift,do_self	/* 28-2B { para lshift , */
	.long do_self,do_self,do_self,do_self	/* 2C-2F z x c v */
	.long do_self,do_self,do_self,do_self	/* 30-33 b n m , */
	.long do_self,minus,rshift,do_self	/* 34-37 . - rshift * */
	.long alt,do_self,caps,func		/* 38-3B alt sp caps f1 */
	.long func,func,func,func		/* 3C-3F f2 f3 f4 f5 */
	.long func,func,func,func		/* 40-43 f6 f7 f8 f9 */
	.long func,num,scroll,cursor		/* 44-47 f10 num scr home */
	.long cursor,cursor,do_self,cursor	/* 48-4B up pgup - left */
	.long cursor,cursor,do_self,cursor	/* 4C-4F n5 right + end */
	.long cursor,cursor,cursor,cursor	/* 50-53 dn pgdn ins del */
	.long none,none,do_self,func		/* 54-57 sysreq ? < f11 */
	.long func,none,none,none		/* 58-5B f12 ? ? ? */
	.long none,none,none,none		/* 5C-5F ? ? ? ? */
	.long none,none,none,none		/* 60-63 ? ? ? ? */
	.long none,none,none,none		/* 64-67 ? ? ? ? */
	.long none,none,none,none		/* 68-6B ? ? ? ? */
	.long none,none,none,none		/* 6C-6F ? ? ? ? */
	.long none,none,none,none		/* 70-73 ? ? ? ? */
	.long none,none,none,none		/* 74-77 ? ? ? ? */
	.long none,none,none,none		/* 78-7B ? ? ? ? */
	.long none,none,none,none		/* 7C-7F ? ? ? ? */
	.long none,none,none,none		/* 80-83 ? br br br */
	.long none,none,none,none		/* 84-87 br br br br */
	.long none,none,none,none		/* 88-8B br br br br */
	.long none,none,none,none		/* 8C-8F br br br br */
	.long none,none,none,none		/* 90-93 br br br br */
	.long none,none,none,none		/* 94-97 br br br br */
	.long none,none,none,none		/* 98-9B br br br br */
	.long none,unctrl,none,none		/* 9C-9F br unctrl br br */
	.long none,none,none,none		/* A0-A3 br br br br */
	.long none,none,none,none		/* A4-A7 br br br br */
	.long none,none,unlshift,none		/* A8-AB br br unlshift br */
	.long none,none,none,none		/* AC-AF br br br br */
	.long none,none,none,none		/* B0-B3 br br br br */
	.long none,none,unrshift,none		/* B4-B7 br br unrshift br */
	.long unalt,none,uncaps,none		/* B8-BB unalt br uncaps br */
	.long none,none,none,none		/* BC-BF br br br br */
	.long none,none,none,none		/* C0-C3 br br br br */
	.long none,none,none,none		/* C4-C7 br br br br */
	.long none,none,none,none		/* C8-CB br br br br */
	.long none,none,none,none		/* CC-CF br br br br */
	.long none,none,none,none		/* D0-D3 br br br br */
	.long none,none,none,none		/* D4-D7 br br br br */
	.long none,none,none,none		/* D8-DB br ? ? ? */
	.long none,none,none,none		/* DC-DF ? ? ? ? */
	.long none,none,none,none		/* E0-E3 e0 e1 ? ? */
	.long none,none,none,none		/* E4-E7 ? ? ? ? */
	.long none,none,none,none		/* E8-EB ? ? ? ? */
	.long none,none,none,none		/* EC-EF ? ? ? ? */
	.long none,none,none,none		/* F0-F3 ? ? ? ? */
	.long none,none,none,none		/* F4-F7 ? ? ? ? */
	.long none,none,none,none		/* F8-FB ? ? ? ? */
	.long none,none,none,none		/* FC-FF ? ? ? ? */

# 用于等待键盘控制器缓冲为空，不存在超时处理，如果缓冲区永远不空，陷入死循环
kb_wait:
	pushl %eax
1:	inb $0x64,%al		# 读键盘控制器状态
	testb $0x02,%al		# 测试输入缓冲期是否为空
	jne 1b				# 不空，陷入循环
	popl %eax
	ret
# 向复位线输出负脉冲，使系统复位重启
reboot:
	call kb_wait
	movw $0x1234,0x472	 
	movb $0xfc,%al
	outb %al,$0x64		# 向系统复位和 A20 线输出负脉冲
die:	jmp die			# 死机
