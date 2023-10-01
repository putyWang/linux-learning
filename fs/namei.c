/*
 *  linux/fs/namei.c
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 * Some corrections by tytso.
 */

#include <linux/sched.h>
#include <linux/kernel.h>
#include <asm/segment.h>

#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <const.h>
#include <sys/stat.h>

#define ACC_MODE(x) ("\004\002\006\377"[(x)&O_ACCMODE])

/*
 * comment out this line if you want names > NAME_LEN chars to be
 * truncated. Else they will be disallowed.
 */
/* #define NO_TRUNCATE */

#define MAY_EXEC 1 		// 可执行权限位
#define MAY_WRITE 2		// 可写权限位
#define MAY_READ 4		// 可读权限位

/**
 * 检测文件访问许可权限
 * @param inode i 节点指针
 * @param mask 访问属性屏蔽码
 * @return 0-无权限，1-有权限
*/
static int permission(struct m_inode * inode,int mask)
{
	int mode = inode->i_mode;

	// 如果该 i 节点有对应的设备单该 i 节点的连接数等于 0 则直接返回
	if (inode->i_dev && !inode->i_nlinks)
		return 0;
	// 当前有效用户与i节点用户id相同，则取文件宿主的用户访问权限
	else if (current->euid==inode->i_uid)
		mode >>= 6;
	// 当前有效组 id 与 i 节点用户组 id 相同，则取组用户的访问权限
	else if (current->egid==inode->i_gid)
		mode >>= 3;
	// 当前进程所属用户或所属用户组拥有对应权限，或该用户位超级用户，表示有权访问文件
	if (((mode & mask & 0007) == mask) || suser())
		return 1;
	return 0; 		// 其他情况返回无权限
}

/**
 * 指定长度字符长比较函数
 * @param len 比较的字符串长度
 * @param name 文件名字符串
 * @param de 目录项结构
 * @return 相同返回 1，不同返回 0
*/
static int match(int len,const char * name,struct dir_entry * de)
{
	register int same __asm__("ax");

	if (!de || !de->inode || len > NAME_LEN)
		return 0;
	if (len < NAME_LEN && de->name[len])
		return 0;
	__asm__("cld\n\t"
		"fs ; repe ; cmpsb\n\t"
		"setz %%al"
		:"=a" (same)
		:"0" (0),"S" ((long) name),"D" ((long) de->name),"c" (len)
		:"cx","di","si");
	return same;
}

