/*
 *  linux/mm/memory.c
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 * demand-loading started 01.12.91 - seems it is high on the list of
 * things wanted, and it should be easy to implement. - Linus
 */

/*
 * Ok, demand-loading was easy, shared pages a little bit tricker. Shared
 * pages started 02.12.91, seems to work. - Linus.
 *
 * Tested sharing by executing about 30 /bin/sh: under the old kernel it
 * would have taken more than the 6M I have free, but it worked well as
 * far as I could see.
 *
 * Also corrected some "invalidate()"s - I wasn't doing enough of them.
 */

#include <signal.h>
#include <asm/system.h>
#include <linux/sched.h>
#include <linux/head.h>
#include <linux/kernel.h>

void do_exit(long code);

static inline void oom(void)
{
	printk("out of memory\n\r");
	do_exit(SIGSEGV);
}

#define invalidate() \
__asm__("movl %%eax,%%cr3"::"a" (0))

/* these are not to be changed without changing head.s etc */
#define LOW_MEM 			0x100000
#define PAGING_MEMORY 		(15*1024*1024)
#define PAGING_PAGES 		(PAGING_MEMORY>>12)
#define MAP_NR(addr) 		(((addr)-LOW_MEM)>>12)
#define USED 				100

#define CODE_SPACE(addr) ((((addr)+4095)&~4095) < \
current->start_code + current->end_code)

static long HIGH_MEMORY = 0;
static long total_pages = 0;

/* chenwg
 * 复制一页4KB的内存
 *
 */
#define copy_page(from,to) \
__asm__("cld ; rep ; movsl"::"S" (from),"D" (to),"c" (1024))

static unsigned char mem_map [ PAGING_PAGES ] = {0,};

/*
 * Get physical address of first (actually last :-) free page, and mark it
 * used. If no free pages left, return 0.
 * 
 * 获取一个空闲页的物理地址，获取的页物理地址从LOW_MEM开始
 * 如果成功返回一个物理地址，如果没有返回0
 * 
 *
 */
unsigned long get_free_page(void)
{
register unsigned long __res asm("ax");

__asm__("std ; repne ; scasb\n\t"
	"jne 1f\n\t"
	"movb $1,1(%%edi)\n\t"
	"sall $12,%%ecx\n\t"
	"addl %2,%%ecx\n\t"
	"movl %%ecx,%%edx\n\t"
	"movl $1024,%%ecx\n\t"
	"leal 4092(%%edx),%%edi\n\t"
	"rep ; stosl\n\t"
	" movl %%edx,%%eax\n"
	"1: cld"
	:"=a" (__res)
	:"0" (0),"i" (LOW_MEM),"c" (PAGING_PAGES),
	"D" (mem_map+PAGING_PAGES-1)
	);
return __res;
}

/*
 * Free a page of memory at physical address 'addr'. Used by
 * 'free_page_tables()'
 *
 * 释放一个页，如果地址小于LOW_MEM 1MB以下直接退出
 * addr为物理地址
 */
void free_page(unsigned long addr)
{
	if (addr < LOW_MEM) {
		return;
	}
	if (addr >= HIGH_MEMORY) {
		panic("trying to free nonexistent page");
	}
	/* 
	 * 物理内存减去低端内存然后除以4KB得到物理页号，
	 * 获取页时mem_map[addr]设置为1，因此释放时mem_map[addr]--为0，从而释放页
	 * 这里多了一个判断是因为此页可能被共享，如果是共享的页只是减少计数
	 * 当计数为0才真正的释放页
	 *
	 */
	addr -= LOW_MEM;
	addr >>= 12;
	if (mem_map[addr]--) {
		return;
	}
	mem_map[addr] = 0;
	panic("trying to free free page");
}

/*
 * This function frees a continuos block of page tables, as needed
 * by 'exit()'. As does copy_page_tables(), this handles only 4Mb blocks.
 *
 * 释放页表连续的内存块
 * 根据线性地址和长度，释放对应内存页表所制定的内存块并设置表项空闲
 * 该函数处理4MB的内存块
 *
 */
