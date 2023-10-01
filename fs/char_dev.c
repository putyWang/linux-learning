/*
 *  linux/fs/char_dev.c
 *
 *  (C) 1991  Linus Torvalds
 */

#include <errno.h>
#include <sys/types.h>

#include <linux/sched.h>
#include <linux/kernel.h>

#include <asm/segment.h>
#include <asm/io.h>

extern int tty_read(unsigned minor,char * buf,int count);
extern int tty_write(unsigned minor,char * buf,int count);

typedef (*crw_ptr)(int rw,unsigned minor,char * buf,int count,off_t * pos);	// 定义字符设备读写函数指针类型

/**
 * 串口终端读写函数
 * @param rw 读写命令符
 * @param minor 终端子设备号
 * @param buf 缓冲区
 * @param count 读写字节数
 * @param pos 读写操作当前指针
 * @return 实际读写字符数
*/
static int rw_ttyx(int rw,unsigned minor,char * buf,int count,off_t * pos)
{
	return ((rw==READ) ? tty_read(minor,buf,count) : tty_write(minor,buf,count)); // 调用实际串口终端读写函数
}

/**
 * 终端读写函数
 * @param rw 读写命令符
 * @param minor 终端子设备号
 * @param buf 缓冲区
 * @param count 读写字节数
 * @param pos 读写操作当前指针
 * @return 实际读写字符数
*/
static int rw_tty(int rw,unsigned minor,char * buf,int count, off_t * pos)
{
	if (current->tty<0)
		return -EPERM;
	return rw_ttyx(rw,current->tty,buf,count,pos);	// 调用终端读写函数，返回读写字节数
}

/**
 * 内存数据读写函数（未实现）
 * @param rw 读写命令符
 * @param buf 缓冲区
 * @param count 读写字节数
 * @param pos 读写操作当前指针
 * @return 实际读写字符数
*/
static int rw_ram(int rw,char * buf, int count, off_t *pos)
{
	return -EIO;
}

/**
 * 内存数据读写操作函数（未实现）
 * @param rw 读写命令符
 * @param buf 缓冲区
 * @param count 读写字节数
 * @param pos 读写操作当前指针
 * @return 实际读写字符数
*/
static int rw_mem(int rw,char * buf, int count, off_t * pos)
{
	return -EIO;
}

/**
 * 内核数据区读写函数（未实现）
 * @param rw 读写命令符
 * @param buf 缓冲区
 * @param count 读写字节数
 * @param pos 读写操作当前指针
 * @return 实际读写字符数
*/
static int rw_kmem(int rw,char * buf, int count, off_t * pos)
{
	return -EIO;
}

/**
 * 端口读写操作函数
 * @param rw 读写命令符
 * @param buf 缓冲区
 * @param count 读写字节数
 * @param pos 端口地址
 * @return 实际读写字节数
*/
static int rw_port(int rw,char * buf, int count, off_t * pos)
{
	int i=*pos;

	while (count-->0 && i<65536) {			// 终端端口只能小于 65536
		if (rw==READ)
			put_fs_byte(inb(i),buf++);		// 将指定端口读取到指定 buf 缓冲区
		else
			outb(get_fs_byte(buf++),i);		// 将 buf 数据发送到指定端口
		i++;								// 前移一个端口
	}
	i -= *pos;								// 计算读写字节数
	*pos += i;								// 更新端口号
	return i;
}

/**
 * 内存读写操作函数
 * @param rw 读写命令符
 * @param minor 终端子设备号
 * @param buf 缓冲区
 * @param count 读写字节数
 * @param pos 读写操作当前指针
 * @return 实际读写字符数
*/
static int rw_memory(int rw, unsigned minor, char * buf, int count, off_t * pos)
{
	// 根据内存设备子设备号，分别调用不同的内存读写函数
	switch(minor) {
		case 0:
			return rw_ram(rw,buf,count,pos);
		case 1:
			return rw_mem(rw,buf,count,pos);
		case 2:
			return rw_kmem(rw,buf,count,pos);
		case 3:
			return (rw==READ)?0:count;	/* rw_null */
		case 4:
			return rw_port(rw,buf,count,pos);
		default:
			return -EIO;
	}
}

#define NRDEVS ((sizeof (crw_table))/(sizeof (crw_ptr)))	// 定义系统中设备总数

static crw_ptr crw_table[]={
	NULL,		// 无设备 nodev
	rw_memory,	// 虚拟盘 /dev/mem 等
	NULL,		// 软盘 /dev/fd 
	NULL,		// 硬盘 /dev/hd
	rw_ttyx,	// 串口终端设备 /dev/ttyx
	rw_tty,		// 终端设备 /dev/tty
	NULL,		// 打印机设备 /dev/lp
	NULL};		// 未命名管道设备 unnamed pipes

/**
 * 字符设备读写操作函数
 * @param rw 读写命令符
 * @param dev 设备号
 * @param buf 缓冲区
 * @param count 读写字节数
 * @param pos 读写指针
 * @return 实际读写字符数
*/
int rw_char(int rw,int dev, char * buf, int count, off_t * pos)
{
	crw_ptr call_addr;

	if (MAJOR(dev)>=NRDEVS)								// 未定义返回错误码
		return -ENODEV;
	if (!(call_addr=crw_table[MAJOR(dev)]))				// 指定设备不是字符读写设备，返回错误
		return -ENODEV;
	return call_addr(rw,MINOR(dev),buf,count,pos);		// 执行指定字符设备操作函数
}
