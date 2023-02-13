#ifndef _SCHED_H
#define _SCHED_H

/*
 * NR_TASKS 当前内核只支持64个进程
 * HZ 定义系统时钟的滴答数，一秒内100个滴答，每个滴答10ms
 */
#define NR_TASKS 	64
#define HZ 			100

/* 
 * FIRST_TASK 任务0，第一个进程，通过move_to_user手工创建
 * LAST_TASK 最后一个任务
 */
#define FIRST_TASK 	task[0]
#define LAST_TASK 	task[NR_TASKS-1]

#include <linux/head.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <signal.h>

#if (NR_OPEN > 32)
#error "Currently the close-on-exec-flags are in one word, max 32 files/proc"
#endif

/*
 * 定义进程状态
 * TASK_RUNNING 进程处于运行状态，可以被调度
 * TASK_ZOMBIE 僵尸进程，已经掉用了exit，但是没有被父进程wait_pid
 */
#define TASK_RUNNING			0
#define TASK_INTERRUPTIBLE		1
#define TASK_UNINTERRUPTIBLE	2
#define TASK_ZOMBIE				3
#define TASK_STOPPED			4

#ifndef NULL
#define NULL ((void *) 0)
#endif

extern int copy_page_tables(struct task_struct *tsk);
extern int free_page_tables(struct task_struct *tsk);
extern void clear_page_tables(struct task_struct * tsk);
extern void show_mem(void);

extern void sched_init(void);
extern void schedule(void);
extern void trap_init(void);
#ifndef PANIC
void panic(const char * str);
#endif
extern int tty_write(unsigned minor,char * buf,int count);

typedef int (*fn_ptr)();

struct i387_struct {
	long	cwd;
	long	swd;
	long	twd;
	long	fip;
	long	fcs;
	long	foo;
	long	fos;
	long	st_space[20];	/* 8*10 bytes for each FP-reg = 80 bytes */
};

struct tss_struct {
	long	back_link;	/* 16 high bits zero */
	long	esp0;
	long	ss0;		/* 16 high bits zero */
	long	esp1;
	long	ss1;		/* 16 high bits zero */
	long	esp2;
	long	ss2;		/* 16 high bits zero */
	long	cr3;
	long	eip;
	long	eflags;
	long	eax,ecx,edx,ebx;
	long	esp;
	long	ebp;
	long	esi;
	long	edi;
	long	es;		/* 16 high bits zero */
	long	cs;		/* 16 high bits zero */
	long	ss;		/* 16 high bits zero */
	long	ds;		/* 16 high bits zero */
	long	fs;		/* 16 high bits zero */
	long	gs;		/* 16 high bits zero */
	long	ldt;		/* 16 high bits zero */
	long	trace_bitmap;	/* bits: trace 0, bitmap 16-31 */
	struct i387_struct i387;
};

struct task_struct {
/* these are hardcoded - don't touch */
	long state;	/* -1 unrunnable, 0 runnable, >0 stopped */
	long counter;
	long priority;
	long signal;
	struct sigaction sigaction[32];
	long blocked;	/* bitmap of masked signals */
	long stack_top;
/* various fields */
	int exit_code;
	unsigned long start_code,end_code,end_data,brk,start_stack;
	long pid,father,pgrp,session,leader;
	unsigned short uid,euid,suid;
	unsigned short gid,egid,sgid;
	long alarm;
	long utime,stime,cutime,cstime,start_time;
	unsigned short used_math;
/* file system info */
	int tty;		/* -1 if no tty, so it must be signed */
	unsigned short umask;
	
	struct m_inode * pwd;
	struct m_inode * root;
	struct m_inode * executable;
	unsigned long close_on_exec;
	struct file * filp[NR_OPEN];
/* ldt for this task 0 - zero 1 - cs 2 - ds&ss */
	struct desc_struct ldt[3];
/* tss for this task */
	struct tss_struct tss;
};

/*
 *  INIT_TASK is used to set up the first task table, touch at
 * your own risk!. Base=0, limit=0x9ffff (=640kB)
 */
