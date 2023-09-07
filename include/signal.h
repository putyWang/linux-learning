#ifndef _SIGNAL_H
#define _SIGNAL_H

#include <sys/types.h>

typedef int sig_atomic_t;
typedef unsigned int sigset_t;		/* 32 bits */

#define _NSIG             32
#define NSIG		_NSIG

#define SIGHUP		 1
#define SIGINT		 2
#define SIGQUIT		 3
#define SIGILL		 4
#define SIGTRAP		 5
#define SIGABRT		 6
#define SIGIOT		 6
#define SIGUNUSED	 7
#define SIGFPE		 8
#define SIGKILL		 9
#define SIGUSR1		10
#define SIGSEGV		11
#define SIGUSR2		12
#define SIGPIPE		13
#define SIGALRM		14
#define SIGTERM		15
#define SIGSTKFLT	16
#define SIGCHLD		17
#define SIGCONT		18
#define SIGSTOP		19
#define SIGTSTP		20
#define SIGTTIN		21
#define SIGTTOU		22

/* Ok, I haven't implemented sigactions, but trying to keep headers POSIX */
#define SA_NOCLDSTOP	1
#define SA_NOMASK	0x40000000
#define SA_ONESHOT	0x80000000

#define SIG_BLOCK          0	/* for blocking signals */
#define SIG_UNBLOCK        1	/* for unblocking signals */
#define SIG_SETMASK        2	/* for setting the signal mask */

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

void (*signal(int _sig, void (*_func)(int)))(int);
int raise(int sig);
int kill(pid_t pid, int sig);
int sigaddset(sigset_t *mask, int signo);
int sigdelset(sigset_t *mask, int signo);
int sigemptyset(sigset_t *mask);
int sigfillset(sigset_t *mask);
int sigismember(sigset_t *mask, int signo); /* 1 - is, 0 - not, -1 error */
int sigpending(sigset_t *set);
int sigprocmask(int how, sigset_t *set, sigset_t *oldset);
int sigsuspend(sigset_t *sigmask);
int sigaction(int sig, struct sigaction *act, struct sigaction *oldact);

#endif /* _SIGNAL_H */
