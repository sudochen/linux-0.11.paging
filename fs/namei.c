/*
 * linux/fs/namei.c
 *
 * (C) 1991  Linus Torvalds
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

#define MAY_EXEC 	1
#define MAY_WRITE 	2
#define MAY_READ 	4

/*
 * permission()
 *
 * is used to check for read/write/execute permissions on a file.
 * I don't know if we should look at just the euid or both euid and
 * uid, but that should be easily changed.
 */
static int permission(struct m_inode *inode, int mask)
{
	int mode = inode->i_mode;

	/* special case: not even root can read/write a deleted file */
	if (inode->i_dev && !inode->i_nlinks)
		return 0;
	else if (current->euid == inode->i_uid)
		mode >>= 6;
	else if (current->egid == inode->i_gid)
		mode >>= 3;
	if (((mode & mask & 0007) == mask) || suser())
		return 1;
	return 0;
}

/*
 * ok, we cannot use strncmp, as the name is not in our data space.
 * Thus we'll have to use match. No big problem. Match also makes
 * some sanity tests.
 *
 * NOTE! unlike strncmp, match returns 1 for success, 0 for failure.
 */
static int match(int len,const char * name,struct dir_entry * de)
{
	register int same ;

	if (!de || !de->inode || len > NAME_LEN)
		return 0;
	if (len < NAME_LEN && de->name[len])
		return 0;
	__asm__("cld\n\t"
		"fs ; repe ; cmpsb\n\t"
		"setz %%al"
		:"=a" (same)
		:"0" (0),"S" ((long) name),"D" ((long) de->name),"c" (len)
		);
	return same;
}

/*
 * find_entry()
 *
 * finds an entry in the specified directory with the wanted name. It
 * returns the cache buffer in which the entry was found, and the entry
 * itself (as a parameter - res_dir). It does NOT read the inode of the
 * entry - you'll have to do that yourself if you want to.
 *
 * This also takes care of the few special cases due to '..'-traversal
 * over a pseudo-root and a mount point.
 *
 * 在指定的目录中寻找一个name目录，
 * 返回一个含有找到目录项的高速缓冲区以及目录项本身(res_dir)
 * dir 指定目录的inode
 * name 文件名
 * namelen 文件名长度
 * dir_entry 找到的目录结构
 *
 */