/**
 * 查询指定目录是否存在指定名字的目录项
 * @param dir 指定目录 i 节点的指针
 * @param name 文件名
 * @param namelen 文件名长度
 * @param res_dir 返回的目录项结构指针
 * @return 高数缓冲区指针
*/
static struct buffer_head * find_entry(struct m_inode ** dir,
	const char * name, int namelen, struct dir_entry ** res_dir)
{
	int entries;
	int block,i;
	struct buffer_head * bh;
	struct dir_entry * de;
	struct super_block * sb;

// 如果定义了 NO_TRUNCATE，则若文件名长度超过最大长度 NAME_LEN ，则返回
#ifdef NO_TRUNCATE
	if (namelen > NAME_LEN)
		return NULL;
#else
// 未定义 NO_TRUNCATE，则根据最大长度 NAME_LEN 对名字进行截取
	if (namelen > NAME_LEN)
		namelen = NAME_LEN;
#endif
	entries = (*dir)->i_size / (sizeof (struct dir_entry));	// 计算本目录中目录项数 entries
	*res_dir = NULL;										// 置空返回目录项结构指针
	if (!namelen)
		return NULL;
	// .. 指向上一级目录
	if (namelen==2 && get_fs_byte(name)=='.' && get_fs_byte(name+1)=='.') {
		// dir 已经指向根文件目录，则 .. 表示指向当前目录 .
		if ((*dir) == current->root)
			namelen=1;
		// dir 指向本设备的根目录，将 dir 指向当前设备锁安装的 i 节点
		else if ((*dir)->i_num == ROOT_INO) {
			// 在一个安装点上的'..'将会导致目录交换到安装到文件系统的目录 i 节点
			// 获取该目录所在设备文件系统对应的超级块
			sb=get_super((*dir)->i_dev);
			// 释放旧目录i节点，并将目录 i 节点指向超级块所安装的 i 节点
			if (sb->s_imount) {
				iput(*dir);
				(*dir)=sb->s_imount;
				(*dir)->i_count++;
			}
		}
	}
	// i 节点所指向的第一个磁盘块号为 0，返回 NULL
	if (!(block = (*dir)->i_zone[0]))
		return NULL;
	// 将目录节点第一个直接块数据读取到缓存中
	if (!(bh = bread((*dir)->i_dev,block)))
		return NULL;
	// 在目录项数据块中搜索匹配指定文件名的目录项
	i = 0;
	de = (struct dir_entry *) bh->b_data;
	// 遍历目录项中所有数据，找出对应名字的文件项
	while (i < entries) {
		// 超出数据块
		if ((char *)de >= BLOCK_SIZE+bh->b_data) {
			brelse(bh); // 释放高速缓存内存
			bh = NULL;
			// 读入下一目录项数据块，该块不为空时将其读入缓冲中
			if (!(block = bmap(*dir,i/DIR_ENTRIES_PER_BLOCK)) ||
			    !(bh = bread((*dir)->i_dev,block))) {
				i += DIR_ENTRIES_PER_BLOCK;
				continue;
			}
			// 将 de 指向指定块中数据项
			de = (struct dir_entry *) bh->b_data;
		}

		// 该目录项是否与名字相匹配
		if (match(namelen,name,de)) {
			*res_dir = de;
			return bh; 		//返回文件夹所在块
		}
		de++;
		i++;
	}
	brelse(bh);				// 没找到释放缓冲
	return NULL;
}

/**
 * 在指定文件夹中创建指定名字新文件或文件夹
 * @param dir 文件夹i节点指针
 * @param name 文件名
 * @param namelen 文件名长度
 * @param res_dir 返回的文件夹项指针
 * @return 指向新创建文件夹项的指针
*/
static struct buffer_head * add_entry(struct m_inode * dir,
	const char * name, int namelen, struct dir_entry ** res_dir)
{
	int block,i;
	struct buffer_head * bh;
	struct dir_entry * de;

	*res_dir = NULL;
	// 根据配置设置文件名长度 
#ifdef NO_TRUNCATE
	if (namelen > NAME_LEN)
		return NULL;
#else
	if (namelen > NAME_LEN)
		namelen = NAME_LEN;
#endif
	if (!namelen)
		return NULL;
	// 获取文件夹的直接块 0 
	if (!(block = dir->i_zone[0]))
		return NULL;
	// 将直接块 0 读取到缓冲中
	if (!(bh = bread(dir->i_dev,block)))
		return NULL;
	i = 0;
	de = (struct dir_entry *) bh->b_data;
	while (1) {
		// 当遍历完第一个块时，重新申请一块磁盘块
		if ((char *)de >= BLOCK_SIZE+bh->b_data) {
			brelse(bh);
			bh = NULL;
			block = create_block(dir,i/DIR_ENTRIES_PER_BLOCK);
			if (!block)
				return NULL;
			if (!(bh = bread(dir->i_dev,block))) {
				i += DIR_ENTRIES_PER_BLOCK;
				continue;
			}
			de = (struct dir_entry *) bh->b_data;
		}
		// 当遍历到最后一个dir_entry时，更新文件夹i节点参数
		if (i*sizeof(struct dir_entry) >= dir->i_size) {
			de->inode=0;
			dir->i_size = (i+1)*sizeof(struct dir_entry); 	// 将文件夹项数组长度 + 1
			dir->i_dirt = 1;
			dir->i_ctime = CURRENT_TIME;
		}
		// 新的目录项创建后，为其赋初值
		if (!de->inode) {
			dir->i_mtime = CURRENT_TIME;
			// 设置文件名
			for (i=0; i < NAME_LEN ; i++)
				de->name[i]=(i<namelen)?get_fs_byte(name+i):0;
			bh->b_dirt = 1; 								// 将缓冲区设置为已修改
			*res_dir = de; 									// 返回的目录项指向de
			return bh;
		}
		de++;
		i++;
	}
	brelse(bh);
	return NULL;
}

