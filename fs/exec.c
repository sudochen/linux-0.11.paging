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

int sys_uselib(const char * library)
{
	return -ENOENT;
}

/*
 * create_tables() parses the env- and arg-strings in new user
 * memory and creates the pointer tables from them, and puts their
 * addresses on the "stack", returning the new stack pointer value.
 */
static unsigned long * create_tables(char * p,int argc,int envc)
{
	unsigned long *argv,*envp;
	unsigned long * sp;

	sp = (unsigned long *) (0xfffffffc & (unsigned long) p);
	sp -= envc+1;
	envp = sp;
	sp -= argc+1;
	argv = sp;
	put_fs_long((unsigned long)envp,--sp);
	put_fs_long((unsigned long)argv,--sp);
	put_fs_long((unsigned long)argc,--sp);
	while (argc-->0) {
		put_fs_long((unsigned long) p,argv++);
		while (get_fs_byte(p++)) /* nothing */ ;
	}
	put_fs_long(0,argv);
	while (envc-->0) {
		put_fs_long((unsigned long) p,envp++);
		while (get_fs_byte(p++)) /* nothing */ ;
	}
	put_fs_long(0,envp);
	return sp;
}

/*
 * count() counts the number of arguments/envelopes
 * 获取argv的数量
 */
static int count(char ** argv)
{
	int i=0;
	char ** tmp;

	/*
	 * 将argv的值赋值给tmp
	 */
	if ((tmp = argv)) {

		/* 
		 * argv的格式为
		 * static char * argv[] = { "-/bin/sh", NULL};
		 * argv为数组的地址
		 * 所以这个意思是
		 * 从fs:tmp取数据作为地址，如果不为NULL则计数++
		 * 返回的是字符串的首地址，在这里只统计个数
		 *
		 *
		 */
		while (get_fs_long((unsigned long *) (tmp++)))
			i++;
	}

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
	char *tmp, *pag=NULL;
	int len, offset = 0;
	unsigned long old_fs, new_fs;

	if (!p) {
		return 0;	/* bullet-proofing */
	}

	/* 
	 * ds内核空间 new_fs
	 * fs用户空间 old_fs
	 *
	 */
	new_fs = get_ds();
	old_fs = get_fs();
	/* 
	 * 如果是从内核拷贝到内核空间，设置fs为内核数据段选择子
	 * 在这里我们分析可以只考虑from_kmem = 0的情况，
	 *
	 */
	if (from_kmem==2) {
		set_fs(new_fs);
	}
		
	while (argc-- > 0) {
		if (from_kmem == 1) {
			set_fs(new_fs);
		}
		/*
		 * 我们可以看到这句话是获取参数的起始地址
		 */
		if (!(tmp = (char *)get_fs_long(((unsigned long *)argv)+argc))) {
			panic("argc is wrong");
		}
		if (from_kmem == 1) {
			set_fs(old_fs);
		}

		len=0;		/* remember zero-padding */
		/*
		 * 获取参数字符串的长度
		 * len表示这个字符串的长度
		 * tmp指向这个字符串的末尾
		 */
		do {
			len++;
		} while (get_fs_byte(tmp++));
		/* 
		 * p为128KB -4 如果长度大于128KB - 4 则返回，
		 * 根据注释，我们最多拷贝128KB-4长度的参数
		 * 根据注释，作者说这个不应该发生
		 */
		if (p - len < 0) {	/* this shouldn't happen - 128kB */
			set_fs(old_fs);
			return 0;
		}
		/* p的初始值为128KB的最后四个字节的地址
		 * tmp为argv字符串的大小
		 * len也是argv字符串的大小
		 */
		while (len) {
			--p; --tmp; --len;
			/*
			 * --offset < 0 表示第一次， offset等于p这页面的偏移
			 */
			if (--offset < 0) {
				offset = p % PAGE_SIZE;
				if (from_kmem==2) {
					set_fs(old_fs);
				}
				/*
				 * page[p/PAGE_SIZE]表示p所在page的值如果为0，表示页不存在
				 * 这个时候申请一个页作为pag和page[p/PAGE_SIZE]的值
				 */
				if (!(pag = (char *) page[p/PAGE_SIZE]) &&
				    !(pag = (char *) (page[p/PAGE_SIZE] = get_free_page()))) { 
					return 0;
				}
				if (from_kmem==2) {
					set_fs(new_fs);
				}
			}
			/*
			 * 从这个页的开始存放tmp指向的字节，知道len为0，也就是复制整个字符串
			 * 由于每复制一个字节，p和offset都会减一
			 * 当offset为0时会p会减小到倒数第二个页，以此类推
			 */
			*(pag + offset) = get_fs_byte(tmp);
		}
	}
	if (from_kmem == 2) {
		set_fs(old_fs);
	}
	return p;
}