static struct buffer_head * find_entry(struct m_inode ** dir,
	const char * name, int namelen, struct dir_entry ** res_dir)
{
	int entries;
	int block,i;
	struct buffer_head * bh;
	struct dir_entry * de;
	struct super_block * sb;

#ifdef NO_TRUNCATE
	if (namelen > NAME_LEN)
		return NULL;
#else
	if (namelen > NAME_LEN)
		namelen = NAME_LEN;
#endif
	/*
	 * 计算该目录下的目录数量entries
	 * i_size表示文件长度
	 * 如果文件是目录文件，则一个目录按照dir_entry进行存放
	 * 因此entries就是目录的个数
	 */
	entries = (*dir)->i_size / (sizeof (struct dir_entry));

	/*
	 * 清除返回目录项结构指针
	 */
	*res_dir = NULL;
	if (!namelen)
		return NULL;
	/* check for '..', as we might have to do some "magic" for it */
	/*
	 * 如果是..表示上一级目录，上一级目录是是
	 * 对上一级目录进行判断
	 */
	if (namelen == 2 && get_fs_byte(name) == '.' && get_fs_byte(name+1) == '.') {
		/* '..' in a pseudo-root results in a faked '.' (just change namelen) */
		/*
		 * 如果当前目录已经是根目录，则将将目录设置为'.'
		 * 也就是只设置长度就可以满足
		 * 
		 */
		if ((*dir) == current->root)
			namelen = 1;
		/*
		 * 如果当前目录是文件系统的根节点，则需要取超级块
		 * 
		 *
		 */
		else if ((*dir)->i_num == ROOT_INO) {
		/* '..' over a mount-point results in 'dir' being exchanged for the mounted
   		 directory-inode. NOTE! We set mounted, so that we can iput the new dir */
			/*
			 * 取超级块，并设置dir为挂载的目录
			 * 目录引用计数加1
			 * 如果当前dir的是根节点，或者超级块挂载的inode
			 * 并重置dir为超级块挂在的inode
			 *
			 */
			sb = get_super((*dir)->i_dev);
			if (sb->s_imount) {
				iput(*dir);
				(*dir) = sb->s_imount;
				(*dir)->i_count++;
			}
		}
	}
	/*
	 * 如果该inode所指向的第一个直接磁盘块号为0，则错误
	 */
	if (!(block = (*dir)->i_zone[0])) {
		return NULL;
	}
	/*
	 * 读取该block的数据
	 */
	if (!(bh = bread((*dir)->i_dev, block))) {
		return NULL;
	}
	/*
	 * 开始搜索pathname
	 *
	 */
	i = 0;
	de = (struct dir_entry *) bh->b_data;

	while (i < entries) {
		if ((char *)de >= BLOCK_SIZE + bh->b_data) {
			brelse(bh);
			bh = NULL;
			/*
			 * 进入这个分支说明已经读完一个BLOCK_SIZE了
			 * 读完一个block还没有找到，继续读下一个block
			 * bmap函数会返回实际的逻辑块号，参数是i节点和相对i节点的块号偏移量
			 * bmap第一个参数是inode信息
			 * 第二个参数是文件中的block
			 *
			 * 
			 */
			if (!(block = bmap(*dir, i/DIR_ENTRIES_PER_BLOCK)) ||
			    !(bh = bread((*dir)->i_dev, block))) {
				i += DIR_ENTRIES_PER_BLOCK;
				continue;
			}
			de = (struct dir_entry *) bh->b_data;
		}
		if (match(namelen, name, de)) {
			*res_dir = de;
			return bh;
		}
		de++;
		i++;
	}
	brelse(bh);
	return NULL;
}

/*
 * add_entry()
 *
 * adds a file entry to the specified directory, using the same
 * semantics as find_entry(). It returns NULL if it failed.
 *
 * NOTE!! The inode part of 'de' is left at 0 - which means you
 * may not sleep between calling this and putting something into
 * the entry, as someone else might have used it while you slept.
 */
static struct buffer_head * add_entry(struct m_inode * dir,
	const char * name, int namelen, struct dir_entry ** res_dir)
{
	int block,i;
	struct buffer_head * bh;
	struct dir_entry * de;

	*res_dir = NULL;
#ifdef NO_TRUNCATE
	if (namelen > NAME_LEN)
		return NULL;
#else
	if (namelen > NAME_LEN)
		namelen = NAME_LEN;
#endif
	if (!namelen)
		return NULL;
	if (!(block = dir->i_zone[0]))
		return NULL;
	if (!(bh = bread(dir->i_dev, block)))
		return NULL;
	i = 0;
	de = (struct dir_entry *) bh->b_data;
	while (1) {
		if ((char *)de >= BLOCK_SIZE+bh->b_data) {
			brelse(bh);
			bh = NULL;
			block = create_block(dir, i/DIR_ENTRIES_PER_BLOCK);
			if (!block)
				return NULL;
			if (!(bh = bread(dir->i_dev, block))) {
				i += DIR_ENTRIES_PER_BLOCK;
				continue;
			}
			de = (struct dir_entry *) bh->b_data;
		}
		/*
		 * 如果i*sizeof(struct dir_entry) >= dir->i_size
		 * 说明这个i所在的entry可以用
		 */
		if (i*sizeof(struct dir_entry) >= dir->i_size) {
			de->inode=0;
			dir->i_size = (i+1)*sizeof(struct dir_entry);
			dir->i_dirt = 1;
			dir->i_ctime = CURRENT_TIME;
		}
		if (!de->inode) {
			dir->i_mtime = CURRENT_TIME;
			for (i=0; i < NAME_LEN ; i++)
				de->name[i]=(i<namelen)?get_fs_byte(name+i):0;
			bh->b_dirt = 1;
			*res_dir = de;
			return bh;
		}
		de++;
		i++;
	}
	brelse(bh);
	return NULL;
}