int free_page_tables(unsigned long from,unsigned long size)
{
	unsigned long *pg_table;
	unsigned long * dir, nr;

	/*
	 * 释放的内存块地址需要以4MB为边界
	 *
	 */
	if (from & 0x3fffff) {
		panic("free_page_tables called with wrong alignment");
	}
	/*
	 * 0地址为内核和内核缓冲区，不能释放
	 *
	 */
	if (!from) {
		panic("Trying to free up swapper memory space");
	}
	
	/* 
	 * 计算所占页表目录项数，
	 * 一个页面目录项大小为4MB, 一共1024个页表目录项，共4GB空间
	 *
	 * size + 0x3fffff表示将size扩大到4MB然后对齐
	 * 经过这样的计算size表示的是需要释放的页表目录数量
	 *
	 */
	size = (size + 0x3fffff) >> 22;
	
	/* 
	 * 计算from所在的页面目录项，
	 * 目录项索引为form >> 22, 
	 * 由于每项占用4个字节，并且页目录是从0地址开始，
	 * 因此实际的目录项地址为(from >> 22) << 2即from >> 20
	 *
	 */
	dir = (unsigned long *) ((from>>20) & 0xffc); /* _pg_dir = 0 */

	/* 
	 * 主循环是页表目录项
	 * size表示有多少个页表目录项
	 * dir表示页表目录项的地址地址，每加一次就是下一个页表目录项
	 *
	 */
	for ( ; size-- > 0; dir++) {
		/*
		 * 如果该页表目录无效继续，页表目录的低12位有特殊的意义，在计算
		 * 页表时低12位可全部看作0，因为页表目录，页表，页都要求4KB对齐
		 *
		 */
		if (!(1 & *dir)) {
			continue;
		}
		/* 
		 * 取页表的物理地址
		 *
		 */
		pg_table = (unsigned long *) (0xfffff000 & *dir);

		/* 
		 * 每一个页表有1024个页，此处是循环释放1024个页
		 *
		 */
		for (nr=0; nr<1024; nr++) {
			/*
			 * 如果此页有效，则释放此页
			 * 0xfffff000 & *pg_table 表示页的物理地址
			 *
			 *
			 */
			if (1 & *pg_table) {
				free_page(0xfffff000 & *pg_table);
			}
			/* 
			 * 此页表项清零，指向下一个页表项
			 *
			 */
			*pg_table = 0;
			pg_table++;
		}
		/* 由于一个页表也占用了一个页，
		 * 当页表里面的页释放完成后，释放此页表
		 * 0xfffff000 & *dir 表示页表的物理地址
		 *
		 *
		 */
		free_page(0xfffff000 & *dir);
		*dir = 0;
	}
	invalidate();
	return 0;
}

/*
 *  Well, here is one of the most complicated functions in mm. It
 * copies a range of linerar addresses by copying only the pages.
 * Let's hope this is bug-free, 'cause this one I don't want to debug :-)
 *
 * Note! We don't copy just any chunks of memory - addresses have to
 * be divisible by 4Mb (one page-directory entry), as this makes the
 * function easier. It's used only by fork anyway.
 *
 * NOTE 2!! When from==0 we are copying kernel space for the first
 * fork(). Then we DONT want to copy a full page-directory entry, as
 * that would lead to some serious memory waste - we just copy the
 * first 160 pages - 640kB. Even that is more than we need, but it
 * doesn't take any more memory - we don't copy-on-write in the low
 * 1 Mb-range, so the pages can be shared with the kernel. Thus the
 * special case for nr=xxxx.
 */