/**
 * 根据给出路径名搜索最顶层目录 i 节点的函数
 * @param pathname 目录路径名
 * @return 目录的i节点指针
*/
static struct m_inode * get_dir(const char * pathname)
{
	char c;
	const char * thisname;
	struct m_inode * inode;
	struct buffer_head * bh;
	int namelen,inr,idev;
	struct dir_entry * de;

	// 当前进程没有根目录节点或者当前进程根目录引用数为 0，表明当前系统出错
	if (!current->root || !current->root->i_count)
		panic("No root inode");
	// 当前进程没有工作目录节点或者当前进程工作目录引用数为 0，表明当前系统出错
	if (!current->pwd || !current->pwd->i_count)
		panic("No cwd inode");
	//pathname 以 '/' 开始时，为绝对路径，从根目录开始查找
	if ((c=get_fs_byte(pathname))=='/') {
		inode = current->root;
		pathname++;
	// 其他情况为相对地址，从当前操作目录开始操作
	} else if (c)
		inode = current->pwd;
	else
		return NULL;
	inode->i_count++; 		// 对应起始 i 节点引用计数 +1
	while (1) {
		thisname = pathname;
		// i 节点不是目录，且没有访问该目录的访问许可，则释放该 i 节点 ，并返回 NULL
		if (!S_ISDIR(inode->i_mode) || !permission(inode,MAY_EXEC)) {
			iput(inode); 		// 释放i节点
			return NULL;
		}
		// 从路径名开始检索字符，直到字符已经是结尾符（NULL）或者是 '/'，此时 namelen 为需要处理的文件名长度
		for(namelen=0;(c=get_fs_byte(pathname++))&&(c!='/');namelen++)
			/* nothing */ ;
		// c 为空时 表明已经到达指定文件夹
		// 文件名不是以 '/' 结尾的，因此文件结尾处 c = null 
		if (!c)
			return inode;
		// 查询当前文件夹中匹配项内容
		if (!(bh = find_entry(&inode,thisname,namelen,&de))) {
			iput(inode); 				// 不存在则释放i节点同时写回盘中
			return NULL;
		}
		inr = de->inode;				// 获取查询到的目录项 i 节点号
		idev = inode->i_dev;			// 获取当前 i 节点的设备号
		brelse(bh); 					// 释放 高速缓存
		iput(inode);  					// 释放 i 节点
		if (!(inode = iget(idev,inr)))	// 获取对应 i 节点数据
			return NULL;
	}
}

/**
 * 获取指定路径所在的最顶层目录的 i 节点指针，同时设文件名
 * @param pathname 路径名
 * @param pathname 需要返回的路径名长度
 * @param pathname 需要返回的最顶层的目录名称
*/
static struct m_inode * dir_namei(const char * pathname,
	int * namelen, const char ** name)
{
	char c;
	const char * basename;
	struct m_inode * dir;
	if (!(dir = get_dir(pathname))) 	// 获取指定目录最顶层目录的 i 节点
		return NULL;
	basename = pathname;
	while (c=get_fs_byte(pathname++))	// 获取不带路径的文件名
		if (c=='/')
			basename=pathname;
	*namelen = pathname-basename-1; 	// 获取文件名长度
	*name = basename; 					// 获取文件名
	return dir;
}

/**
 * 根据指定路径名获取对应文件 i 节点指针
 * @param pathname 完整路径名
 * @return 路径对应的 i 节点指针
*/
struct m_inode * namei(const char * pathname)
{
	const char * basename;
	int inr,dev,namelen;
	struct m_inode * dir;
	struct buffer_head * bh;
	struct dir_entry * de;

