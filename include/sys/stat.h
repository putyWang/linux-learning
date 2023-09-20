#ifndef _SYS_STAT_H
#define _SYS_STAT_H

#include <sys/types.h>

/**
 * 设备文件的状态信息结构
*/
struct stat {
	dev_t	st_dev;   // 含有文件的设备号
	ino_t	st_ino;   // 文件 i 节点号
	umode_t	st_mode;  // 文件属性
	nlink_t	st_nlink; // 指定文件的链接数
	uid_t	st_uid;   // 文件的用户（标识）号
	gid_t	st_gid;   // 文件的组号
	dev_t	st_rdev;  // 设备号（如果文件是特殊的字符文件或块文件）
	off_t	st_size;  // 文件大小（字节数）（如果文件是常规文件）
	time_t	st_atime; // 上次（最后）访问时间
	time_t	st_mtime; // 最后修改时间
	time_t	st_ctime; // 最后节点修改时间
};

// 以下这些是 st_mode 值的符号名称
// 文件类型
#define S_IFMT  00170000 // 文件类型（8 进制表示）
#define S_IFREG  0100000 // 常规文件
#define S_IFBLK  0060000 // 块特殊（设备）文件
#define S_IFDIR  0040000 // 目录文件
#define S_IFCHR  0020000 // 字符设备文件
#define S_IFIFO  0010000 // FIFO 特殊文件
// 文件属性位
#define S_ISUID  0004000 // 执行时设置用户 ID（set-user-ID）
#define S_ISGID  0002000 // 执行时设置组 ID
#define S_ISVTX  0001000 // 对于目录，受限删除标志

#define S_ISREG(m)	(((m) & S_IFMT) == S_IFREG) // 调试是否为常规文件
#define S_ISDIR(m)	(((m) & S_IFMT) == S_IFDIR) // 是否为目录文件
#define S_ISCHR(m)	(((m) & S_IFMT) == S_IFCHR) // 是否为字符设备文件
#define S_ISBLK(m)	(((m) & S_IFMT) == S_IFBLK) // 是否为块设备文件
#define S_ISFIFO(m)	(((m) & S_IFMT) == S_IFIFO) // 是否为 FIFO 特殊文件

#define S_IRWXU 00700 // 宿主可以读、写、执行/搜索
#define S_IRUSR 00400 // 宿主读许可
#define S_IWUSR 00200 // 宿主写许可
#define S_IXUSR 00100 // 宿主执行/搜索许可

#define S_IRWXG 00070 // 组成员可以读、写、执行/搜索
#define S_IRGRP 00040 // 组成员读许可
#define S_IWGRP 00020 // 组成员写许可
#define S_IXGRP 00010 // 组成员执行/搜索许可

#define S_IRWXO 00007 // 其他人读、写、执行/搜索许可
#define S_IROTH 00004 // 其他人读许可
#define S_IWOTH 00002 // 其他人写许可
#define S_IXOTH 00001 // 其他人执行/搜索许可

extern int chmod(const char *_path, mode_t mode);
extern int fstat(int fildes, struct stat *stat_buf);
extern int mkdir(const char *_path, mode_t mode);
extern int mkfifo(const char *_path, mode_t mode);
extern int stat(const char *filename, struct stat *stat_buf);
extern mode_t umask(mode_t mask);

#endif
