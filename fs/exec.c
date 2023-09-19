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
#define MAX_ARG_PAGES 32

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
	// 堆栈指针是以 4 字节（1节）为边界寻址的，因此这里让 sp 为 4 的整数倍
	sp = (unsigned long *) (0xfffffffc & (unsigned long) p);
	// sp 向下移动，空出环境参数所占的空间个数，并让环境参数指针 envp 指向该处
	sp -= envc+1;
	envp = sp;
	// sp 向下移动，空出命令行参数所占的空间个数，并让命令行参数指针 argv 指向该处
	sp -= argc+1;
	argv = sp;
	// 将环境参数指针、命令行参数指针与参数个数压入堆栈
	put_fs_long((unsigned long)envp,--sp);
	put_fs_long((unsigned long)argv,--sp);
	put_fs_long((unsigned long)argc,--sp);
	// 将命令行参数放入前面空出来的相应地方，最后放入一个 NULL 字符
	while (argc-->0) {
		put_fs_long((unsigned long) p,argv++);
		while (get_fs_byte(p++)) /* nothing */ ;
	}
	put_fs_long(0,argv);
	// 将环境参数放入前面空出来的相应地方，最后放入一个 NULL 字符
	while (envc-->0) {
		put_fs_long((unsigned long) p,envp++);
		while (get_fs_byte(p++)) /* nothing */ ;
	}
	put_fs_long(0,envp);
	return sp; // 返回最新的堆栈指针
}

/*
 * count() counts the number of arguments/envelopes
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
static unsigned long copy_strings(int argc,char ** argv,unsigned long *page,
		unsigned long p, int from_kmem)
{
	char *tmp, *pag;
	int len, offset = 0;
	unsigned long old_fs, new_fs;

	if (!p)
		return 0;	/* bullet-proofing */
	new_fs = get_ds();
	old_fs = get_fs();
	if (from_kmem==2)
		set_fs(new_fs);
	while (argc-- > 0) {
		if (from_kmem == 1)
			set_fs(new_fs);
		if (!(tmp = (char *)get_fs_long(((unsigned long *)argv)+argc)))
			panic("argc is wrong");
		if (from_kmem == 1)
			set_fs(old_fs);
		len=0;		/* remember zero-padding */
		do {
			len++;
		} while (get_fs_byte(tmp++));
		if (p-len < 0) {	/* this shouldn't happen - 128kB */
			set_fs(old_fs);
			return 0;
		}
		while (len) {
			--p; --tmp; --len;
			if (--offset < 0) {
				offset = p % PAGE_SIZE;
				if (from_kmem==2)
					set_fs(old_fs);
				if (!(pag = (char *) page[p/PAGE_SIZE]) &&
				    !(pag = (char *) page[p/PAGE_SIZE] =
				      (unsigned long *) get_free_page())) 
					return 0;
				if (from_kmem==2)
					set_fs(new_fs);

			}
			*(pag + offset) = get_fs_byte(tmp);
		}
	}
	if (from_kmem==2)
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

	// 根据 text_size 计算以页面长度为边界的代码段限长
	code_limit = text_size+PAGE_SIZE -1;
	code_limit &= 0xFFFFF000;
	data_limit = 0x4000000; // 设置代码段长度为 64 MB
	// 将代码段基址与数据段基址全部设置为当前进程中的代码段描述符表中的代码段基址
	code_base = get_base(current->ldt[1]); 
	data_base = code_base;
	// 设置当前进程的代码段与数据段描述符基址与限长
	set_base(current->ldt[1],code_base);
	set_limit(current->ldt[1],code_limit);
	set_base(current->ldt[2],data_base);
	set_limit(current->ldt[2],data_limit);
