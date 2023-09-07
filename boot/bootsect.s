!
! SYS_SIZE is the number of clicks (16 bytes) to be loaded.
! 0x3000 is 0x30000 bytes = 196kB, more than enough for current
! versions of linux

! SYSSIZE 表示需要加载的节数（1节 = 16字节）为 0x30000 字节，192kb（1024字节为1kb）
! 指的是 system 模块的大小，本处给的是最大默认值
SYSSIZE = 0x3000
!
!	bootsect.s		(C) 1991 Linus Torvalds
!
! bootsect.s is loaded at 0x7c00 by the bios-startup routines, and moves
! iself out of the way to address 0x90000, and jumps there.
!
! It then loads 'setup' directly after itself (0x90200), and the system
! at 0x10000, using BIOS interrupts. 
!
! NOTE! currently system is at most 8*65536 bytes long. This should be no
! problem, even in the future. I want to keep it simple. This 512 kB
! kernel size should be enough, especially as this doesn't contain the
! buffer cache as in minix
!
! The loader has been made as simple as possible, and continuos
! read errors will result in a unbreakable loop. Reboot by hand. It
! loads pretty fast by getting whole sectors at a time whenever possible.

! bootsect.s 被 bios 启动子程序加载到 0x7c00 处，随后将其自己移动到 0x90000 处（574kb），
! 并跳转至那里，然后使用 bios 中断将 steup 直接加载到他代码之后 （0x90200， 576.5kb），并将 system 模块加载到 0x10000 处（574kb）处，

! 使用 .global 关键字定义了 六个全局标识符
.globl begtext, begdata, begbss, endtext, enddata, endbss

! .text 声明文本段 
.text
begtext:

! .data 声明数据段
.data
begdata:

! .bss 声明为初始化数据段
.bss
begbss:

! .text 声明文本段 
.text

! setup 程序扇区数值
SETUPLEN = 4				! nr of setup-sectors
! bootseg 的原始段地址
BOOTSEG  = 0x07c0			! original address of boot-sector
! 移动后的 bootsect 段地址
INITSEG  = 0x9000			! we move boot here - out of the way
! setup 程序移动地址
SETUPSEG = 0x9020			! setup starts here
! system 模块 加载地址
SYSSEG   = 0x1000			! system loaded at 0x10000 (65536).
! 停止加载的段地址
ENDSEG   = SYSSEG + SYSSIZE		! where to stop loading

! ROOT_DEV:	0x000 - same type of floppy as boot.
!		0x301 - first partition on first drive etc

! ROOT_DEV 设置根文件系统设备
! 0x000 - 引导软驱设备
! 0x301 - 第一个硬盘上的第一个分区 。。。
! 0x306 - 第二个硬盘的第一个分区
ROOT_DEV = 0x306

entry start ! 告诉连接器程序从该处开始
start:
	mov	ax,#BOOTSEG ! 将寄存器 ds 中的设置为 0x07c0
	mov	ds,ax
	mov	ax,#INITSEG ! 将寄存器 es 中的设置为 0x9000
	mov	es,ax
	mov	cx,#256 ! 设置移动计数值为 256 
	sub	si,si ! 设置源地址为 ds:si = 0x07c0:0x0000
	sub	di,di ! 设置目标地址为 es:si = 0x9000:0x0000
	rep ! 重复执行下一行直到 cx 中数递减到 0 
	movw ! 每次移动一个字
	jmpi	go,INITSEG ! 段间跳转，INITSEG 表示跳转地址
! 下面 4 行将 ds es ss 三个寄存器值都置成 移动后的段处（0x9000）
go:	mov	ax,cs ! 
	mov	ds,ax
	mov	es,ax
! put stack at 0x9ff00.
	mov	ss,ax
! 设置新的堆栈段的位置，sp 指向远远大于 512 偏移的位置
	mov	sp,#0xFF00		! arbitrary value >>512

! load the setup-sectors directly after the bootblock.
! Note that 'es' is already set up.

! 下面一段代码用于将 setup 代码从磁盘第 2 个扇区加载到 0x9020 开始处
! 共加载 4 个扇区，读出错则复位驱动器，同时重试；
! 读出错时所使用的中断用法：ah= 0x02 -读磁盘扇区到内存，al = 需要读出的扇区数量，ch = 磁道（柱面）号的低 8 位，cl = 开始扇区（位 0 ～ 5），磁道号高 2 位（位 6 ～ 7）
! dh = 磁头号，dl = 驱动器号（若是硬盘则是置位 7）， es:bx -> 指向数据缓冲区，出错时则 CF 标志置位
load_setup:
	mov	dx,#0x0000		! drive 0, head 0 ! 驱动器 0，磁头 0
	mov	cx,#0x0002		! sector 2, track 0 ! 扇区 2，磁道 0
	mov	bx,#0x0200		! address = 512, in INITSEG ! INITSEG段 512 偏移处
	mov	ax,#0x0200+SETUPLEN	! service 2, nr of sectors ! 服务号 2 ，后面是扇区数
	int	0x13			! read it
	jnc	ok_load_setup		! ok - continue ! 若正常 则继续
	mov	dx,#0x0000
	mov	ax,#0x0000		! reset the diskette ! 复位磁盘
	int	0x13
	j	load_setup