int copy_page_tables(unsigned long from, unsigned long to, long size)
{
	unsigned long * from_page_table;
	unsigned long * to_page_table;
	unsigned long this_page;
	unsigned long * from_dir, * to_dir;
	unsigned long nr;

	/*
	 * 源地址和目的地址都是要在4M的边界上
	 *
	 */
	if ((from&0x3fffff) || (to&0x3fffff)) {
		panic("copy_page_tables called with wrong alignment");
	}

	/* 
	 * 获取源页表目录项和目的页表目录项的地址指针，以及大小size
	 * 如果看不懂，请看free_page_tables函数的说明
	 *
	 *
	 */
	from_dir = (unsigned long *) ((from>>20) & 0xffc); /* _pg_dir = 0 */
	to_dir = (unsigned long *) ((to>>20) & 0xffc);
	size = ((unsigned) (size+0x3fffff)) >> 22;

	for( ; size-->0 ; from_dir++,to_dir++) {
		/*
		 * 如果目的目录项的页表已经存在，则出错
		 *
		 */
		if (1 & *to_dir) {
			panic("copy_page_tables: already exist");
		}
		/*
		 * 如果源目录项的页表不存在，则忽略
		 */
		if (!(1 & *from_dir)) {
			continue;
		}

		/*
		 * 获取源页表地址
		 */
		from_page_table = (unsigned long *) (0xfffff000 & *from_dir);
		/*
		 * 获取一个空闲页面作为目的页表
		 */
		if (!(to_page_table = (unsigned long *) get_free_page())) {
			return -1;	/* Out of memory, see freeing */
		}

		/* 设置目的页表目录的地址为新申请的物理页表地址并设置属性
		 * 设置页表，并将此也变的后3位设置为111，表示(usr, RW, preset)
		 *
		 */
		*to_dir = ((unsigned long) to_page_table) | 7;

		/* from == 0表示是内核空间的页表目录项的起始地址
		 * 如果是内核则只需拷贝160个页，640KB的空间
		 * 640KB也是定义INIT_TASK的数据段和代码段长度
		 * 如果是其他也就是非任务0，则拷贝1024个页，共4MB空间
		 *
		 */
		//nr = (from == 0) ? 0xA0 :1 024;
		if (from) {
			nr = 1024;
		} else {
			nr = 160;
		}
		
		for ( ; nr-- > 0; from_page_table++, to_page_table++) {
			/*
			 * 将当前源页表项存到临时变量this_page
			 *
			 */
			this_page = *from_page_table;
			/* 
			 * 如果当前的页表项不存在则不用拷贝
			 */
			if (!(1 & this_page)) {
				continue;
			}
			/*
			 * 设置目的页表为只读，我们看到目的页表为只读
			 *
			 */
			this_page &= ~2;
			/*
			 * 将临时页表项内容赋值给目的页表项
			 * 通过此代码我们看到，
			 * 系统只是做了页表的设置并没有实现真正的数据拷贝
			 *
			 */
			*to_page_table = this_page;
			/* 
			 * 对于是1MB以下的内存，说明是内核页面因此不需要对mem_map进行设置
			 * 
			 * 如果代码是在任务0中创建任务1，则下面的代码不会用到
			 * 只有当调用者的代码处于主内存中(大于LOW_MEN)1MB时才会执行
			 * 这种情况需要在进程调用了execve()装载并执行了新代码才会出现
			 *
			 *
			 */
			if (this_page > LOW_MEM) {
				/* 
				 * 下面的内容是使其源页表项也可读，这样哪个进程先写会触发
				 * 缺页异常从而分配页进行使用
				 *
				 *
				 */
				*from_page_table = this_page;
				this_page -= LOW_MEM;
				this_page >>= 12;
				mem_map[this_page]++;
			}
		}
	}
	invalidate();
	return 0;
}

/*
 * This function puts a page in memory at the wanted address.
 * It returns the physical address of the page gotten, 0 if
 * out of memory (either when trying to access page-table or
 * page.)
 *
 * 下面的代码将一个物理页放在制定的address处，返回物理地址
 */
unsigned long put_page(unsigned long page,unsigned long address)
{
	unsigned long tmp, *page_table;

	/* NOTE !!! This uses the fact that _pg_dir=0 */

	/* chenwg
	 * 如果页小于1M或者大于HIGH_MEMORY则告警
	 * 如果所在的页的映射没有置位则告警
	 *
	 */
	if (page < LOW_MEM || page >= HIGH_MEMORY) {
		printk("Trying to put page %p at %p\n",page,address);
	}
	if (mem_map[(page-LOW_MEM)>>12] != 1) {
		printk("mem_map disagrees with %p at %p\n",page,address);
	}
	
	/* chenwg
	 * 根据地址计算页表目录项指针，在内核态中，页表目录从0地址开始，我们知道
	 * 在启用了CPU的页式内存管理后，虚拟地址右移22位得到页表目录的索引号
	 * 又因为每个页表目录占用4个字节因此将索引左移2位得到页表目录的地址，
	 * 页表目录的地址存放是页表的地址，每个页表要求4KB对其，每个页也要求4KB对齐
	 * 因此不管是页表目录存放的页表的地址还是页表里存放页的地址都低12位无效
	 * 所有的计算都已页表在0地址存放为基础
	 *
	 */
	page_table = (unsigned long *) ((address>>20) & 0xffc);

	/* chenwg
	 * *page_table获取页表的地址，其bit0表示该页表是否有效
	 * 如果有效获取页表地址
	 * 否则获取一个页作为页表
	 *
	 */
	if ((*page_table)&1) {
		page_table = (unsigned long *) (0xfffff000 & *page_table);
	} else {
		if (!(tmp=get_free_page())) {
			return 0;
		}
		*page_table = tmp|7;
		page_table = (unsigned long *) tmp;
	}
	/* chenwg
	 * address>>12表示这个虚拟地址在页表中的偏移
	 * 在页表的制定位置写上物理地址并设置此页有效
	 *
	 */
	page_table[(address>>12) & 0x3ff] = page | 7;
	/*
	 * 页表发生变化了，不明白为什么不用刷新
	 */
	/* no need for invalidate */
	return page;
}

/*
 * table_entry页表项指针
 */
void un_wp_page(unsigned long * table_entry)
{
	unsigned long old_page,new_page;

	
	/* 获取此页对应的物理地址
	 *
	 */
	old_page = 0xfffff000 & *table_entry;
	if (old_page >= LOW_MEM && mem_map[MAP_NR(old_page)]==1) {
		*table_entry |= 2;
		invalidate();
		return;
	}
	if (!(new_page=get_free_page()))
		oom();
	if (old_page >= LOW_MEM)
		mem_map[MAP_NR(old_page)]--;
	*table_entry = new_page | 7;
	invalidate();
	copy_page(old_page,new_page);
}	

/*
 * This routine handles present pages, when users try to write
 * to a shared page. It is done by copying the page to a new address
 * and decrementing the shared-page counter for the old page.
 *
 * If it's in code space we exit with a segment error.
 */
void do_wp_page(unsigned long error_code,unsigned long address)
{
#if 0
/* we cannot do this yet: the estdio library writes to code space */
/* stupid, stupid. I really want the libc.a from GNU */
	if (CODE_SPACE(address))
		do_exit(SIGSEGV);
#endif
	un_wp_page((unsigned long *)
		(((address>>10) & 0xffc) + (0xfffff000 &
		*((unsigned long *) ((address>>20) &0xffc)))));

}

void write_verify(unsigned long address)
{
	unsigned long page;

	if (!( (page = *((unsigned long *) ((address>>20) & 0xffc)) )&1))
		return;
	page &= 0xfffff000;
	page += ((address>>10) & 0xffc);
	if ((3 & *(unsigned long *) page) == 1)  /* non-writeable, present */
		un_wp_page((unsigned long *) page);
	return;
}

void get_empty_page(unsigned long address)
{
	unsigned long tmp;

	if (!(tmp=get_free_page()) || !put_page(tmp,address)) {
		free_page(tmp);		/* 0 is ok - ignored */
		oom();
	}
}

/*
 * try_to_share() checks the page at address "address" in the task "p",
 * to see if it exists, and if it is clean. If so, share it with the current
 * task.
 *
 * NOTE! This assumes we have checked that p != current, and that they
 * share the same executable.
 */
static int try_to_share(unsigned long address, struct task_struct * p)
{
	unsigned long from;
	unsigned long to;
	unsigned long from_page;
	unsigned long to_page;
	unsigned long phys_addr;

	from_page = to_page = ((address>>20) & 0xffc);
	from_page += ((p->start_code>>20) & 0xffc);
	to_page += ((current->start_code>>20) & 0xffc);
/* is there a page-directory at from? */
	from = *(unsigned long *) from_page;
	if (!(from & 1))
		return 0;
	from &= 0xfffff000;
	from_page = from + ((address>>10) & 0xffc);
	phys_addr = *(unsigned long *) from_page;
/* is the page clean and present? */
	if ((phys_addr & 0x41) != 0x01)
		return 0;
	phys_addr &= 0xfffff000;
	if (phys_addr >= HIGH_MEMORY || phys_addr < LOW_MEM)
		return 0;
	to = *(unsigned long *) to_page;
	if (!(to & 1)) {
		if ((to = get_free_page()))
			*(unsigned long *) to_page = to | 7;
		else
			oom();
	}
	to &= 0xfffff000;
	to_page = to + ((address>>10) & 0xffc);
	if (1 & *(unsigned long *) to_page)
		panic("try_to_share: to_page already exists");
/* share them: write-protect */
	*(unsigned long *) from_page &= ~2;
	*(unsigned long *) to_page = *(unsigned long *) from_page;
	invalidate();
	phys_addr -= LOW_MEM;
	phys_addr >>= 12;
	mem_map[phys_addr]++;
	return 1;
}

