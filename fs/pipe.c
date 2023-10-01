/*
 *  linux/fs/pipe.c
 *
 *  (C) 1991  Linus Torvalds
 */

#include <signal.h>

#include <linux/sched.h>
#include <linux/mm.h>	/* for get_free_page */
#include <asm/segment.h>

/**
 * 读管道文件
 * @param inode 管道文件 i 节点
 * @param buf 数据缓冲区指针
 * @param count 希望读取的字节数
 * @return 读取的字节数
*/
int read_pipe(struct m_inode * inode, char * buf, int count)
{
	int chars, size, read = 0;

	while (count>0) {
		while (!(size=PIPE_SIZE(*inode))) {	
			wake_up(&inode->i_wait);			// 管道为空时，唤醒等待写进程
			if (inode->i_count != 2) 			// 管道 i 节点没有写进程时，返回已经读取的字节数
				return read;
			sleep_on(&inode->i_wait);			// 等待写进程写入数据后将本读进程唤醒
		}
		chars = PAGE_SIZE-PIPE_TAIL(*inode);	// 获取管道数据区还能写的字节数
		if (chars > count)						// chars 设置为剩余字节数、管道中还未读取字符数与剩余字符数中的最小值
			chars = count;
		if (chars > size)
			chars = size;
		count -= chars;							// count 更新为还需要读取的字节数
		read += chars;							// 计算已经读取的字节数
		size = PIPE_TAIL(*inode);				// size 指向管道尾部
		PIPE_TAIL(*inode) += chars;				// 更新管道尾部
		PIPE_TAIL(*inode) &= (PAGE_SIZE-1);
		while (chars-->0)						// 循环读取指定管道中 chars 字节数据
			put_fs_byte(((char *)inode->i_size)[size++],buf++);
	}
	wake_up(&inode->i_wait);					// 唤醒等待的写进程
	return read;
}
	
/**
 * 写管道文件
 * @param inode 管道文件 i 节点
 * @param buf 数据缓冲区指针
 * @param count 希望写入的字节数
 * @return 写入的字节数
*/
int write_pipe(struct m_inode * inode, char * buf, int count)
{
	int chars, size, written = 0;

	while (count>0) {
		while (!(size=(PAGE_SIZE-1)-PIPE_SIZE(*inode))) {
			wake_up(&inode->i_wait);					// 当前管道满时，唤醒等待的该管道的读进程
			if (inode->i_count != 2) {					// 如果已经没有读管道，则向当前进程发送 SIGPIPE 信号，并返回已经写的字节数
				current->signal |= (1<<(SIGPIPE-1));
				return written?written:-1;
			}
			sleep_on(&inode->i_wait);					// 等待读管道进程将管道中数据读出
		}
		chars = PAGE_SIZE-PIPE_HEAD(*inode);			// 将 chars 字节数设置为当前剩余未写字节数、当前管道剩余数据字节数与当前管道剩余字节数中的最小小值
		if (chars > count)
			chars = count;
		if (chars > size)
			chars = size;
		count -= chars;									// 更新还需写字节数
		written += chars;								// 更新已经写的字节数
		size = PIPE_HEAD(*inode);						// 将 size 指向管道头部位置
		PIPE_HEAD(*inode) += chars;						// 更新管道头指针
		PIPE_HEAD(*inode) &= (PAGE_SIZE-1);
		while (chars-->0)								// 将 buf 中指定数量多的字符复制到管道中
			((char *)inode->i_size)[size++]=get_fs_byte(buf++);
	}
	wake_up(&inode->i_wait);							// 唤醒等待的该管道的读进程
	return written;
}

/**
 * 创建管道系统调用函数
 * @param fildes 文件句柄对指针（fildes[0] 用于读缓冲中的数据，fildes[1] 用于向管道中写数据）
 * @return 成功 - 0，出错 - 1
*/
int sys_pipe(unsigned long * fildes)
{
	struct m_inode * inode;
	struct file * f[2];
	int fd[2];
	int i,j;

	j=0;
	for(i=0;j<2 && i<NR_FILE;i++)				// 遍历文件表查询两个空闲文件项，并将其引用计数设置为 1
		if (!file_table[i].f_count)
			(f[j++]=i+file_table)->f_count++;
	if (j==1)									// j等于1时，只申请到了 1 的空闲项，将其引用计数置为 0
		f[0]->f_count=0;
	if (j<2)									// j 小于 2，说明未申请到足够的文件项，返回 -1
		return -1;
	j=0;
	for(i=0;j<2 && i<NR_OPEN;i++)				// 遍历当前进程打开文件表，查询两个当前进程空闲文件项，将指定文件项指针指向申请的文件
		if (!current->filp[i]) {
			current->filp[ fd[j]=i ] = f[j];
			j++;
		}
	if (j==1)									// j等于1时，只申请到了 1 的空闲项，将其设置为空
		current->filp[fd[0]]=NULL;
	if (j<2) {									// j 小于 2，说明未申请到足够的文件项，将申请到的两个空闲项引用计数置0，并返回 -1
		f[0]->f_count=f[1]->f_count=0;
		return -1;
	}
	if (!(inode=get_pipe_inode())) {			// 申请管道 i 节点失败
		current->filp[fd[0]] =
			current->filp[fd[1]] = NULL;		// 将申请到的本进程空闲文件项置为 NULL
		f[0]->f_count = f[1]->f_count = 0;		// 将申请到的两个空闲项引用计数置0
		return -1;								// 返回 -1
	}
	f[0]->f_inode = f[1]->f_inode = inode;		// 将申请到的本进程空闲文件项 i 节点设置为申请到管道 i 节点
	f[0]->f_pos = f[1]->f_pos = 0;				// 将指定文件偏移位置为 0
	f[0]->f_mode = 1;							// 第一个文件设置为读管道文件		
	f[1]->f_mode = 2;							// 第二个文件设置为写管道文件
	put_fs_long(fd[0],0+fildes);				// fildes[0] 指向 fd[0]
	put_fs_long(fd[1],1+fildes);				// fildes[1] 指向 fd[1]
	return 0;
}