	if (!(dir = dir_namei(pathname,&namelen,&basename)))	// 获取文件所在目录的i 节点（dir），不带路径的名字长（namelen），不带路径的文件名（basename）
		return NULL;
	if (!namelen)											// 文件名长度为0时，说明指定路径为文件夹，直接返回
		return dir;
	bh = find_entry(&dir,basename,namelen,&de);				// de 指向指定文件名对应的目录项
	if (!bh) {
		iput(dir);
		return NULL;
	}
	inr = de->inode;										// 文件 i 节点号
	dev = dir->i_dev;										// 文件设备号
	brelse(bh);												// 释放对应缓冲区
	iput(dir);												// 释放对应目录项 i 节点
	dir=iget(dev,inr);										// 读取指定文件的 i 节点
	if (dir) {
		dir->i_atime=CURRENT_TIME; 							// 设置该 i 节点的最后访问时间
		dir->i_dirt=1; 										// 设置已修改标志
	}
	return dir; 											// 返回文件 i 节点
}

/**
 * 打开指定路径名对应的文件
 * @param pathname 文件路径名
 * @param flag 打开文件标志（只读 O_RDONLY, 只写 O_WRONLY, 读写 O_RDWR）
 * @param mode 模式（创建文件时指定文件使用的许可属性）
 * @param res_inode 返回对应文件路径名的 i 节点指针
 * @return 成功返回 0，失败返回错误码
*/
int open_namei(const char * pathname, int flag, int mode,
	struct m_inode ** res_inode)
{
	const char * basename;
	int inr,dev,namelen;
	struct m_inode * dir, *inode;
	struct buffer_head * bh;
	struct dir_entry * de;

	// 如果文件访问许可模式为只读（0），当文件截 0 标志 O_TRUNC 却置位了，则改为只写标志
	if ((flag & O_TRUNC) && !(flag & O_ACCMODE))
		flag |= O_WRONLY;
	mode &= 0777 & ~current->umask;							// 将用户设置的模式与进程的模式屏蔽码相与获取创建文件的模式码
	mode |= I_REGULAR;										// 添加普通文件标志
	if (!(dir = dir_namei(pathname,&namelen,&basename)))	// 获取指定文件所在文件夹的i节点与文件名
		return -ENOENT
	if (!namelen) {											// 文件名长度为 0，表明指定路径所指向的文件为文件夹
		if (!(flag & (O_ACCMODE|O_CREAT|O_TRUNC))) {
			*res_inode=dir;
			return 0;
		}
		iput(dir); 											// 如果 flag 存在不能对文件夹使用的操作，释放指定i节点，返回错误码
		return -EISDIR;
	}
	bh = find_entry(&dir,basename,namelen,&de);				// 查询指定文件目录项
	if (!bh) {												// 该文件不存在时，创建文件目录项
		if (!(flag & O_CREAT)) {							// 不是创建文件以及对文件夹的写权限，则释放文件夹内存并返回
			iput(dir);
			return -ENOENT;
		}
		if (!permission(dir,MAY_WRITE)) {
			iput(dir);
			return -EACCES;
		}
		inode = new_inode(dir->i_dev);						// 在文件夹所在设备上创建新的i节点
		if (!inode) {
			iput(dir);
			return -ENOSPC;
		}
		// 重新设置 i 多余参数
		inode->i_uid = current->euid;
		inode->i_mode = mode;
		inode->i_dirt = 1;
		bh = add_entry(dir,basename,namelen,&de);			// 创建或获取文件中指定文件
		if (!bh) {											// 未获取到时，清理对应i节点，并返回错误码
			inode->i_nlinks--;
			iput(inode);
			iput(dir);
			return -ENOSPC;
		}
		de->inode = inode->i_num;							// 设置目录项的 i 节点号
		bh->b_dirt = 1;
		brelse(bh);											// 将目录项写回磁盘
		iput(dir);											// 释放文件夹
		*res_inode = inode;									// 将新建的 i 节点返回
		return 0;
	}
	inr = de->inode;										// 获取 i 节点号
	dev = dir->i_dev;										// 获取 i 目录项所在节点号
	brelse(bh);
	iput(dir);
	if (flag & O_EXCL)
		return -EEXIST;
	if (!(inode=iget(dev,inr)))								// 根据从目录项获得的设备号与 i 节点号获取对应 i 节点
		return -EACCES;
	if ((S_ISDIR(inode->i_mode) && (flag & O_ACCMODE)) ||
	    !permission(inode,ACC_MODE(flag))) {				// 没有对应权限，则释放返回错误码
		iput(inode);
		return -EPERM;
	}
	inode->i_atime = CURRENT_TIME; 							// 设置最后访问时间
	if (flag & O_TRUNC)
		truncate(inode);
	*res_inode = inode;
	return 0;
}