/* make sure fs points to the NEW data segment */
	__asm__("pushl $0x17\n\tpop %%fs"::); // fs段寄存器中放入局部表数据段描述符的选择符（0x17）+
	// 将参数与环境空已存放数据的页面放到数据段线性地址的末端
	data_base += data_limit;
	for (i=MAX_ARG_PAGES-1 ; i>=0 ; i--) {
		data_base -= PAGE_SIZE;
		if (page[i])
			put_page(page[i],data_base);
	}
	return data_limit;
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
	unsigned long page[MAX_ARG_PAGES]; // 参数和环境字符串空间的页面指针数组 
	int i,argc,envc;
	int e_uid, e_gid;
	int retval;
	int sh_bang = 0; // 控制是否需要执行脚本处理代码
	// 参数和环境字符串空间中的的偏移指针，初始化为指向该空间的最后一个长字处
	unsigned long p=PAGE_SIZE*MAX_ARG_PAGES-4;
	// eip[1] 中是源代码段寄存器 cs，其中选择符不可以是内核段选择符，即内核不能调用本函数
	if ((0xffff & eip[1]) != 0x000f)
		panic("execve called from supervisor mode");
	// 初始化参数和环境串空间的页面指针数组（表）
	for (i=0 ; i<MAX_ARG_PAGES ; i++)	/* clear page-table */
		page[i]=0;
	// 获取可执行 文件的 i 节点
	if (!(inode=namei(filename)))		/* get executables inode */
		return -ENOENT;
	argc = count(argv); // 参数数
	envc = count(envp); // 环境变量数
	
