/*
 * This file contains some defines for the AT-hd-controller.
 * Various sources. Check out some definitions (see comments with
 * a ques).
 */
#ifndef _HDREG_H
#define _HDREG_H

// 硬盘控制器寄存器端口
#define HD_DATA		0x1f0	    // 硬盘写端口
#define HD_ERROR	0x1f1	    // 硬盘错误端口
#define HD_NSECTOR	0x1f2	    /* nr of sectors to read/write */
#define HD_SECTOR	0x1f3	    /* starting sector */
#define HD_LCYL		0x1f4	    /* starting cylinder */
#define HD_HCYL		0x1f5	    /* high byte of starting cyl */
#define HD_CURRENT	0x1f6	    /* 101dhhhh , d=drive, hhhh=head */
#define HD_STATUS	0x1f7	    /* see status-bits */
#define HD_PRECOMP HD_ERROR	    /* same io address, read=error, write=precomp */
#define HD_COMMAND HD_STATUS	/* same io address, read=status, write=cmd */

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
 * 硬盘分区结构
*/
struct partition {
	unsigned char boot_ind;		/* 0x80 - active (unused) */
	unsigned char head;		/* ? */
	unsigned char sector;		/* ? */
	unsigned char cyl;		/* ? */
	unsigned char sys_ind;		/* ? */
	unsigned char end_head;		/* ? */
	unsigned char end_sector;	/* ? */
	unsigned char end_cyl;		/* ? */
	unsigned int start_sect;	/* starting sector counting from 0 */
	unsigned int nr_sects;		/* nr of sectors in partition */
};

#endif