/**
 * 创建一个文件节点
 * @param filename 全限定文件名
 * @param mode 文件模式
 * @param dev 文件所在设备号
*/
int sys_mknod(const char * filename, int mode, int dev)
{
	const char * basename;
	int namelen;
	struct m_inode * dir, * inode;
	struct buffer_head * bh;
	struct dir_entry * de;
	
	if (!suser())											// 只有超级管理员可以创建文件节点					
		return -EPERM;
	if (!(dir = dir_namei(filename,&namelen,&basename)))	// 获取指定目录 i 节点和文件名
		return -ENOENT;
	if (!namelen) {											// 不能为文件夹
		iput(dir);
		return -ENOENT;
	}
	if (!permission(dir,MAY_WRITE)) {						// 文件夹必须拥有写权限
		iput(dir);
		return -EPERM;
	}
	bh = find_entry(&dir,basename,namelen,&de);				// 指定文件名目录项已存在时返回
	if (bh) {
		brelse(bh);
		iput(dir);
		return -EEXIST;
	}
	inode = new_inode(dir->i_dev);							// 在指定设备上创建空的 i 节点
	if (!inode) {
		iput(dir);
		return -ENOSPC;
	}
	inode->i_mode = mode;									// 设置 i 节点访问模式
	if (S_ISBLK(mode) || S_ISCHR(mode))						// 将 i 节点第一个直接块号设置为设备号
		inode->i_zone[0] = dev;
	inode->i_mtime = inode->i_atime = CURRENT_TIME;			// 设置 i 节点的修改时间与访问时间
	inode->i_dirt = 1;										// 设置 i 节点为脏
	bh = add_entry(dir,basename,namelen,&de);				// 向指定文件夹中新建指定目录项
	if (!bh) {
		iput(dir);
		inode->i_nlinks=0;
		iput(inode);
		return -ENOSPC;
	}
	de->inode = inode->i_num;								// 设置 i 节点设备号
	bh->b_dirt = 1;											// i 节点设置为脏
	iput(dir);
	iput(inode);
	brelse(bh);
	return 0;
}