ok_load_setup:

! Get disk drive parameters, specifically nr of sectors/track

! 取磁盘驱动器参数，特别是每道扇区数量；取磁盘驱动器参数 INT 0x13 调用格式与返回信息如下：
! ah= 0x08，dl = 驱动器号（若是硬盘则是置位 7 为 1），返回：ah= 0，al = 0，bl = 驱动器类型（AT/PS2），ch = 最大磁道号的低 8 位，cl = 每磁道最大扇区数（位 0 ～ 5），最大磁道号高 2 位（位 6 ～ 7）
! dh = 最大磁头数，dl = 驱动器数量， es:di -> 软驱磁盘参数表，出错时则 CF 置位，且 ah = 状态码
	mov	dl,#0x00
	mov	ax,#0x0800		! AH=8 is get drive parameters
	int	0x13
	mov	ch,#0x00
	seg cs ! 表示下一条语句的操作数在 cs 段寄存器所指的段中
	mov	sectors,cx ! 保存每磁道扇区数
	mov	ax,#INITSEG
	mov	es,ax ! 因为上面去磁盘参数中断修改了 es，需要重新设置

! Print some inane message

	mov	ah,#0x03		! read cursor pos
	xor	bh,bh ! 读光标位置
	int	0x10
	
	mov	cx,#24 ! 共 24 个字符
	mov	bx,#0x0007		! page 0, attribute 7 (normal)
	mov	bp,#msg1 ! 指向要显示的字符串
	mov	ax,#0x1301		! write string, move cursor
	int	0x10 ! 写字符串并移动光标

! ok, we've written the message, now
! we want to load the system (at 0x10000)

	mov	ax,#SYSSEG
	mov	es,ax		! segment of 0x010000
	call	read_it ! 读磁盘上的 system 模块，es 为输入参数
	call	kill_motor ! 关闭电动机，这样就可以知道驱动器的状态了

! After that we check which root-device to use. If the device is
! defined (!= 0), nothing is done and the given device is used.
! Otherwise, either /dev/PS0 (2,28) or /dev/at0 (2,8), depending
! on the number of sectors that the BIOS reports currently.
! 然后检查要使用哪个根文件系统设备（简称根设备），如果已指定了设置（! = 0），就直接使用给定设备；
! 否则需要根据 BIOS 报告的每磁道扇区数来确定到底使用 /dev/PS0（2，28）还是/dev/at0（2，8）
	seg cs
	mov	ax,root_dev ! 取 508,509 字节处的根设备好号并判断是否已被定义
	cmp	ax,#0
	jne	root_defined
	seg cs
! 获取之前保存的每磁道扇区数，若 sectors = 15 说明是 1.2MB 的驱动器，若 sectors = 18 说明是 1.44MB 的软驱，因为是可引导的驱动器，所以肯定是 A 驱
	mov	bx,sectors
	mov	ax,#0x0208		! /dev/ps0 - 1.2Mb
	cmp	bx,#15
	je	root_defined
	mov	ax,#0x021c		! /dev/PS0 - 1.44Mb
	cmp	bx,#18
	je	root_defined
undef_root: ! 如果 15 与 18 值都不是，则进入死循环
	jmp undef_root
root_defined:
	seg cs
	mov	root_dev,ax ! 保存已检查设备号

! after that (everyting loaded), we jump to
! the setup-routine loaded directly after
! the bootblock:

! 所有程序到此全都加载完毕，我们就跳转到被加载在 bootsect 后面的 setup 程序中去
	jmpi	0,SETUPSEG ! 跳转到 0x9020:0000 (setup.s 程序的开始处)，程序到此结束

! This routine loads the system at address 0x10000, making sure
! no 64kB boundaries are crossed. We try to load it as fast as
! possible, loading whole tracks whenever we can.
!
! in:	es - starting address segment (normally 0x1000)
!

! 该子程序将系统模块加载到内存地址：0x10000 处，并确定没有跨越 64 kb 的内存边界，
! 我们试图尽快地进行加载，只要可能，就每次加载整条磁道的数据。输入 es-开始内存地址段值（通常是 0x1000）
sread:	.word 1+SETUPLEN	! sectors read of current track ! 磁道中已读扇区数，开始时已读入 1 扇区的引导扇区
head:	.word 0			! current head ! 当前磁头号
track:	.word 0			! current track ! 当前磁道号

read_it:
! 下面几行测试输入的段值，从盘上读入的数据必须存放在位于内存地址 64KB 的边界开始处，否则进入死循环
! 清 bx 寄存器，用于表示当前段内存放数据的开始位置
	mov ax,es
	test ax,#0x0fff
die:	jne die			! es must be at 64kB boundary
	xor bx,bx		! bx is starting address within segment
