/*
 *  linux/kernel/hd.c
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 * This is the low-level hd interrupt support. It traverses the
 * request-list, using interrupts to jump between functions. As
 * all the functions are called within interrupts, we may not
 * sleep. Special care is recommended.
 * 
 *  modified by Drew Eckhardt to check nr of hd's from the CMOS.
 */

#include <linux/config.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/hdreg.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/segment.h>

#define MAJOR_NR 3				// 硬盘主设备号时3（必须在包含进 blk.h 文件进行定义）
#include "blk.h"

/**
 * 读 CMOS 参数宏函数（读取当前内核时间）
 * @param addr 地址
*/
#define CMOS_READ(addr) ({ \
outb_p(0x80|addr,0x70); \
inb_p(0x71); \
})

/* Max read/write errors/sector */
#define MAX_ERRORS	7 			// 单个请求最大错误次数
#define MAX_HD		2 			// 系统支持的最大硬盘数量

static void recal_intr(void);

static int recalibrate = 1; 	//重新矫正标志，将磁头移动到 0 柱面
static int reset = 1; 			// 复位标志

/*
 *  This struct defines the HD's and their types.
 */
/**
 * 硬盘参数结构（在setup.s文件中获取）
 * 定义了硬盘参数与类型
 * 六个参数分别为磁头数、每磁道扇区数、柱面数、写前预补偿柱面号、磁头着陆区柱面号与控制字节
*/
struct hd_i_struct {
	int head,sect,cyl,wpcom,lzone,ctl;
	};
#ifdef HD_TYPE
struct hd_i_struct hd_info[] = { HD_TYPE }; // hd_info 为config.h中手动定义的磁盘信息
// NR_HD 为硬盘数
#define NR_HD ((sizeof (hd_info))/(sizeof (struct hd_i_struct)))
#else
// 未手动定义时，全部设置为 0
struct hd_i_struct hd_info[] = { {0,0,0,0,0,0},{0,0,0,0,0,0} };
static int NR_HD = 0;
#endif

/**
 * 定义磁盘分区结构（每个硬盘分为 5 个分区）
 * index 为 5 倍数时存储整个磁盘的参数
*/
static struct hd_struct {
	long start_sect; 	// 起始扇区号
	long nr_sects; 		// 分区扇区总数
} *hd[5*MAX_HD]={{0,0},};
/**
 * 读端口 
 * @param port 读取的端口号 
 * @param buf 保存读取的缓冲区
 * @param nr 读取的字节数
*/
#define port_read(port,buf,nr) \
__asm__("cld;rep;insw"::"d" (port),"D" (buf),"c" (nr):"cx","di")

/**
 * 写端口
 * @param port 写数据的端口
 * @param buf 存有需要写的数据的缓冲
 * @param nr 写的字节数
*/
#define port_write(port,buf,nr) \
__asm__("cld;rep;outsw"::"d" (port),"S" (buf),"c" (nr):"cx","si")

extern void hd_interrupt(void);
extern void rd_load(void);

