/*
 *  linux/init/main.c
 *
 *  (C) 1991  Linus Torvalds
 */

#define __LIBRARY__
#include <unistd.h>
#include <time.h>


/*
 * we need this inline - forking from kernel space will result
 * in NO COPY ON WRITE (!!!), until an execve is executed. This
 * is no problem, but for the stack. This is handled by not letting
 * main() use the stack at all after fork(). Thus, no function
 * calls - which means inline code for fork too, as otherwise we
 * would use the stack upon exit from 'fork()'.
 *
 * Actually only pause and fork are needed inline, so that there
 * won't be any messing with the stack from main(), but we define
 * some others too.
 */
static inline int fork(void) __attribute__((always_inline));
static inline int pause(void) __attribute__((always_inline));
static inline _syscall0(int,fork)
static inline _syscall0(int,pause)
static inline _syscall1(int,setup,void *,BIOS)
static inline _syscall0(int,sync)

#include <linux/tty.h>
#include <linux/sched.h>
#include <linux/head.h>
#include <asm/system.h>
#include <asm/io.h>

#include <stddef.h>
#include <stdarg.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>

#include <linux/fs.h>

static char printbuf[1024];

extern int vsprintf();
extern void init(void);
extern void blk_dev_init(void);
extern void chr_dev_init(void);
extern void hd_init(void);
extern void floppy_init(void);
extern void mem_init(long start, long end);
extern long get_total_pages(void);
extern long rd_init(long mem_start, int length);
extern long kernel_mktime(struct tm * tm);
extern long startup_time;

/*
 * This is set up by the setup-routine at boot-time
 */
#define EXT_MEM_K (*(unsigned short *)0x90002)
#define DRIVE_INFO (*(struct drive_info *)0x90080)
#define ORIG_ROOT_DEV (*(unsigned short *)0x901FC)

/*
 * Yeah, yeah, it's ugly, but I cannot find how to do this correctly
 * and this seems to work. I anybody has more info on the real-time
 * clock I'd be interested. Most of this was trial and error, and some
 * bios-listing reading. Urghh.
 */

#define CMOS_READ(addr) ({ \
outb_p(0x80|addr,0x70); \
inb_p(0x71); \
})

#define BCD_TO_BIN(val) ((val)=((val)&15) + ((val)>>4)*10)

static void time_init(void)
{
	struct tm time;

	do {
		time.tm_sec = CMOS_READ(0);
		time.tm_min = CMOS_READ(2);
		time.tm_hour = CMOS_READ(4);
		time.tm_mday = CMOS_READ(7);
		time.tm_mon = CMOS_READ(8);
		time.tm_year = CMOS_READ(9);
	} while (time.tm_sec != CMOS_READ(0));
	BCD_TO_BIN(time.tm_sec);
	BCD_TO_BIN(time.tm_min);
	BCD_TO_BIN(time.tm_hour);
	BCD_TO_BIN(time.tm_mday);
	BCD_TO_BIN(time.tm_mon);
	BCD_TO_BIN(time.tm_year);
	time.tm_mon--;
	startup_time = kernel_mktime(&time);
}

static long memory_end = 0;
static long buffer_memory_end = 0;
static long main_memory_start = 0;

extern int printk(const char * fmt, ...);

struct drive_info { char dummy[32]; } drive_info;