#define INIT_TASK \
/* state etc */	{ 0,15,15, \
/* signals */	0,{{},},0, PAGE_SIZE+(long)&init_task, \
/* ec,brk... */	0,0,0,0,0,0, \
/* pid etc.. */	0,-1,0,0,0, \
/* uid etc */	0,0,0,0,0,0, \
/* alarm */	0,0,0,0,0,0,\
/* math */	0, \
/* fs info */	-1,0022,NULL,NULL,NULL,0, \
/* filp */	{NULL,}, \
	{ \
		{0,0}, \
/* ldt */		{0x9f,0xc0fa00}, \
		{0x9f,0xc0f200} \
	}, \
/*tss*/	{0,PAGE_SIZE+(long)&init_task,0x10,0,0,0,0,(long)&swapper_pg_dir,\
	 0,0,0,0,0,0,0,0, \
	 0,0,0x17,0x17,0x17,0x17,0x17,0x17, \
	 _LDT(0),0x80000000, \
		{} \
	}, \
}

extern struct task_struct *task[NR_TASKS];
extern struct task_struct *last_task_used_math;
extern struct task_struct *current;
extern long volatile jiffies;
extern long startup_time;

#define CURRENT_TIME (startup_time+jiffies/HZ)

extern void add_timer(long jiffies, void (*fn)(void));
extern void sleep_on(struct task_struct ** p);
extern void interruptible_sleep_on(struct task_struct ** p);
extern void wake_up(struct task_struct ** p);

/*
 * Entry into gdt where to find first TSS. 0-nul, 1-cs, 2-ds, 3-syscall
 * 4-TSS0, 5-LDT0, 6-TSS1 etc ...
 */
#define FIRST_TSS_ENTRY 4
#define FIRST_LDT_ENTRY (FIRST_TSS_ENTRY+1)
/* _TSS(n) 表示任务n的TSS段描述子，其地址指向一个在GDT中的TSS段，每个任务都有自己TSS段
 * _LDT(n) 表示任务n的LDT段描述子，其地址指向一个在GDT中的LDT段，每个任务都有自己LDT段
 * 如n为0
 * _TSS(0) = ((unsigned long)0 << 4) + (4 << 3)表示为二进制为0b'00100 000，其index为4
 * _LDT(0) = ((unsigned long)0 << 4) + (5 << 3)表示为二进制为0b'00101 000，其index为5
 * _TSS(1) = ((unsigned long)1 << 4) + (4 << 3)表示为二进制为0b'00110 000，其index为6
 * _LDT(1) = ((unsigned long)1 << 4) + (5 << 3)表示为二进制为0b'00111 000，其index为7
 *
 * 
 */
#define _TSS(n) ((((unsigned long) n)<<4)+(FIRST_TSS_ENTRY<<3))
#define _LDT(n) ((((unsigned long) n)<<4)+(FIRST_LDT_ENTRY<<3))
/*
 * 加载LDT和TSS
 */
#define ltr(n) 	__asm__("ltr %%ax"::"a" (_TSS(n)))
#define lldt(n)	__asm__("lldt %%ax"::"a" (_LDT(n)))

/*
 * gcc的内联汇编函数格式
 * asm("assemble": output : input : 受影响的寄存器)
 * m 表示内存操作数
 * i 表示立即操作数
 * a 使用寄存器eax
 * b 使用寄存器ebx
 * c 使用寄存器ecx
 * d 使用寄存器edx
 * S 使用esi
 * D 使用edi
 * q 动态分配字节可寻址的寄存器
 * r 使用任意动态分配的寄存器
 * A 使用eax和edx联合的64位寄存器
 * o 内存操作数，但前提是此地址是可偏移的，向地址添加一个小偏移会给出一个有效地址
 * 
 * 当前进程的任务号，存放在n中
 * 这个代码在使用内核堆栈进行任务切换时有问题
 * 
 * str是将当前TSS所在的有效地址（有就是偏移地址）放到ax寄存器中，
 * 然后将eax-FIRST_TSS_ENTRY<<3也就是eax-32
 * 为什么是32呢，因为进程占用是从第4个描述符开始的，每个描述符占用8个字节，一共32个字节
 * 最后再将eax向右移动4位，也就是除以16，
 * 为什么除16，是因为每个进程占用两个描述符，分别是TSS和LDT，一共16个字节
 * 看到这应该明白为什么不能在内核栈进程切换的任务中使用了吧
 * 
 */
