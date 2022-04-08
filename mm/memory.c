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

#define PAGING_MEMORY 		(16*1024*1024)
#define PAGING_PAGES 		(PAGING_MEMORY>>12)
#define MAP_NR(addr) 		(((unsigned long)(addr))>>12)
#define USED 				(1<<7)
#define PAGE_DIRTY			0x40
#define PAGE_ACCESSED		0x20
#define PAGE_USER			0x04
#define PAGE_RW				0x02
#define PAGE_PRESENT		0x01
#define PAGE_SHIFT 			12


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
	:"0" (0),"i" (0),"c" (PAGING_PAGES),
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
	int i = 0;
	
	if (addr >= HIGH_MEMORY) {
		panic("trying to free nonexistent page");
	}

	i = MAP_NR(addr);

	if (mem_map[i] & USED) {
		//printk("system reserve mem, ignore free\n");
		return;
	}

	if (!mem_map[i]) {
		panic("trying to free free page");
	}

	mem_map[i]--;
	return;
}

static void free_one_table(unsigned long * page_dir)
{

	int j;
	unsigned long pg_table = *page_dir;
	unsigned long * page_table;

	if (!pg_table)
		return;
		
	if (pg_table >= HIGH_MEMORY|| !(pg_table & 1)) {
		printk("Bad page table: [%08x]=%08x\n",page_dir,pg_table);
		*page_dir = 0;
		return;
	}
	
	*page_dir = 0;
	if (mem_map[MAP_NR(pg_table)] & USED) {
		return;
	}
		
	page_table = (unsigned long *) (pg_table & 0xfffff000);
	for (j = 0 ; j < 1024 ; j++,page_table++) {
		unsigned long pg = *page_table;
		
		if (!pg)
			continue;
		*page_table = 0;
		if (1 & pg)
			free_page(0xfffff000 & pg);
	}
	free_page(0xfffff000 & pg_table);
}

void clear_page_tables(struct task_struct * tsk)
{
	int i;
	unsigned long * page_dir;
	unsigned long tmp;

	if (!tsk)
		return;
	if (tsk == task[0])
		panic("task[0] (swapper) doesn't support exec() yet\n");


	page_dir = (unsigned long *) tsk->tss.cr3;
	if (!page_dir) {
		printk("Trying to clear kernel page-directory: not good\n");
		return;
	}
	for (i = 0 ; i < 768 ; i++,page_dir++)
		free_one_table(page_dir);
	invalidate();
	return;
}


/*
 * This function frees up all page tables of a process when it exits.
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
	
	tsk->tss.cr3 = (unsigned long) 0;
	if (tsk == current)
		__asm__ __volatile__("movl %0,%%cr3"::"a" (tsk->tss.cr3));
		
	page_dir = (unsigned long *) pg_dir;
	for (i = 0 ; i < 1024 ; i++,page_dir++)
		free_one_table(page_dir);
		
	*page_dir = 0;
	free_page(pg_dir);
	invalidate();
	return 0;
}


/*
 * copy_page_tables() just copies the whole process memory range:
 * note the special handling of RESERVED (ie kernel) pages, which
 * means that they are always shared by all processes.
 */
int copy_page_tables(struct task_struct * tsk)
{
	int i;
	int page_count = 1024;
	unsigned long old_pg_dir, *old_page_dir;
	unsigned long new_pg_dir, *new_page_dir;

	old_pg_dir = current->tss.cr3;
	new_pg_dir = get_free_page();
	if (!new_pg_dir)
		return -1;
	
	tsk->tss.cr3 = new_pg_dir;
	old_page_dir = (unsigned long *) old_pg_dir;
	new_page_dir = (unsigned long *) new_pg_dir;

	/* 
	 * old_pg_dir如果是0，表示第一个进程，第一个进程只有160个也有效，这样
	 * 做可以节省很多内存
	 *
	 */
	if (!old_pg_dir) {
		page_count = 160;
	} else {
		page_count = 1024;
	}
	
	for (i = 0 ; i < 1024 ; i++,old_page_dir++,new_page_dir++) {
		int j;
		unsigned long old_pg_table, *old_page_table;
		unsigned long new_pg_table, *new_page_table;

		old_pg_table = *old_page_dir;
		if (!old_pg_table)
			continue;
		if (old_pg_table >= HIGH_MEMORY || !(1 & old_pg_table)) {
			printk("copy_page_tables: bad page table: "
				"probable memory corruption  %d %p\n", i, old_pg_table);
			*old_page_dir = 0;
			continue;
		}

		/* 
		 * i >= 768表示3GB以上的内核，3GB以上的内存表示内核空间
		 * 所有进程共享内核空间，内核空间的页表都是一个
		 *
		 */
		if (mem_map[MAP_NR(old_pg_table)] & USED && i >= 768) {
			*new_page_dir = old_pg_table;
			continue;
		}

		new_pg_table = get_free_page();
		if (!new_pg_table) {
			free_page_tables(tsk);
			return -1;
		}
		*new_page_dir = new_pg_table | PAGE_ACCESSED | 7;
		old_page_table = (unsigned long *) (0xfffff000 & old_pg_table);
		new_page_table = (unsigned long *) (0xfffff000 & new_pg_table);
		for (j = 0 ; j < page_count ; j++,old_page_table++,new_page_table++) {
			unsigned long pg;
			pg = *old_page_table;
			if (!pg)
				continue;
			if (!(pg & PAGE_PRESENT)) {
				continue;
			}
			pg &= ~2;
			*new_page_table = pg;
			if (mem_map[MAP_NR(pg)] & USED)
				continue;
			*old_page_table = pg;
			mem_map[MAP_NR(pg)]++;
		}
	}
	invalidate();
	//show_mem();
	return 0;
}

