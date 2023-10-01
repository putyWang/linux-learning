/*
 *  linux/fs/exec.c
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 * #!-checking implemented by tytso.
 */

/*
 * Demand-loading implemented 01.12.91 - no need to read anything but
 * the header into memory. The inode of the executable is put into
 * "current->executable", and page faults do the actual loading. Clean.
 *
 * Once more I can proudly say that linux stood up to being changed: it
 * was less than 2 hours work to get demand-loading completely implemented.
 */

#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <a.out.h>

#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <asm/segment.h>

extern int sys_exit(int exit_code);
extern int sys_close(int fd);

/*
 * MAX_ARG_PAGES defines the number of pages allocated for arguments
 * and envelope for the new program. 32 should suffice, this gives
 * a maximum env+arg of 128kB !
 */
#define MAX_ARG_PAGES 32		// 新程序分配给参数和环境变量使用的最大页数，最大内存为 128 KB0
 
/*
 * create_tables() parses the env- and arg-strings in new user
 * memory and creates the pointer tables from them, and puts their
 * addresses on the "stack", returning the new stack pointer value.
 */
/**
 * 在新用户堆栈中创建环境和参数变量指针表
 * @param p 以数据段为起点的参数和环境信息偏移指针
 * @param argc 参数个数
 * @param envc 环境变量数
 * @return 堆栈指针
*/
static unsigned long * create_tables(char * p,int argc,int envc)
{
	unsigned long *argv,*envp;
	unsigned long * sp;
	sp = (unsigned long *) (0xfffffffc & (unsigned long) p);	// 堆栈指针是以 4 字节（1节）为边界寻址的，因此这里让 sp 为 4 的整数倍
	sp -= envc+1;								// sp 向下移动，空出环境参数所占的空间个数，并让环境参数指针 envp 指向该处
	envp = sp;
	sp -= argc+1;								// sp 向下移动，空出命令行参数所占的空间个数，并让命令行参数指针 argv 指向该处
	argv = sp;
	put_fs_long((unsigned long)envp,--sp);		// 将环境参数指针压入堆栈
	put_fs_long((unsigned long)argv,--sp);		// 将命令行参数指针压入堆栈
	put_fs_long((unsigned long)argc,--sp);		// 将参数个数压入堆栈
	while (argc-->0) {
		put_fs_long((unsigned long) p,argv++);	// 将命令行参数放入argv指针指向位置
		while (get_fs_byte(p++)) /* nothing */;
	}
	put_fs_long(0,argv);						// 最后放入一个 NULL 字符
	while (envc-->0) {
		put_fs_long((unsigned long) p,envp++);	// 将环境参数放入envp指针指向位置
		while (get_fs_byte(p++)) /* nothing */ ;
	}
	put_fs_long(0,envp);						// 最后放入一个 NULL 字符
	return sp; 									// 返回最新的堆栈指针
}

/**
 * 获取命令行参数数
 * @param argv 命令行参数指针
 * @return 命令行参数个数
*/
static int count(char ** argv)
{
	int i=0;
	char ** tmp;

	if (tmp = argv)
		while (get_fs_long((unsigned long *) (tmp++)))
			i++;

	return i;
}

/*
 * 'copy_string()' copies argument/envelope strings from user
 * memory to free pages in kernel mem. These are in a format ready
 * to be put directly into the top of new user memory.
 *
 * Modified by TYT, 11/24/91 to add the from_kmem argument, which specifies
 * whether the string and the string array are from user or kernel segments:
 * 
 * from_kmem     argv *        argv **
 *    0          user space    user space
 *    1          kernel space  user space
 *    2          kernel space  kernel space
 * 
 * We do this by playing games with the fs segment register.  Since it
 * it is expensive to load a segment register, we try to avoid calling
 * set_fs() unless we absolutely have to.
 */
