#ifndef _BLK_H
#define _BLK_H

// 设置块设备数量
#define NR_BLK_DEV	7 // 块设备数量
/*
 * NR_REQUEST is the number of entries in the request-queue.
 * NOTE that writes may use only the low 2/3 of these: reads
 * take precedence.
 *
 * 32 seems to be a reasonable number: enough to get some benefit
 * from the elevator-mechanism, but not so much as to lock a lot of
 * buffers when they are in the queue. 64 seems to be too many (easily
 * long pauses in reading when heavy writing/syncing is going on)
 */
// 默认块设备请求数量
#define NR_REQUEST	32

/*
 * Ok, this is an expanded form so that we can use the same
 * request for paging requests when that is implemented. In
 * paging, 'bh' is NULL, and 'waiting' is used to wait for
 * read/write completion.
 */
/**
 * 块设备请求队列中项的结构，dev=-1 时表明该项未被使用
 * request 请求结构的扩展形式，实现之后可以在分页请求中使用
 * 分页处理中，‘bh’ 为null，waiting 用于等待读写的完成
*/
struct request {
	int dev;		/* -1 if no request */ // 使用的设备号（硬盘为分区号）
	int cmd;		/* READ or WRITE */ // 命令 (read 或 write)
	int errors; // 操作时产生的异常
	unsigned long sector; // 起始扇区
	unsigned long nr_sectors; // 读/写扇区数
	char * buffer; // 数据缓冲区
	struct task_struct * waiting; // 指向任务等待执行完成的地方
	struct buffer_head * bh; // 缓冲区头指针
	struct request * next; // 指向下一请求项
};

/*
 * This is used in the elevator algorithm: Note that
 * reads always go before writes. This is natural: reads
 * are much more time-critical than writes.
 */
#define IN_ORDER(s1,s2) \
((s1)->cmd<(s2)->cmd || (s1)->cmd==(s2)->cmd && \
((s1)->dev < (s2)->dev || ((s1)->dev == (s2)->dev && \
(s1)->sector < (s2)->sector)))

/**
 * 块存储设备结构
*/
struct blk_dev_struct {
	void (*request_fn)(void); // 请求操作的函数指针
	struct request * current_request; // 请求信息结构
};

extern struct blk_dev_struct blk_dev[NR_BLK_DEV];
extern struct request request[NR_REQUEST];
extern struct task_struct * wait_for_request;

#ifdef MAJOR_NR // 主设备号

/*
 * Add entries as needed. Currently the only block devices
 * supported are hard-disks and floppies.
 */
/**
 * 条件编译主设备制定信息
 * 主要支持 1-虚拟盘，2-软盘，3-硬盘
*/
#if (MAJOR_NR == 1)
/* ram disk */
#define DEVICE_NAME "ramdisk" // 设备名称 ramdisk
#define DEVICE_REQUEST do_rd_request // 虚拟盘设备请求处理函数
#define DEVICE_NR(device) ((device) & 7) // 设备号 0～7
#define DEVICE_ON(device)  // 开启设备，虚拟盘无需开启或关闭
#define DEVICE_OFF(device) // 关闭设备

#elif (MAJOR_NR == 2)
/* floppy */
#define DEVICE_NAME "floppy" // 设备名称 floppy
#define DEVICE_INTR do_floppy // 设备中断处理程序
#define DEVICE_REQUEST do_fd_request // 软盘设备请求处理函数
#define DEVICE_NR(device) ((device) & 3) // 设备号（0～3）
#define DEVICE_ON(device) floppy_on(DEVICE_NR(device)) // 打开设备函数 
#define DEVICE_OFF(device) floppy_off(DEVICE_NR(device)) // 关闭设备函数

#elif (MAJOR_NR == 3)
/* harddisk */
#define DEVICE_NAME "harddisk"
#define DEVICE_INTR do_hd
#define DEVICE_REQUEST do_hd_request
#define DEVICE_NR(device) (MINOR(device)/5) // 设备号 0～1；每个硬盘由 4 个分区
#define DEVICE_ON(device) // 硬盘一直开启无需开启或关闭
#define DEVICE_OFF(device)

#elif
/* unknown blk device */
#error "unknown blk device"

#endif

#define CURRENT (blk_dev[MAJOR_NR].current_request) //CURRENT 指向主设备号的当前请求结构
#define CURRENT_DEV DEVICE_NR(CURRENT->dev) // CURRENT_DEV 为当前设备号

#ifdef DEVICE_INTR
void (*DEVICE_INTR)(void) = NULL;
#endif
static void (DEVICE_REQUEST)(void);

extern inline void unlock_buffer(struct buffer_head * bh)
{
	if (!bh->b_lock)
		printk(DEVICE_NAME ": free buffer being unlocked\n");
	bh->b_lock=0;
	wake_up(&bh->b_wait);
}

/**
 * 块设备处理任务结束
*/
extern inline void end_request(int uptodate)
{
	DEVICE_OFF(CURRENT->dev); // 关闭块设备
	// 处理缓冲区
	if (CURRENT->bh) {
		CURRENT->bh->b_uptodate = uptodate; // 设置缓冲区更新标志
		unlock_buffer(CURRENT->bh); // 解锁缓冲区
	}
	// 处理错误
	if (!uptodate) {
		printk(DEVICE_NAME " I/O error\n\r");
		printk("dev %04x, block %d\n\r",CURRENT->dev,
			CURRENT->bh->b_blocknr);
	}
	// 唤醒等待该请求项的进程
	wake_up(&CURRENT->waiting);
	// 唤醒等待请求的进程
	wake_up(&wait_for_request);
	// 设置当前设备为 未使用
	CURRENT->dev = -1;
	// 处理下一请求
	CURRENT = CURRENT->next;
}

/**
 * 定义请求初始化宏
*/
#define INIT_REQUEST \
repeat: \
	if (!CURRENT) \ //首先如果当前请求为空时 直接返回返回
		return; \
	if (MAJOR(CURRENT->dev) != MAJOR_NR) \ //然后判断当前设备号是否与 主设备号一致 不一致死机
		panic(DEVICE_NAME ": request list destroyed"); \
	if (CURRENT->bh) { \
		if (!CURRENT->bh->b_lock) \ //随后判断当前设备指向缓冲区是否已经被锁定 未锁定死机
			panic(DEVICE_NAME ": block not locked"); \
	}

#endif

#endif
