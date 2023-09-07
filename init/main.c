/*
 *  linux/init/main.c
 *
 *  (C) 1991  Linus Torvalds
 */
// 定义该变量为了包括定义在 unistd.h 中的内嵌汇编代码等信息
#define __LIBRARY__
#include <unistd.h>
 /*
	*.h 头文件所在默认目录是 include/，则在代码中不用明确指出位置；
	当不是 unix 标准头文件，则需要指明所在目录，并用双引号括住；
	标准符号常数与类型文件：该文件定义了各种符号常数和类型，并声明了各种函数；
	如果定义了 __LIBRARY__ 变量，则还包括系统调用号和内嵌汇编；
 */
#include <time.h> // 时间类型头文件，主要定义了 tm 结构和一些有关时间的函数圆形

/*
 * we need this inline - forking from kernel space will result
 * in NO COPY ON WRITE (!!!), until an execve is executed. This
 * is no problem, but for the stack. This is handled by not letting
 * main() use the stack at all after fork(). Thus, no function
 * calls - which means inline code for fork too, as otherwise we
 * would use the stack upon exit from 'fork()'.
 *
 * Actually only pause and fork are needed inline, so that there
 * won't be any messing with the stack from main(), but we define
 * some others too.
 */
/*
	由于从内核空间创建进程将导致直到执行下一个 execve 之前没有写时复制，可能会导致堆栈出现问题
	因此在 fork() 调用之后不让 main() 函数使用堆栈；
	因为该原因，不能出现函数调用，就连 fork 也需要使用内嵌代码，否则在从 fork 推出时就要使用堆栈了；
	实际上只有 pause 与 fork 需要使用内嵌方式，以保证 main() 不会弄乱堆栈，同时我们还定义了其他一些内嵌宏函数；
*/
// _syscall 为 unistd.h 内嵌，以汇编代码的形式调用linux系统中断 0x80(所有系统调用的入口)；
// inline 关键字用于定义内联函数
// 第一条语句其实是 int fork() 创建进程系统调用；
// _syscall0 以 0 结尾表示没有参数
static inline _syscall0(int,fork)
static inline _syscall0(int,pause) // int pause() 系统调用：暂停进程的执行，直到收到一个信号
static inline _syscall1(int,setup,void *,BIOS) // int setup(void * BIOS) 系统调用
static inline _syscall0(int,sync) // int sync() 系统调用

#include <linux/tty.h> // tty 头文件，定义了有关 tty_io, 串行通信方面的参数、常数
#include <linux/sched.h> // 调度程序头文件，定义了任务结构 task_strucy、第一个初始任务的数据，还有一些以宏的形式定义的有关描述符参数设置和获取的嵌入式汇编函数程序；
#include <linux/head.h> // head头文件，定义了段描述符的简单结构，和几个选择符常量
#include <asm/system.h> // 系统头文件，以宏的形式定义了许多有关的设置或修改，描述符/中断门等的嵌入式汇编子程序
#include <asm/io.h> // io 头文件，以宏的嵌入汇编程序形式定义对 io 端口操作的函数

#include <stddef.h> // 标准定义头文件，定义了 NULL，offsetof(TYPE, MEMBER)
#include <stdarg.h> // 标准参数头文件，以宏的形式定义变量参数列表；主要说明了一个类型（va_list）和三个宏（va_start、va_arg 与 va_end），vsprintf、vprintf、vfprintf
#include <unistd.h>
#include <fcntl.h> // 文件控制头文件，用于文件及其描述符的操作控制常数符号的定义
#include <sys/types.h> // 类型头文件，定义了基本的系统数据类型

#include <linux/fs.h> // 文件系统头文件，定义文件表结构（file、buffer_head、m_inode等）

static char printbuf[1024]; // 内核显示信息的字符数组缓存

extern int vsprintf(); // 送格式化输出到一字符串之中
extern void init(void); // 函数原型，初始化
extern void blk_dev_init(void); // 块设备初始化子程序
extern void chr_dev_init(void); // 字符设备初始化子程序
extern void hd_init(void); // 硬盘初始化子程序
extern void floppy_init(void); // 软驱初始化子程序
extern void mem_init(long start, long end); // 内存管理初始化子程序
extern long rd_init(long mem_start, int length); // 虚拟盘初始化子程序
extern long kernel_mktime(struct tm * tm); // 建立内核时间

extern long startup_time; // 内核启动时间变量

/*
 * This is set up by the setup-routine at boot-time
 */
