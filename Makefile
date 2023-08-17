# 是否使用 RAM虚拟盘 
# 使用的话在该地方指定大小
RAMDISK = #-DRAMDISK=512

# as86 的汇编编译器与连接器，-0 生成 8086 目标程序， -a 表示生成与 gas、gld 生成兼容代码
AS86	=as86 -0 -a
LD86	=ld86 -0

# gas 为 GNU 汇编编译器
AS	=gas
# gld 为 GNU 汇编连接器
LD	=gld
# GNU 连接器用到的参数，-s 输出文件中省略所有符号信息， -x 删除所有局部符号；-M 表示在标准输出设备上打印链接映像
LDFLAGS	=-s -x -M

# gcc 是 GNU c程序编译器 在 unix 系统脚本程序中，引用已定义标识符时，使用 $() 将指定标识符包裹起来
CC	=gcc $(RAMDISK)
# gcc 命令参数；-wall 表示打印所有警告; -O 表示对代码进行优化; -fstrength-reduce 表示优化循环语句; 
# -fomit-frame-pointer 不要将指针保存在寄存器中，删除; -fcombine-regs 现阶段已废除选项; -mstring-insns 是 linus 自己为 gcc 程序添加的优化字符串的选项，可去掉;
CFLAGS	=-Wall -O -fstrength-reduce -fomit-frame-pointer \
-fcombine-regs -mstring-insns
# cpp 是在 gcc 前使用的预处理程序； -nostdinc 与 -Iinclude 表示不要搜索标准目录中的头文件；-I 则表示搜索当前或指定目录中的头文件
CPP	=cpp -nostdinc -Iinclude

# 指定生成镜像文件默认根目录所在设备，可以是软盘；空着时默认使用 /dev/hd6
ROOT_DEV=/dev/hd6

# kernel mm fs 目录生成镜像文件默认产生位置
ARCHIVES=kernel/kernel.o mm/mm.o fs/fs.o
# 块和字符设备库文件；.a 表示是个包含多个可执行二进制代码子程序集合的库文件，通常使用 GNU 的 ar 程序生成
# ar 是 GNU 的 二进制文件处理程序，用于创建，修改以及从归档文件中抽取文件
DRIVERS =kernel/blk_drv/blk_drv.a kernel/chr_drv/chr_drv.a
# 数学运算库文件
MATH	=kernel/math/math.a
# 由 lib 目录中文件编译生成的库文件
LIBS	=lib/lib.a

# 老式 make 隐式后缀规则，表示使用指定命令将所有 .c 文件生成 .s 汇编程序
# -S 表示对代码适当编码后不进行汇编就停止；-o 表示输出文件格式；$*.s 表示自动目标标量；$< 替换为第一个先决条件，此处为 .c
.c.s:
	$(CC) $(CFLAGS) \
	-nostdinc -Iinclude -S -o $*.s $< 
# 将所有 .s 汇编程序编译成 .o 目标文件；-c 表示只编译不链接
.s.o:
	$(AS) -c -o $*.o $<
# 将所有 .c 文件编译成 .o 目标文件，同样 -c 也表示不链接
.c.o:
	$(CC) $(CFLAGS) \
	-nostdinc -Iinclude -c -o $*.o $<

# all 表示创建 makeFile 最顶层目标；
all:	Image

# 第一行指定最顶层目标文件 Image 产生源文件
# 第二行表示使用 tools 目录下的 build 工具程序组装成 ROOT_DEV 目录下的 Image 镜像文件
# 第三行 sync 命令表示 缓冲块数据立即写盘并更新超级块
Image: boot/bootsect boot/setup tools/system tools/build
	tools/build boot/bootsect boot/setup tools/system $(ROOT_DEV) > Image
	sync

