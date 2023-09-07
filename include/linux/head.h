#ifndef _HEAD_H
#define _HEAD_H

/**
 * 定义段描述符的数据结构 
*/
typedef struct desc_struct {
	unsigned long a,b; // 每个描述符由8字节构成，每个描述符表共有 256 项
} desc_table[256];

extern unsigned long pg_dir[1024]; // 内存目录页数组，每个目录项 4 字节，从物理地址0开始
extern desc_table idt,gdt; // 中断描述表，全局描述符表

#define GDT_NUL 0 // 全局描述符 0 项不用
#define GDT_CODE 1 // 第一项，内核代码段描述符项
#define GDT_DATA 2 // 第二项。内核数据段描述符项
#define GDT_TMP 3 // 第三项，系统段描述符，Linux 未使用

#define LDT_NUL 0 // 每个局部描述符第一项不用
#define LDT_CODE 1 // 第一项，用户代码段描述符项
#define LDT_DATA 2 // 第二项，用户程序数据段描述符项

#endif
