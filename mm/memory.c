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
__asm__ __volatile__("movl %%cr3,%%eax\n\tmovl %%eax,%%cr3":::"ax")


/* these are not to be changed without changing head.s etc */
/*
 * 这里的参数和head.s有对应关系
 */
#define PAGING_MEMORY 		(16*1024*1024)
#define PAGE_SHIFT 			(12)
#define PAGING_PAGES 		(PAGING_MEMORY>>PAGE_SHIFT)
#define PAGING_ADDR(nr)		(((unsigned long)(nr))<<PAGE_SHIFT)
#define MAP_NR(addr) 		(((unsigned long)(addr))>>PAGE_SHIFT)
/*
 * mem_map中的标记为USED表示内核维护的页
 */
#define USED 				0x80
/*
 * 页属性
 */
#define PAGE_DIRTY			0x40
#define PAGE_ACCESSED		0x20
#define PAGE_USER			0x04
#define PAGE_RW				0x02
#define PAGE_PRESENT		0x01

/*
 * HIGH_MEMORY 表示系统内存的最大值
 * LOW_MEMORY 表示内存管理的最小值
 * total_pages 表示当前系统中一共有多个页
 * mem_map 系统中每个页都对应mem_map中的一个数组项，BIT8表示中个页是内核保留页，不参与内存管理
 * 
 */
static unsigned long HIGH_MEMORY = 0;
static unsigned long LOW_MEMORY = 0;
static unsigned long available_pages = 0;
static unsigned char mem_map [ PAGING_PAGES ] = {0,};
/* chenwg
 * 复制一页4KB的内存
 *
 */
void copy_page(unsigned long from, unsigned to)
{
#ifdef LINUX_ORG
	__asm__("cld ; rep ; movsl"::"S" (from),"D" (to),"c" (1024));
#else
	unsigned int i = 0;
	for (i = 0; i < 4096; i += 4) {
		*(unsigned int*)(to + i) = *(unsigned int*)(from + i);
	}
#endif
}

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
#ifdef LINUX_ORG
register unsigned long __res asm("ax");

	__asm__("std ; repne ; scasb\n\t"	//方向位置位
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
		:"0" (0), "i" (0), "c" (PAGING_PAGES), "D" (mem_map+PAGING_PAGES-1)
	);
	return __res;
#else
	unsigned long j = 0;
	unsigned long i = MAP_NR(LOW_MEMORY);

	/*
	 * 我们希望获取页是一个不可打断的过程，因此禁用中断
	 */
	while(i < PAGING_PAGES) {
		/*
		 * 如果是保留页或者页计数不为0
		 */
		if (mem_map[i] & USED || mem_map[i]) {
			i++;
			continue;
		}
		/*
		 * 设置页计数为1
		 */
		mem_map[i] = 1;
		/*
		 * 将页编号转换为物理地址
		 */
		i = PAGING_ADDR(i);
		/*
		 * 将物理地址清零
		 */
		for (j = 0; j < 4096; j += 4) {
			*((unsigned int *)(i + j)) = 0;
		}
		return i;
	}
	return 0;
#endif
}

/*
 *
 * 设释放一个物理页，addr为物理地址
 * 
 */
void free_page(unsigned long addr)
{
	/*
	 * 如果所给的地址大于系统的最大地址或者地址映射标记为USED，直接退出
	 */
	if (addr >= HIGH_MEMORY) {
		panic("trying to free nonexistent page");
	}
	if (mem_map[MAP_NR(addr)] & USED) {
		printk("system reserve mem, ignore free\n");
		return;
	}
	/*
	 * 如果内存计数不为0，则减去一次计数，此时释放成功
	 * 否则panic
	 */
	if (mem_map[MAP_NR(addr)]) {
		mem_map[MAP_NR(addr)]--;
		return;	
	}
	panic("trying to free free page");
	return;
}

/*
 * 释放一个页表
 */