/**
 * 创建目录系统调用
 * @param filename 全限定文件夹名（不以/结尾）
 * @param mode 文件模式
*/
int sys_mkdir(const char * pathname, int mode)
{
	const char * basename;
	int namelen;
	struct m_inode * dir, * inode;
	struct buffer_head * bh, *dir_block;
	struct dir_entry * de;

	if (!suser())											// 只有超级管理员能够创建
		return -EPERM;
	if (!(dir = dir_namei(pathname,&namelen,&basename)))	// 获取需要创建目录的目录
		return -ENOENT;
	if (!namelen) {											// 创建目录名不存在
		iput(dir);
		return -ENOENT;
	}
	if (!permission(dir,MAY_WRITE)) {						// 指定目录没有写权限直接返回
		iput(dir);
		return -EPERM;
	}
	bh = find_entry(&dir,basename,namelen,&de);				// 指定目录下存在与该目录重名目录项返回
	if (bh) {
		brelse(bh);
		iput(dir);
		return -EEXIST;
	}
	inode = new_inode(dir->i_dev);							// 获取指定目录所在设备的空 i 节点
	if (!inode) {
		iput(dir);
		return -ENOSPC;
	}
	inode->i_size = 32;										// 将 i 节点长度设置为 1 个目录项大小（32 字节）
	inode->i_dirt = 1;										// i 节点设置为脏
	inode->i_mtime = inode->i_atime = CURRENT_TIME;			// 设置指定 i 节点的更新以及访问时间
	if (!(inode->i_zone[0]=new_block(inode->i_dev))) {		// 为新的 i 节点的第一个直接块申请磁盘上的空块
		iput(dir);
		inode->i_nlinks--;
		iput(inode);
		return -ENOSPC;
	}
	inode->i_dirt = 1;										// 重新更新 i 节点脏标志
	if (!(dir_block=bread(inode->i_dev,inode->i_zone[0]))) {// 将申请到的 i 节点第一个直接块读入内存中
		iput(dir);
		free_block(inode->i_dev,inode->i_zone[0]);
		inode->i_nlinks--;
		iput(inode);
		return -ERROR;
	}
	de = (struct dir_entry *) dir_block->b_data;			// 将目录项 . 加入本目录
	de->inode=inode->i_num;									// 目录项 . 指向本目录
	strcpy(de->name,".");
	de++;
	de->inode = dir->i_num;									// 目录项 .. 指向上一级目录（dir）
	strcpy(de->name,"..");									// 将目录项 .. 加入本目录
	inode->i_nlinks = 2;
	dir_block->b_dirt = 1;									// 将新加的目录项更新到磁盘
	brelse(dir_block);
	inode->i_mode = I_DIRECTORY | (mode & 0777 & ~current->umask);	// 设置指定 i 节点为目录 i 节点，并将权限设置为本进程权限屏蔽位
	inode->i_dirt = 1;										// 将 i 节点置为脏
	bh = add_entry(dir,basename,namelen,&de);				// 向指定目录中添加新建目录项
	if (!bh) {												// 添加失败释放是所有相关数据
		iput(dir);
		free_block(inode->i_dev,inode->i_zone[0]);
		inode->i_nlinks=0;
		iput(inode);
		return -ENOSPC;
	}
	de->inode = inode->i_num;								// 设置新建目录项指向新建的目录 i 节点			
	bh->b_dirt = 1;											// 将更新目录项的块缓冲区更新
	dir->i_nlinks++;										// 目标文件夹引用数加一
	dir->i_dirt = 1;										// 将目标文件夹 i 节点写回盘
	iput(dir);
	iput(inode);
	brelse(bh);
	return 0;
}

/**
 * 检查指定文件夹是否为空
 * @param inode 目标文件夹 i 节点
 * @return 空的 - 1， 非空 - 0
*/
static int empty_dir(struct m_inode * inode)
{
	int nr,block;
	int len;
	struct buffer_head * bh;
	struct dir_entry * de;

	len = inode->i_size / sizeof (struct dir_entry);						// 获取指定 i 节点包含目录项个数
	if (len<2 || !inode->i_zone[0] ||
	    !(bh=bread(inode->i_dev,inode->i_zone[0]))) {						// 目录项至少拥有 . 与 .. 两项，同时其第一个直接块不能为空
	    	printk("warning - bad directory on dev %04x\n",inode->i_dev);
		return 0;
	}
	de = (struct dir_entry *) bh->b_data;									// 获取第一个直接块上的目录项指针
	if (de[0].inode != inode->i_num || !de[1].inode || 						// 第一个目录项名必须为 . 且指向本目录 i 节点以及第二个目录项名必须为 .. 且指向 i 节点不能为空
	    strcmp(".",de[0].name) || strcmp("..",de[1].name)) {
	    	printk("warning - bad directory on dev %04x\n",inode->i_dev);
		return 0;
	}
	nr = 2;
	de += 2;
	while (nr<len) {														// 遍历出前两项外的所有目录项
		if ((void *) de >= (void *) (bh->b_data+BLOCK_SIZE)) {				// 当前目录项所处内存已超过所属块内存
			brelse(bh);
			block=bmap(inode,nr/DIR_ENTRIES_PER_BLOCK);						// 获取下一个当前 nr 个目录项所属块
			if (!block) {													// 块为空跳过当前块
				nr += DIR_ENTRIES_PER_BLOCK;
				continue;
			}
			if (!(bh=bread(inode->i_dev,block)))							// 读取块信息出错，返回 0
				return 0;
			de = (struct dir_entry *) bh->b_data;							// de 指向新块的第一个目录项
		}
		if (de->inode) {													// 所有目录项指向的 i 节点都应该为空
			brelse(bh);
			return 0;
		}
		de++;
		nr++;
	}
	brelse(bh);
	return 1;
}

