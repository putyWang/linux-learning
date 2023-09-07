!
!	setup.s		(C) 1991 Linus Torvalds
!
! setup.s is responsible for getting the system data from the BIOS,
! and putting them into the appropriate places in system memory.
! both setup.s and system has been loaded by the bootblock.
!
! This code asks the bios for memory/disk/other parameters, and
! puts them in a "safe" place: 0x90000-0x901FF, ie where the
! boot-block used to be. It is then up to the protected mode
! system to read them from there before the area is overwritten
! for buffer-blocks.
!

! setup.s 负责从 BIOS 中获取系统数据，并将这些数据放到系统内存合适的地方（由于 bootsect.s 已运行完成，因此全部加载覆盖到 bootsect.s 所在内存 0x90000-0x901ff）
! 读取的数据包括内存/磁盘/其他参数，然后在缓冲块被覆盖掉之前有保护模式的system模块代码读取

! NOTE! These had better be the same as in bootsect.s!

INITSEG  = 0x9000	! we move boot here - out of the way ! 原 bootsect.s 程序加载位置
SYSSEG   = 0x1000	! system loaded at 0x10000 (65536). ! 原 system 模块加载位置
SETUPSEG = 0x9020	! this is the current segment ! 本程序所在位置

.globl begtext, begdata, begbss, endtext, enddata, endbss
.text
begtext:
.data
begdata:
.bss
begbss:
.text

entry start
start:

! ok, the read went well so we get current cursor position and save it for
! posterity.

! 整个磁盘正常读取，现在将光标位置保存以备之后使用

	mov	ax,#INITSEG	! this is done in bootsect already, but... ! 虽然 bootsect.s 中已经在 ds 保存过 0x90000，但新的一个程序还是重新保存下为好
	mov	ds,ax
	mov	ah,#0x03	! read cursor pos ! 将 ah 设置为 0x03 
	xor	bh,bh
	int	0x10		! save it in known place, con_init fetches ! 使用 0x10 中断及 ah=0x03 与 bh = 页号 参数读取当前光标所在位置（返回 ch = 页号、 cl = 扫描结束线、dh = 行号、dl = 列号）；
	mov	[0],dx		! it from 0x90000. ! 将上文读取到的光标 保存到 0x90000 处

! Get memory size (extended mem, kB)

	! 使用 0x15 中断及 ah=0x88 参数读取当前内存参数（返回 ax = 从 0x100000 处开始的扩展内存大小 KB）；
	mov	ah,#0x88
	int	0x15
	mov	[2],ax ! 将读取到的 内存信息保存到 0x90002 处

! Get video-card data:

	! 使用 0x10 中断及 ah=0x0f 参数读取当前显卡显示模式（返回 ah = 字符列数，al=显示模式，bh=当前显示页）；
	mov	ah,#0x0f
	int	0x10
	mov	[4],bx		! bh = display page ! 当前页保存到 0x90004
	mov	[6],ax		! al = video mode, ah = window width ! 当前显示页存放在 0x90006

! check for EGA/VGA and some config parameters
	! 使用 0x10 中断及 ah=0x0f 与 bl=0x10 参数获取附加功能选择方式（返回 bh = 显示状态 [0x00-彩色模式，I/O 端口=0x3dX；0x01-单色模式，I/O 端口=0x3bX] ，bl=安装的显示内存[0x00-64k,0x01-128k,0x02-192k,0x03=256k], cx = 显卡特性参数）；
	mov	ah,#0x12
	mov	bl,#0x10
	int	0x10
	mov	[8],ax ! 不知道存啥
	mov	[10],bx ! 将显示内存与显示内存保存至 0x9000A 处
	mov	[12],cx ! 将显卡特性参数保存至 0x9000C 处

! 获取系统第一个 硬盘信息（硬盘参数表）将其复制到 0x90090
! Get hd0 data

	mov	ax,#0x0000 ! 将 ds 中至设置为 0x0000
	mov	ds,ax
	lds	si,[4*0x41] 
	mov	ax,#INITSEG
	mov	es,ax
	mov	di,#0x0080
	mov	cx,#0x10
	rep
	movsb

! 获取系统第二个 硬盘信息（硬盘参数表）将其复制到 0x90090
! Get hd1 data

	mov	ax,#0x0000
	mov	ds,ax
	lds	si,[4*0x46]
	mov	ax,#INITSEG
	mov	es,ax
	mov	di,#0x0090
	mov	cx,#0x10
	rep
	movsb