static void free_one_table(unsigned long * page_dir)
{
	/*
	 * pg_table是此页表的物理地址
	 * page_table是将pg_table转化为指针，方便计算
	 */
	int j;
	unsigned long pg_table = *page_dir;
	unsigned long *page_table;

	/*
	 * 如果页表不存在或者页表的物理无效
	 */
	if (!pg_table) {
		return;
	}
		
	if (pg_table >= HIGH_MEMORY|| !(pg_table & 1)) {
		printk("Bad page table: [%08x]=%08x\n", page_dir, pg_table);
		return;
	}

	if (mem_map[MAP_NR(pg_table)] & USED) {
		return;
	}
		
	/*
	 * 遍历页表，然后针对每一个页表项清零
	 * 如果页表项对应的页有效，则释放这个物理页
	 * 
	 */
	page_table = (unsigned long *) (pg_table & 0xfffff000);
	for (j = 0 ; j < 1024 ; j++, page_table++) {
		/*
		 * pg是页地址
		 */
		unsigned long pg = *page_table;
		
		if (!pg)
			continue;
			
		if (mem_map[MAP_NR(pg)] & USED)
			continue;
			
		*page_table = 0;
		if (1 & pg)
			free_page(0xfffff000 & pg);
	}
	/* 
	 * 页表目录项清零
	 * 释放页表所占用的页
	 */
	*page_dir = 0;
	free_page(0xfffff000 & pg_table);
}

/*
 * 释放tsk用户空间的所有页表
 * 这个函数只要用在exec系统调用中
 * exec系统会使用新的代码和数据将当前的tsk覆盖，但是任务0不能进行exec调用
 * 
 */
void clear_page_tables(struct task_struct * tsk)
{
	int i;
	unsigned long * page_dir;

	/*
	 * 检查任务是否有效
	 */
	if (!tsk)
		return;
	if (tsk == task[0])
		panic("task[0] (swapper) doesn't support exec() yet\n");

	/*
	 * 获取tsk的页表目录地址
	 */
	page_dir = (unsigned long *) tsk->tss.cr3;
	if (!page_dir) {
		printk("Trying to clear kernel page-directory: not good\n");
		return;
	}
	/*
	 * 释放0GB-3GB的页表目录项
	 */
	for (i = 0 ; i < 768 ; i++, page_dir++)
		free_one_table(page_dir);
	invalidate();
	return;
}


/*
 * This function frees up all page tables of a process when it exits.
 * 释放所有0GB-4GB的页表
 * 并使用swapper_pg_dir作为临时页表
 * 这个函数主要在exit中进行调用
 */
int free_page_tables(struct task_struct * tsk)
{
	int i;
	unsigned long pg_dir;
	unsigned long * page_dir;

	if (!tsk)
		return 1;
		
	if (tsk == task[0]) {
		panic("Trying to free up task[0] (swapper) memory space");
	}
	
	pg_dir = tsk->tss.cr3;
	if (!pg_dir) {
		printk("Trying to free kernel page-directory: not good\n");
		return 1;
	}
	
	/*
	 * 使用任务0的页表并使用
	 */
	tsk->tss.cr3 = (unsigned long) swapper_pg_dir;
	if (tsk == current)
		__asm__ __volatile__("movl %0,%%cr3"::"a" (tsk->tss.cr3));
		
	page_dir = (unsigned long *) pg_dir;
	for (i = 0 ; i < 1024 ; i++,page_dir++) {
		free_one_table(page_dir);
	}
	
	free_page(pg_dir);
	invalidate();
	return 0;
}


/*
 * copy_page_tables() just copies the whole process memory range:
 * note the special handling of RESERVED (ie kernel) pages, which
 * means that they are always shared by all processes.
 * 
 * 这个函数拷贝current的页表到tsk中，但并未真正分配内存
 */