#define TASK_SIZE	(0xC0000000)
static unsigned long change_ldt(unsigned long text_size,unsigned long * page)
{
	unsigned long code_limit,data_limit,code_base,data_base;
	int i;
	/*
	 * code_limit和data_limit都设置为3GB，0xC0000000
	 * code_base和data_base都设置为0
	 */
	code_limit = TASK_SIZE;
	data_limit = TASK_SIZE;
	code_base = data_base = 0;
	current->start_code = code_base;
	/*
	 * 设置当前进程的LDT
	 */
	set_base(current->ldt[1],code_base);
	set_limit(current->ldt[1],code_limit);
	set_base(current->ldt[2],data_base);
	set_limit(current->ldt[2],data_limit);

	
	/* make sure fs points to the NEW data segment */
	/*
	 * 恢复FS为0x17
	 */
	__asm__("pushl $0x17\n\tpop %%fs"::);

	/* 
	 * data_base现在是3GB的地址空间
	 */
	data_base += data_limit;	
	for (i = MAX_ARG_PAGES - 1; i >= 0; i--) {	
		/*
		 * data_base 现在是3GB的地址第一个页
		 */
		data_base -= PAGE_SIZE;
		/* 
		 * 如果page[i]有效，也就是参数有效
		 * 则将此物理页映射到data_base地址处
		 */
		if (page[i]) {
			put_page(page[i], data_base);
		}
	}
	return data_limit;
}

/*
 * 'do_execve()' executes a new program.
 */