/*
 * get_dir()
 *
 * Getdir traverses the pathname until it hits the topmost directory.
 * It returns NULL on failure.
 *
 * 根据给出的路径名进行搜索，直到达到最顶端的目录
 * 如果失败返回NULL
 *
 */
static struct m_inode * get_dir(const char * pathname)
{
	char c;
	const char * thisname;
	struct m_inode * inode;
	struct buffer_head * bh;
	int namelen, inr, idev;
	struct dir_entry * de;

	/*
	 * 如果当前进程没有设定根i节点或者计数为0，panic
	 */
	if (!current->root || !current->root->i_count)
		panic("No root inode");
	/*
	 * 如果当前进程没有工作目录也panic
	 */
	if (!current->pwd || !current->pwd->i_count)
		panic("No cwd inode");

	/*
	 * 如果第一个字符为'/'说明是绝对地址，则从当前进程的根目录开始操作
	 * 否则从当前进程的工作目录开始操作
	 * 否则错误
	 *
	 */
	if ((c = get_fs_byte(pathname)) == '/') {
		inode = current->root;
		pathname++;
	} else if (c)
		inode = current->pwd;
	else
		return NULL;	/* empty name is bad */
	/*
	 * 增加引用计数
	 */
	inode->i_count++;
	while (1) {
		/*
		 * 第一次
		 * thisname = pathname = /usr/sbin/test
		 * 第二次
		 * thisname = pathname = /sbin/test
		 * 第三次
		 * thisname = pathname = /test
		 *
		 */
		thisname = pathname;
		/*
		 * 如果该节点不是目录，并且没有权限直接返回NULL
		 * 可以看出目录必须有可执行权限
		 *
		 */
		if (!S_ISDIR(inode->i_mode) || !permission(inode, MAY_EXEC)) {;
			iput(inode);
			return NULL;
		}
		/*
		 * 遍历pathname如果，直到c为0或者c为'/'
		 */
		for(namelen = 0; (c = get_fs_byte(pathname++)) && (c != '/'); namelen++)
		/* nothing */ ;
		/*
		 * 如果c为0，表示已经到了指定目录，返回inode信息
		 *
		 */
		if (!c)
			return inode;
		/*
		 * 调用查找制定目录和文件名的函数
		 * 在当前目录中查找目录或者文件
		 * 如
		 * thisname = /usr/sbin/test
		 * namelen的长度为strlen("usr")
		 *
		 */
	
		if (!(bh = find_entry(&inode, thisname, namelen, &de))) {
			iput(inode);
			return NULL;
		}
		inr = de->inode;
		idev = inode->i_dev;
		brelse(bh);
		iput(inode);
		if (!(inode = iget(idev, inr))) {
			return NULL;
		}
	}
}

/*
 * dir_namei()
 *
 * dir_namei() returns the inode of the directory of the
 * specified name, and the name within that directory.
 *
 * 返回pathname指定目录名的i节点指针，以及在最顶层的目录名称
 */
static struct m_inode * dir_namei(const char * pathname,
	int * namelen, const char ** name)
{
	char c;
	const char * basename;
	struct m_inode * dir;
	/*
	 * 根据pathname找到最终的目录，例如
	 * /usr/sbintest
	 * 最终找到/usr/src这个inode
	 */
	if (!(dir = get_dir(pathname))) {
		return NULL;
	}
	/*
	 * 根据pathname或者basename
	 */
	basename = pathname;
	while ((c = get_fs_byte(pathname++)))
		if (c == '/')
			basename = pathname;
	*namelen = pathname - basename - 1;
	*name = basename;
	return dir;
}