/* This may be used only once, enforced by 'static int callable' */
/**
 * 通过读取 CMOS 和 硬盘参数表信息，来设置硬盘分区结构 hd，并加载 RAM 虚拟盘和根文件系统
 * @param BOIS setup.s 程序从 BOIS 取得的 2 个硬盘的基本参数表
 * @return 0-成功，其他-错误号
*/
int sys_setup(void * BIOS)
{
	static int callable = 1;	// 标识函数是否为第一次运行 
	int i,drive;
	unsigned char cmos_disks;
	struct partition *p;
	struct buffer_head * bh;

	// 由于该程序只运行一次，因此在初始化时将该参数设置为 1，运行后改为 0，确保只会运行一次
	if (!callable)
		return -1;
	callable = 0;

// 当没在 config.h 手动定义硬盘参数时，就从硬盘BOIS 中读取
#ifndef HD_TYPE		
	// 设置 硬盘1 与 硬盘2 的对应参数
	for (drive=0 ; drive<2 ; drive++) {
		hd_info[drive].cyl = *(unsigned short *) BIOS;
		hd_info[drive].head = *(unsigned char *) (2+BIOS);
		hd_info[drive].wpcom = *(unsigned short *) (5+BIOS);
		hd_info[drive].ctl = *(unsigned char *) (8+BIOS);
		hd_info[drive].lzone = *(unsigned short *) (12+BIOS);
		hd_info[drive].sect = *(unsigned char *) (14+BIOS);
		BIOS += 16;
	}
	// 设置实际硬盘数量
	if (hd_info[1].cyl)
		NR_HD=2;
	else
		NR_HD=1;
#endif
	// 设置每个硬盘的起始区号和扇区总数
	for (i=0 ; i<NR_HD ; i++) {
		hd[i*5].start_sect = 0;
		hd[i*5].nr_sects = hd_info[i].head*
				hd_info[i].sect*hd_info[i].cyl;
	}

	/*
		We querry CMOS about hard disks : it could be that 
		we have a SCSI/ESDI/etc controller that is BIOS
		compatable with ST-506, and thus showing up in our
		BIOS table, but not register compatable, and therefore
		not present in CMOS.

		Furthurmore, we will assume that our ST-506 drives
		<if any> are the primary drives in the system, and 
		the ones reflected as drive 1 or 2.

		The first drive is stored in the high nibble of CMOS
		byte 0x12, the second in the low nibble.  This will be
		either a 4 bit drive type or 0xf indicating use byte 0x19 
		for an 8 bit type, drive 1, 0x1a for drive 2 in CMOS.

		Needless to say, a non-zero value means we have 
		an AT controller hard disk for that drive.

		
	*/

	// 检测硬盘是否为 AT 控制器兼容
	if ((cmos_disks = CMOS_READ(0x12)) & 0xf0)
		if (cmos_disks & 0x0f)
			NR_HD = 2;
		else
			NR_HD = 1;·
	else
		NR_HD = 0;
	// NR_HD 为 0 时，表明两个硬盘都不是 AT 兼容的，清空硬盘起始扇区数与扇区总数
	for (i = NR_HD ; i < 2 ; i++) {
		hd[i*5].start_sect = 0;
		hd[i*5].nr_sects = 0;
	}
	for (drive=0 ; drive<NR_HD ; drive++) {
		// 读取指定硬盘起始扇区处的 硬盘分区表参数
		if (!(bh = bread(0x300 + drive*5,0))) {
			printk("Unable to read partition table of drive %d\n\r",
				drive);
			panic("");
		}
		// 通过判断 硬盘第一个扇区 0x1fe 处保存的字符是否为 55AA 判断分区表是否有效
		if (bh->b_data[510] != 0x55 || (unsigned char)
		    bh->b_data[511] != 0xAA) {
			printk("Bad partition table on drive %d\n\r",drive);
			panic("");
		}
		p = 0x1BE + (void *)bh->b_data; // 分区表位于 第一个分区的 0x1BE 处
		// 设置分区参数
		for (i=1;i<5;i++,p++) {
			hd[i+5*drive].start_sect = p->start_sect;
			hd[i+5*drive].nr_sects = p->nr_sects;
		}
		brelse(bh); // 释放指定缓冲区
	}
	// 硬盘存在且分区表已读入时，打印对应参数
	if (NR_HD)
		printk("Partition table%s ok.\n\r",(NR_HD>1)?"s":"");
	rd_load(); 		// 加载创建RAMDISK
	mount_root(); 	// 安装根文件系统
	return (0);
}

/**
 * 循环等待驱动器就绪
*/
static int controller_ready(void)
{
	int retries=10000;

	while (--retries && (inb_p(HD_STATUS)&0xc0)!=0x40);
	return (retries);
}

/**
 * 判断硬盘控制器是否已经就绪和寻道状态
*/
static int win_result(void)
{
	int i=inb_p(HD_STATUS); // 获取控制器状态信息

	// 控制器状态为 就绪或者寻道结束时成功
	if ((i & (BUSY_STAT | READY_STAT | WRERR_STAT | SEEK_STAT | ERR_STAT))
		== (READY_STAT | SEEK_STAT))
		return(0); /* ok */
	if (i&1) i=inb(HD_ERROR); //存在错误时读取错误寄存器
	return (1);
}