int do_execve(unsigned long * eip,long tmp,char * filename,
	char ** argv, char ** envp)
{
	struct m_inode * inode;
	struct buffer_head * bh;
	struct exec ex;
	unsigned long page[MAX_ARG_PAGES];
	int i,argc,envc;
	int e_uid, e_gid;
	int retval;
	int sh_bang = 0;
	unsigned long p = PAGE_SIZE*MAX_ARG_PAGES-4;

	/* 
	 * eip[i]表示栈中返回的cs地址
	 * 其中的选择子不能为内核选择子，也就是说内核程序不能调用此函数
	 *
	 */
	if ((0xffff & eip[1]) != 0x000f)
		panic("execve called from supervisor mode");

	/* 
	 * 初始化参数和环境的页表
	 *
	 */
	for (i=0; i < MAX_ARG_PAGES; i++)	/* clear page-table */
		page[i] = 0;
	/*
	 * 获取可执行文件的对应的inode 
	 *
	 */
	if (!(inode = namei(filename)))		/* get executables inode */
		return -ENOENT;

	/*
	 *
	 */
	argc = count(argv);
	envc = count(envp);
	
restart_interp:
	/*
	 * 必须是一个常规文件
	 */
	if (!S_ISREG(inode->i_mode)) {	/* must be regular file */
		retval = -EACCES;
		goto exec_error2;
	}
	/* 
	 * 权限检查
	 */
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
	/*
	 * 读取一块数据
	 */
	if (!(bh = bread(inode->i_dev,inode->i_zone[0]))) {
		retval = -EACCES;
		goto exec_error2;
	}
	/*
	 * 获取文件头部
	 */
	ex = *((struct exec *) bh->b_data);	/* read exec-header */

	/*
	 * 如果是脚本则执行脚本，我们可以先不关注这个
	 */
	if ((bh->b_data[0] == '#') && (bh->b_data[1] == '!') && (!sh_bang)) {
		/*
		 * This section does the #! interpretation.
		 * Sorta complicated, but hopefully it will work.  -TYT
		 */

		char buf[1023], *cp, *interp, *i_name, *i_arg;
		unsigned long old_fs;

		strncpy(buf, bh->b_data+2, 1022);
		brelse(bh);
		iput(inode);
		buf[1022] = '\0';
		if ((cp = strchr(buf, '\n'))) {
			*cp = '\0';
			for (cp = buf; (*cp == ' ') || (*cp == '\t'); cp++);
		}
		if (!cp || *cp == '\0') {
			retval = -ENOEXEC; /* No interpreter name found */
			goto exec_error1;
		}
		interp = i_name = cp;
		i_arg = 0;
		for ( ; *cp && (*cp != ' ') && (*cp != '\t'); cp++) {
 			if (*cp == '/')
				i_name = cp+1;
		}
		if (*cp) {
			*cp++ = '\0';
			i_arg = cp;
		}
		/*
		 * OK, we've parsed out the interpreter name and
		 * (optional) argument.
		 */
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
		p = copy_strings(1, &filename, page, p, 1);
		argc++;
		if (i_arg) {
			p = copy_strings(1, &i_arg, page, p, 2);
			argc++;
		}
		p = copy_strings(1, &i_name, page, p, 2);
		argc++;
		if (!p) {
			retval = -ENOMEM;
			goto exec_error1;
		}
		/*
		 * OK, now restart the process with the interpreter's inode.
		 */
		old_fs = get_fs();
		set_fs(get_ds());
		if (!(inode=namei(interp))) { /* get executables inode */
			set_fs(old_fs);
			retval = -ENOENT;
			goto exec_error1;
		}
		set_fs(old_fs);
		goto restart_interp;
	}
	brelse(bh);
	if (N_MAGIC(ex) != ZMAGIC || ex.a_trsize || ex.a_drsize ||
		ex.a_text + ex.a_data + ex.a_bss > (16*1024*1024) ||
		inode->i_size < ex.a_text + ex.a_data + ex.a_syms + N_TXTOFF(ex)) {
		retval = -ENOEXEC;
		goto exec_error2;
	}
	if (N_TXTOFF(ex) != BLOCK_SIZE) {
		retval = -ENOEXEC;
		goto exec_error2;
	}
	/*
	 * 拷贝环境变量
	 */
	if (!sh_bang) {
		/* envc表示个数，envp表示字符串数组的地址，
		 * page是存放页表的数组，p是page的编译，0是标记
		 * 执行完下面的语句
		 * 代码会将envp和argv卡拷贝到page指向的页面中，p表示page中的偏移
		 *
		 */
		p = copy_strings(envc, envp, page, p, 0);
		p = copy_strings(argc, argv, page, p, 0);
		if (!p) {
			retval = -ENOMEM;
			goto exec_error2;
		}
	}
	/* OK, This is the point of no return */
	if (current->executable)
		iput(current->executable);
	current->executable = inode;
	for (i=0 ; i<32 ; i++) {
		if (current->sigaction[i].sa_handler != SIG_IGN)
			current->sigaction[i].sa_handler = NULL;
	}
	for (i=0 ; i<NR_OPEN ; i++)
		if ((current->close_on_exec>>i)&1)
			sys_close(i);
	current->close_on_exec = 0;
	/*
	 * 清理0-3GB的页表
	 */
	clear_page_tables(current);
	if (last_task_used_math == current)
		last_task_used_math = NULL;
	current->used_math = 0;
	/* 
	 * change_ldt将page中有效部分映射到3GB地址，并返回0xC0000000
	 * MAX_ARG_PAGES*PAGE_SIZE是给envp和argv预留的空间
	 * change_ldt(ex.a_text, page) - MAX_ARG_PAGES*PAGE_SIZE 返回的是预留空间的起始地址
	 * p是page内的偏移，因此预留空间的起始地址加上p偏移，
	 * 此时p就是参数存放的虚拟地址
	 */
	p += change_ldt(ex.a_text, page) - MAX_ARG_PAGES*PAGE_SIZE;
	/*
	 * 处理环境变量和参数，返回新的参数存放地址的虚拟地址，
	 * 也就是用户程序堆栈的虚拟地址
	 */
	p = (unsigned long) create_tables((char *)p, argc, envc);

	/*
	 * |   code   |   data   |   bss    | brk
	 * |----------|----------|----------|
	 * 
	 * brk一般是malloc系统调用的起始地址
	 * 
	 */
	current->brk = ex.a_bss +
		(current->end_data = ex.a_data +
		(current->end_code = ex.a_text));
	/*
	 * 设置堆栈
	 */
	current->start_stack = p & 0xfffff000;
	current->euid = e_uid;
	current->egid = e_gid;
	i = ex.a_text+ex.a_data;
	while (i&0xfff)
		put_fs_byte(0,(char *) (i++));
	eip[0] = ex.a_entry;		/* eip, magic happens :-) */
	eip[3] = p;					/* stack pointer */
	return 0;
exec_error2:
	iput(inode);
exec_error1:
	for (i=0 ; i<MAX_ARG_PAGES ; i++) {
		free_page(page[i]);
	}
	return(retval);
}
