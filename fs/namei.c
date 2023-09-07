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

#define MAY_EXEC 1 // 可执行权限位
#define MAY_WRITE 2
#define MAY_READ 4

/*
 *	permission()
 *
 * is used to check for read/write/execute permissions on a file.
 * I don't know if we should look at just the euid or both euid and
 * uid, but that should be easily changed.
 */
/**
 * 检测文件访问许可权限
 * @param inode i 节点指针
 * @param mask 访问属性屏蔽码
 * @return 0-无权限，1-有权限
*/
static int permission(struct m_inode * inode,int mask)
{
	int mode = inode->i_mode;

/* special case: not even root can read/write a deleted file */
	// 如果该 i 节点有对应的设备单该 i 节点的连接数等于 0 则直接返回
	if (inode->i_dev && !inode->i_nlinks)
		return 0;
	// 当前有效用户与i节点用户id相同，则取文件宿主的用户访问权限
	else if (current->euid==inode->i_uid)
		mode >>= 6;
	// 当前有效组 id 与 i 节点用户组 id 相同，则取组用户的访问权限
	else if (current->egid==inode->i_gid)
		mode >>= 3;
	// 如果上述取得的权限与屏蔽码相同，或者是超级用户，表明有权限
	if (((mode & mask & 0007) == mask) || suser())
		return 1;
	return 0; // 其他情况返回无权限
}

/*
 * ok, we cannot use strncmp, as the name is not in our data space.
 * Thus we'll have to use match. No big problem. Match also makes
 * some sanity tests.
 *
 * NOTE! unlike strncmp, match returns 1 for success, 0 for failure.
 */
/**
 * 指定长度字符长比较函数
 * @param len 比较的字符串长度
 * @param name 文件名指针
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

/*
 *	find_entry()
 *
 * finds an entry in the specified directory with the wanted name. It
 * returns the cache buffer in which the entry was found, and the entry
 * itself (as a parameter - res_dir). It does NOT read the inode of the
 * entry - you'll have to do that yourself if you want to.
 *
 * This also takes care of the few special cases due to '..'-traversal
 * over a pseudo-root and a mount point.
 */
/**
 * 查询指定目录和文件名的目录项
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
	// 计算本目录中目录项数 entries，置空返回目录项结构指针
	entries = (*dir)->i_size / (sizeof (struct dir_entry));
	*res_dir = NULL;
	if (!namelen)
		return NULL;
/* check for '..', as we might have to do some "magic" for it */
	// 如果 dir 为 ..,
	if (namelen==2 && get_fs_byte(name)=='.' && get_fs_byte(name+1)=='.') {
/* '..' in a pseudo-root results in a faked '.' (just change namelen) */
		// 当前目录为进程的跟节点目录，则将文件名改为 ‘.’
		if ((*dir) == current->root)
			namelen=1;
		// 否则若该目录 i 节点号等于 ROOT_INO(1) 的话，说明时文件系统的根节点
		else if ((*dir)->i_num == ROOT_INO) {
/* '..' over a mount-point results in 'dir' being exchanged for the mounted
   directory-inode. NOTE! We set mounted, so that we can iput the new dir */
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
	// 读取 i 节点指向的第一块磁盘块
	if (!(bh = bread((*dir)->i_dev,block)))
		return NULL;
	// 在目录项数据块中搜索匹配指定文件名的目录项
	i = 0;
	de = (struct dir_entry *) bh->b_data;
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

		// 匹配文件名
		if (match(namelen,name,de)) {
			*res_dir = de;
			return bh; //返回文件夹所在块
		}
		de++;
		i++;
	}
	brelse(bh); // 没找到释放缓冲
	return NULL;
}

/*
 *	add_entry()
 *
 * adds a file entry to the specified directory, using the same
 * semantics as find_entry(). It returns NULL if it failed.
 *
 * NOTE!! The inode part of 'de' is left at 0 - which means you
 * may not sleep between calling this and putting something into
 * the entry, as someone else might have used it while you slept.
 */