#define str(n) \
__asm__("str %%ax\n\t" \
	"subl %2,%%eax\n\t" \
	"shrl $4,%%eax" \
	:"=a" (n) \
	:"a" (0),"i" (FIRST_TSS_ENTRY<<3))

/*
 * switch_to(n) should switch tasks to task nr n, first
 * checking that n isn't the current task, in which case it does nothing.
 * This also clears the TS-flag if the task we switched to has used
 * tha math co-processor latest.
 */

#define switch_to(n) {\
struct {long a,b;} __tmp; \
__asm__("cmpl %%ecx,current\n\t" \
	"je 1f\n\t" \
	"movw %%dx,%1\n\t" \
	"xchgl %%ecx,current\n\t" \
	"ljmp *%0\n\t" \
	"cmpl %%ecx,last_task_used_math\n\t" \
	"jne 1f\n\t" \
	"clts\n" \
	"1:" \
	::"m" (*&__tmp.a),"m" (*&__tmp.b), \
	"d" (_TSS(n)),"c" ((long) task[n])); \
}

/*
 * PAGE_ALIGN搜索全部代码，没有用到 
 */
#if 0
#define PAGE_ALIGN(n) (((n)+0xfff)&0xfffff000)
#endif

/*
 * 设置位于地址addr处描述符的基地址
 */
#define _set_base(addr,base)  \
__asm__ ("pushl %%edx\n\t" \
	"movw %%dx,%0\n\t" \
	"rorl $16,%%edx\n\t" \
	"movb %%dl,%1\n\t" \
	"movb %%dh,%2\n\t" \
	"popl %%edx" \
	::"m" (*((addr)+2)), \
	 "m" (*((addr)+4)), \
	 "m" (*((addr)+7)), \
	 "d" (base) \
	)

/*
 * 设置位于地址addr处描述符的长度
 */
#define _set_limit(addr,limit) \
__asm__ ("pushl %%edx\n\t" \
	"movw %%dx,%0\n\t" \
	"rorl $16,%%edx\n\t" \
	"movb %1,%%dh\n\t" \
	"andb $0xf0,%%dh\n\t" \
	"orb %%dh,%%dl\n\t" \
	"movb %%dl,%1\n\t" \
	"popl %%edx" \
	::"m" (*(addr)), \
	 "m" (*((addr)+6)), \
	 "d" (limit) \
	)

#define set_base(ldt,base)		_set_base( ((char *)&(ldt)) , (base) )
#define set_limit(ldt,limit)	_set_limit( ((char *)&(ldt)) , (limit-1)>>12 )

#if 0
#define _get_base(addr) ({\
unsigned long __base; \
__asm__("movb %3,%%dh\n\t" \
	"movb %2,%%dl\n\t" \
	"shll $16,%%edx\n\t" \
	"movw %1,%%dx" \
	:"=d" (__base) \
	:"m" (*((addr)+2)), \
	 "m" (*((addr)+4)), \
	 "m" (*((addr)+7)) \
        :"memory"); \
__base;})
#endif

static inline unsigned long _get_base(char * addr)
{
	unsigned long __base;
	__asm__("movb %3,%%dh\n\t"
			"movb %2,%%dl\n\t"
			"shll $16,%%edx\n\t"
			"movw %1,%%dx"
			:"=&d" (__base)
			:"m" (*((addr)+2)),
			"m" (*((addr)+4)),
			"m" (*((addr)+7)));
		return __base;
}

#if 0
#define _get_limit(segment) ({ \
unsigned long __limit; \
__asm__("lsll %1,%0\n\tincl %0":"=r" (__limit):"r" (segment)); \
__limit;})
#endif

static inline unsigned long _get_limit(unsigned long segment)
{
	unsigned long __limit;
	__asm__("lsll %1,%0\n\t"
			"incl %0"
			:"=&r" (__limit)
			:"r" (segment));	
	return __limit;
}

#define get_base(ldt)		_get_base( ((char *)&(ldt)) )
#define get_limit(segment) 	_get_limit(segment)

#endif