int copy_page_tables(struct task_struct * tsk)
{
	int i;
	int page_count = 1024;
	unsigned long old_pg_dir, *old_page_dir;
	unsigned long new_pg_dir, *new_page_dir;

	/*
	 * 获取当前进程的页表目录的地址
	 */
	old_pg_dir = current->tss.cr3;
	/*
	 * 获取一个物理页用于新进程的页表目录
	 */
	new_pg_dir = get_free_page();
	if (!new_pg_dir)
		return -1;
	
	/*
	 * 将新页表目录的物理地址存放在TSS的CR3中，CR3存放的是页表目录基地址
	 */
	tsk->tss.cr3 = new_pg_dir;
	old_page_dir = (unsigned long *) old_pg_dir;
	new_page_dir = (unsigned long *) new_pg_dir;

	/* 
	 * 如果是第一个进程调用了fock，则只需复制160个页，也就是640KB的空间
	 * 第一个进程是手工创建出来的，
	 * 在head.s模块中我们使用了一个页表目录共1024个页表维护4M的空间
	 * 但其实640K就够用了
	 * 由此我们也很容易就知道，在调用了exec之前，所有的进程都是160个页
	 * 这样做可以节省很多内存
	 *
	 */
	if (current == task[0]) {
		page_count = 160;
	} else {
		page_count = 1024;
	}
	
	/*
	 * 变量页表目录，进程0用户空间只有一个目录项，
	 * 内核空间从768项开始有4项有效，共16MB的空间
	 * 
	 */
	for (i = 0 ; i < 1024 ; i++,old_page_dir++,new_page_dir++) {
		int j;
		unsigned long old_pg_table, *old_page_table;
		unsigned long new_pg_table, *new_page_table;

		/*
		 * 页表目录存放了页表的地址，每个页表也有1024项 
		 */
		old_pg_table = *old_page_dir;
		/*
		 * 如果页表无效
		 */
		if (!old_pg_table)
			continue;
		/*
		 * 如果页表的值大于系统的最大地址或者页表的值无效
		 */
		if (old_pg_table >= HIGH_MEMORY || !(1 & old_pg_table)) {
			continue;
		}

		/* 
		 * i >= 768表示3GB以上的内核，3GB以上的内存表示内核空间
		 * 所有进程共享内核空间，内核空间的页表都是一个
		 * 并且可读可写
		 * 对于进程0执行fork时，虽然USED标记成立，但是其用户空间的页表不能共用
		 *
		 */
		if (mem_map[MAP_NR(old_pg_table)] & USED && i >= 768) {
			*new_page_dir = old_pg_table;
			continue;
		}

		/*
		 * 获取一个物理页作为新的页表
		 * 如果获取失败则释放所有页目录
		 * 
		 */
		new_pg_table = get_free_page();
		if (!new_pg_table) {
			free_page_tables(tsk);
			return -1;
		}
		/*
		 * 根据硬件要求设置相应的位，并将此页表地址付给页表目录项中
		 */
		*new_page_dir = new_pg_table | PAGE_ACCESSED | 7;

		/*
		 * 开始复制页表
		 */
		old_page_table = (unsigned long *) (0xfffff000 & old_pg_table);
		new_page_table = (unsigned long *) (0xfffff000 & new_pg_table);
		/*
		 * 对于第一个page_count为160，共640KB
		 */
		for (j = 0 ; j < page_count ; j++,old_page_table++,new_page_table++) {
			unsigned long pg;
			pg = *old_page_table;
			/*
			 * 页无效
			 */
			if (!pg)
				continue;
			/*
			 * 页不存在
			 */
			if (!(pg & PAGE_PRESENT)) {
				continue;
			}
			/*
			 * 设置页属性清除读写标记,PAGE_RW是读/写（Read/Write）标志。
			 * PAGE_RW如果等于1，表示页面可以被读、写或执行。如果为0，表示页面只读或可执行
			 * 当处理器运行在超级用户特权级（级别0、1或2）时不起作用
			 */
			pg &= ~PAGE_RW;
			*new_page_table = pg;

			/*
			 * 如果页表对应的内存映射表标记为USED，则只将新进程的页表标记为只读
			 * 否认就将两个进程的页都设置为只读，无论哪个进程先运行，都会抽发写保护
			 * 只有进程0页表对应的内存映射为USED
			 */
			if (mem_map[MAP_NR(pg)] & USED)
				continue;
			*old_page_table = pg;
			/*
			 * 增加页的计数
			 */
			mem_map[MAP_NR(pg)]++;
		}
	}
	invalidate();
	return 0;
}

/*
 * page是页的物理地址，address是虚拟地址
 * 这个函数的目的是将虚拟地址address映射到物理地址page上
 * 
 */