! 下列代码检查系统是否存在第二个磁盘，如果不存则将第二个表清空
! Check that there IS a hd1 :-)

	! 使用 0x13 中断及 ax=0x01500 与 dl=驱动器（0x8x-硬盘，0x80-第一个硬盘，0x81-第二个硬盘）参数调用程序取盘功能（返回 ah = 类型码[00-没有这个盘，CF 置位；01-软驱，没有 change-line 支持；02-软驱（或其他可移动设备），有 change-line 支持；03-硬盘]）；
	mov	ax,#0x01500
	mov	dl,#0x81
	int	0x13
	jc	no_disk1 
	cmp	ah,#3 ! 是硬盘是跳过相关操作
	je	is_disk1
	! 第二个硬盘不存在时 执行 no_disk1 逻辑 清空 第二张表
no_disk1:
	mov	ax,#INITSEG
	mov	es,ax
	mov	di,#0x0090
	mov	cx,#0x10
	mov	ax,#0x00
	rep
	stosb
is_disk1:

! now we want to move to protected mode ...
! 下列代码在保护模式下运行

	cli			! no interrupts allowed ! 禁止中断

! first we move the system to it's rightful place

	! 下列到 end_move 之间的代码将 之前复制在 0x10000 - 0x8ffff 的 system 模块代码移动到 0x00000 - 0x7ffff
	mov	ax,#0x0000
	cld			! 'direction'=0, movs moves forward
do_move:
	mov	es,ax		! destination segment
	add	ax,#0x1000
	cmp	ax,#0x9000
	jz	end_move ! 判断 当前 ax 值 是否为 0x90000, 是的话 表明已完成复制，不是的话说明还未执行复制代码
	mov	ds,ax		! source segment
	sub	di,di
	sub	si,si
	mov 	cx,#0x8000
	rep
	movsw
	jmp	do_move

! then we load the segment descriptors

end_move:

	! 保护模式：lidt 指令用于加载中断描述符表（idt）寄存器，操作数为六个字节，0～1字节表示描述符长度值，2～5 字节用于表示描述符表的 32 位线性基地址（首地址）
	! 中断描述符表中的每一个表项（8个字节）指出发生中断时需要调用与中断向量类似的代码信息；lgdt 指令用于加载全局描述符表（gdt）寄存器，其操作数格式与 lidt 指令的相同；
	! 全局描述表中的每个描述符项（8个字节）描述了保护模式下数据和代码块的信息，
	! 其中包括段的最大长度限制（16位）、段的线性基地址（32位）、段的特权级、段是否在内存、读写许可以及其它一些保护模式运行标志
	mov	ax,#SETUPSEG	! right, forgot this at first. didn't work :-)
	mov	ds,ax
	lidt	idt_48		! load idt with 0,0
	lgdt	gdt_48		! load gdt with whatever appropriate

! that was painless, now we enable A20

! 下段代码开启 A20 地址线
	call	empty_8042 ! 等待输入缓存器为空，
	mov	al,#0xD1		! command write ! 0xD1 命令码 表示要写数据到 8042 P2口，P2 口的位1 用于A20 线选通
	out	#0x64,al
	call	empty_8042 ! 等待输入缓存器空 以查看命令是否被接受
	mov	al,#0xDF		! A20 on ! 选通 A20 地址线的参数
	out	#0x60,al
	call	empty_8042 ! 输入缓冲区为空 表明 A20 地址线已选通