/**
 * 在指定文件夹中添加指定项
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
			dir->i_size = (i+1)*sizeof(struct dir_entry); // 将文件夹项数组长度 + 1
			dir->i_dirt = 1;
			dir->i_ctime = CURRENT_TIME;
		}
		// 新的目录项创建后，为其赋初值
		if (!de->inode) {
			dir->i_mtime = CURRENT_TIME;
			// 设置文件名
			for (i=0; i < NAME_LEN ; i++)
				de->name[i]=(i<namelen)?get_fs_byte(name+i):0;
			bh->b_dirt = 1; // 将缓冲区设置为已修改
			*res_dir = de; // 返回的目录项指向de
			return bh;
		}
		de++;
		i++;
	}
	brelse(bh);
	return NULL;
}

/*
 *	get_dir()
 *
 * Getdir traverses the pathname until it hits the topmost directory.
 * It returns NULL on failure.
 */
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

	// 判断当前进程使用拥有跟节点，同时根节点引用是否为空
	if (!current->root || !current->root->i_count)
		panic("No root inode");
	if (!current->pwd || !current->pwd->i_count)
		panic("No cwd inode");
	//pathname 以 '/' 开始时，为绝对路径，从根目录开始操作
	if ((c=get_fs_byte(pathname))=='/') {
		inode = current->root;
		pathname++;
	// 其他情况为相对地址，从当前操作目录开始操作
	} else if (c)
		inode = current->pwd;
	else
		return NULL;	/* empty name is bad */
	inode->i_count++; // 没操作一次将对应的 i 节点引用计数 +1
	while (1) {
		thisname = pathname;
		// i 节点不是目录，且没有访问该目录的访问许可，则释放该 i 节点 ，并返回
		if (!S_ISDIR(inode->i_mode) || !permission(inode,MAY_EXEC)) {
			iput(inode); // 释放i节点
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
			iput(inode); // 不存在则释放i节点同时写回盘中
			return NULL;
		}
		inr = de->inode;
		idev = inode->i_dev;
		brelse(bh); // 释放 高速缓存
		iput(inode);  // 释放 i 节点
		// 获取 inr 对应的i节点
		if (!(inode = iget(idev,inr)))
			return NULL;
	}
}

/*
 *	dir_namei()
 *
 * dir_namei() returns the inode of the directory of the
 * specified name, and the name within that directory.
 */
/**
 * 获取指定目录名的 i 节点指针，以及在最顶层的目录名称
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
	// 获取指定目录最顶层目录的 i 节点
	if (!(dir = get_dir(pathname)))
		return NULL;
	basename = pathname;
	// 获取不带路径的文件名
	while (c=get_fs_byte(pathname++))
		if (c=='/')
			basename=pathname;
	*namelen = pathname-basename-1; // 获取文件名长度
	*name = basename; // 获取文件名
	return dir;
}

/*
 *	namei()
 *
 * is used by most simple commands to get the inode of a specified name.
 * Open, link etc use their own routines, but this is enough for things
 * like 'chmod' etc.
 */
struct m_inode * namei(const char * pathname)
{
	const char * basename;
	int inr,dev,namelen;
	struct m_inode * dir;
	struct buffer_head * bh;
	struct dir_entry * de;

	// 获取文件所在目录的i 节点（dir），不带路径的名字长（namelen），不带路径的文件名（basename）
	if (!(dir = dir_namei(pathname,&namelen,&basename)))
		return NULL;
	// 文件名长度为0时，说明指定路径为文件夹，
	if (!namelen)			/* special case: '/usr/' etc */
		return dir;
	// 将指定文件读取到缓冲区中
	bh = find_entry(&dir,basename,namelen,&de);
	// 读取失败
	if (!bh) {
		iput(dir);
		return NULL;
	}
	inr = de->inode;
	dev = dir->i_dev;
	brelse(bh);
	iput(dir);
	// 读取指定文件的 i 节点
	dir=iget(dev,inr);
	if (dir) {
		dir->i_atime=CURRENT_TIME; // 设置该 i 节点的最后访问时间
		dir->i_dirt=1; // 设置已修改标志
	}
	return dir; // 返回文件 i 节点
}

/*
 *	open_namei()
 *
 * namei for open - this is in fact almost the whole open-routine.
 */
