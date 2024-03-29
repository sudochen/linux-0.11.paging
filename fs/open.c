/*
 *  linux/fs/open.c
 *
 *  (C) 1991  Linus Torvalds
 */

/* #include <string.h> */
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <utime.h>
#include <sys/stat.h>

#include <linux/sched.h>
#include <linux/tty.h>
#include <linux/kernel.h>
#include <asm/segment.h>

int sys_ustat(int dev, struct ustat * ubuf)
{
	return -ENOSYS;
}

int sys_utime(char * filename, struct utimbuf * times)
{
	struct m_inode * inode;
	long actime,modtime;

	if (!(inode=namei(filename)))
		return -ENOENT;
	if (times) {
		actime = get_fs_long((unsigned long *) &times->actime);
		modtime = get_fs_long((unsigned long *) &times->modtime);
	} else
		actime = modtime = CURRENT_TIME;
	inode->i_atime = actime;
	inode->i_mtime = modtime;
	inode->i_dirt = 1;
	iput(inode);
	return 0;
}

/*
 * XXX should we use the real or effective uid?  BSD uses the real uid,
 * so as to make this call useful to setuid programs.
 */
int sys_access(const char * filename,int mode)
{
	struct m_inode * inode;
	int res, i_mode;

	mode &= 0007;
	if (!(inode=namei(filename)))
		return -EACCES;
	i_mode = res = inode->i_mode & 0777;
	iput(inode);
	if (current->uid == inode->i_uid)
		res >>= 6;
	else if (current->gid == inode->i_gid)
		res >>= 6;
	if ((res & 0007 & mode) == mode)
		return 0;
	/*
	 * XXX we are doing this test last because we really should be
	 * swapping the effective with the real user id (temporarily),
	 * and then calling suser() routine.  If we do call the
	 * suser() routine, it needs to be called last. 
	 */
	if ((!current->uid) &&
	    (!(mode & 1) || (i_mode & 0111)))
		return 0;
	return -EACCES;
}

int sys_chdir(const char * filename)
{
	struct m_inode * inode;

	if (!(inode = namei(filename)))
		return -ENOENT;
	if (!S_ISDIR(inode->i_mode)) {
		iput(inode);
		return -ENOTDIR;
	}
	iput(current->pwd);
	current->pwd = inode;
	return (0);
}

int sys_chroot(const char * filename)
{
	struct m_inode * inode;

	if (!(inode=namei(filename)))
		return -ENOENT;
	if (!S_ISDIR(inode->i_mode)) {
		iput(inode);
		return -ENOTDIR;
	}
	iput(current->root);
	current->root = inode;
	return (0);
}

int sys_chmod(const char * filename,int mode)
{
	struct m_inode * inode;

	if (!(inode=namei(filename)))
		return -ENOENT;
	if ((current->euid != inode->i_uid) && !suser()) {
		iput(inode);
		return -EACCES;
	}
	inode->i_mode = (mode & 07777) | (inode->i_mode & ~07777);
	inode->i_dirt = 1;
	iput(inode);
	return 0;
}

int sys_chown(const char * filename,int uid,int gid)
{
	struct m_inode * inode;

	if (!(inode=namei(filename)))
		return -ENOENT;
	if (!suser()) {
		iput(inode);
		return -EACCES;
	}
	inode->i_uid=uid;
	inode->i_gid=gid;
	inode->i_dirt=1;
	iput(inode);
	return 0;
}

int sys_open(const char * filename, int flag, int mode)
{
	struct m_inode * inode;
	struct file * f;
	int i,fd;

	/*
	 * 将用户打开文件传的模式与当前进程的umask的屏蔽码进行与操作
	 * 比如当前umask设置为002
	 * 表示普通用户不可以写
	 */
	mode &= 0777 & ~current->umask;
	
	/*
	 * 遍历urrent的filp[20]，20表示一个进程最多打开20个文件
	 * 如果不能找到一个空闲的filp，返回错误
	 */
	for(fd = 0; fd < NR_OPEN; fd++)
		if (!current->filp[fd])
			break;
	if (fd >= NR_OPEN)
		return -EINVAL;
	current->close_on_exec &= ~(1<<fd);
	/*
	 * 在file_table找一个空闲项
	 * file_table是一个全局变量
	 * NR_FILE表示系统最大打开文件的数量，定义为64
	 * 如果不能找到空闲项，返回错误
	 * 
	 */
	f = 0 + file_table;
	for (i=0; i < NR_FILE; i++, f++)
		if (!f->f_count) 
			break;
	if (i >= NR_FILE) {
		return -EINVAL;
	}


	/*
	 * 当前进程的filp和fd对应的file_table，并且增加计数
	 */
	(current->filp[fd]=f)->f_count++;
	/*
	 * 调用函数执行打开操作，如果返回值小于0，表示出错
	 * 出错需清除filp
	 * 这个函数调用完成后inode是文件inode节点信息
	 * 如filename = /usr/sbin/test
	 * 那么inode的是/usr/sbin/test的inode
	 *
	 */
	if ((i = open_namei(filename, flag, mode, &inode)) <0 ) {
		current->filp[fd] = NULL;
		f->f_count = 0;
		return i;
	}
	/* ttys are somewhat special (ttyxx major==4, tty major==5) */
	/*
	 * S_ISCHAR表示文件是不是字符设备文件
	 *
	 */
	if (S_ISCHR(inode->i_mode)) {
		if (MAJOR(inode->i_zone[0])==4) {
			if (current->leader && current->tty<0) {
				current->tty = MINOR(inode->i_zone[0]);
				tty_table[current->tty].pgrp = current->pgrp;
			}
		} else if (MAJOR(inode->i_zone[0])==5)
			if (current->tty<0) {
				iput(inode);
				current->filp[fd]=NULL;
				f->f_count=0;
				return -EPERM;
			}
	}
	/* Likewise with block-devices: check for floppy_change */
	/*
	 * 是否是块设备文件
	 *
	 */
	if (S_ISBLK(inode->i_mode))
		check_disk_change(inode->i_zone[0]);
	/*
	 * 填充文件指针结构数据
	 *
	 */
	f->f_mode = inode->i_mode;
	f->f_flags = flag;
	f->f_count = 1;
	f->f_inode = inode;
	f->f_pos = 0;

	return (fd);
}

int sys_creat(const char * pathname, int mode)
{
	return sys_open(pathname, O_CREAT | O_TRUNC, mode);
}

int sys_close(unsigned int fd)
{	
	struct file * filp;

	if (fd >= NR_OPEN)
		return -EINVAL;
	current->close_on_exec &= ~(1<<fd);
	if (!(filp = current->filp[fd]))
		return -EINVAL;
	current->filp[fd] = NULL;
	if (filp->f_count == 0)
		panic("Close: file count is 0");
	if (--filp->f_count)
		return (0);
	iput(filp->f_inode);
	return (0);
}
