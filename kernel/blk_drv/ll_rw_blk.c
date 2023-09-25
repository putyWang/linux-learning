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

struct request request[NR_REQUEST]; 			// 请求数组（含有加载 nr 扇区数据到内存中的所有必须的信息）

struct task_struct * wait_for_request = NULL; 	// 请求数组已满时，最新的等待进程指针

/* blk_dev_struct is:
 *	do_request-address
 *	next-request
 */
/**
 * 数组使用主设备号作为索引
*/
struct blk_dev_struct blk_dev[NR_BLK_DEV] = {
	{ NULL, NULL },		// 0 - 无设备
	{ NULL, NULL },		// 1 - 内存
	{ NULL, NULL },		// 2 - 软驱设备
	{ NULL, NULL },		// 3 - 硬盘设备
	{ NULL, NULL },		// 4- ttyx 设备
	{ NULL, NULL },		// 5- tty 设备
	{ NULL, NULL }		// 6- lp 打印机设备
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

/**
 * 向链表中添加请求项
 * @param dev 指定块设备指针
 * @param req 请求结构信息
*/
static void add_request(struct blk_dev_struct * dev, struct request * req)
{
	struct request * tmp;

	req->next = NULL;
	cli(); 										// 禁止中断
	if (req->bh)
		req->bh->b_dirt = 0; 					// 复位缓冲区 脏标志
	// 当前没请求时，直接将 req 置为当前请求
	if (!(tmp = dev->current_request)) {
		dev->current_request = req;
		sti(); 									// 开启中断
		(dev->request_fn)(); 					// 直接执行请求
		return;
	}
	// 已有请求项时，遍历所有请求使用电梯算法获取最优位置，然后插入请求
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
 * 创建块设备读写请求并插入请求队列
 * @param major 设备号
 * @param rw 读写命令
 * @param bh 需要操作的缓冲指针
*/
static void make_request(int major,int rw, struct buffer_head * bh)
{
	struct request * req;
	int rw_ahead;

	// READ 与 WRITE 后面的 'A' 字符代表英文单词 Ahead，标识提前读与提前写的意思，
	// 当指定的缓冲区正在使用已被上锁时，就直接放弃预读/写请求
	if (rw_ahead = (rw == READA || rw == WRITEA)) {
		if (bh->b_lock) 		// 当前缓冲上锁时，返回
			return;
		if (rw == READA)
			rw = READ; 			// 设置读命令
		else
			rw = WRITE; 		// 设置写命令
	}
	if (rw!=READ && rw!=WRITE)	// 命令非读写时报错，死机
		panic("Bad block dev command, must be R/W/RA/WA");
	lock_buffer(bh); 			// 对指定缓冲上锁
	// 当写命令且缓冲不需要更新到设备或读命令且缓冲区未被更新，则直接解锁该缓冲块并退出
	if ((rw == WRITE && !bh->b_dirt) || (rw == READ && bh->b_uptodate)) {
		unlock_buffer(bh);
		return;
	}
repeat:
	if (rw == READ)				// 读请求从末尾开始搜索空位的请求
		req = request+NR_REQUEST;
	else						// 写请求则是从 2/3 处开始搜索					
		req = request+((NR_REQUEST*2)/3);
	while (--req >= request)	// 查找请求数组中空闲项
		if (req->dev<0)
			break;
	// 没空闲项时，则阻塞当前进程直到存在空闲项时被唤醒
	if (req < request) {
		if (rw_ahead) { 		// 预读写命令直接返回
			unlock_buffer(bh);
			return;
		}
		sleep_on(&wait_for_request);
		goto repeat;
	}
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

	// 设备号不合法或指定设备请求处理函数指针为空时，输出错误
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
		request[i].dev = -1; 	// 初始化空闲请求项 dev 初始值位 -1 
		request[i].next = NULL; // 初始化请求下一项为 NULL
	}
}
