/*
 *  linux/kernel/blk_drv/ramdisk.c
 *
 *  Written by Theodore Ts'o, 12/2/91
 */

#include <string.h>

#include <linux/config.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <asm/system.h>
#include <asm/segment.h>
#include <asm/memory.h>

#define MAJOR_NR 1 	// 虚拟盘主设备号为 1
#include "blk.h"

char	*rd_start;	// 虚拟盘在内存中的起始位置
int	rd_length = 0;	// 虚拟盘所在内存大小（字节）

/**
 * 执行虚拟盘读写操作函数；
 * 程序结构与与 do_hd_request() 类似
*/
void do_rd_request(void)
{
	int	len;
	char	*addr;

	INIT_REQUEST;								// 检测请求合法性（设备号与主设备是否一致、指定缓冲区是否被锁定）
	addr = rd_start + (CURRENT->sector << 9); 	// 根据当前操作的起始分区计算所操作内存的起始地址（一个扇区为 512 B）
	len = CURRENT->nr_sectors << 9;				// 通过操作的扇区数计算出需要操作的扇区数
	// 子设备号不为 1 或者对应内存起始位置 > 虚拟盘结尾，则结束该请求，并跳转到 repeat 处
	if ((MINOR(CURRENT->dev) != 1) || (addr+len > rd_start+rd_length)) {
		end_request(0);
		goto repeat;
	}
	// 进行写操作时，请求项的缓冲区内容复制到 addr 处，长度为length
	if (CURRENT-> cmd == WRITE) {
		(void ) memcpy(addr,
			      CURRENT->buffer,
			      len);
	// 进行读操作时，与写操作相反，是将内存中的内容复制到缓冲区
	} else if (CURRENT->cmd == READ) {
		(void) memcpy(CURRENT->buffer, 
			      addr,
			      len);
	// 命令只能为读写命令
	} else
		panic("unknown ramdisk-command");
	//执行完毕时，置更新标志，继续处理下一请求项
	end_request(1);
	goto repeat;
}

/**
 * 虚拟盘初始化函数
 * @param mem_start 虚拟盘内存起始地址
 * @param length 虚拟盘内存长度
*/
long rd_init(long mem_start, int length)
{
	int	i;
	char	*cp;

	blk_dev[MAJOR_NR].request_fn = DEVICE_REQUEST;	// 设置虚拟盘请求操作函数
	rd_start = (char *) mem_start;					// 设置虚拟盘内存起始地址（与主内存地址一致）
	rd_length = length;								// 初始化虚拟盘长度
	cp = rd_start;									// 初始化内存虚拟盘区域的内存（对齐清零）
	for (i=0; i < length; i++)
		*cp++ = '\0';
	return(length);									// 返回虚拟盘区域大小
}

/**
 * 加载根文件系统到 Ramdisk
*/
void rd_load(void)
{
	struct buffer_head *bh;
	struct super_block	s;
	int		block = 256;	/* Start at block 256 */
	int		i = 1;
	int		nblocks;
	char		*cp;		/* Move pointer */
	
	if (!rd_length) 									// 未使用虚拟盘时直接退出
		return;
	printk("Ram disk: %d bytes, starting at 0x%x\n", rd_length,
		(int) rd_start);
	if (MAJOR(ROOT_DEV) != 2)							// 根文件所在设备不在软盘时退出
		return;
	bh = breada(ROOT_DEV,block+1,block,block+2,-1); 	// 读取 256+1，256，256+2 上的块
	if (!bh) {
		printk("Disk error while looking for ramdisk!\n");
		return;
	}
	*((struct d_super_block *) &s) = *((struct d_super_block *) bh->b_data);	// 将 256 + 1 块上的数据（软盘设备的块设备）设置到超级块中
	brelse(bh); 										// 释放该缓冲区
	if (s.s_magic != SUPER_MAGIC)						// 当文件系统不是 minix 时，直接返回
		return;
	nblocks = s.s_nzones << s.s_log_zone_size; 			// 块数 = 逻辑块数（区段数）* 2 ^ (每区段块数的幂)
	// 系统块的个数大于内存中虚拟盘所能容纳的块数，无法加载，打印错误消息然后退出
	if (nblocks > (rd_length >> BLOCK_SIZE_BITS)) {
		printk("Ram disk image too big!  (%d blocks, %d avail)\n", 
			nblocks, rd_length >> BLOCK_SIZE_BITS);
		return;
	}
	// 输出系统块所占用内存
	printk("Loading %d bytes into ram disk... 0000k", 
		nblocks << BLOCK_SIZE_BITS);
	cp = rd_start;
	// 循环将所有系统块全部加载置缓冲区
	while (nblocks) {
		if (nblocks > 2) 
			bh = breada(ROOT_DEV, block, block+1, block+2, -1);
		else
			bh = bread(ROOT_DEV, block);
		if (!bh) {
			printk("I/O error on block %d, aborting load\n", 
				block);
			return;
		}
		(void) memcpy(cp, bh->b_data, BLOCK_SIZE); 	// 将缓冲区对应块数据移动到 虚拟盘对应内存
		brelse(bh); 								// 释放 bh 处的内存
		printk("\010\010\010\010\010%4dk",i); 		// 打印当前加载的块数量
		cp += BLOCK_SIZE; 							// 移动虚拟盘基地址
		block++; 									// 移动块地址
		nblocks--; 									// 移动软盘块地址
		i++; 										// 对已移动块数量进行计数
	}
	printk("\010\010\010\010\010done \n");
	ROOT_DEV=0x0101; // 修改 ROOT_DEV 主设备指向虚拟内存
}