/*
 * share_page() tries to find a process that could share a page with
 * the current one. Address is the address of the wanted page relative
 * to the current data space.
 *
 * We first check if it is at all feasible by checking executable->i_count.
 * It should be >1 if there are other tasks sharing this inode.
 */
static int share_page(unsigned long address)
{
	struct task_struct ** p;

	if (!current->executable)
		return 0;
	if (current->executable->i_count < 2)
		return 0;
	for (p = &LAST_TASK ; p > &FIRST_TASK ; --p) {
		if (!*p)
			continue;
		if (current == *p)
			continue;
		if ((*p)->executable != current->executable)
			continue;
		if (try_to_share(address,*p))
			return 1;
	}
	return 0;
}

void do_no_page(unsigned long error_code,unsigned long address)
{
	int nr[4];
	unsigned long tmp;
	unsigned long page;
	int block,i;

	address &= 0xfffff000;
	tmp = address - current->start_code;
	if (!current->executable || tmp >= current->end_data) {
		get_empty_page(address);
		return;
	}
	if (share_page(tmp))
		return;
	if (!(page = get_free_page()))
		oom();
/* remember that 1 block is used for header */
	block = 1 + tmp/BLOCK_SIZE;
	for (i=0 ; i<4 ; block++,i++)
		nr[i] = bmap(current->executable,block);
	bread_page(page,current->executable->i_dev,nr);
	i = tmp + 4096 - current->end_data;
	tmp = page + 4096;
	while (i-- > 0) {
		tmp--;
		*(char *)tmp = 0;
	}
	if (put_page(page,address))
		return;
	free_page(page);
	oom();
}

long get_total_pages(void)
{
	return total_pages;
}

/*******************************************************************************
	start_mem用作分页的物理内存的起始地址
	end_mem 实际物理内存的最大地址
*******************************************************************************/
void mem_init(long start_mem, long end_mem)
{
	int i;

/*******************************************************************************
	HIGH_MEMORY是一个变量，用于记录当前内存的最大限制

	PAGING_PAGES定义为(PAGING_MEMORY>>12)
	PAGING_MEMORY的值为15*1024*1024为15MB，在Linux内核中最多能使用的内存为16MB
	最低的1MB属于内核系统不在内存管理内，即LOW的值为0x100000

	因此在系统最开始处先将所有的页面就设置为已用后面在根据实际内存数进行清除

	MAP_NR(addr)定义为(((addr) - LOW_MEM) >> 12)表示页编号，我们可以看到页编号
	去除了最低的1MB空间，并且页编码从start_mem开始，也就是说buffer和ramdisk区域
	也都被设置为已用

	end_mem -= start_mem计算出可用内存的大小
	end_mem >>= 12 右移12位相当于除以4096，表示可用内存大小占用的页数
	然后设置mem_map对应的标志

	此时系统可用内存已经在mem_map进行管理了，哪些页使用过，哪些页还未使用
	一目了然
*******************************************************************************/

	HIGH_MEMORY = end_mem;
	for (i=0 ; i<PAGING_PAGES ; i++)
		mem_map[i] = USED;
	i = MAP_NR(start_mem);
	end_mem -= start_mem;
	end_mem >>= 12;
	total_pages= end_mem;
	while (end_mem-->0)
		mem_map[i++]=0;
}

void calc_mem(void)
{
	int i,j,k,free=0;
	long * pg_tbl;

	for(i=0 ; i<PAGING_PAGES ; i++)
		if (!mem_map[i]) free++;
	printk("%d pages free (of %d)\n\r", free, PAGING_PAGES);
	for(i=2 ; i<1024 ; i++) {
		if (1&pg_dir[i]) {
			pg_tbl=(long *) (0xfffff000 & pg_dir[i]);
			for(j=k=0 ; j<1024 ; j++)
				if (pg_tbl[j]&1)
					k++;
			printk("Pg-dir[%d] uses %d pages\n",i,k);
		}
	}
}