# 第一行 表示 disk 是由 Image 镜像文件生成
# 第二行 dd 为 UNIX 标准命令：复制文件根据选项进行转化及格式化；bs= 表示一次读/写字节数；if= 表示输入的文件；of= 表示输出到的文件，此处为一个软盘
disk: Image
	dd bs=8192 if=Image of=/dev/PS0

tools/build: tools/build.c
	$(CC) $(CFLAGS) \
	-o tools/build tools/build.c

# 使用上面的 .s.o规则将 head.s 编译成 head.o 文件
boot/head.o: boot/head.s

# 第一行指定 tools/system 文件的源文件
# 第二行则表示 将 所有指定原镜像与库文件链接生成 tools/system；>System.map 表明 gld 需要将链接镜像文件保存在 System.map 文件中
tools/system:	boot/head.o init/main.o \
		$(ARCHIVES) $(DRIVERS) $(MATH) $(LIBS)
	$(LD) $(LDFLAGS) boot/head.o init/main.o \
	$(ARCHIVES) \
	$(DRIVERS) \
	$(MATH) \
	$(LIBS) \
	-o tools/system > System.map

# kernel/math/math.a 由下一行命令实现
kernel/math/math.a:
	(cd kernel/math; make)

# kernel/blk_drv/blk_drv.a 由下一行命令实现
kernel/blk_drv/blk_drv.a:
	(cd kernel/blk_drv; make)

# kernel/chr_drv/chr_drv.a 由下一行命令实现
kernel/chr_drv/chr_drv.a:
	(cd kernel/chr_drv; make)

# kernel/kernel.o 由下一行命令实现
kernel/kernel.o:
	(cd kernel; make)

# mm/mm.o 由下一行命令实现
mm/mm.o:
	(cd mm; make)

# fs/fs.o 由下一行命令实现
fs/fs.o:
	(cd fs; make)

# lib/lib.o 由下一行命令实现
lib/lib.a: 
	(cd lib; make)

# -s 表示去掉文件中的符号信息
boot/setup: boot/setup.s
	$(AS86) -o boot/setup.o boot/setup.s
	$(LD86) -s -o boot/setup boot/setup.o

boot/bootsect:	boot/bootsect.s
	$(AS86) -o boot/bootsect.o boot/bootsect.s
	$(LD86) -s -o boot/bootsect boot/bootsect.o

# 在 bootsect.s 程序开头添加 system 长度信息
tmp.s:	boot/bootsect.s tools/system
	(echo -n "SYSSIZE = (";ls -l tools/system | grep system \
		| cut -c25-31 | tr '\012' ' '; echo "+ 15 ) / 16") > tmp.s
	cat boot/bootsect.s >> tmp.s

# make clean 命令执行的代码
clean:
	rm -f Image System.map tmp_make core boot/bootsect boot/setup
	rm -f init/*.o tools/system tools/build boot/*.o
	(cd mm;make clean)
	(cd fs;make clean)
	(cd kernel;make clean)
	(cd lib;make clean)

# make backup 首先执行 clean 规则，然后对 linux 文件夹进行压缩 变成 backup.Z文件
backup: clean
	(cd .. ; tar cf - linux | compress - > backup.Z)
	sync

# 用于个文件间依赖关系，确定文件是否该重新编译
dep:
	sed '/\#\#\# Dependencies/q' < Makefile > tmp_make
	(for i in init/*.c;do echo -n "init/";$(CPP) -M $$i;done) >> tmp_make
	cp tmp_make Makefile
	(cd fs; make dep)
	(cd kernel; make dep)
	(cd mm; make dep)

### Dependencies:
init/main.o : init/main.c include/unistd.h include/sys/stat.h \
  include/sys/types.h include/sys/times.h include/sys/utsname.h \
  include/utime.h include/time.h include/linux/tty.h include/termios.h \
  include/linux/sched.h include/linux/head.h include/linux/fs.h \
  include/linux/mm.h include/signal.h include/asm/system.h include/asm/io.h \
  include/stddef.h include/stdarg.h include/fcntl.h 
