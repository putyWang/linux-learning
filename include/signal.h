#ifndef _SIGNAL_H
#define _SIGNAL_H

#include <sys/types.h>

typedef int sig_atomic_t; // 定义了信号原子操作类型
typedef unsigned int sigset_t;		/* 32 bits */ // 定义信号集类型

#define _NSIG             32 // 定义信号种类，32种
#define NSIG		_NSIG // 定义信号集类型
// linux 0.11 已定义的22个信号
#define SIGHUP		 1 // 挂断控制终端或进程
#define SIGINT		 2 // 来自键盘的中断
#define SIGQUIT		 3 // 来自键盘的退出
#define SIGILL		 4 // 非法指令
#define SIGTRAP		 5 // 跟踪断点
#define SIGABRT		 6 // 异常结束
#define SIGIOT		 6
#define SIGUNUSED	 7 // 没有使用
#define SIGFPE		 8 // 协处理器出错
#define SIGKILL		 9 // 强迫进程停止
#define SIGUSR1		10 // 用户信号1，进程可使用
#define SIGSEGV		11 // 无效内存引用
#define SIGUSR2		12 // 用户信号2， 进程可使用
#define SIGPIPE		13 // 管道写出错，无读者
#define SIGALRM		14 // 实时定时器报警
#define SIGTERM		15 // 进程终止
#define SIGSTKFLT	16 // 栈出错（协处理器）
#define SIGCHLD		17 // 子进程停止或被终止
#define SIGCONT		18 // 恢复进程继续执行
#define SIGSTOP		19 // 停止进程的执行
#define SIGTSTP		20 // tty 发出停止进程，可忽略
#define SIGTTIN		21 // 后台进程请求输入
#define SIGTTOU		22 // 后台进程请求输出

/* Ok, I haven't implemented sigactions, but trying to keep headers POSIX */
#define SA_NOCLDSTOP	1 // 当子进程处于停止状态，就不对 SIGCHLD 进行处理 
#define SA_NOMASK	0x40000000 // 不阻止在指定的信号处理程序（信号句柄）中再收到该信号
#define SA_ONESHOT	0x80000000 // 信号句柄一旦被调用过就恢复到默认处理句柄

// 以下三个常量用于 sigprocmask() 函数改变阻塞信号集（屏蔽码），这些参数会改变该函数的行为
#define SIG_BLOCK          0	// 阻塞信号集中加上指定信号集
#define SIG_UNBLOCK        1	// 阻塞信号集中删除指定信号集
#define SIG_SETMASK        2	// 设置阻塞信号集（信号屏蔽码）

#define SIG_DFL		((void (*)(int))0)	/* default signal handling */ // 默认信号处理程序（信号句柄）
#define SIG_IGN		((void (*)(int))1)	/* ignore signal */ // 设置阻塞信号集（信号屏蔽码）

/**
 * 信号数据结构、
 * 引起触发信号处理的信号也将被阻塞，除非使用 SA_NOMASK 标识
*/
struct sigaction {
	// 对应某信号指定要采取的行为，可以是 SIG_DFL、SIG_IGN来忽略该信号 也可以是指向处理该信号函数的一个指针
	void (*sa_handler)(int);
	// 给出对信号的屏蔽码，在执行时将会屏蔽这些信号的处理
	sigset_t sa_mask;
	// 指定改变信号处理过程的信号集（由SA_NOCLDSTOP、SA_NOMASK与SA_ONESHOT所定义）
	int sa_flags;
	// 回复函数指针，由函数库提供，用于清理用户堆栈
	void (*sa_restorer)(void);
};

void (*signal(int _sig, void (*_func)(int)))(int); // 为信号 _sig 安装一个新的信号处理程序（信号句柄），与 sigaction() 类似
int raise(int sig); // 向当前进程发送一个信号、作用等价于 kill(getpid(),sig)
int kill(pid_t pid, int sig); // 可用于向任何进程组或进程发送任何信号
int sigaddset(sigset_t *mask, int signo); // 向信号集中添加信号
int sigdelset(sigset_t *mask, int signo); // 从信号值中去除指定的信号
int sigemptyset(sigset_t *mask); // 从信号集中清除指定信号集
int sigfillset(sigset_t *mask); // 向信号集中置入所有信号
int sigismember(sigset_t *mask, int signo); // 判断一个信号是不是信号集中的，1-是，0-不是，4-出错
int sigpending(sigset_t *set); // 对 set 中的信号进行检测，看是否有挂起的信号
int sigprocmask(int how, sigset_t *set, sigset_t *oldset); // 改变目前的被阻塞信号集
int sigsuspend(sigset_t *sigmask); // 用 sigmask 临时替换进程的信号屏蔽码，然后暂停该进程直到收到一个信号
int sigaction(int sig, struct sigaction *act, struct sigaction *oldact); // 用于改变进程在收到指定信号时所采取的行动

#endif /* _SIGNAL_H */
