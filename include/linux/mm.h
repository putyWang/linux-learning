#ifndef _MM_H
#define _MM_H

#define PAGE_SIZE 4096  // 定义内存页面的大小（字节数）

extern unsigned long get_free_page(void);
extern unsigned long put_page(unsigned long page,unsigned long address);
extern void free_page(unsigned long addr);

#endif