/**
 * 打开文件 namei 函数
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
	// 将用户设置的模式与进程的模式屏蔽码相与获取创建文件的模式码
	mode &= 0777 & ~current->umask;
	// 添加普通文件标志
	mode |= I_REGULAR;
	// 获取指定文件所在文件夹的i节点与文件名
	if (!(dir = dir_namei(pathname,&namelen,&basename)))
		return -ENOENT;
	// 文件名长度为 0，表明指定路径所指向的文件为文件夹
	if (!namelen) {			/* special case: '/usr/' etc */
		if (!(flag & (O_ACCMODE|O_CREAT|O_TRUNC))) {
			*res_inode=dir;
			return 0;
		}
		iput(dir); // 如果 flag 存在不能对文件夹使用的操作，释放指定i节点，返回错误码
		return -EISDIR;
	}
	// 查询指定文件
	bh = find_entry(&dir,basename,namelen,&de);
	// 该文件不存在时
	if (!bh) {
		// 不是创建文件以及对文件夹的写权限，则释放文件夹内存并返回
		if (!(flag & O_CREAT)) {
			iput(dir);
			return -ENOENT;
		}
		if (!permission(dir,MAY_WRITE)) {
			iput(dir);
			return -EACCES;
		}
		// 在文件夹所在设备上创建新的i节点
		inode = new_inode(dir->i_dev);
		if (!inode) {
			iput(dir);
			return -ENOSPC;
		}
		// 重新设置 i 多余参数
		inode->i_uid = current->euid;
		inode->i_mode = mode;
		inode->i_dirt = 1;
		// 创建或获取文件中指定文件
		bh = add_entry(dir,basename,namelen,&de);
		// 未获取到时，清理对应i节点，并返回错误码
		if (!bh) {
			inode->i_nlinks--;
			iput(inode);
			iput(dir);
			return -ENOSPC;
		}
		// 设置 目录项的i 节点 号
		de->inode = inode->i_num;
		// 将目录项写回磁盘
		bh->b_dirt = 1;
		brelse(bh);
		// 释放文件夹
		iput(dir);
		// 将新建的 i 节点返回
		*res_inode = inode;
		return 0;
	}
	inr = de->inode;
	dev = dir->i_dev;
	brelse(bh);
	iput(dir);
	if (flag & O_EXCL)
		return -EEXIST;
	// 根据从目录项获得的设备号与 i 节点号获取对应 i 节点
	if (!(inode=iget(dev,inr)))
		return -EACCES;
	// 没有对应权限，则释放返回错误码
	if ((S_ISDIR(inode->i_mode) && (flag & O_ACCMODE)) ||
	    !permission(inode,ACC_MODE(flag))) {
		iput(inode);
		return -EPERM;
	}
	inode->i_atime = CURRENT_TIME; // 设置最后访问时间
	if (flag & O_TRUNC)
		truncate(inode);
	*res_inode = inode;
	return 0;
}

int sys_mknod(const char * filename, int mode, int dev)
{
	const char * basename;
	int namelen;
	struct m_inode * dir, * inode;
	struct buffer_head * bh;
	struct dir_entry * de;
	
	if (!suser())
		return -EPERM;
	if (!(dir = dir_namei(filename,&namelen,&basename)))
		return -ENOENT;
	if (!namelen) {
		iput(dir);
		return -ENOENT;
	}
	if (!permission(dir,MAY_WRITE)) {
		iput(dir);
		return -EPERM;
	}
	bh = find_entry(&dir,basename,namelen,&de);
	if (bh) {
		brelse(bh);
		iput(dir);
		return -EEXIST;
	}
	inode = new_inode(dir->i_dev);
	if (!inode) {
		iput(dir);
		return -ENOSPC;
	}
	inode->i_mode = mode;
	if (S_ISBLK(mode) || S_ISCHR(mode))
		inode->i_zone[0] = dev;
	inode->i_mtime = inode->i_atime = CURRENT_TIME;
	inode->i_dirt = 1;
	bh = add_entry(dir,basename,namelen,&de);
	if (!bh) {
		iput(dir);
		inode->i_nlinks=0;
		iput(inode);
		return -ENOSPC;
	}
	de->inode = inode->i_num;
	bh->b_dirt = 1;
	iput(dir);
	iput(inode);
	brelse(bh);
	return 0;
}