/**
 * 向硬盘控制器发送命令块
 * @param drive 硬盘编号
 * @param nsect 读写扇区数
 * @param sect 起始扇区
 * @param head 磁头号
 * @param cyl 柱面号
 * @param cmd 命令码
 * @param intr_addr 硬盘中断处理程序将调用的处理函数
*/
static void hd_out(unsigned int drive,unsigned int nsect,unsigned int sect,
		unsigned int head,unsigned int cyl,unsigned int cmd,
		void (*intr_addr)(void))
{
	register int port asm("dx"); // port 变量对应寄存器 dx

	// 驱动器号 > 1 或 磁头号 > 15 时 死机不支持
	if (drive>1 || head>15)
		panic("Trying to write bad sector");
	// 驱动器超时未就绪时死机
	if (!controller_ready())
		panic("HD controller not ready");
	do_hd = intr_addr; 					// do_hd 指向硬盘中断处理程序
	outb_p(hd_info[drive].ctl,HD_CMD); 	// 向控制寄存器中输入控制字节
	port=HD_DATA; 						// 置 dx 为寄存器端口
	outb_p(hd_info[drive].wpcom>>2,++port); // 参数：写预补偿柱面号（除以 4）
	outb_p(nsect,++port); 				// 参数：读写扇区数
	outb_p(sect,++port); 				// 参数：起始扇区
	outb_p(cyl,++port); 				// 参数：柱面号低8位
	outb_p(cyl>>8,++port); 				// 参数：柱面号高 8 位
	outb_p(0xA0|(drive<<4)|head,++port); // 参数：驱动器 + 磁头号
	outb(cmd,++port); 					// 命令：硬盘控制命令
}

/**
 * 等待硬盘就绪，及循环等待主状态控制器忙标志位复位；
 * 若仅有就绪或寻道结束标志置位，则成功并返回 0；
 * 若经过一段时间后仍为忙 则返回 1 
 * @return 控制器就绪 - 0，控制器忙 - 1
*/
static int drive_busy(void)
{
	unsigned int i;

	for (i = 0; i < 10000; i++)
		if (READY_STAT == (inb_p(HD_STATUS) & (BUSY_STAT|READY_STAT))) // 循环 10000次 一旦就绪标志位置位则退出循环
			break;
	i = inb(HD_STATUS); // 再取硬盘状态
	i &= BUSY_STAT | READY_STAT | SEEK_STAT; // 检测忙位、就绪位和寻道结束位
	if (i == READY_STAT | SEEK_STAT) // 仅满足就绪与寻道状态时，返回 0
		return(0);
	printk("HD controller times out\n\r"); // 忙时输出日志，同时返回 1
	return(1);
}

/**
 * 复位磁盘控制器
*/
static void reset_controller(void)
{
	int	i;

	outb(4,HD_CMD); 				// 向控制寄存器端口发送 4-复位 控制字节
	for(i = 0; i < 100; i++) nop(); // 等待一段时间（复位完成）后
	outb(hd_info[0].ctl & 0x0f ,HD_CMD); // 在发送正常的控制字节（不禁止重试、重读）
	if (drive_busy()) 				// 等待硬盘就绪超时，展示错误信息
		printk("HD-controller still busy\n\r");
	if ((i = inb(HD_ERROR)) != 1) 	// 读取硬盘错误寄存器，若有错误则提示
		printk("HD-controller reset failed: %02x\n\r",i);
}

/**
 * 磁盘复位函数
 * @param nr 当前硬盘号
*/
static void reset_hd(int nr)
{
	reset_controller(); 							// 复位控制器
	hd_out(nr,hd_info[nr].sect,hd_info[nr].sect,hd_info[nr].head-1,
		hd_info[nr].cyl,WIN_SPECIFY,&recal_intr); 	// 发送硬盘控制命令 "建立驱动器参数"
}

/**
 * 中断处理函数设置有误时，执行
*/
void unexpected_hd_interrupt(void)
{
	printk("Unexpected HD interrupt\n\r");
}

/**
 * 读写硬盘失败调用函数
*/
static void bad_rw_intr(void)
{
	// 如果读取硬盘出错次数大于等于最大值（默认为 7 ）时，则处理错误并处理下一个请求
	if (++CURRENT->errors >= MAX_ERRORS)
		end_request(0);
	// 如果读取硬盘出错次数大于最大值的一半（默认为 3 ）时，则置重置位
	if (CURRENT->errors > MAX_ERRORS/2)
		reset = 1;
}

/**
 * 读中断调用函数，执行硬盘中断请求中调用
*/
static void read_intr(void)
{
	// 判断当前硬盘控制器状态是否能使用
	if (win_result()) {
		bad_rw_intr();
		do_hd_request();
		return;
	}
	// 一次读取一个扇区 (256 字节) 到缓冲区
	port_read(HD_DATA,CURRENT->buffer,256);
	CURRENT->errors = 0; 	// 重置错误次数
	CURRENT->buffer += 512; // 移动缓冲区指针
	CURRENT->sector++; 		// 扇区 + 1
	// 数据未读完时 
	if (--CURRENT->nr_sectors) {
		do_hd = &read_intr; // 再次置硬盘调用 c 函数指针为 read_intr()
		return;
	}
	// 读完后，正常退出，通知阻塞的进程，处理下一请求
	end_request(1); 
	do_hd_request();
}