void main(int __a, int __b, int __c)		/* This really IS void, no error here. */
{			/* The startup routine assumes (well, ...) this */
/*
 * Interrupts are still disabled. Do necessary setups, then
 * enable them
 */

/*******************************************************************************
	ORIG_ROOT_DEV为0x901FC，由于在前面的程序中回见bootsect程序拷贝到0x90000处，
	bootsect的508地址存放的时根设备的编号301，0x1fc=508，所有此处存放的是根设备
	的设备号;
	DRIVE_INFO存放的是第一个硬盘信息
	EXT_MEM_K系统从1MB开始的扩展内存数值(KB)，复习一下实模式下最多访问1MB空间
	memory_end & 0xffff000进行内存对齐，我们看到后面有3个0，一共12位，因此我们
	知道内核要求页对齐即4KB对其

	我们根据代码看到如果硬盘大于16MB，则内存为16MB,
	如果内存大于12MB, buffer_memory_end为4MB
	如果内存大于6MB，buffer_memory_end为2MB
	否则buffer_memory_end为1M
	memory_end最多16MB
*******************************************************************************/
 	ROOT_DEV = ORIG_ROOT_DEV;
 	drive_info = DRIVE_INFO;
	memory_end = (1<<20) + (EXT_MEM_K<<10);
	memory_end &= 0xfffff000;
	if (memory_end > 16*1024*1024)
		memory_end = 16*1024*1024;
	if (memory_end > 12*1024*1024) 
		buffer_memory_end = 4*1024*1024;
	else if (memory_end > 6*1024*1024)
		buffer_memory_end = 2*1024*1024;
	else
		buffer_memory_end = 1*1024*1024;
	main_memory_start = buffer_memory_end;
#ifdef RAMDISK_SIZE
	main_memory_start += rd_init(main_memory_start, RAMDISK_SIZE*1024);
#endif
/*******************************************************************************
	创建mem_map数组，并将main_memory_start到memory_end之间的内存
	以4KB为一组，进行创建
*******************************************************************************/
	mem_init(main_memory_start,memory_end);
	trap_init();
	blk_dev_init();
	chr_dev_init();
	tty_init();
	printk("params a %d b %d c %d\n", __a, __b, __c);
	printk("mem_start is %dMB\n", main_memory_start/(1024*1024));
	printk("men_end is %dMB\n", memory_end/(1024*1024));
	printk("system has %d pages\n", get_total_pages());
#ifdef RAMDISK_SIZE
	printk("ramdisk size is %dMB", RAMDISK_SIZE/1024);
#endif
	printk("kernel time init\n");
	time_init();
	printk("kernel sched init\n");
	sched_init();
	printk("kernel buffer init\n");
	buffer_init(buffer_memory_end);
	printk("kernel hd init\n");
	hd_init();
	printk("kernel fp init\n");
	floppy_init();
	printk("kernel move to user\n");
	/*
	 * sti允许中断
	 *
	 */	
	sti();
	/*
	 执行完move_to_user_mode函数后，程序会手工切到task0执行，测代码段和数据段
	 和内核一致，堆栈也使用了内核堆栈，运行权限变成3, 因此我们可以看出Linux中
	 使用0和3权限
	 具体可以查看move_to_user_mode分析
 	*/
	move_to_user_mode();

	/*	
     fork程序是一个系统调用，使用_syscall0进程展开生成，0表示没有参数
     
     #define _syscall0(type,name) \
     	type name(void) \
     	{ \
     	long __res; \
     	__asm__ volatile ("int $0x80" \
     	 : "=a" (__res) \
     	 : "0" (__NR_##name)); \
     	if (__res >= 0) \
     	 return (type) __res; \
     	errno = -__res; \
     	return -1; \
     
     根据前面的定义static inline _syscall0(int,fork) 展开
     
     int fork() {
     	register eax __ret;
     	eax= __NR_fork;
     	int 0x80
     	if (eax >= 0)
     		return int __res;
     	error = - __res
     	return -1
     }
     INT 0x80是软中断函数，其调用流程为:
     CPU通过中断向量0x80找到对应的描述符，此描述符包含了段选择子和偏移地址已经DPL
     CPU检查当前的DPL是否小于描述符的DPL
     CPU会从当前TSS段中找到中断处理程序的栈选择子和栈指针作为新的栈地址(tss.ss0, tss.esp0)
     如果DPL发生变化则将当前的SS, ESP, EFLAGS, CS, EIP压入新的栈中
     如果DPL没有发生变化则将EFLAGS, CS, EIP压如新的栈中
     CPU从中断描述符中取CS:EIP作为新的运行地址
     
     fork执行完毕后是进程1，此时进程0和进程1使用相同的用户空间栈，
     为了进程之间互不影响因此
     暂时不使用栈，函数以内联的形式进行调用，试想一下
     如果以函数调用的形成当fork时发生切换，系统将当前的SS, SP压栈，
     此时pause进程也将SS, SP压栈
     pause的堆栈数据会覆盖fork的堆栈数据，使fork函数返回到pause函数这里，
     从而init不能执行
     同理，也可能导致pause进程执行到init程序里
	*/
	if (!fork()) {		/* we count on this going ok */
		init();
	}
/*
 *   NOTE!!   For any other task 'pause()' would mean we have to get a
 * signal to awaken, but task0 is the sole exception (see 'schedule()')
 * as task 0 gets activated at every idle moment (when no other tasks
 * can run). For task0 'pause()' just means we go check if some other
 * task can run, and if not we return here.
 */
	for(;;) pause();
}

static int printf(const char *fmt, ...)
{
	va_list args;
	int i;

	va_start(args, fmt);
	write(1,printbuf,i=vsprintf(printbuf, fmt, args));
	va_end(args);
	return i;
}

static char * argv_rc[] = { "/bin/sh", NULL };
static char * envp_rc[] = { "HOME=/", NULL };

static char * argv[] = { "-/bin/sh",NULL };
static char * envp[] = { "HOME=/usr/root", NULL };

void init(void)
{
	int pid,i;

	setup((void *) &drive_info);
	(void) open("/dev/tty0",O_RDWR,0);
	(void) dup(0);
	(void) dup(0);
	printf("%d buffers = %d bytes buffer space\n\r",NR_BUFFERS,
		NR_BUFFERS*BLOCK_SIZE);
	printf("Free mem: %d bytes\n\r",memory_end-main_memory_start);
	if (!(pid=fork())) {
		close(0);
		if (open("/etc/rc",O_RDONLY,0))
			_exit(1);
		execve("/bin/sh",argv_rc,envp_rc);
		_exit(2);
	}
	if (pid>0)
		while (pid != wait(&i))
			/* nothing */;
	while (1) {
		if ((pid=fork())<0) {
			printf("Fork failed in init\r\n");
			continue;
		}
		if (!pid) {
			close(0);close(1);close(2);
			setsid();
			(void) open("/dev/tty0",O_RDWR,0);
			(void) dup(0);
			(void) dup(0);
			_exit(execve("/bin/sh",argv,envp));
		}
		while (1)
			if (pid == wait(&i))
				break;
		printf("\n\rchild %d died with code %04x\n\r",pid,i);
		sync();
	}
	_exit(0);	/* NOTE! _exit, not exit() */
}