int sys_mkdir(const char * pathname, int mode)
{
	const char * basename;
	int namelen;
	struct m_inode * dir, * inode;
	struct buffer_head * bh, *dir_block;
	struct dir_entry * de;

	if (!suser())
		return -EPERM;
	if (!(dir = dir_namei(pathname,&namelen,&basename)))
		return -ENOENT;
	if (!namelen) {
		iput(dir);
		return -ENOENT;
	}
	if (!permission(dir,MAY_WRITE)) {
		iput(dir);
		return -EPERM;
	}
	bh = find_entry(&dir,basename,namelen,&de);
	if (bh) {
		brelse(bh);
		iput(dir);
		return -EEXIST;
	}
	inode = new_inode(dir->i_dev);
	if (!inode) {
		iput(dir);
		return -ENOSPC;
	}
	inode->i_size = 32;
	inode->i_dirt = 1;
	inode->i_mtime = inode->i_atime = CURRENT_TIME;
	if (!(inode->i_zone[0]=new_block(inode->i_dev))) {
		iput(dir);
		inode->i_nlinks--;
		iput(inode);
		return -ENOSPC;
	}
	inode->i_dirt = 1;
	if (!(dir_block=bread(inode->i_dev,inode->i_zone[0]))) {
		iput(dir);
		free_block(inode->i_dev,inode->i_zone[0]);
		inode->i_nlinks--;
		iput(inode);
		return -ERROR;
	}
	de = (struct dir_entry *) dir_block->b_data;
	de->inode=inode->i_num;
	strcpy(de->name,".");
	de++;
	de->inode = dir->i_num;
	strcpy(de->name,"..");
	inode->i_nlinks = 2;
	dir_block->b_dirt = 1;
	brelse(dir_block);
	inode->i_mode = I_DIRECTORY | (mode & 0777 & ~current->umask);
	inode->i_dirt = 1;
	bh = add_entry(dir,basename,namelen,&de);
	if (!bh) {
		iput(dir);
		free_block(inode->i_dev,inode->i_zone[0]);
		inode->i_nlinks=0;
		iput(inode);
		return -ENOSPC;
	}
	de->inode = inode->i_num;
	bh->b_dirt = 1;
	dir->i_nlinks++;
	dir->i_dirt = 1;
	iput(dir);
	iput(inode);
	brelse(bh);
	return 0;
}

/*
 * routine to check that the specified directory is empty (for rmdir)
 */
static int empty_dir(struct m_inode * inode)
{
	int nr,block;
	int len;
	struct buffer_head * bh;
	struct dir_entry * de;

	len = inode->i_size / sizeof (struct dir_entry);
	if (len<2 || !inode->i_zone[0] ||
	    !(bh=bread(inode->i_dev,inode->i_zone[0]))) {
	    	printk("warning - bad directory on dev %04x\n",inode->i_dev);
		return 0;
	}
	de = (struct dir_entry *) bh->b_data;
	if (de[0].inode != inode->i_num || !de[1].inode || 
	    strcmp(".",de[0].name) || strcmp("..",de[1].name)) {
	    	printk("warning - bad directory on dev %04x\n",inode->i_dev);
		return 0;
	}
	nr = 2;
	de += 2;
	while (nr<len) {
		if ((void *) de >= (void *) (bh->b_data+BLOCK_SIZE)) {
			brelse(bh);
			block=bmap(inode,nr/DIR_ENTRIES_PER_BLOCK);
			if (!block) {
				nr += DIR_ENTRIES_PER_BLOCK;
				continue;
			}
			if (!(bh=bread(inode->i_dev,block)))
				return 0;
			de = (struct dir_entry *) bh->b_data;
		}
		if (de->inode) {
			brelse(bh);
			return 0;
		}
		de++;
		nr++;
	}
	brelse(bh);
	return 1;
}