/**
 * 复制指定个数到的参数字符串到参数和环境空间
 * @param argc 命令行参数个数
 * @param argv 命令行参数指针
 * @param page 参数和环境空间页面指针数组
 * @param p	参数表空间中的偏移指针，始终指向已复制串的头部
 * @param from_kmem 字符串来源标志
 * @return 参数和环境空间当前头部指针
*/
static unsigned long copy_strings(int argc,char ** argv,unsigned long *page,
		unsigned long p, int from_kmem)
{
	char *tmp, *pag;
	int len, offset = 0;
	unsigned long old_fs, new_fs;

	if (!p)
		return 0;	/* bullet-proofing */
	new_fs = get_ds();			// 将 ds 段寄存器中的值存储到 new_fs 中
	old_fs = get_fs();			// 将 fs 段寄存器中的值存储到 old_fs 中
	if (from_kmem==2)			// 如果字符串和字符串数组都来自内核空间，则设置 fs 段寄存器指向内核数据段（ds）
		set_fs(new_fs);
	while (argc-- > 0) {		// 循环处理各个参数，从最后一参数逆向开始复制到指定偏移地址处
		if (from_kmem == 1)		// 字符串在用户空间而字符串数组在内核空间，则设置 fs 段寄存器指向内核数据段（ds）
			set_fs(new_fs);
		if (!(tmp = (char *)get_fs_long(((unsigned long *)argv)+argc)))	// 取 fs 段总最后一参数指针到 tmp
			panic("argc is wrong");
		if (from_kmem == 1)		// 字符串在用户空间而字符串数组在内核空间，恢复 fs 段寄存器值
			set_fs(old_fs);
		len=0;		/* remember zero-padding */
		do {
			len++;				// 计算该参数字符串长度 len，并使 tmp 指向该参数字符串末端（字符串以 NULL 结尾）
		} while (get_fs_byte(tmp++));
		if (p-len < 0) {		// 如果该字符串长度超过此时参数和环境空间中还剩余的空闲长度，则恢复 fs 段寄存器并返回 0
			set_fs(old_fs);
			return 0;
		}
		while (len) {
			--p; --tmp; --len;
			if (--offset < 0) {
				offset = p % PAGE_SIZE;						// 获取 p 在当前页中偏移位置
				if (from_kmem==2)							// 如果字符串和字符串数组都来自内核空间，则恢复 fs 段寄存器值
					set_fs(old_fs);
				if (!(pag = (char *) page[p/PAGE_SIZE]) &&	// 偏移值 p 所在的串空间页面指针数组项 page[p/PAGE_SIZE]) 相应页项不存在时
				    !(pag = (char *) page[p/PAGE_SIZE] =
				      (unsigned long *) get_free_page()))	// 偏移值 p 所在的串空间页面指针数组项 page[p/PAGE_SIZE]) 指向新页面
					return 0;
				if (from_kmem==2)							// 如果字符串和字符串数组都来自内核空间，则设置 fs 段寄存器指向内核数据段（ds）
					set_fs(new_fs);

			}
			*(pag + offset) = get_fs_byte(tmp);				// 将 tmp 指向的字节复制到 p 指针所指向的地址
		}
	}
	if (from_kmem==2)										// 如果字符串和字符串数组都来自内核空间，则恢复 fs 段寄存器值
		set_fs(old_fs);
	return p;
}

/**
 * 修改局部描述表中的描述符基址和段限长，并将参数和环境空间页面放置在数据段末端
 * @param text_size 执行文件头中 a_text 字段的给出的代码段长度值
 * @param page 参数和环境空间页面指针
 * @return 数据段限长值（64MB）
*/
static unsigned long change_ldt(unsigned long text_size,unsigned long * page)
{
	unsigned long code_limit,data_limit,code_base,data_base;
	int i;

	
	code_limit = text_size+PAGE_SIZE -1;
	code_limit &= 0xFFFFF000;				// 根据 text_size 计算以页面长度为边界的代码段限长
	data_limit = 0x4000000; 				// 设置代码段长度为 64 MB
	code_base = get_base(current->ldt[1]); 	// 将代码段基址与数据段基址全部设置为当前进程中的代码段描述符表中的代码段基址
	data_base = code_base;
	set_base(current->ldt[1],code_base);	// 设置当前进程的代码段基址
	set_limit(current->ldt[1],code_limit);	// 设置当前进程的代码段限长
	set_base(current->ldt[2],data_base);	// 设置当前进程的数据段描述符基址
	set_limit(current->ldt[2],data_limit);	// 设置当前进程的数据段描述符限长
	__asm__("pushl $0x17\n\tpop %%fs"::); 	// fs段寄存器中放入局部表数据段描述符的选择符（0x17）+
	data_base += data_limit;				// 将参数与环境空已存放数据的页面放到数据段线性地址的末端
	for (i=MAX_ARG_PAGES-1 ; i>=0 ; i--) {	// 将参数和环境空间已存放数据的页面（共可有 MAX_ARG_PAGES 页，128KB）放到数据段线性地址的末端
		data_base -= PAGE_SIZE;
		if (page[i])
			put_page(page[i],data_base);
	}
	return data_limit;						// 返回数据段限长
}

/*
 * 'do_execve()' executes a new program.
 */
/**
 * execve 系统中断调用函数，加载并执行子进程
 * @param eip 指向堆栈中调用系统中段的程序代码指针 eip 处
 * @param tmp 系统中断在调用 sys_execve 时的返回地址
 * @param filename 被执行程序文件名
 * @param argv 被执行程序指针参数
 * @param envp 环境变量指针数组
 * @return 调用成功不返回，失败设置出错号，并返回 -1
*/
int do_execve(unsigned long * eip,long tmp,char * filename,
	char ** argv, char ** envp)
{
	struct m_inode * inode;
	struct buffer_head * bh;
	struct exec ex;
	unsigned long page[MAX_ARG_PAGES]; 			// 参数和环境字符串空间的页面指针数组 
	int i,argc,envc;
	int e_uid, e_gid;
	int retval;
	int sh_bang = 0; 							// 控制是否需要执行脚本处理代码
	unsigned long p=PAGE_SIZE*MAX_ARG_PAGES-4;	// 参数和环境字符串空间中的的偏移指针，初始化为指向该空间的最后一个长字处
	if ((0xffff & eip[1]) != 0x000f)			// eip[1] 中是源代码段寄存器 cs，其中选择符不可以是内核段选择符，即内核不能调用本函数
		panic("execve called from supervisor mode");
	for (i=0 ; i<MAX_ARG_PAGES ; i++)			// 初始化参数和环境串空间的页面指针数组（表）
		page[i]=0;
	if (!(inode=namei(filename)))				// 获取可执行 文件的 i 节点
		return -ENOENT;
	argc = count(argv); 						// 参数数
	envc = count(envp); 						// 环境变量数
	
restart_interp:
	if (!S_ISREG(inode->i_mode)) {				// 该文件只能为常规文件
		retval = -EACCES;
		goto exec_error2;
	}
	i = inode->i_mode;							// 检查文件的执行权限
	e_uid = (i & S_ISUID) ? inode->i_uid : current->euid;
	e_gid = (i & S_ISGID) ? inode->i_gid : current->egid;
	if (current->euid == inode->i_uid)
		i >>= 6;
	else if (current->egid == inode->i_gid)
		i >>= 3;
	if (!(i & 1) &&
	    !((inode->i_mode & 0111) && suser())) {
		retval = -ENOEXEC;
		goto exec_error2;
	}
	if (!(bh = bread(inode->i_dev,inode->i_zone[0]))) {	// 读取 i 节点的直接块 0（文件的执行头）
		retval = -EACCES;
		goto exec_error2;
	}
	ex = *((struct exec *) bh->b_data); 				// 获取文件执行头
	if ((bh->b_data[0] == '#') && (bh->b_data[1] == '!') && (!sh_bang)) {	// 以 #!（脚本文件） 开头且sh_bang 未置位，处理脚本文件的执行
		char buf[1023], *cp, *interp, *i_name, *i_arg;
		unsigned long old_fs;
		strncpy(buf, bh->b_data+2, 1022);		// 复制文件 #! 后面的头一行字符串到 buf 字符数组中
		brelse(bh); 							// 释放指定缓冲块
		iput(inode); 							// 释放 i 节点
		buf[1022] = '\0';						// 取第一行内容，并删除开始的空格、制表符
		if (cp = strchr(buf, '\n')) {
			*cp = '\0';
			for (cp = buf; (*cp == ' ') || (*cp == '\t'); cp++);
		}
		if (!cp || *cp == '\0') {				// 只有空格、制表符设置出错码，跳转到错误处理
			retval = -ENOEXEC;
			goto exec_error1;
		}
		interp = i_name = cp;					// 处理后就得到了开头是脚本解释执行程序名称的第一行内容
		i_arg = 0;
		for ( ; *cp && (*cp != ' ') && (*cp != '\t'); cp++) {
 			if (*cp == '/')						// 第一个字符串为脚本指执行程序文件名（i_name）
				i_name = cp+1;
		}
		if (*cp) {								// 若文件名后还有字符，应该是参数串，令 i_arg 指向该串
			*cp++ = '\0';
			i_arg = cp;
		}
		if (sh_bang++ == 0) {					// 若 sh_bang 标志没有设置，则设置他同时复制指定个数的环境变量串以及参数串到参数和环境空间中
			p = copy_strings(envc, envp, page, p, 0);
			p = copy_strings(--argc, argv+1, page, p, 0);
		}
		p = copy_strings(1, &filename, page, p, 1);	// 复制脚本程序名到参数与环境空间中
		argc++;
		if (i_arg) {
			p = copy_strings(1, &i_arg, page, p, 2);	// 复制解释程序的参数到参数和环境空间中
			argc++;
		}
		p = copy_strings(1, &i_name, page, p, 2);	// 复制解释程序文件名到参数和环境空间中
		argc++;
		if (!p) {
			retval = -ENOMEM;
			goto exec_error1;
		}
		old_fs = get_fs();				// 保留原 fs 段寄存器（指向原用户数据段），现置其指向内核数据段
		set_fs(get_ds());
		if (!(inode=namei(interp))) {	// 取解释程序的 i 节点，并跳转到 restart_interp 处重新处理
			set_fs(old_fs);
			retval = -ENOENT;
			goto exec_error1;
		}
		set_fs(old_fs);
		goto restart_interp;
	}
	brelse(bh);							// 释放该缓冲区
	// 下面对执行头信息进行处理
	// 当执行文件不是需求页可执行文件、代码重定位部分长度 a_trsize 不等于 0 、数据重定位信息长度补等于 0、代码段+数据段+堆段长度超过 50MB 或 i 节点表明的该执行文件长度小于代码段 + 数据段 + 符号表长度 + 执行头部分长度的总和时不执行程序
	if (N_MAGIC(ex) != ZMAGIC || ex.a_trsize || ex.a_drsize ||
		ex.a_text+ex.a_data+ex.a_bss>0x3000000 ||
		inode->i_size < ex.a_text+ex.a_data+ex.a_syms+N_TXTOFF(ex)) {
		retval = -ENOEXEC;
		goto exec_error2;
	}
	// 执行文件的执行头部长度不等于一个内存块大小（1024 字节），该文件也无法执行544
	if (N_TXTOFF(ex) != BLOCK_SIZE) {
		printk("%s: N_TXTOFF != BLOCK_SIZE. See a.out.h.", filename);
		retval = -ENOEXEC;
		goto exec_error2;
	}
	// 如果 sh_bang 标志位没有设置，表明环境变量页面未被设置，则需要复制指定个数个环境变量字符串到参数与环境空间之中
	if (!sh_bang) {
		p = copy_strings(envc,envp,page,p,0);
		p = copy_strings(argc,argv,page,p,0);
		if (!p) {
			retval = -ENOMEM;
			goto exec_error2;
		}
	}
	if (current->executable)		// 若源程序也是一个可执行文件，则释放其 i 节点，并让进程 executable 指向新程序 i 节点
		iput(current->executable);
	current->executable = inode;
	for (i=0 ; i<32 ; i++)			// 复位所有信号处理句柄
		current->sigaction[i].sa_handler = NULL;
	for (i=0 ; i<NR_OPEN ; i++)		// 根据执行时关闭位图中文件句柄位图标志，关闭指定打开文件，并对该标志进行复位
		if ((current->close_on_exec>>i)&1)
			sys_close(i);
	current->close_on_exec = 0;
	free_page_tables(get_base(current->ldt[1]),get_limit(0x0f));	// 根据指定的基地址和限长，释放源程序代码代码段和数据段所对应的页表内存块指定的内存块及页表本身
	free_page_tables(get_base(current->ldt[2]),get_limit(0x17));
	if (last_task_used_math == current)								// 如果上次任务使用了协处理器指向的是当前进程，将其置空，并复位该标志位
		last_task_used_math = NULL;
	current->used_math = 0;
	p += change_ldt(ex.a_text,page)-MAX_ARG_PAGES*PAGE_SIZE;		// 根据 a_text 修改局部表中描述符基址与限长，并将参数和环境空间页面放置在数据段的末端
	p = (unsigned long) create_tables((char *)p,argc,envc);			// 执行下面语句之后，p 此时是以数据段起始处为原点的偏移值，仍指向参数和环境空间数据开始处，即转换为堆栈指针
	current->brk = ex.a_bss +
		(current->end_data = ex.a_data +
		(current->end_code = ex.a_text));							// 将当前的各个字段设置为新执行程序的信息
	current->start_stack = p & 0xfffff000;							// 将进程堆栈开始字段设置为堆栈指针所在页面，并设置进程用户id与组id
	current->euid = e_uid;
	current->egid = e_gid;
	i = ex.a_text+ex.a_data;										// 初始化一页 bss 数据段（全设置为 0）
	while (i&0xfff)
		put_fs_byte(0,(char *) (i++));
	// 返回指令将弹出这些堆栈数据并使得 CPU 去执行新的程序而不是返回到原调用系统中断的程序中去了
	eip[0] = ex.a_entry;	// 将原调用系统中断的程序在堆栈上的代码指针替换为新执行程序的入口点
	eip[3] = p;				// 并将堆栈指针替换为新执行程序的堆栈指针
	return 0;
exec_error2:
	iput(inode);
exec_error1:
	for (i=0 ; i<MAX_ARG_PAGES ; i++)
		free_page(page[i]);
	return(retval);
}