unsigned long put_page(unsigned long page, unsigned long address)
{
	unsigned long tmp, *page_table;
	struct task_struct *tsk = current;

	/* NOTE !!! This uses the fact that _pg_dir=0 */

	/*
	 * 如果物理地址page大于系统最大内存，返回错误
	 */
	if (page >= HIGH_MEMORY)
		printk("put_dirty_page: trying to put page %p at %p\n",page,address);
	
	/*
	 * 只有新获取的内存页才能被映射
	 */
	if (mem_map[MAP_NR(page)] != 1)
		printk("mem_map disagrees with %p at %p\n",page,address);

	/*
	 * (address>>20) & 0xffc) 可以得出address对应的页表目录项
	 * 然后加上CR0基地址就是页表目录项的地址
	 */	
	page_table = (unsigned long *) (tsk->tss.cr3 + ((address>>20) & 0xffc));
	
	/*
	 * 如果此页表目录项有效则根据页表目录项内容获取页表的地址
	 * 否则申请一个物理页作为这个页表目录项的页表并设置相应的属性
	 * 
	 */
	if ((*page_table)&1) {
		page_table = (unsigned long *) (0xfffff000 & *page_table);
	} else {
		if (!(tmp=get_free_page())) {
			return 0;
		}
		*page_table = tmp | PAGE_ACCESSED |7;
		page_table = (unsigned long *) tmp;
	}
	/*
	 * page_table已经是页表的地址了
	 * address >> PAGE_SHIFT 获取address在页中的偏移
	 * 下面语句执行完毕后page_table就是address对应的页表项
	 * 如果address对应的页表项有内容，打印错误并重新映射到page上
	 * 最后返回物理地址
	 */
	page_table += (address >> PAGE_SHIFT) & 0x3ff;
	if (*page_table) {
		printk("put_dirty_page: page already exists\n");
		*page_table = 0;
		invalidate();
	}
	*page_table = page | (PAGE_DIRTY | PAGE_ACCESSED | 7);
	/* no need for invalidate */
	return page;
}



/*
 * table_entry页表项指针
 * 
 */