unsigned long put_page(unsigned long page, unsigned long address)
{
	unsigned long tmp, *page_table;
	struct task_struct *tsk = current;

/* NOTE !!! This uses the fact that _pg_dir=0 */

	if (page >= HIGH_MEMORY)
		printk("put_dirty_page: trying to put page %p at %p\n",page,address);
		
	if (mem_map[MAP_NR(page)] != 1)
		printk("mem_map disagrees with %p at %p\n",page,address);
		
	page_table = (unsigned long *) (tsk->tss.cr3 + ((address>>20) & 0xffc));
	
	if ((*page_table)&1)
		page_table = (unsigned long *) (0xfffff000 & *page_table);
	else {
		if (!(tmp=get_free_page()))
			return 0;
		*page_table = tmp | PAGE_ACCESSED |7;
		page_table = (unsigned long *) tmp;
	}
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
 */
void un_wp_page(unsigned long * table_entry)
{
	unsigned long old_page,new_page;

	
	/* 获取此页对应的物理地址，如果原页面不是保留并且其值为1表示没有共享，设置写标记
	 *
	 */
	old_page = 0xfffff000 & *table_entry;

	if (!(mem_map[MAP_NR(old_page)] & USED) && mem_map[MAP_NR(old_page)]==1) {
		*table_entry |= 2;
		invalidate();
		return;
	}
	if (!(new_page=get_free_page())) {
		printk("%s-%d\n", __func__, __LINE__);
		oom();
	}

	/*
	 *  如果原页面不是保留且不为1，表示已经被共享，给其值减一，设置新的页表
	 *
	 */
	if (!(mem_map[MAP_NR(old_page)] & USED))
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
		un_wp_page((unsigned long *)address);
	(((address>>10) & 0xffc) + (0xfffff000 &
	*((unsigned long *) ((address>>20) &0xffc)))));
#endif
	unsigned long* dir_base = (unsigned long *)current->tss.cr3;
	unsigned long* dir_item = dir_base + (address >> 22);
	un_wp_page((unsigned long *)
		(((address>>10) & 0xffc) + (0xfffff000 & *dir_item)));

#if 0
	unsigned long address;

	printk("%s address is %p start is %p pid %d\n", __func__, __address, current->start_code, current->pid);
	/*
	 * 页表地址
	 */
	address = current->tss.cr3 + ((__address>>20) & 0xffc);
	/*
	 * 页表地址
	 */
	address = address & 0xfffff000;
	/*
	 * 页在页表的地址
	 */
	address = address + (__address>>10) & 0xffc;
	/*
	 * 页的物理地址
	 */
	address = *(unsigned long *)(address);
	un_wp_page((unsigned long *)address);
#endif
}


void write_verify(unsigned long address)
{
	unsigned long page;

	page = *(unsigned long *) (current->tss.cr3 + ((address>>20) & 0xffc));
	if (!(page & PAGE_PRESENT))
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
		printk("%s-%d tmp is %p\n", __func__, __LINE__, tmp);
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
	struct task_struct *tsk = current;
	
	unsigned long from;
	unsigned long to;
	unsigned long from_page;
	unsigned long to_page;
	unsigned long phys_addr;

	from_page = p->tss.cr3 + ((address>>20) & 0xffc);
	to_page = tsk->tss.cr3 + ((address>>20) & 0xffc);
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
	if (phys_addr >= HIGH_MEMORY)
		return 0;
	if (mem_map[MAP_NR(phys_addr)] & USED)
		return 0;
	to = *(unsigned long *) to_page;
	if (!(to & 1)) {
		to = get_free_page();
		if (!to)
			return 0;
		*(unsigned long *) to_page = to | PAGE_ACCESSED | 7;
	}
	to &= 0xfffff000;
	to_page = to + ((address>>10) & 0xffc);
	if (1 & *(unsigned long *) to_page)
		panic("try_to_share: to_page already exists");
/* share them: write-protect */
	*(unsigned long *) from_page &= ~2;
	*(unsigned long *) to_page = *(unsigned long *) from_page;
	invalidate();
	phys_addr >>= PAGE_SHIFT;
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
	if (!(page = get_free_page())) {
		printk("%s-%d\n", __func__, __LINE__);
		oom();
	}
		
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
	printk("%s-%d\n", __func__, __LINE__);
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

void show_mem(void)
{
	int i,free = 0,total = 0,reserved = 0;
	int shared = 0;

	printk("Mem-info:\n");
	
	i = HIGH_MEMORY >> PAGE_SHIFT;
	while (i-- > 0) {
		total++;
		if (mem_map[i] & USED)
			reserved++;
		else if (!mem_map[i])
			free++;
		else
			shared += mem_map[i]-1;
	}
	printk("Buffer blocks:   %6d\n", nr_buffers);
	printk("Tatal pages:     %6d\n", total);
	printk("Free pages:      %6d\n", free);
	printk("Reserved pages:  %6d\n", reserved);
	printk("Shared pages:    %6d\n", shared);
}