! well, that went ok, I hope. Now we have to reprogram the interrupts :-(
! we put them right after the intel-reserved hardware interrupts, at
! int 0x20-0x2F. There they won't mess up anything. Sadly IBM really
! messed this up with the original PC, and they haven't been able to
! rectify it afterwards. Thus the bios puts interrupts at 0x08-0x0f,
! which is used for the internal hardware interrupts as well. We just
! have to reprogram the 8259's, and it isn't fun.

	! 一切正常的情况下，接下来重新对中断进行编程，将其放在 Intel 保留的中断之后（int 0x20-0x2F），以至于不会与 Intel 中断产生冲突，
	! PC 的中断放在 0x08-0x0F，这些中断也被用于内部硬件中断，所以需要对 8259 中中断控制器进行编程；

	mov	al,#0x11		! initialization sequence ! 初始化中断序列，0x11 表示初始化命令开始，是 ICW1 命令字，表示边沿触发、多片 8059 级联、最后要发 ICW4 命令字
	out	#0x20,al		! send it to 8259A-1 ! 发送到 8259A 主芯片
	.word	0x00eb,0x00eb		! jmp $+2, jmp $+2 ! 两条跳转指令，表示跳转到下一条指令，起延时作用
	out	#0xA0,al		! and to 8259A-2 ! 在发送到 8259A 从芯片
	.word	0x00eb,0x00eb 
	mov	al,#0x20		! start of hardware int's (0x20)
	out	#0x21,al ! 将 ICW2 命令字，主芯片起始中断号（0x20）发送到 8259A 主芯片
	.word	0x00eb,0x00eb
	mov	al,#0x28		! start of hardware int's 2 (0x28)
	out	#0xA1,al ! 将 ICW2 命令字，从芯片起始中断号（0x28）发送到 8259A 主芯片
	.word	0x00eb,0x00eb
	mov	al,#0x04		! 8259-1 is master
	out	#0x21,al ! 送 主芯片 ICW3 命令字，主芯片的 IR2 连从芯片
	.word	0x00eb,0x00eb
	mov	al,#0x02		! 8259-2 is slave
	out	#0xA1,al ! 送从芯片 ICW3 命令字，表示通过 int 注脚链接主芯片 IR2 引脚
	.word	0x00eb,0x00eb
	mov	al,#0x01		! 8086 mode for both
	out	#0x21,al ! 送主芯片 ICW4 命令字，8086模式；普通 EOI 方式
	.word	0x00eb,0x00eb
	out	#0xA1,al ! 送从芯片 ICW4 命令字，8086模式；普通 EOI 方式
	.word	0x00eb,0x00eb
	mov	al,#0xFF		! mask off all interrupts for now
	out	#0x21,al ! 屏蔽主芯片所有中断请求
	.word	0x00eb,0x00eb
	out	#0xA1,al ! 屏蔽从芯片所有中断请求

! well, that certainly wasn't fun :-(. Hopefully it works, and we don't
! need no steenking BIOS anyway (except for the initial loading :-).
! The BIOS-routine wants lots of unnecessary data, and it's less
! "interesting" anyway. This is how REAL programmers do it.
!
! Well, now's the time to actually move into protected mode. To make
! things as simple as possible, we do no register set-up or anything,
! we let the gnu-compiled 32-bit programs do that. We just jump to
! absolute address 0x00000, in 32-bit protected mode.

! 接下来通过将控制寄存器 CR0 比特位0 置为 1 来使 cpu 进入保护模式中工作

	mov	ax,#0x0001	! protected mode (PE) bit ! 将 位0 设置为 1
	lmsw	ax		! This is it! ! 加载机器状态字
	jmpi	0,8		! jmp offset 0 of segment 8 (cs) ! 跳转至 cs 段 8 （全局表中参数 ） 偏移 0 处 至 system 模块头 开始执行

! This routine checks that the keyboard command queue is empty
! No timeout is used - if this hangs there is something wrong with
! the machine, and we probably couldn't proceed anyway.

! empty_8042 子程序检查键盘命令队列是否为空，不使用超时方法——如果该处死机，则说明PC机有问题，没办法继续处理；
! 只有当 输入缓冲区为空时（状态寄存器位2 = 0）才可以对其进行写命令
empty_8042:
	.word	0x00eb,0x00eb ! 两个跳转指令机械码，作为延时空操作
	in	al,#0x64	! 8042 status port ! 读 AT 键盘控制器状态寄存器
	test	al,#2		! is input buffer full? ! 测试位2 ，缓冲区是否满了
	jnz	empty_8042	! yes - loop
	ret

! 下列 gdt 给出了 三个全局描述符项
gdt:
	! 第一个描述符，不用
	.word	0,0,0,0		! dummy

	! 下列四行为第二个描述符表项，为系统代码段描述符（偏移量为 0x05）加载代码段寄存器时使用
	.word	0x07FF		! 8Mb - limit=2047 (2048*4096=8Mb)
	.word	0x0000		! base address=0
	.word	0x9A00		! code read/exec
	.word	0x00C0		! granularity=4096, 386

	! 下列四行为第三个描述符表项，为系统数据段描述符（偏移量为 0x10）加载代码数据寄存器时使用
	.word	0x07FF		! 8Mb - limit=2047 (2048*4096=8Mb)
	.word	0x0000		! base address=0
	.word	0x9200		! data read/write
	.word	0x00C0		! granularity=4096, 386

! lidit指令操作数，6个字节，第一行两个字节表示 idt 表限长，第二行四个字节表示 idt 所在基地址 
idt_48:
	.word	0			! idt limit=0
	.word	0,0			! idt base=0L

! lgdt 指令操作数
gdt_48:
	! 全局表长度为 2 KB， 因为每 8B 组成一个段描述符项所以表总共可有256项 
	.word	0x800		! gdt limit=2048, 256 GDT entries
	! 四字节构成的线性地址：0x0009 << 16 + 0x0200 + gdt，即 0x90200 + gdt 本程序偏移地址
	.word	512+gdt,0x9	! gdt base = 0X9xxxx
	
.text
endtext:
.data
enddata:
.bss
endbss:
