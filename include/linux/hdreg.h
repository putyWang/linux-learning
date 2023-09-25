/*
 * This file contains some defines for the AT-hd-controller.
 * Various sources. Check out some definitions (see comments with
 * a ques).
 */
#ifndef _HDREG_H
#define _HDREG_H

// 硬盘控制器寄存器端口
#define HD_DATA		0x1f0	    // 数据寄存器端口（扇区数据-读、写、格式化）
#define HD_ERROR	0x1f1	    // 读-硬盘错误端口（错误状态），写-写前预补偿寄存器
#define HD_NSECTOR	0x1f2	    // 扇区寄存器（扇区数-读、写、检验、格式化）
#define HD_SECTOR	0x1f3	    // 扇区号寄存器（扇区号-读、写、检验）
#define HD_LCYL		0x1f4	    // 柱面号低 8 位寄存器（柱面号低字节-读、写、检验、格式化）
#define HD_HCYL		0x1f5	    // 柱面号高 2 位寄存器（柱面号高字节-读、写、检验、格式化）
#define HD_CURRENT	0x1f6	    // 驱动号/磁头寄存器（驱动器号/磁头号-101dhhhh，d=驱动器号，h=磁头号）
#define HD_STATUS	0x1f7	    // 读-主状态寄存器，写-命令寄存器
#define HD_PRECOMP HD_ERROR	    // 写-硬盘控制寄存器
#define HD_COMMAND HD_STATUS	// 读-数字输出寄存器（与 1.2MB 软盘合用）

#define HD_CMD		0x3f6       // 控制寄存器端口

// 硬盘状态寄存器各位的定义
#define ERR_STAT	0x01        // 命令执行错误
#define INDEX_STAT	0x02        // 收到索引
#define ECC_STAT	0x04	    // ECC 校验错
#define DRQ_STAT	0x08        // 硬盘状态寄存器请求服务位
#define SEEK_STAT	0x10        // 寻道结束
#define WRERR_STAT	0x20        // 驱动器故障
#define READY_STAT	0x40        // 驱动器是否就绪
#define BUSY_STAT	0x80        // 控制器忙

// 硬盘命令值
#define WIN_RESTORE		0x10    // 驱动器重新校正（驱动器复位）
#define WIN_READ		0x20    // 读扇区
#define WIN_WRITE		0x30    // 写扇区
#define WIN_VERIFY		0x40    // 扇区校验
#define WIN_FORMAT		0x50    // 格式化磁道
#define WIN_INIT		0x60    // 控制器初始化
#define WIN_SEEK 		0x70    // 寻道
#define WIN_DIAGNOSE    0x90    // 控制器诊断
#define WIN_SPECIFY		0x91    // 建立驱动器参数

// 错误寄存器各比特位含义，执行控制器命令时含义与其他命令时时不同的，格式 诊断命令时（其他命令时）
#define MARK_ERR	0x01	// 无错误（数据标志丢失）
#define TRK0_ERR	0x02	// 控制器出错（磁道 0 错）
#define ABRT_ERR	0x04	// ECC 部件错（命令放弃）
#define ID_ERR		0x10	// （ID 未找到）
#define ECC_ERR		0x40	// （ECC 错误）
#define	BBD_ERR		0x80	// （坏扇区）

/**
 * 硬盘分区表结构
*/
struct partition {
	unsigned char boot_ind;		// 引导标志，4 个分区中同时只能有一个分区是可引导的（0x00-不从该分区引导操作系统，0x80-从该分区引导操作系统）
	unsigned char head;			// 分区起始磁头号
	unsigned char sector;		// 分区起始扇区号（位 0-5）和起始柱面号高 2 位（位 6-7）
	unsigned char cyl;			// 分区起始柱面号低 8 位
	unsigned char sys_ind;		// 分区类型字节（0x0b-DOS,0x80-Old MINIX,0x83-Linux）
	unsigned char end_head;		// 分区结束磁头号
	unsigned char end_sector;	// 结束扇区号（位 0-5 ）和结束柱面号高 2 位（位 6-7）
	unsigned char end_cyl;		// 分区结束柱面号低 8 位
	unsigned int start_sect;	// 分区起始物理扇区号
	unsigned int nr_sects;		// 分区占用的扇区数
};

#endif
