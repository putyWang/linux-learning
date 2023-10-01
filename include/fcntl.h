#ifndef _FCNTL_H
#define _FCNTL_H

#include <sys/types.h>

/* open/fcntl - NOCTTY, NDELAY isn't implemented yet */
#define O_ACCMODE	00003 // 文件访问模式屏蔽码
// 打开文件 open() 与文件控制 fcntl() 函数使用的文件访问模式，只能使用下列三个参数之一
#define O_RDONLY	   00 // 以只读方式打开文件
#define O_WRONLY	   01 // 以只写方式打开文件
#define O_RDWR		   02 // 以读写方式打开文件
// 下面是文件创建标志，用于 open() ，可以与上面三个参数使用 "位或" 的方式进行组合使用
#define O_CREAT		00100	/* not fcntl */ // 如果文件不存在时就进行创建
#define O_EXCL		00200	/* not fcntl */ // 独占使用文件标志
#define O_NOCTTY	00400	/* not fcntl */ // 不分配控制终端
#define O_TRUNC		01000	// 若文件已存在且为写操作，则长度截为 0
#define O_APPEND	02000 	// 以添加的方式打开，文件指针置位文件尾
#define O_NONBLOCK	04000	/* not fcntl */ // 非阻塞的方式打开或者操作文件
#define O_NDELAY	O_NONBLOCK // 非阻塞的方式打开或者操作文件 

/**
 * 下面定义了 fcntl() 函数的相关命令
 */
#define F_DUPFD		0	/* dup */ // 复制文件句柄最小数值的句柄
#define F_GETFD		1	/* get f_flags */ // 取文件句柄标志
#define F_SETFD		2	/* set f_flags */ // 设置文件句柄标志
#define F_GETFL		3	/* more flags (cloexec) */ // 取文件状态标志与访问模式
#define F_SETFL		4   // 设置文件状态标志与访问模式
// 下面是文件锁定命令，fcntl() 函数第三个参数 lock 是指向 flock 结构的指针
#define F_GETLK		5	/* not implemented */ // 返回阻止锁定的 flock 结构
#define F_SETLK		6   // 设置（F_RDLCK 或 F_WRLCK） 或清除（F_UNLCK）锁定
#define F_SETLKW	7   // 等待设置或者清除锁定

/* for F_[GET|SET]FL */
// 执行 exec() 蔟函数式关闭文件句柄（执行时关闭）
#define FD_CLOEXEC	1	/* actually anything with low bit set goes */

/* Ok, these are locking features, and aren't implemented at any
 * level. POSIX wants them.
 */
// 锁定类型
#define F_RDLCK		0 // 共享或读文件锁定
#define F_WRLCK		1 // 独占或写文件锁定
#define F_UNLCK		2 // 文件解锁

/* Once again - not implemented, but ... */
/**
 * 文件锁定操作数据结构
*/
struct flock {
	short l_type; // 锁定类型（值为上述三个锁定类型常数）
	short l_whence; // 开始偏移（SEEK_SET、SEEK_CUR 或 SEEK_END）
	off_t l_start; // 阻塞锁定的开始处，相对偏移
	off_t l_len; // 阻塞锁定的大小，如果是 0 则为到文件末尾
	pid_t l_pid; // 加锁进程 id
};

// 创建新文件或重写一个已存在文件函数原型
extern int creat(const char * filename,mode_t mode);
// 文件句柄操作的函数原型
extern int fcntl(int fildes,int cmd, ...);
// 打开文件原型
extern int open(const char * filename, int flags, ...);

#endif