/**
 * 删除指定名称对应的文件夹
 * @param name 文件夹权限定名
 * @return 成功 - 0，失败 - 错误号
*/
int sys_rmdir(const char * name)
{
	const char * basename;
	int namelen;
	struct m_inode * dir, * inode;
	struct buffer_head * bh;
	struct dir_entry * de;

	if (!suser())										// 只有超级管理员能够删除文件夹
		return -EPERM;
	if (!(dir = dir_namei(name,&namelen,&basename)))	// 获取指定文件夹所在目录
		return -ENOENT;
	if (!namelen) {										// 文件夹名不能为空
		iput(dir);
		return -ENOENT;
	}
	if (!permission(dir,MAY_WRITE)) {					// 需要拥有目标文件夹写权限
		iput(dir);
		return -EPERM;
	}
	bh = find_entry(&dir,basename,namelen,&de);			// 查询指定目录项
	if (!bh) {
		iput(dir);
		return -ENOENT;
	}
	if (!(inode = iget(dir->i_dev, de->inode))) {		// 获取目录项对应 i 节点
		iput(dir);
		brelse(bh);
		return -EPERM;
	}
	if ((dir->i_mode & S_ISVTX) && current->euid &&
	    inode->i_uid != current->euid) {				// 目标文件夹拥有受限删除标志时，只有文件夹所属用户才能删除指定文件夹
		iput(dir);
		iput(inode);
		brelse(bh);
		return -EPERM;
	}
	if (inode->i_dev != dir->i_dev || inode->i_count>1) {	// i 节点所属设备必须与目标文件夹所属设备一致且没有其他进程正在使用该目录时才能删除指定文件夹
		iput(dir);
		iput(inode);
		brelse(bh);
		return -EPERM;
	}
	if (inode == dir) {									// 不允许删除 . 目录项
		iput(inode);
		iput(dir);
		brelse(bh);
		return -EPERM;
	}
	if (!S_ISDIR(inode->i_mode)) {						// 删除对象必须为目录
		iput(inode);
		iput(dir);
		brelse(bh);
		return -ENOTDIR;
	}
	if (!empty_dir(inode)) {							// 指定目录项必须不为空
		iput(inode);
		iput(dir);
		brelse(bh);
		return -ENOTEMPTY;
	}
	if (inode->i_nlinks != 2)							// 该 i 节点链接数不等于提示文件夹链接数有问题
		printk("empty directory has nlink!=2 (%d)",inode->i_nlinks);
	de->inode = 0;										// 将该目录项指向 i 节点号设置为 0
	bh->b_dirt = 1;										// 将该目录项所属缓冲区写回盘
	brelse(bh);
	inode->i_nlinks=0;									// 清空 i 节点链接数
	inode->i_dirt=1;									// 将 i 节点置为脏
	dir->i_nlinks--;									// 所属目录链接数 -1
	dir->i_ctime = dir->i_mtime = CURRENT_TIME;			// 设置所属目录更新时间
	dir->i_dirt=1;										// 将 i 节点与所属目录写回盘
	iput(dir);
	iput(inode);
	return 0;
}