! 判断是否已经读入全部数据，比较当前所读段是否就是系统数据末端所处的段，
! 如果不是就跳转到 ok1_read 标志处继续读取数据吗，否则子程序返回
rp_read:
	mov ax,es
	cmp ax,#ENDSEG		! have we loaded all yet?
	jb ok1_read
	ret
! 计算和验证当前磁道需要读取的扇区数，放在 ax 寄存器内，根据当前磁道还未读取的扇区数以及段内数据字节开始偏移位置，
! 计算如果全部读取未读扇区，所读总字节数是否会超过 64KB 段长限制，
! 若超过，则根据此次最多能读入的字节数（54KB - 段内偏移位置），反算出此次需要读取的扇区数
ok1_read:
	seg cs 
	mov ax,sectors ! 取每磁道扇区数
	sub ax,sread ! 减去当前磁道已读扇区数
	mov cx,ax ! cx = ax = 当前磁道未读扇区数
	shl cx,#9 ! cx = cx * 512 字节
	add cx,bx ! cx = cx + 段内偏移值（bx），即此次操作后段内读入字节数
	jnc ok2_read ! 若没有超过 64KB 字节，则跳转至 ok2_read 处执行
	je ok2_read
	xor ax,ax ! 若加上上次将读磁道上所有未读扇区时会超过 64KB
	sub ax,bx ! 则计算最多能读取的字节数
	shr ax,#9 ! 将最多能读取的字节数转换成需要读取的扇区数
ok2_read:
	call read_track
	mov cx,ax ! cx = 该次操作已读取的扇区数
	add ax,sread ! 当前磁道上已经读取的扇区数
	seg cs
	cmp ax,sectors 
	jne ok3_read ! 如果当前磁道上还有扇区未读，则跳转到 ok3_read
	mov ax,#1 ! 之后读 磁盘上下一磁头上的数据
	sub ax,head ! 计算下一磁头
	jne ok4_read ! 如果是 0 磁头，再去读 1 磁头面上的扇区数据
	inc track ! 否则去读下一磁道
ok4_read:
	mov head,ax ! 保存当前磁头号
	xor ax,ax ! 请当前磁道已读扇区数
ok3_read:
	mov sread,ax ! 保存当前磁道已读扇区
	shl cx,#9 ! 上次已读扇区 * 512B
	add bx,cx ! 调整当前段内数据开始位置
	jnc rp_read ! 若小于 64 KB 界，则跳到 rp_read ,否则调整当前段为读下一段数据做准备
	mov ax,es
	add ax,#0x1000 ! 将段基址调整为指向下一个 64 KB 内存开始处
	mov es,ax
	xor bx,bx ! 清段内数据开始偏移值
	jmp rp_read ! 跳转至 rp_read 处 继续读数据

! 读当前磁道上指定开始扇区与需读扇区数的数据到 es:bx 开始处，
read_track:
	push ax
	push bx
	push cx
	push dx
	mov dx,track ! 取当前磁道号
	mov cx,sread ! 取当前磁道上已读扇区数
	inc cx ! cl = 开始读扇区
	mov ch,dl ! ch = 当前磁道号
	mov dx,head ! 取当前磁头号
	mov dh,dl ! dh = 磁头号
	mov dl,#0 ! dl = 驱动器号
	and dx,#0x0100 ! 磁头号不大于 1
	mov ah,#2 ! ah = 2, 读磁盘扇区功能号
	int 0x13
	jc bad_rt ! 若出错，则跳转至 bad_rt
	pop dx
	pop cx
	pop bx
	pop ax
	ret
bad_rt:	mov ax,#0 ! 执行驱动器复位操作（磁盘中断功能号 0），再跳到 read_track 重试
	mov dx,#0
	int 0x13
	pop dx
	pop cx
	pop bx
	pop ax
	jmp read_track

/*
 * This procedure turns off the floppy drive motor, so
 * that we enter the kernel in a known state, and
 * don't have to worry about it later.
 */

! 用于关闭软驱电动机，这样在进入内核后它处于已知状态，以后就无需担心他了
kill_motor:
	push dx
	mov dx,#0x3f2 ! 软驱控制卡的驱动端口，只写
	mov al,#0 ! A 驱动器，关闭 FDC，禁止 DMA 和中断请求，关闭电动机
	outb ! 将 al 中的内容输出到 dx 指定的端口去
	pop dx
	ret

sectors:
	.word 0 ! 存放当前启动软盘每磁道的扇区数

msg1:
	.byte 13,10 ! 回车换行的 ASC II 码
	.ascii "Loading system ..."
	.byte 13,10,13,10 ! 共 24 个 ASC II 码字符

.org 508 ! 表示下列语句从地址 508（0x1FC）开始，所以 root_dev 在启动扇区第 508 开始的 2B 中
root_dev:
	.word ROOT_DEV ! 这里存放根文件系统所在的设备号
boot_flag:
	.word 0xAA55 ! 硬盘有效标识

.text
endtext:
.data
enddata:
.bss
endbss:
