/*
 * This file has definitions for some important file table
 * structures etc.
 */

#ifndef _FS_H
#define _FS_H

#include <sys/types.h>

/* devices are as follows: (same as minix, so we can use the minix
 * file system. These are major numbers.)
 * 以下为系统的设备（与 MINIX 系统一样，使用 MINIX 的文件系统，以下为主设备号）
 * 0 - unused (nodev) // 没有用到
 * 1 - /dev/mem // 内存
 * 2 - /dev/fd // 软盘设备
 * 3 - /dev/hd // 硬盘设备
 * 4 - /dev/ttyx //tty 串行终端设备
 * 5 - /dev/tty // tty 终端设备
 * 6 - /dev/lp // 打印设备
 * 7 - unnamed pipes // 未命名管道
 */

#define IS_SEEKABLE(x) ((x)>=1 && (x)<=3) // 是不是可以寻找定位的设备

#define READ 0      // 磁盘读命令
#define WRITE 1     // 磁盘写命令码
#define READA 2		// 预读命令
#define WRITEA 3	// 预写命令

void buffer_init(long buffer_end); // 初始化缓冲函数原型

#define MAJOR(a) (((unsigned)(a))>>8) // 取高字节（主设备号）
#define MINOR(a) ((a)&0xff) // 取低字节（次设备号）

#define NAME_LEN 14 // 文件夹名最大长度值
#define ROOT_INO 1  // 根 i 节点

#define I_MAP_SLOTS 8 // i节点位图槽数
#define Z_MAP_SLOTS 8 // 逻辑块（区段块）位图槽数
#define SUPER_MAGIC 0x137F // minix 文件系统中魔数

#define NR_OPEN 20 // 单个进程打开文件数最大值
#define NR_INODE 32 // i 节点数组最大数量
#define NR_FILE 64 // 文件最大数
#define NR_SUPER 8 // 超级块最大数
#define NR_HASH 307 /*缓冲区头哈希表 具有307项*/
#define NR_BUFFERS nr_buffers
#define BLOCK_SIZE 1024  // 数据块长度
#define BLOCK_SIZE_BITS 10 // 数据块长度所占的比特位数
#ifndef NULL
#define NULL ((void *) 0)
#endif

#define INODES_PER_BLOCK ((BLOCK_SIZE)/(sizeof (struct d_inode)))        // 每个块上可存放的 i 节点个数
#define DIR_ENTRIES_PER_BLOCK ((BLOCK_SIZE)/(sizeof (struct dir_entry))) // 每个块上能够存储的目录项数

#define PIPE_HEAD(inode) ((inode).i_zone[0]) // 管道头指针
#define PIPE_TAIL(inode) ((inode).i_zone[1]) // 管道尾指针
#define PIPE_SIZE(inode) ((PIPE_HEAD(inode)-PIPE_TAIL(inode))&(PAGE_SIZE-1)) // 管道大小
#define PIPE_EMPTY(inode) (PIPE_HEAD(inode)==PIPE_TAIL(inode)) // 管道空
#define PIPE_FULL(inode) (PIPE_SIZE(inode)==(PAGE_SIZE-1)) // 管道满
// 管道头指针递增
#define INC_PIPE(head) \
__asm__("incl %0\n\tandl $4095,%0"::"m" (head))

typedef char buffer_block[BLOCK_SIZE]; // 块缓冲区

/**
 * 缓冲区头数据结构，程序中常用 bh 缩写
*/
struct buffer_head {
	char * b_data;			/* pointer to data block (1024 bytes) */ /*指向缓冲块针*/
	unsigned long b_blocknr;	/* block number */ /*块号*/
	unsigned short b_dev;		/* device (0 = free) */ /*数据源设备号*/
	unsigned char b_uptodate; /*更新标志，数据是否已经更新*/
	unsigned char b_dirt;		/* 0-clean,1-dirty */ /*修改标志，0-未修改 1-修改*/
	unsigned char b_count;		/* users using this block */ /*使用的用户数*/
	unsigned char b_lock;		/* 0 - ok, 1 -locked */ /*缓冲区是否已经被锁定*/
	struct task_struct * b_wait; /*指向等待该缓冲区解锁的任务*/
	struct buffer_head * b_prev; /*hash 队列前一块*/
	struct buffer_head * b_next; /*hash 队列后一块*/
	struct buffer_head * b_prev_free; /*空闲表的前一块*/
	struct buffer_head * b_next_free; /*空闲表的后一块*/
};

/**
 * 硬盘上 索引节点（i 节点）数据结构
*/
struct d_inode {
	unsigned short i_mode; /*文件类型与属性*/
	unsigned short i_uid; /*用户 id*/
	unsigned long i_size; /*文件大小*/
	unsigned long i_time; /*修改时间*/
	unsigned char i_gid; /*组 id（文件拥有者所在的组）*/
	unsigned char i_nlinks; //连接数（指向该i节点的目录数）
	unsigned short i_zone[9]; /*直接（0～6）、间接（7）或双重间接（8）逻辑块号，zone 是区的意思，可译成区段，或逻辑号*/
};