int sys_rmdir(const char * name)
{
	const char * basename;
	int namelen;
	struct m_inode * dir, * inode;
	struct buffer_head * bh;
	struct dir_entry * de;

	if (!suser())
		return -EPERM;
	if (!(dir = dir_namei(name,&namelen,&basename)))
		return -ENOENT;
	if (!namelen) {
		iput(dir);
		return -ENOENT;
	}
	if (!permission(dir,MAY_WRITE)) {
		iput(dir);
		return -EPERM;
	}
	bh = find_entry(&dir,basename,namelen,&de);
	if (!bh) {
		iput(dir);
		return -ENOENT;
	}
	if (!(inode = iget(dir->i_dev, de->inode))) {
		iput(dir);
		brelse(bh);
		return -EPERM;
	}
	if ((dir->i_mode & S_ISVTX) && current->euid &&
	    inode->i_uid != current->euid) {
		iput(dir);
		iput(inode);
		brelse(bh);
		return -EPERM;
	}
	if (inode->i_dev != dir->i_dev || inode->i_count>1) {
		iput(dir);
		iput(inode);
		brelse(bh);
		return -EPERM;
	}
	if (inode == dir) {	/* we may not delete ".", but "../dir" is ok */
		iput(inode);
		iput(dir);
		brelse(bh);
		return -EPERM;
	}
	if (!S_ISDIR(inode->i_mode)) {
		iput(inode);
		iput(dir);
		brelse(bh);
		return -ENOTDIR;
	}
	if (!empty_dir(inode)) {
		iput(inode);
		iput(dir);
		brelse(bh);
		return -ENOTEMPTY;
	}
	if (inode->i_nlinks != 2)
		printk("empty directory has nlink!=2 (%d)",inode->i_nlinks);
	de->inode = 0;
	bh->b_dirt = 1;
	brelse(bh);
	inode->i_nlinks=0;
	inode->i_dirt=1;
	dir->i_nlinks--;
	dir->i_ctime = dir->i_mtime = CURRENT_TIME;
	dir->i_dirt=1;
	iput(dir);
	iput(inode);
	return 0;
}

int sys_unlink(const char * name)
{
	const char * basename;
	int namelen;
	struct m_inode * dir, * inode;
	struct buffer_head * bh;
	struct dir_entry * de;

	if (!(dir = dir_namei(name,&namelen,&basename)))
		return -ENOENT;
	if (!namelen) {
		iput(dir);
		return -ENOENT;
	}
	if (!permission(dir,MAY_WRITE)) {
		iput(dir);
		return -EPERM;
	}
	bh = find_entry(&dir,basename,namelen,&de);
	if (!bh) {
		iput(dir);
		return -ENOENT;
	}
	if (!(inode = iget(dir->i_dev, de->inode))) {
		iput(dir);
		brelse(bh);
		return -ENOENT;
	}
	if ((dir->i_mode & S_ISVTX) && !suser() &&
	    current->euid != inode->i_uid &&
	    current->euid != dir->i_uid) {
		iput(dir);
		iput(inode);
		brelse(bh);
		return -EPERM;
	}
	if (S_ISDIR(inode->i_mode)) {
		iput(inode);
		iput(dir);
		brelse(bh);
		return -EPERM;
	}
	if (!inode->i_nlinks) {
		printk("Deleting nonexistent file (%04x:%d), %d\n",
			inode->i_dev,inode->i_num,inode->i_nlinks);
		inode->i_nlinks=1;
	}
	de->inode = 0;
	bh->b_dirt = 1;
	brelse(bh);
	inode->i_nlinks--;
	inode->i_dirt = 1;
	inode->i_ctime = CURRENT_TIME;
	iput(inode);
	iput(dir);
	return 0;
}

int sys_link(const char * oldname, const char * newname)
{
	struct dir_entry * de;
	struct m_inode * oldinode, * dir;
	struct buffer_head * bh;
	const char * basename;
	int namelen;

	oldinode=namei(oldname);
	if (!oldinode)
		return -ENOENT;
	if (S_ISDIR(oldinode->i_mode)) {
		iput(oldinode);
		return -EPERM;
	}
	dir = dir_namei(newname,&namelen,&basename);
	if (!dir) {
		iput(oldinode);
		return -EACCES;
	}
	if (!namelen) {
		iput(oldinode);
		iput(dir);
		return -EPERM;
	}
	if (dir->i_dev != oldinode->i_dev) {
		iput(dir);
		iput(oldinode);
		return -EXDEV;
	}
	if (!permission(dir,MAY_WRITE)) {
		iput(dir);
		iput(oldinode);
		return -EACCES;
	}
	bh = find_entry(&dir,basename,namelen,&de);
	if (bh) {
		brelse(bh);
		iput(dir);
		iput(oldinode);
		return -EEXIST;
	}
	bh = add_entry(dir,basename,namelen,&de);
	if (!bh) {
		iput(dir);
		iput(oldinode);
		return -ENOSPC;
	}
	de->inode = oldinode->i_num;
	bh->b_dirt = 1;
	brelse(bh);
	iput(dir);
	oldinode->i_nlinks++;
	oldinode->i_ctime = CURRENT_TIME;
	oldinode->i_dirt = 1;
	iput(oldinode);
	return 0;
}