/*
 * namei()
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

	if (!(dir = dir_namei(pathname,&namelen,&basename)))
		return NULL;
	if (!namelen)			/* special case: '/usr/' etc */
		return dir;
	bh = find_entry(&dir,basename,namelen,&de);
	if (!bh) {
		iput(dir);
		return NULL;
	}
	inr = de->inode;
	dev = dir->i_dev;
	brelse(bh);
	iput(dir);
	dir=iget(dev,inr);
	if (dir) {
		dir->i_atime=CURRENT_TIME;
		dir->i_dirt=1;
	}
	return dir;
}

/*
 * open_namei()
 *
 * namei for open - this is in fact almost the whole open-routine.
 *
 * pathname 文件名
 * flag 文件打开标志
 * mode 文件访问的属性
 * res_inmode pathname 文件的inode信息
 * 如果打开成功 res_inode 表示文件inode信息，失败返回小于0
 *
 */
int open_namei(const char * pathname, int flag, int mode,
	struct m_inode ** res_inode)
{
	const char * basename;
	int inr,dev,namelen;
	struct m_inode * dir, *inode;
	struct buffer_head * bh;
	struct dir_entry * de;

	/*
	 * 文件标志修订，暂时不用分析
	 *
	 */
	if ((flag & O_TRUNC) && !(flag & O_ACCMODE))
		flag |= O_WRONLY;
	mode &= 0777 & ~current->umask;
	mode |= I_REGULAR;

	/*
	 * 找到文件所在文件夹的inode
	 * pathname 如果是/usr/sbin/test
	 * namelen 为strlen("test")
	 * basename 为test
	 * dir 表示test所在文件夹的inode
	 *
	 */
	if (!(dir = dir_namei(pathname, &namelen, &basename)))
		return -ENOENT;

	/* 
	 * 如果namelen为0，表示打开一个目录
	 * 如果是打开目录，政治界返回目录的inode
	 *
	 */
	if (!namelen) {			/* special case: '/usr/' etc */
		if (!(flag & (O_ACCMODE|O_CREAT|O_TRUNC))) {
			*res_inode = dir;
			return 0;
		}
		iput(dir);
		return -EISDIR;
	}
	bh = find_entry(&dir, basename, namelen, &de);
	if (!bh) {
		if (!(flag & O_CREAT)) {
			iput(dir);
			return -ENOENT;
		}
		if (!permission(dir,MAY_WRITE)) {
			iput(dir);
			return -EACCES;
		}
		inode = new_inode(dir->i_dev);
		if (!inode) {
			iput(dir);
			return -ENOSPC;
		}
		inode->i_uid = current->euid;
		inode->i_mode = mode;
		inode->i_dirt = 1;
		bh = add_entry(dir, basename, namelen, &de);
		if (!bh) {
			inode->i_nlinks--;
			iput(inode);
			iput(dir);
			return -ENOSPC;
		}
		de->inode = inode->i_num;
		bh->b_dirt = 1;
		brelse(bh);
		iput(dir);
		*res_inode = inode;
		return 0;
	}
	inr = de->inode;
	dev = dir->i_dev;
	brelse(bh);
	iput(dir);
	if (flag & O_EXCL)
		return -EEXIST;
	if (!(inode = iget(dev, inr)))
		return -EACCES;
	if ((S_ISDIR(inode->i_mode) && (flag & O_ACCMODE)) ||
	    !permission(inode,ACC_MODE(flag))) {
		iput(inode);
		return -EPERM;
	}
	inode->i_atime = CURRENT_TIME;
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
	/*
	 * 根据filename获取当前目录的i节点以及basename
	 */
	if (!(dir = dir_namei(filename, &namelen, &basename)))
		return -ENOENT;
	/*
	 * 如果basename的长度为0则退出
	 */
	if (!namelen) {
		iput(dir);
		return -ENOENT;
	}
	/*
	 * 检查权限
	 */
	if (!permission(dir, MAY_WRITE)) {
		iput(dir);
		return -EPERM;
	}
	/*
	 * 检查basename是否存在，如果存在返回错误
	 */
	bh = find_entry(&dir, basename, namelen, &de);
	if (bh) {
		brelse(bh);
		iput(dir);
		return -EEXIST;
	}
	/*
	 * 获取一个空闲节点
	 */
	inode = new_inode(dir->i_dev);
	if (!inode) {
		iput(dir);
		return -ENOSPC;
	}
	inode->i_mode = mode;
	/*
	 * 如果是字符设备文件或者块设备文件，i_zone[0]为设备号
	 */
	if (S_ISBLK(mode) || S_ISCHR(mode))
		inode->i_zone[0] = dev;
	inode->i_mtime = inode->i_atime = CURRENT_TIME;
	inode->i_dirt = 1;