// 1Mb 之后扩展内存大小
#define EXT_MEM_K (*(unsigned short *)0x90002)
// 硬盘参数基址
#define DRIVE_INFO (*(struct drive_info *)0x90080)
// 根文件系统所在的设备号
#define ORIG_ROOT_DEV (*(unsigned short *)0x901FC)

/*
 * Yeah, yeah, it's ugly, but I cannot find how to do this correctly
 * and this seems to work. I anybody has more info on the real-time
 * clock I'd be interested. Most of this was trial and error, and some
 * bios-listing reading. Urghh.
 */
// 实现对 CMOS 时间的读取
#define CMOS_READ(addr) ({ \
// 将  0x80|addr 内存地址 发送到 0x70 端口以获取对应时间
outb_p(0x80|addr,0x70); \
// 接收发送信号的返回
inb_p(0x71); \
})

// 将bcd转化为数字
#define BCD_TO_BIN(val) ((val)=((val)&15) + ((val)>>4)*10)

/*
	内核时间初始化
*/
static void time_init(void)
{
	struct tm time;
	// 控制时间误差在一秒内
	do {
		time.tm_sec = CMOS_READ(0);
		time.tm_min = CMOS_READ(2);
		time.tm_hour = CMOS_READ(4);
		time.tm_mday = CMOS_READ(7);
		time.tm_mon = CMOS_READ(8);
		time.tm_year = CMOS_READ(9);
	} while (time.tm_sec != CMOS_READ(0));

	BCD_TO_BIN(time.tm_sec);
	BCD_TO_BIN(time.tm_min);
	BCD_TO_BIN(time.tm_hour);
	BCD_TO_BIN(time.tm_mday);
	BCD_TO_BIN(time.tm_mon);
	BCD_TO_BIN(time.tm_year);
	time.tm_mon--; // 月份范围为 0～11
	// 设置启动时间，单位 秒
	startup_time = kernel_mktime(&time);
}

// 机器具有的机械内存
static long memory_end = 0;
// 高速缓存去末端地址
static long buffer_memory_end = 0;
// 主内存起始地址
static long main_memory_start = 0;

struct drive_info { char dummy[32]; } drive_info; // 用于存放硬盘参数表信息

/*
* 系统初始化入口(引导程序在系统加载到内存后进行调用)
*/
void main(void)		/* This really IS void, no error here. */
{			/* The startup routine assumes (well, ...) this */
/*
 * Interrupts are still disabled. Do necessary setups, then
 * enable them
 */
	// 设置机器根目录所在设备号
 	ROOT_DEV = ORIG_ROOT_DEV;
	// 设置硬盘相关信息
 	drive_info = DRIVE_INFO;
	// 计算机器实际内存大小，首先使用额外内存加上 1Mb
	// 忽略不到 1页（4KB）的内存数
	memory_end = (1<<20) + (EXT_MEM_K<<10);
	memory_end &= 0xfffff000;
	// 内存最大设置为 16MB
	if (memory_end > 16*1024*1024)
		memory_end = 16*1024*1024;
	// 根据内存大小设置 缓冲区大小
	if (memory_end > 12*1024*1024) 
		buffer_memory_end = 4*1024*1024;
	else if (memory_end > 6*1024*1024)
		buffer_memory_end = 2*1024*1024;
	else
		buffer_memory_end = 1*1024*1024;
	// 将主内存起始地址与缓冲区末端地址对齐
	main_memory_start = buffer_memory_end;
	// 虚拟盘存在时，主内存需流出虚拟盘所在内存
#ifdef RAMDISK
	main_memory_start += rd_init(main_memory_start, RAMDISK*1024);
#endif
	/*
		下面是对内核进行初始化工作
	*/
	// 初始化主内存
	mem_init(main_memory_start,memory_end);
	// 陷阱门初始化
	trap_init();
	// 块设备初始化
	blk_dev_init();
	chr_dev_init();
	tty_init(); // 终端初始化
	time_init();
	sched_init(); 
	buffer_init(buffer_memory_end);
	hd_init();
	floppy_init();
	sti(); // 初始化完成 重新开启中断
	// 下面过程通过在堆栈中设置参数，利用中断返回指令启动第一个任务
	move_to_user_mode(); // 移动到用户模式运行
	if (!fork()) {		/* we count on this going ok */
		init(); // 在新建的子进程中运行
	}
/*
 *   NOTE!!   For any other task 'pause()' would mean we have to get a
 * signal to awaken, but task0 is the sole exception (see 'schedule()')
 * as task 0 gets activated at every idle moment (when no other tasks
 * can run). For task0 'pause()' just means we go check if some other
 * task can run, and if not we return here.
 */
/**
 * 对于其他线程来说，pause() 函数将意味着我们必须等收到一个信号才会返回就绪运行态，
 * 但是任务0，是一个例外，因为任务 0 在 任何空闲时间里都会被激活（当没有其他任务运行时），
 * 因此对于任务 0，pause() 函数仅仅意味着我们可以回来查看是否还有其他任务可以运行，没有的话就在这里一值循环
*/
	for(;;) pause();
}

