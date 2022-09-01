/*
 *  linux/kernel/fork.c
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 *  'fork.c' contains the help-routines for the 'fork' system call
 * (see also system_call.s), and some misc functions ('verify_area').
 * Fork is rather simple, once you get the hang of it, but the memory
 * management can be a bitch. See 'mm/mm.c': 'copy_page_tables()'
 */
#include <string.h>
#include <errno.h>

#include <linux/sched.h>
#include <linux/kernel.h>
#include <asm/segment.h>
#include <asm/system.h>

extern void write_verify(unsigned long address);

long last_pid=0;

void verify_area(void * addr,int size)
{
	unsigned long start;

	start = (unsigned long) addr;
	size += start & 0xfff;
	start &= 0xfffff000;
	start += get_base(current->ldt[2]);
	while (size>0) {
		size -= 4096;
		write_verify(start);
		start += 4096;
	}
}

int copy_mem(int nr,struct task_struct * p)
{
	unsigned long old_data_base,new_data_base,data_limit;
	unsigned long old_code_base,new_code_base,code_limit;

	code_limit = get_limit(0x0f);
	data_limit = get_limit(0x17);
	old_code_base = get_base(current->ldt[1]);
	old_data_base = get_base(current->ldt[2]);
	if (old_data_base != old_code_base) {
		printk("ldt[0]: %08x %08x\n",current->ldt[0].a,current->ldt[0].b);
		printk("ldt[1]: %08x %08x\n",current->ldt[1].a,current->ldt[1].b);
		printk("ldt[2]: %08x %08x\n",current->ldt[2].a,current->ldt[2].b);
		panic("We don't support separate I&D");
	}
	if (data_limit < code_limit)
		panic("Bad data_limit");
	new_data_base = old_data_base;
	new_code_base = old_code_base;
	p->start_code = new_code_base;
	set_base(p->ldt[1],new_code_base);
	set_base(p->ldt[2],new_data_base);
	return copy_page_tables(p);
}


extern void first_return_from_kernel(void);

/*
 *  Ok, this is the main fork-routine. It copies the system process
 * information (task[nr]) and sets up the necessary registers. It
 * also copies the data segment in it's entirety.
 */
int copy_process(int nr,long ebp,long edi,long esi,long gs,long none,
		long ebx,long ecx,long edx,
		long fs,long es,long ds,
		long eip,long cs,long eflags,long esp,long ss)
{
	struct task_struct *p;
	int i;
	struct file *f;
	long *stack_top = NULL;

	p = (struct task_struct *) get_free_page();
	if (!p)
		return -EAGAIN;
	task[nr] = p;

	// NOTE!: the following statement now work with gcc 4.3.2 now, and you
	// must compile _THIS_ memcpy without no -O of gcc.#ifndef GCC4_3
	/* 如果使用了 memcpy 函数，因为task_struct是个联合体会拷贝堆栈数据
	 * 此处的语法不会拷贝堆栈数据
	 *
	 */
	*p = *current;	/* NOTE! this doesn't copy the supervisor stack */
	p->state = TASK_UNINTERRUPTIBLE;
	p->pid = last_pid;
	p->father = current->pid;
	p->counter = p->priority;
	p->signal = 0;
	p->alarm = 0;
	p->leader = 0;		/* process leadership doesn't inherit */
	p->utime = p->stime = 0;
	p->cutime = p->cstime = 0;
	p->start_time = jiffies;

#ifdef CONFIG_SWITCH_TSS
	p->tss.back_link = 0;
	p->tss.esp0 = PAGE_SIZE + (long) p;
	p->tss.ss0 = 0x10;
	p->tss.eip = eip;
	p->tss.eflags = eflags;
	p->tss.eax = 0; 		/*为什么进程返回0的原因*/
	p->tss.ecx = ecx;
	p->tss.edx = edx;
	p->tss.ebx = ebx;
	p->tss.esp = esp;
	p->tss.ebp = ebp;
	p->tss.esi = esi;
	p->tss.edi = edi;
	p->tss.es = es & 0xffff;
	p->tss.cs = cs & 0xffff;
	p->tss.ss = ss & 0xffff;
	p->tss.ds = ds & 0xffff;
	p->tss.fs = fs & 0xffff;
	p->tss.gs = gs & 0xffff;
	p->tss.ldt = _LDT(nr);
	p->tss.trace_bitmap = 0x80000000;
#else	
	stack_top = (long *)(PAGE_SIZE + (long)p);
	*(--stack_top) = ss & 0xffff;
	*(--stack_top) = esp;
	*(--stack_top) = eflags;
	*(--stack_top) = cs & 0xffff;
	*(--stack_top) = eip;
	*(--stack_top) = (long)first_return_from_kernel;
	*(--stack_top) = ebp;
	*(--stack_top) = edx;
	*(--stack_top) = ecx;
	*(--stack_top) = ebx;
	*(--stack_top) = 0; 	/*为什么进程返回0的原因，这里是EAX寄存器的内容*/
	*(--stack_top) = edi;
	*(--stack_top) = esi;
	*(--stack_top) = eflags;
	*(--stack_top) = gs & 0xffff;
	*(--stack_top) = fs & 0xffff; 
	*(--stack_top) = es & 0xffff; 
	*(--stack_top) = ds & 0xffff; 
	p->stack_top = (long)stack_top;
#endif

	if (last_task_used_math == current)
		__asm__("clts ; fnsave %0"::"m" (p->tss.i387));
	if (copy_mem(nr,p)) {
		task[nr] = NULL;
		free_page((long) p);
		return -EAGAIN;
	}
	for (i=0; i<NR_OPEN;i++)
		if ((f=p->filp[i]))
			f->f_count++;
	if (current->pwd)
		current->pwd->i_count++;
	if (current->root)
		current->root->i_count++;
	if (current->executable)
		current->executable->i_count++;

#ifdef CONFIG_SWITCH_TSS
	set_tss_desc(gdt+(nr<<1)+FIRST_TSS_ENTRY,&(p->tss));
	set_ldt_desc(gdt+(nr<<1)+FIRST_LDT_ENTRY,&(p->ldt));
#else
	set_ldt_desc(gdt+(nr<<1)+FIRST_LDT_ENTRY,&(p->ldt));
#endif

	p->state = TASK_RUNNING;	/* do this last, just in case */
	return last_pid;
}

int find_empty_process(void)
{
	int i;

repeat:
	/* 如果last_pid满了，则从重新开始编号
	 *
	 */
	if ((++last_pid) < 0) 
		last_pid=1;

	/* 查找last_pid是否已经被占用，如果是则++last_pid继续尝试
	 *
	 */
	for(i=0; i<NR_TASKS; i++)
		if (task[i] && task[i]->pid == last_pid) 
			goto repeat;

	/* 在task数组中寻找一个空的task并返回其数组下标
	 *
	 */
	for(i=1; i<NR_TASKS; i++)
		if (!task[i])
			return i;
			
	return -EAGAIN;
}