/**
 * 写中断调用函数，执行写中断请求中调用
*/
static void write_intr(void)
{
	// 判断当前硬盘控制器状态是否能使用
	if (win_result()) {
		bad_rw_intr();
		do_hd_request();
		return;
	}
	// 当前需要写的扇区还未写完时
	if (--CURRENT->nr_sectors) {
		CURRENT->sector++; // 扇区号 + 1
		CURRENT->buffer += 512; // 缓冲区 +1
		do_hd = &write_intr; // 再次调用 读 函数
		port_write(HD_DATA,CURRENT->buffer,256); // 读取一个扇区
		return;
	}
	// 读完后，正常退出，通知阻塞的进程，处理下一请求
	end_request(1);
	do_hd_request();
}

/**
 * 硬盘重新矫正（复位）中断调用函数；在硬盘中断处理函数中调用
 * 如果硬盘控制器返回错误信息，首先进行硬盘读写失败处理，然后请求硬盘做相应（复位）处理
*/
static void recal_intr(void)
{
	if (win_result())
		bad_rw_intr();
	do_hd_request(); // 重新执行请求
}

/**
 * 硬盘读写请求执行方法
*/
void do_hd_request(void)
{
	int i,r;
	unsigned int block,dev;
	unsigned int sec,head,cyl;
	unsigned int nsect;

	INIT_REQUEST; 				// 检测请求项的合法性
	dev = MINOR(CURRENT->dev); 	// dev 指向当前分区号
	block = CURRENT->sector; 	// bolck 指向当前需操作的起始扇区
	
	// 当分区不存在以及起始扇区大于该分区扇区数-2（因为每次读写要求两个扇区，因此读取起始扇区不能大于该分区扇区数-2）时，直接退出
	if (dev >= 5*NR_HD || block+2 > hd[dev].nr_sects) {
		end_request(0);
		goto repeat;
	}
	block += hd[dev].start_sect; // 指向实际硬盘起始扇区
	dev /= 5; 					// dev 指向实际硬盘号 0 或者 1
	// 下面两行嵌入式汇编代码，根据硬盘起始扇区号和每磁道扇区数计算在磁道中的扇区号（sec）、所在柱面号（cyl）和磁头号（head）
	__asm__("divl %4":"=a" (block),"=d" (sec):"0" (block),"1" (0),
		"r" (hd_info[dev].sect));
	__asm__("divl %4":"=a" (cyl),"=d" (head):"0" (block),"1" (0),
		"r" (hd_info[dev].head));
	sec++;
	nsect = CURRENT->nr_sectors; //预读取扇区数
	// 如果复位标志为 1 这执行复位操作
	if (reset) {
		reset = 0;
		recalibrate = 1; 		// 复位后需要进行重新矫正
		reset_hd(CURRENT_DEV);
		return;
	}
	// 当重新矫正标志置位时，首先需要重新矫正标志
	if (recalibrate) {
		recalibrate = 0;
		hd_out(dev,hd_info[CURRENT_DEV].sect,0,0,0,
			WIN_RESTORE,&recal_intr);	// 发送重新矫正命令
		return;
	}	
	// 处理写请求
	if (CURRENT->cmd == WRITE) {
		// 发写读命令
		hd_out(dev,nsect,sec,head,cyl,WIN_WRITE,&write_intr);
		// 等待 硬盘控制器请求服务位置位
		for(i=0 ; i<3000 && !(r=inb_p(HD_STATUS)&DRQ_STAT) ; i++)
			/* nothing */ ;
		// 循环结束后依然未就绪，则此次写失败，处理下一个硬盘请求
		if (!r) {
			bad_rw_intr();
			goto repeat;
		}
		// 写 一个扇区
		port_write(HD_DATA,CURRENT->buffer,256);
		// 处理读请求
	} else if (CURRENT->cmd == READ) {
		// 发送读命令
		hd_out(dev,nsect,sec,head,cyl,WIN_READ,&read_intr);
	} else
		panic("unknown hd-command");
}

/**
 * 硬盘初始化程序
*/
void hd_init(void)
{
	blk_dev[MAJOR_NR].request_fn = DEVICE_REQUEST;
	// 设置 硬盘中断向量为 46
	set_intr_gate(0x2E,&hd_interrupt);
	outb_p(inb_p(0x21)&0xfb,0x21); // 复位主 8259A int2 屏蔽位，允许从片发送中断信号
	outb(inb_p(0xA1)&0xbf,0xA1); // 复位 硬盘中断屏蔽位，允许硬盘控制器发送中断请求信号
}
