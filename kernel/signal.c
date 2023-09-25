/*
 *  linux/kernel/signal.c
 *
 *  (C) 1991  Linus Torvalds
 */

#include <linux/sched.h>
#include <linux/kernel.h>
#include <asm/segment.h>

#include <signal.h>

volatile void do_exit(int error_code);

/**
 * 获取当前进程信号屏蔽码
 * @return 进程信号屏蔽码，对应信号位图
*/
int sys_sgetmask()
{
	return current->blocked;
}

/**
 * 为当前进程设置新的进程信号屏蔽码
 * @param newmask 新进程信号屏蔽码
 * @return 旧的进程信号屏蔽码
*/
int sys_ssetmask(int newmask)
{
	int old=current->blocked;

	current->blocked = newmask & ~(1<<(SIGKILL-1));
	return old;
}

/**
 * 将 sigaction 数据复制到 fs 段的 to 地址处
 * @param from sigaction 指针
 * @param to fs 段中地址
*/
static inline void save_old(char * from,char * to)
{
	int i;

	verify_area(to, sizeof(struct sigaction)); // 验证 to 处的内存是否足够
	for (i=0 ; i< sizeof(struct sigaction) ; i++) { 
		put_fs_byte(*from,to); // 复制到 fs 段，一般为用户数据段
		from++;
		to++;
	}
}

/**
 * 将 sigaction 从 fs 段的 from 地址处复制到 to 地址处 
 * @param from sigaction 指针
 * @param to fs 段中地址
*/
static inline void get_new(char * from,char * to)
{
	int i;

	for (i=0 ; i< sizeof(struct sigaction) ; i++)
		*(to++) = get_fs_byte(from++);
}

/**
 * 为指定信号安装新的信号句柄（信号处理程序）
 * @param sigum 指定的信号
 * @param handler 指定的句柄
 * @param restorer 恢复函数指针
 * @return 失败：-1，成功返回旧信号句柄
*/
int sys_signal(int signum, long handler, long restorer)
{
	struct sigaction tmp;

	// 信号值需要在 1～32 的范围内，且 SIGKILL 信号句柄不可改变
	if (signum<1 || signum>32 || signum==SIGKILL)
		return -1;
	// 设置新信号参数
	tmp.sa_handler = (void (*)(int)) handler;
	tmp.sa_mask = 0;
	tmp.sa_flags = SA_ONESHOT | SA_NOMASK; // 该信号只使用一次就恢复默认值
	tmp.sa_restorer = (void (*)(void)) restorer;
	// 将当前进程指定信号修改为上述完善的，同时返回旧句柄
	handler = (long) current->sigaction[signum-1].sa_handler;
	current->sigaction[signum-1] = tmp;
	return handler;
}

/**
 * sigaction() 函数的系统调用，改变进程在收到一个信号时的操作
 * @param sigum 指定的信号
 * @param action 新操作
 * @param oldaction 原操作
 * @return 失败：-1，成功：0
*/
int sys_sigaction(int signum, const struct sigaction * action,
	struct sigaction * oldaction)
{
	struct sigaction tmp;

	// 信号值需要在 1～32 的范围内，且 SIGKILL 信号句柄不可改变
	if (signum<1 || signum>32 || signum==SIGKILL)
		return -1;
	tmp = current->sigaction[signum-1]; // tmp 暂时保存旧操作
	// 将 action 安装到 指定信号处
	get_new((char *) action,
		(char *) (signum-1+current->sigaction));
	// oldaction 不为空时，将原操作保存到 oldaction 指针处
	if (oldaction)
		save_old((char *) &tmp,(char *) oldaction);
	// 如果允许信号在自己的信号句柄中收到，则令屏蔽码为0，否则设置屏蔽本信号
	if (current->sigaction[signum-1].sa_flags & SA_NOMASK)
		current->sigaction[signum-1].sa_mask = 0;
	else
		current->sigaction[signum-1].sa_mask |= (1<<(signum-1));
	return 0;
}

/**
 * 系统调用中断处理程序中真正的信号处理程序
 * @param signr 指定的信号
*/
void do_signal(long signr,long eax, long ebx, long ecx, long edx,
	long fs, long es, long ds,
	long eip, long cs, long eflags,
	unsigned long * esp, long ss)
{
	unsigned long sa_handler;
	long old_eip=eip;
	struct sigaction * sa = current->sigaction + signr - 1;
	int longs;
	unsigned long * tmp_esp;

	sa_handler = (unsigned long) sa->sa_handler;
	// 信号句柄为 SIG_IGN (忽略)，直接返回
	if (sa_handler==1)
		return;
	// 信号句柄为 SIG_DFL (默认处理)，信号为 SIGCHLD 直接返回，其他情况进程退出
	if (!sa_handler) {
		if (signr==SIGCHLD)
			return;
		else
			do_exit(1<<(signr-1));
	}
	// 如果该信号只能服务一次，则将 信号句柄设置为空
	if (sa->sa_flags & SA_ONESHOT)
		sa->sa_handler = NULL;
	*(&eip) = sa_handler; // 用户调用系统调用的代码指针 eip 指向该信号处理句柄
	longs = (sa->sa_flags & SA_NOMASK)?7:8; // 如果允许信号自己的处理句柄收到信号自己，则需要将进程的阻塞码也压入堆栈
	*(&esp) -= longs; // 堆栈指针向下移动指定长度以便之后将参数压入堆栈
	verify_area(esp,longs*4); // 验证 esp 指定内存是否足够
	tmp_esp=esp; // esp 暂存
	put_fs_long((long) sa->sa_restorer,tmp_esp++); // 首先将恢复函数压在最外层
	put_fs_long(signr,tmp_esp++); // 然后设置信号
	if (!(sa->sa_flags & SA_NOMASK))
		put_fs_long(current->blocked,tmp_esp++); // 设置屏蔽码
	put_fs_long(eax,tmp_esp++);
	put_fs_long(ecx,tmp_esp++);
	put_fs_long(edx,tmp_esp++);
	put_fs_long(eflags,tmp_esp++);
	put_fs_long(old_eip,tmp_esp++);
	current->blocked |= sa->sa_mask; // 进程阻塞码的码位添加上 sa_mask 中的码位
}
