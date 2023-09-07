/*
 *  linux/kernel/blk_dev/ll_rw.c
 *
 * (C) 1991 Linus Torvalds
 */

/*
 * This handles all read/write requests to block devices
 */
#include <errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <asm/system.h>

#include "blk.h"

/*
 * The request-struct contains all necessary data
 * to load a nr of sectors into memory
 */
/**
 * 请求数组
*/
struct request request[NR_REQUEST];

/*
 * used to wait on when there are no free requests
 */
/**
 * 请求数组已满时，临时等待区
*/
struct task_struct * wait_for_request = NULL;

/* blk_dev_struct is:
 *	do_request-address
 *	next-request
 */
/**
 * 数组使用主设备号作为索引
*/
struct blk_dev_struct blk_dev[NR_BLK_DEV] = {
	{ NULL, NULL },		/* no_dev */ // 0 - 无设备
	{ NULL, NULL },		/* dev mem */ // 1 - 内存
	{ NULL, NULL },		/* dev fd */ // 2 - 软驱设备
	{ NULL, NULL },		/* dev hd */ // 3 - 硬盘设备
	{ NULL, NULL },		/* dev ttyx */ // 4- ttyx 设备
	{ NULL, NULL },		/* dev tty */ // 5- tty 设备
	{ NULL, NULL }		/* dev lp */ // 6- lp 打印机设备
};

/**
 * 对指定缓冲上锁
 * @param bh 指定需要上锁缓冲头指针
*/
static inline void lock_buffer(struct buffer_head * bh)
{
	cli();
	while (bh->b_lock) // 等待其他进程释放锁
		sleep_on(&bh->b_wait);
	bh->b_lock=1;
	sti();
}

/**
 * 对指定缓冲解锁
 * @param bh 指定需要解锁缓冲头指针
*/
static inline void unlock_buffer(struct buffer_head * bh)
{	
	// 该程序未上锁，输出错误日志 
	if (!bh->b_lock)
		printk("ll_rw_block.c: buffer not locked\n\r");
	bh->b_lock = 0;
	wake_up(&bh->b_wait); // 唤醒等待该缓冲区解锁的任务
}

/*
 * add-request adds a request to the linked list.
 * It disables interrupts so that it can muck with the
 * request-lists in peace.
 */
/**
 * 向链表中添加请求项
 * @param dev 设备指针
 * @param req 请求对象
*/
static void add_request(struct blk_dev_struct * dev, struct request * req)
{
	struct request * tmp;

	req->next = NULL;
	cli(); // 禁止中断
	if (req->bh)
		req->bh->b_dirt = 0; // 复位缓冲区 脏标志
	// 当前没请求时，直接将 req 置为当前请求
	if (!(tmp = dev->current_request)) {
		dev->current_request = req;
		sti(); // 开启中断
		(dev->request_fn)(); // 直接执行请求
		return;
	}
	// 已有请求项时，使用电梯算法获取最优位置，然后插入请求
	for ( ; tmp->next ; tmp=tmp->next)
		if ((IN_ORDER(tmp,req) ||
		    !IN_ORDER(tmp,tmp->next)) &&
		    IN_ORDER(req,tmp->next))
			break;
	req->next=tmp->next;
	tmp->next=req;
	sti(); // 开启中断
}

/**
 * 新增块设备读写请求
 * @param major 设备号
 * @param rw 读写控制字
 * @param bh 需要操作的缓冲头
*/
static void make_request(int major,int rw, struct buffer_head * bh)
{
	struct request * req;
	int rw_ahead;

/* WRITEA/READA is special case - it is not really needed, so if the */
/* buffer is locked, we just forget about it, else it's a normal read */
	if (rw_ahead = (rw == READA || rw == WRITEA)) {
		if (bh->b_lock) // 当前缓冲上锁时，返回
			return;
		if (rw == READA)
			rw = READ; // 设置读命令
		else
			rw = WRITE; // 设置写命令
	}
	// 命令非读写时报错，死机
	if (rw!=READ && rw!=WRITE)
		panic("Bad block dev command, must be R/W/RA/WA");
	lock_buffer(bh); // 对指定缓冲上锁
	// 当写命令且缓冲不需要更新到设备或读命令且缓冲区未被更新，则直接解锁该缓冲块并退出
	if ((rw == WRITE && !bh->b_dirt) || (rw == READ && bh->b_uptodate)) {
		unlock_buffer(bh);
		return;
	}
repeat:
/* we don't allow the write-requests to fill up the queue completely:
 * we want some room for reads: they take precedence. The last third
 * of the requests are only for reads.
 */
	// 读请求从末尾开始搜索空位的请求
	if (rw == READ)
		req = request+NR_REQUEST;
	else
	// 写请求则是从 2/3 处开始搜索
		req = request+((NR_REQUEST*2)/3);
/* find an empty request */
	// 查找当前请求数组中空闲项
	while (--req >= request)
		if (req->dev<0)
			break;
/* if none found, sleep on new requests: check for rw_ahead */
	// 没空闲项时，让该请求项睡眠等待空闲
	if (req < request) {
		if (rw_ahead) { // 预读写命令直接返回
			unlock_buffer(bh);
			return;
		}
		sleep_on(&wait_for_request);
		goto repeat;
	}
/* fill up the request-info, and add it to the queue */
	// 创建请求信息，并添加到请求队列中去
	req->dev = bh->b_dev;
	req->cmd = rw;
	req->errors=0;
	req->sector = bh->b_blocknr<<1;
	req->nr_sectors = 2;
	req->buffer = bh->b_data;
	req->waiting = NULL;
	req->bh = bh;
	req->next = NULL;
	add_request(major+blk_dev,req);
}

/**
 * 发起指定缓冲写回请求
 * @param rw 读写控制字
 * @param bh 需要写回的缓冲头
*/
void ll_rw_block(int rw, struct buffer_head * bh)
{
	unsigned int major;

	// 设备号不合法时，输出错误
	if ((major=MAJOR(bh->b_dev)) >= NR_BLK_DEV ||
	!(blk_dev[major].request_fn)) {
		printk("Trying to read nonexistent block-device\n\r");
		return;
	}
	make_request(major,rw,bh);
}

/**
 * 块设备初始化程序
 * 
*/
void blk_dev_init(void)
{
	int i;

	// 初始化块设备请求数组队列，将所有全设置为 未请求，下一请求置空
	for (i=0 ; i<NR_REQUEST ; i++) {
		// 设置为空闲
		request[i].dev = -1;
		request[i].next = NULL;
	}
}