void un_wp_page(unsigned long * table_entry)
{
	unsigned long old_page, new_page;
	/* 
	 * 获取此页对应的物理地址，如果原页面不是保留并且其值为1表示没有共享，
	 * 直接设置设置写标记
	 *
	 */
	old_page = 0xfffff000 & *table_entry;

	if (!(mem_map[MAP_NR(old_page)] & USED) && mem_map[MAP_NR(old_page)] == 1) {
		*table_entry |= PAGE_RW;
		invalidate();
		return;
	}
	/*
	 * 获取一个物理页
	 */
	new_page=get_free_page();
	if (!new_page) {
		oom();
	}

	/*
	 * 如果是USED标记的页表示不受内存管理的页，如果没有标记则内存页面使用计数减一
	 *
	 */
	if (!(mem_map[MAP_NR(old_page)] & USED)) {
		mem_map[MAP_NR(old_page)]--;
	}
		
	/*
	 * 将新的物理页的地址写入页表项中，并将原来页中的数据拷贝到新页中
	 */
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
 * 
 * address为虚拟地址，其意义为代码读写address出错
 * 
 */
void do_wp_page(unsigned long error_code, unsigned long address)
{
	/*
	 * dir_base当前进程页表目录基地址
	 * (address >> 22)表示address对应的页表目录项偏移
	 * dir_base + 偏移表示address对应的页表目录项
	 * *dir_item表示页表目录项的值，也就是页表的地址
	 * ((address>>10)表示页内偏移
	 * *dir_item + (address>>10) 就address对应的页
	 */
	unsigned long* dir_base = (unsigned long *)current->tss.cr3;
	unsigned long* dir_item = dir_base + (address >> 22);
	un_wp_page((unsigned long *)(((address>>10) & 0xffc) + (0xfffff000 & *dir_item)));
}

/*
 * 检查虚拟地址是否可写，如果不可写则需要给address分配实际的页
 */
void write_verify(unsigned long address)
{
	unsigned long page;

	page = *(unsigned long *) (current->tss.cr3 + ((address>>20) & 0xffc));
	if (!(page & PAGE_PRESENT)) {
		return;
	}
	page &= 0xfffff000;
	page += ((address>>10) & 0xffc);
	if ((3 & *(unsigned long *) page) == 1) { /* non-writeable, present */
		un_wp_page((unsigned long *) page);
	}
	return;
}

/*
 * 获取一个空闲页并将其映射到虚拟地址address处
 * 
 */
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
 * 
 * 要将address映射到进程p上
 * 
 */
static int try_to_share(unsigned long address, struct task_struct * p)
{
	struct task_struct *tsk = current;
	
	unsigned long from;
	unsigned long to;
	unsigned long from_page;
	unsigned long to_page;
	unsigned long phys_addr;

	/*
	 * 根据address获取页表目录项
	 */
	from_page = p->tss.cr3 + ((address>>20) & 0xffc);
	to_page = tsk->tss.cr3 + ((address>>20) & 0xffc);
	/* is there a page-directory at from? */
	/*
	 * 目的进程的页表目录项是否有效，无效则返回0
	 */
	from = *(unsigned long *) from_page;
	if (!(from & 1))
		return 0;
	/*
	 * 根据address获取页表项的便宜在加上页表地址
	 * 得到address所在的页表项
	 */
	from &= 0xfffff000;
	from_page = from + ((address>>10) & 0xffc);
	/*
	 * 页表项里面存放就是物理页地址
	 */
	phys_addr = *(unsigned long *) from_page;
	/* is the page clean and present? */
	/*
	 * 页是否有效，页大于系统最大地址和USED标记的页都不可共享
	 */
	if ((phys_addr & 0x41) != 0x01) {
		return 0;
	}
	phys_addr &= 0xfffff000;
	if (phys_addr >= HIGH_MEMORY) {
		return 0;
	}
	if (mem_map[MAP_NR(phys_addr)] & USED) {
		return 0;
	}
	/*
	 * 获取目的页表项，也就是目的页表
	 * 如果目的无效则获取一个新页作为目的页表
	 */
	to = *(unsigned long *) to_page;
	if (!(to & 1)) {
		to = get_free_page();
		if (!to) {
			return 0;
		}
		*(unsigned long *) to_page = to | PAGE_ACCESSED | 7;
	}
	/*
	 * 获取目的页对应的页表项，如果目的页表项已经存在则panic
	 * 否则共享这个页
	 */
	to &= 0xfffff000;
	to_page = to + ((address>>10) & 0xffc);
	if (1 & *(unsigned long *) to_page) {
		panic("try_to_share: to_page already exists");
	}
	/* share them: write-protect */
	/*
	 * 设置两个进程的对应页都是是写保护
	 */
	*(unsigned long *) from_page &= ~PAGE_RW;
	*(unsigned long *) to_page = *(unsigned long *) from_page;
	/*
	 * 刷新页表
	 */
	invalidate();
	/*
	 * 增加物理页的引用计数
	 */
	mem_map[MAP_NR(phys_addr)]++;
	return 1;

}

/*
 * share_page() tries to find a process that could share a page with
 * the current one. Address is the address of the wanted page relative
 * to the current data space.
 *
 * We first check if it is at all feasible by checking executable->i_count.
 * It should be >1 if there are other tasks sharing this inode.
 * 
 * 尝试进行address共享
 * 成功返回1，失败返回0
 * 
 */
static int share_page(unsigned long address)
{
	struct task_struct ** p;

	/*
	 * 如果是不可执行的，则返回，executable是执行进程的i节点
	 */
	if (!current->executable)
		return 0;
	if (current->executable->i_count < 2)
		return 0;

	/*
	 * 遍历任务，寻找和当前进程可以共享的进程
	 * 我们可以看到可以共享的条件为executable相等
	 */	
	for (p = &LAST_TASK ; p > &FIRST_TASK ; --p) {
		if (!*p) {
			continue;
		}
		if (current == *p) {
			continue;
		}
		if ((*p)->executable != current->executable) {
			continue;
		}
		if (try_to_share(address, *p)) {
			return 1;
		}
	}
	return 0;
}

/*
 * 缺页异常函数，address表示在此处出现缺页异常
 *
 */
void do_no_page(unsigned long error_code, unsigned long address)
{
	int nr[4];
	unsigned long tmp;
	unsigned long page;
	int block,i;

	/*
	 * current->start_code表示代码段的起始地址，此处为0
	 * tmp就是address
	 */
	address &= 0xfffff000;
	tmp = address - current->start_code;
	/*
	 * 如果当前任务没有executable并且tmp在数据段之后
	 * 表示有可能访问的BSS段产生缺页
	 * 这个时候至需要给address这个虚拟地址上映射一个物理页即可
	 * 
	 */
	if (!current->executable || tmp >= current->end_data) {
		get_empty_page(address);
		return;
	}
	/*
	 * 尝试共享tmp，如果共享成功，直接退出
	 */
	if (share_page(tmp)) {
		return;
	}
	/*
	 * 获取一个新的物理页
	 */
	if (!(page = get_free_page())) {
		printk("%s-%d\n", __func__, __LINE__);
		oom();
	}
		
	/* remember that 1 block is used for header */
	/*
	 * 从磁盘读取数据存放到page处，并将page映射到address
	 */
	block = 1 + tmp/BLOCK_SIZE;
	for (i = 0; i < 4; block++,i++) {
		nr[i] = bmap(current->executable, block);
	}
	bread_page(page, current->executable->i_dev, nr);
	i = tmp + 4096 - current->end_data;
	tmp = page + 4096;
	while (i-- > 0) {
		tmp--;
		*(char *)tmp = 0;
	}
	if (put_page(page, address)) {
		return;	
	}
	free_page(page);
	oom();
}

/*
 * 获取系统的内存
 */
long get_available_pages(void)
{
	return available_pages;
}


/*
 * start_mem 表示内存的起始地址，当内存大小为16M，start_mem为6M
 * end_mem 为16M
 */
void mem_init(long start_mem, long end_mem)
{
	int i;
	/* HIGH_MEMORY是一个变量，记录当前系统内存最大值
	 * PAGING_MEMORY的值为16*1024*1024表示当前系统内存为16M
	 * PAGING_PAGES定义为(PAGING_MEMORY>>12)表示当前系统可分成多少个4KB的页面
	 * MAP_NR(addr)定义为(((unsigned long)(addr))>>12)表示addr的索引号
	 * end_mem -= start_mem计算出可用内存的大小
	 * end_mem >>= 12 右移12位相当于除以4096，表示可用内存大小占用的页数，并将这个值赋值给total_pages
	 * 下面的语句先将mem_map设置为USED，表示所有的内存都已经使用
	 * 然后将将start_mem到end_mem之间的mem_map设置为0，表示空闲
	 */
	HIGH_MEMORY = end_mem;
	LOW_MEMORY = start_mem;
	/*
	 * 先将所有的内存都设置为USED
	 */
	for (i = 0; i < PAGING_PAGES; i++) {
		mem_map[i] = USED;
	}
	/*
	 * 将从start_mem开始到end_mem的mem_map设置为0
	 */
	i = MAP_NR(start_mem);
	end_mem -= start_mem;
	end_mem >>= 12;
	available_pages = end_mem;
	while (end_mem-- > 0) {
		mem_map[i++] = 0;
	}
}

/*
 * 显示当前系统内存的使用情况
 * 
 */
void show_mem(void)
{
	int i,free = 0,total = 0,reserved = 0;
	int shared = 0;
	
	i = HIGH_MEMORY >> PAGE_SHIFT;
	printk("Mem-info %d pages:\n", i);
	while (i-- > 0) {
		total++;
		if (mem_map[i] & USED) {
			reserved++;
		} else if (!mem_map[i]) {
			free++;
		} else {
			shared += mem_map[i]-1;
		}
	}
	printk("Buffer blocks: %d blocks(1KB) %dMB\n", nr_buffers, (nr_buffers*BLOCK_SIZE)/(1024*1024));
	printk("Tatal pages: %d pages(4KB) %dMB\n", total, (total*4096)/(1024*1024));
	printk("Free pages: %d pages(4KB) %dMB\n", free, (free*4096)/(1024*1024));
	printk("Reserved pages: %d pages(4KB) %dMB\n", reserved, (reserved*4096)/(1024*1024));
	printk("Shared pages: %d pages(4KB) %dMB\n", shared, (shared*4096)/(1024*1024));
}