/**
 * 从文件系统中删除指定目录项，如果指定文件链接数为 1 则删除该文件
 * @param name 文件夹权限定名
 * @return 成功 - 0，失败 - 错误号
*/
int sys_unlink(const char * name)
{
	const char * basename;
	int namelen;
	struct m_inode * dir, * inode;
	struct buffer_head * bh;
	struct dir_entry * de;

	if (!(dir = dir_namei(name,&namelen,&basename)))	// 获取指定文件所在目录
		return -ENOENT;
	if (!namelen) {										// 文件名不能为空
		iput(dir);
		return -ENOENT;
	}
	if (!permission(dir,MAY_WRITE)) {					// 需要拥有所在文件夹写权限
		iput(dir);
		return -EPERM;
	}
	bh = find_entry(&dir,basename,namelen,&de);			// 查询指定文件目录项
	if (!bh) {
		iput(dir);
		return -ENOENT;
	}
	if (!(inode = iget(dir->i_dev, de->inode))) {		// 获取指定文件 i 节点
		iput(dir);
		brelse(bh);
		return -ENOENT;
	}
	if ((dir->i_mode & S_ISVTX) && !suser() &&
	    current->euid != inode->i_uid &&
	    current->euid != dir->i_uid) {					// 目标文件夹拥有受限删除标志时，只有文件与所在文件夹的所属用户才能删除该文件
		iput(dir);
		iput(inode);
		brelse(bh);
		return -EPERM;
	}
	if (S_ISDIR(inode->i_mode)) {						// 删除对象不能为目录
		iput(inode);
		iput(dir);
		brelse(bh);
		return -EPERM;
	}
	if (!inode->i_nlinks) {								// 指定文件引用计数为 0，打印警告信息，并将其修正为 1
		printk("Deleting nonexistent file (%04x:%d), %d\n",
			inode->i_dev,inode->i_num,inode->i_nlinks);
		inode->i_nlinks=1;
	}
	de->inode = 0;										// 指定目录项 i 节点为 0 
	bh->b_dirt = 1;										// 目录项所在缓冲区置为脏
	brelse(bh);	
	inode->i_nlinks--;									// 将指定文件链接数减一
	inode->i_dirt = 1;									// 指定文件 i 节点置为脏
	inode->i_ctime = CURRENT_TIME;						// 设置指定 i 节点修改时间
	iput(inode);
	iput(dir);
	return 0;
}

/**
 * 将指定旧文件与新文件名关联
 * @param oldname 被链接的旧文件名
 * @param newname 将要链接的新文件名
 * @return 成功 - 0，失败 - 错误号
*/
int sys_link(const char * oldname, const char * newname)
{
	struct dir_entry * de;
	struct m_inode * oldinode, * dir;
	struct buffer_head * bh;
	const char * basename;
	int namelen;

	oldinode=namei(oldname);						// 获取旧文件的 i 节点
	if (!oldinode)									// 就文件不存在，返回
		return -ENOENT;
	if (S_ISDIR(oldinode->i_mode)) {				// 不允许链接文件夹
		iput(oldinode);
		return -EPERM;
	}
	dir = dir_namei(newname,&namelen,&basename);	// 获取新文件所在文件夹
	if (!dir) {
		iput(oldinode);
		return -EACCES;
	}
	if (!namelen) {									// 新文件名不能为空
		iput(oldinode);
		iput(dir);
		return -EPERM;
	}
	if (dir->i_dev != oldinode->i_dev) {			// 新文件所属文件夹所属设备必须与旧文件所属设备一致
		iput(dir);
		iput(oldinode);
		return -EXDEV;
	}
	if (!permission(dir,MAY_WRITE)) {				// 新文件所属文件夹必须有写权限
		iput(dir);
		iput(oldinode);
		return -EACCES;
	}
	bh = find_entry(&dir,basename,namelen,&de);		// 新文件所属文件夹不允许存在同名文件
	if (bh) {
		brelse(bh);
		iput(dir);
		iput(oldinode);
		return -EEXIST;
	}
	bh = add_entry(dir,basename,namelen,&de);		// 新增目录项
	if (!bh) {
		iput(dir);
		iput(oldinode);
		return -ENOSPC;
	}
	de->inode = oldinode->i_num;					// 目录项指向旧节点
	bh->b_dirt = 1;									// 目录项缓冲区写回盘
	brelse(bh);
	iput(dir);
	oldinode->i_nlinks++;							// 链接数 + 1
	oldinode->i_ctime = CURRENT_TIME;				// 设置修改时间
	oldinode->i_dirt = 1;							// 将旧 i 节点写回盘
	iput(oldinode);
	return 0;
}
