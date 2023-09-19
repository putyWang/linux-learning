/*
 * This file contains some defines for the floppy disk controller.
 * Various sources. Mostly "IBM Microcomputers: A Programmers
 * Handbook", Sanches and Canton.
 */
#ifndef _FDREG_H
#define _FDREG_H // 该定义用来排除是否重复包含该头文件

// 软盘操作函数原型
extern int ticks_to_floppy_on(unsigned int nr);
extern void floppy_on(unsigned int nr);
extern void floppy_off(unsigned int nr);
extern void floppy_select(unsigned int nr);
extern void floppy_deselect(unsigned int nr);

// 软盘控制器的一些断藕和符号的定义
#define FD_STATUS	0x3f4       //主状态寄存器端口
#define FD_DATA		0x3f5       //数据端口
#define FD_DOR		0x3f2		//数字输出寄存器端口
#define FD_DIR		0x3f7		//数字输入寄存器端口
#define FD_DCR		0x3f7		//传输率控制端口

// 主状态各比特位含义
#define STATUS_BUSYMASK	0x0F	// 驱动器忙
#define STATUS_BUSY	0x10		//软盘控制器忙
#define STATUS_DMA	0x20		//0 - 为 DMA 数据传输模式
#define STATUS_DIR	0x40		//方向 0- cpu->fdc，1 相反
#define STATUS_READY	0x80	// 数据段寄存器就位

// 状态字节 0 各比特位含义
#define ST0_DS		0x03		//驱动器选择号
#define ST0_HA		0x04		//磁头号
#define ST0_NR		0x08		//磁盘驱动器未就绪
#define ST0_ECE		0x10		//设备检测出错（零道校准出错）
#define ST0_SE		0x20		//寻道结束
#define ST0_INTR	0xC0		//中断代码位； 00-命令正常结束，01-异常结束，10-命令无效，11-FDD 就绪状态改变

// 状态字节 1 各比特位含义 
#define ST1_MAM		0x01		//未找到地址标志位
#define ST1_WP		0x02		//写保护
#define ST1_ND		0x04		//未找到指定扇区
#define ST1_OR		0x10		//超时
#define ST1_CRC		0x20		//CRC 检测出错
#define ST1_EOC		0x80		//访问超过磁道上最大扇区号

// 状态字节 2 各比特位含义 
#define ST2_MAM		0x01		//未找到数据地址标志
#define ST2_BC		0x02		//磁道坏
#define ST2_SNS		0x04		//检索（扫描条件不充分）
#define ST2_SEH		0x08		//检索条件满足
#define ST2_WC		0x10		//磁道（柱面号）不符
#define ST2_CRC		0x20		//数据场 CRC 校验错
#define ST2_CM		0x40		//读数据遇到删除标志

// 状态字节 3 各比特位含义 
#define ST3_HA		0x04		//磁头号
#define ST3_TZ		0x10		//零磁道信号
#define ST3_WP		0x40		//写保护

// 软盘命令码 
#define FD_RECALIBRATE	0x07	//重新校正（磁头退到 0 磁道）
#define FD_SEEK		0x0F		//磁头寻道
#define FD_READ		0xE6		//读数据命令
#define FD_WRITE	0xC5		//写数据
#define FD_SENSEI	0x08		//检测中断状态
#define FD_SPECIFY	0x03		//设置软驱参数命令（步进速率、磁头卸载时间等）

// DMA 命令码 
#define DMA_READ	0x46        //DMA 读盘
#define DMA_WRITE	0x4A        //DMA 写盘

#endif