/**
 * prointf 程序产生格式化信息并输出到标准输出设备 stdout(1) ，这里指的是在屏幕上展示；
 * 参数 *fmt 指定输出采取的格式；
 * 该子程序为 vsprintf 函数 使用的一个例子
*/
static int printf(const char *fmt, ...)
{
	va_list args;
	int i;

	va_start(args, fmt);
	write(1,printbuf,i=vsprintf(printbuf, fmt, args));
	va_end(args);
	return i;
}

// 调用执行程序时参数的字符串数组
static char * argv_rc[] = { "/bin/sh", NULL };
// 调用执行程序时的环境字符串数组
static char * envp_rc[] = { "HOME=/", NULL };

static char * argv[] = { "-/bin/sh",NULL };
static char * envp[] = { "HOME=/usr/root", NULL };

// 下列程序时函数运行在任务 0 创建的子进程 1 中；
// 首先对第一个要执行的程序 shell 的环境进行初始化，然后加载该程序并执行之
void init(void)
{
	int pid,i;

// 读取硬盘参数包括分区信息并建立系统虚拟盘和安装根文件系统设备；
// 对应的函数为 kernel/blk_drv/hc.c 中的 sys_setUp
	setup((void *) &drive_info);
	// 用读写的访问方式打开设备 /dev/tty0 即终端控制台，返回的句柄号 0 -- stdin 标准输入设备
	(void) open("/dev/tty0",O_RDWR,0);
	(void) dup(0); // 复制句柄 产生句柄 1 号 标准输出设备
	(void) dup(0); // 复制句柄 产生句柄 2 号 标准出错输出设备
	// 打印缓冲区块数与总字节数，每块1024字节
	printf("%d buffers = %d bytes buffer space\n\r",NR_BUFFERS,
		NR_BUFFERS*BLOCK_SIZE);
	// 打印当前主内存空闲字节数
	printf("Free mem: %d bytes\n\r",memory_end-main_memory_start);
	// 下面 fork() 用于创建一个子进程，该子进程关闭了句柄 0（stdin），以只读方式打开 /etc/rc 文件，并执行 /bin/sh 程序，所带参数和环境变量分别由 arg_rc 和 envp_rc 数组给出
	if (!(pid=fork())) {
		close(0); // 子进程关闭文件 0
		if (open("/etc/rc",O_RDONLY,0)) // 使用 只读的方式打开 "/etc/rc" 文件
			_exit(1); // 打开失败，则退出
		execve("/bin/sh",argv_rc,envp_rc);
		_exit(2); // execve() 执行失败则退出
	}
	// 下面if 语句为 进程0执行，&i 存储的是返回状态信息的位置，如果wait 返回值不等于子进程号 则继续等待
	if (pid>0)
		while (pid != wait(&i))
			/* nothing */;
	// 执行到这里时，说明上面创建的子进程已经执行完毕或终止；
	// 下列进程首先创建一个子进程，出错时展示 "Fork failed in init" 并继续创建子进程；
	// 创建的子进程首先关闭之前主进程打开了的三个句柄，然后新创建一个会话并设置进程组号，然后重新打开 "/dev/tty0" 作为 stdin， 并复制生成 stdout，stderr；
	// 使用所选用的另一套参数和环境数组，再次执行系统解释程序 /bin/sh，随后父进程再次运行 wait() 等待；子进程又停止了执行，打印错误信息，重复执行
	while (1) {
		if ((pid=fork())<0) {
			printf("Fork failed in init\r\n");
			continue;
		}
		if (!pid) {
			close(0);close(1);close(2);
			setsid();
			(void) open("/dev/tty0",O_RDWR,0);
			(void) dup(0);
			(void) dup(0);
			_exit(execve("/bin/sh",argv,envp));
		}
		while (1)
			if (pid == wait(&i))
				break;
		printf("\n\rchild %d died with code %04x\n\r",pid,i);
		sync();
	}
	_exit(0);	/* NOTE! _exit, not exit() */
}