/**
 * 内存中的 i 街边结构，前七项与 d_inode 完全一致
*/
struct m_inode {
	unsigned short i_mode; /*文件类型与属性（rwx）*/
	unsigned short i_uid; /*用户 id*/
	unsigned long i_size; /*物理内存页地址*/
	unsigned long i_mtime; /*修改时间*/
	unsigned char i_gid; /*组 id（文件拥有者所在的组）*/
	unsigned char i_nlinks; /*连接数（指向该i节点的目录数）*/
	unsigned short i_zone[9]; /*直接（0～6）、间接（7）或双重间接（8）逻辑块号，zone 是区的意思，可译成区段，或逻辑号*/
/* these are in memory also */
	struct task_struct * i_wait; /*等待该 i 节点的进程*/
	unsigned long i_atime; /*最后访问时间*/
	unsigned long i_ctime; /*i 节点自身修改时间*/
	unsigned short i_dev; /*i 节点所在设备号*/
	unsigned short i_num; /*i 节点号（即是i节点位图中对应位）*/
	unsigned short i_count; /*i 节点被使用的次数 0 表示该节点空闲*/
	unsigned char i_lock; /*锁定标志*/
	unsigned char i_dirt; /*已修改（脏）标志*/
	unsigned char i_pipe; /*管道标志*/
	unsigned char i_mount; /*安装标志*/
	unsigned char i_seek; /*搜寻标志，lseek 时*/
	unsigned char i_update; /*更新标志*/
};

/**
 * 文件数据结构
*/
struct file {
	unsigned short f_mode; // 文件操作模式（RW位）
	unsigned short f_flags; // 标志位
	unsigned short f_count; // 引用计数
	struct m_inode * f_inode; // 文件对应的 i 节点
	off_t f_pos; // 文件位置（读写偏移值）
};

/**
 * 超级块数据结构
 * 存储文件系统信息
*/
struct super_block {
	unsigned short s_ninodes; // i节点数
	unsigned short s_nzones; // 逻辑块数
	unsigned short s_imap_blocks; // i节点位图所占块数
	unsigned short s_zmap_blocks; // 逻辑块位图所占块数
	unsigned short s_firstdatazone; // 第一个逻辑块号
	unsigned short s_log_zone_size; // log2(数据块数/逻辑块)
	unsigned long s_max_size; // 最大文件长度
	unsigned short s_magic; // 文件系统魔数
	// 以下字段仅在内存中存在
	struct buffer_head * s_imap[8]; // i节点位图在高速缓冲块中指针数组
	struct buffer_head * s_zmap[8]; // 逻辑块位图在高速缓冲块指针数组
	unsigned short s_dev; // 超级快所在设备号
	struct m_inode * s_isup; // 被安装文件系统的根目录 i 节点
	struct m_inode * s_imount; // 该文件系统被安装到的 i 节点
	unsigned long s_time; // 修改时间
	struct task_struct * s_wait; // 等待本超级块的进程指针
	unsigned char s_lock; // 锁定标志
	unsigned char s_rd_only; // 只读标志
	unsigned char s_dirt; // 已被修改标志
};

/**
 * 硬盘上超级块结构
*/
struct d_super_block {
	unsigned short s_ninodes; // i节点数
	unsigned short s_nzones; // 逻辑块数
	unsigned short s_imap_blocks; // i节点位图所占块数
	unsigned short s_zmap_blocks; // 逻辑块位图所占块数
	unsigned short s_firstdatazone; // 第一个逻辑块号
	unsigned short s_log_zone_size; // log2(数据块数/逻辑块)
	unsigned long s_max_size; // 最大文件长度
	unsigned short s_magic; // 文件系统魔数
};

// 文件目录项数据结构类型
struct dir_entry {
	unsigned short inode; // 对应 i 节点号
	char name[NAME_LEN]; //文件夹名
};

extern struct m_inode inode_table[NR_INODE]; // 定义 i 节点表数组
extern struct file file_table[NR_FILE]; // 文件表数组
extern struct super_block super_block[NR_SUPER]; // 超级块数组
extern struct buffer_head * start_buffer; // 缓冲区起始内存位置
extern int nr_buffers; // 缓冲块数

// 硬盘操作的函数原型
extern void check_disk_change(int dev);
extern int floppy_change(unsigned int nr);
extern int ticks_to_floppy_on(unsigned int dev);
extern void floppy_on(unsigned int dev);
extern void floppy_off(unsigned int dev);
extern void truncate(struct m_inode * inode);
extern void sync_inodes(void);
extern void wait_on(struct m_inode * inode);
extern int bmap(struct m_inode * inode,int block);
extern int create_block(struct m_inode * inode,int block);
extern struct m_inode * namei(const char * pathname);
extern int open_namei(const char * pathname, int flag, int mode,
	struct m_inode ** res_inode);
extern void iput(struct m_inode * inode);
extern struct m_inode * iget(int dev,int nr);
extern struct m_inode * get_empty_inode(void);
extern struct m_inode * get_pipe_inode(void);
extern struct buffer_head * get_hash_table(int dev, int block);
extern struct buffer_head * getblk(int dev, int block);
extern void ll_rw_block(int rw, struct buffer_head * bh);
extern void brelse(struct buffer_head * buf);
extern struct buffer_head * bread(int dev,int block);
extern void bread_page(unsigned long addr,int dev,int b[4]);
extern struct buffer_head * breada(int dev,int block,...);
extern int new_block(int dev);
extern void free_block(int dev, int block);
extern struct m_inode * new_inode(int dev);
extern void free_inode(struct m_inode * inode);
extern int sync_dev(int dev);
extern struct super_block * get_super(int dev);
extern int ROOT_DEV; // 启动引导时的根文件系统设备号

extern void mount_root(void);

#endif