restart_interp:
	// 该文件只能为常规文件
	if (!S_ISREG(inode->i_mode)) {	/* must be regular file */
		retval = -EACCES;
		goto exec_error2;
	}
	// 检查文件的执行权限
	i = inode->i_mode;
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
	// 读取 i 节点的直接块 0（保存文件的执行头）
	if (!(bh = bread(inode->i_dev,inode->i_zone[0]))) {
		retval = -EACCES;
		goto exec_error2;
	}
	ex = *((struct exec *) bh->b_data); // 获取文件执行头	/* read exec-header */
	// 以 #! 开头且sh_bang 未置位，处理脚本文件的执行
	if ((bh->b_data[0] == '#') && (bh->b_data[1] == '!') && (!sh_bang)) {
		/*
		 * This section does the #! interpretation.
		 * Sorta complicated, but hopefully it will work.  -TYT
		 */

		char buf[1023], *cp, *interp, *i_name, *i_arg;
		unsigned long old_fs;
		// 复制文件 #! 后面的头一行字符串到 buf 字符数组中
		strncpy(buf, bh->b_data+2, 1022);
		brelse(bh); // 释放指定缓冲块
		iput(inode); // 释放 i 节点
		// 取第一行内容，并删除开始的空格、制表符
		buf[1022] = '\0';
		if (cp = strchr(buf, '\n')) {
			*cp = '\0';
			for (cp = buf; (*cp == ' ') || (*cp == '\t'); cp++);
		}
		// 只有空格、制表符设置出错码，跳转到错误处理
		if (!cp || *cp == '\0') {
			retval = -ENOEXEC; /* No interpreter name found */
			goto exec_error1;
		}
		// 处理后就得到了开头是脚本解释执行程序名称的第一行内容
		interp = i_name = cp;
		i_arg = 0;
		for ( ; *cp && (*cp != ' ') && (*cp != '\t'); cp++) {
 			if (*cp == '/')
				i_name = cp+1;
		}
		// 若文件名后还有字符，则应该是参数串，令 i_arg 指向该串
		if (*cp) {
			*cp++ = '\0';
			i_arg = cp;
		}
		/*
		 * OK, we've parsed out the interpreter name and
		 * (optional) argument.
		 */
		// 若 sh_bang 标志没有设置，则设置他同时复制指定个数的环境变量串以及参数串到参数和环境空间中
		if (sh_bang++ == 0) {
			p = copy_strings(envc, envp, page, p, 0);
			p = copy_strings(--argc, argv+1, page, p, 0);
		}
		/*
		 * Splice in (1) the interpreter's name for argv[0]
		 *           (2) (optional) argument to interpreter
		 *           (3) filename of shell script
		 *
		 * This is done in reverse order, because of how the
		 * user environment and arguments are stored.
		 */
		// 复制脚本程序名到参数与环境空间中
		p = copy_strings(1, &filename, page, p, 1);
		// 复制解释程序的参数到参数和环境空间中
		argc++;
		if (i_arg) {
			p = copy_strings(1, &i_arg, page, p, 2);
			argc++;
		}
		// 复制解释程序文件名到参数和环境空间中
		p = copy_strings(1, &i_name, page, p, 2);
		argc++;
		if (!p) {
			retval = -ENOMEM;
			goto exec_error1;
		}
		/*
		 * OK, now restart the process with the interpreter's inode.
		 */
		// 保留原 fs 段寄存器（指向原用户数据段），现置其指向内核数据段
		old_fs = get_fs();
		set_fs(get_ds());
		// 取解释程序的 i 节点，并跳转到 restart_interp 处重新处理
		if (!(inode=namei(interp))) { /* get executables inode */
			set_fs(old_fs);
			retval = -ENOENT;
			goto exec_error1;
		}
		set_fs(old_fs);
		goto restart_interp;
	}
	brelse(bh);// 释放该缓冲区
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
/* OK, This is the point of no return */
	// 若源程序也是一个可执行文件，则释放其 i 节点，并让进程 executable 指向新程序 i 节点
	if (current->executable)
		iput(current->executable);
	current->executable = inode;
	// 复位所有信号处理句柄
	for (i=0 ; i<32 ; i++)
		current->sigaction[i].sa_handler = NULL;
	// 根据执行时关闭位图中文件句柄位图标志，关闭指定打开文件，，并对该标志进行复位
	for (i=0 ; i<NR_OPEN ; i++)
		if ((current->close_on_exec>>i)&1)
			sys_close(i);
	current->close_on_exec = 0;
	// 根据指定的基地址和限长，释放源程序代码代码段和数据段所对应的页表内存块指定的内存块及页表本身
	free_page_tables(get_base(current->ldt[1]),get_limit(0x0f));
	free_page_tables(get_base(current->ldt[2]),get_limit(0x17));
	// 如果上次任务使用了协处理器指向的是当前进程，将其置空，并复位该标志位
	if (last_task_used_math == current)
		last_task_used_math = NULL;
	current->used_math = 0;
	// 根据 a_text 修改局部表中描述符基址与限长，并将参数和环境空间页面放置在数据段的末端
	// 执行下面语句之后，p 此时是以数据段起始处为原点的偏移值，仍指向参数和环境空间数据开始处，即转换为堆栈指针
	p += change_ldt(ex.a_text,page)-MAX_ARG_PAGES*PAGE_SIZE;
	p = (unsigned long) create_tables((char *)p,argc,envc);
	// 将当前的各个字段设置为新执行程序的信息
	current->brk = ex.a_bss +
		(current->end_data = ex.a_data +
		(current->end_code = ex.a_text));
	// 将进程堆栈开始字段设置为堆栈指针所在页面，并设置进程用户id与组id
	current->start_stack = p & 0xfffff000;
	current->euid = e_uid;
	current->egid = e_gid;
	// 初始化一页 bss 数据段（全设置为 0）
	i = ex.a_text+ex.a_data;
	while (i&0xfff)
		put_fs_byte(0,(char *) (i++));
	// 将原调用系统中断的程序在堆栈上的代码指针替换为新执行程序的入口点
	// 并将堆栈指针替换为新执行程序的堆栈指针
	// 返回指令将弹出这些堆栈数据并使得 CPU 去执行新的程序而不是返回到原调用系统中断的程序中去了
	eip[0] = ex.a_entry;		/* eip, magic happens :-) */
	eip[3] = p;			/* stack pointer */
	return 0;
exec_error2:
	iput(inode);
exec_error1:
	for (i=0 ; i<MAX_ARG_PAGES ; i++)
		free_page(page[i]);
	return(retval);
}