	bh = add_entry(dir, basename, namelen, &de);
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

/*
 * 创建目录
 */
int sys_mkdir(const char * pathname, int mode)
{
	const char * basename;
	int namelen;
	struct m_inode * dir, * inode;
	struct buffer_head * bh, *dir_block;
	struct dir_entry * de;

	if (!suser())
		return -EPERM;
	/*
	 * 或者当前目录的i节点
	 * 此时basename为要创建的目录的名称
	 */
	if (!(dir = dir_namei(pathname, &namelen, &basename)))
		return -ENOENT;
	/*
	 * 如果目录名称长度为0则退出
	 */
	if (!namelen) {
		iput(dir);
		return -ENOENT;
	}
	/*
	 * 检查当前目录是否可写
	 */
	if (!permission(dir, MAY_WRITE)) {
		iput(dir);
		return -EPERM;
	}
	/*
	 * 在当前目录中查找basename
	 * 如果找到说明当前目录已经存在名字为basename的入口
	 * 如果存在也返回错误
	 */
	bh = find_entry(&dir, basename, namelen, &de);
	if (bh) {
		brelse(bh);
		iput(dir);
		return -EEXIST;
	}
	/*
	 * 新申请一个节点，new_inode函数会新会将i节点位图置位
	 * inode->i_num表示indoe的索引
	 */
	inode = new_inode(dir->i_dev);
	if (!inode) {
		iput(dir);
		return -ENOSPC;
	}
	/*
	 * 为什么是32，因为需要在目录里建立两个文件，‘.’和'..'
	 * 表示当前目录和上一级目录
	 */
	inode->i_size = 32;
	inode->i_dirt = 1;
	inode->i_mtime = inode->i_atime = CURRENT_TIME;
	/*
	 * new_block返回实际的块索引并将块索引对应的块清零
	 */
	if (!(inode->i_zone[0] = new_block(inode->i_dev))) {
		iput(dir);
		inode->i_nlinks--;
		iput(inode);
		return -ENOSPC;
	}
	inode->i_dirt = 1;
	/*
	 * dir_block为高速缓存
	 */
	if (!(dir_block = bread(inode->i_dev, inode->i_zone[0]))) {
		iput(dir);
		free_block(inode->i_dev, inode->i_zone[0]);
		inode->i_nlinks--;
		iput(inode);
		return -ERROR;
	}
	de = (struct dir_entry *) dir_block->b_data;
	de->inode = inode->i_num;
	strcpy(de->name,".");
	de++;
	de->inode = dir->i_num;
	strcpy(de->name, "..");
	inode->i_nlinks = 2;
	dir_block->b_dirt = 1;
	brelse(dir_block);
	inode->i_mode = I_DIRECTORY | (mode & 0777 & ~current->umask);
	inode->i_dirt = 1;
	bh = add_entry(dir, basename, namelen, &de);
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

int sys_symlink(const char * oldname, const char * newname)
{
	return -EPERM;
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
