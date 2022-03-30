/*
 *  linux/kernel/printk.c
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 * When in kernel-mode, we cannot use printf, as fs is liable to
 * point to 'interesting' things. Make a printf with fs-saving, and
 * all is well.
 */
#include <stdarg.h>
#include <stddef.h>

#include <linux/kernel.h>
#include <linux/sched.h>

static char buf[1024];

extern int vsprintf(char * buf, const char * fmt, va_list args);

static inline int timestamp(const char *fmt, ...)
{
	va_list args;
	int i;
	
	va_start(args, fmt);
	i = vsprintf(buf, fmt, args);
	va_end(args);
	return i;
}

int printk(const char *fmt, ...)
{
	va_list args;
	int i;
	int j;
	
	va_start(args, fmt);
	j=timestamp("[%010d] ", jiffies);
	i=vsprintf(buf+j,fmt,args) + j;
	va_end(args);
	__asm__("push %%fs\n\t"
		"push %%ds\n\t"
		"pop %%fs\n\t"
		"pushl %0\n\t"
		"pushl $buf\n\t"
		"pushl $1\n\t"
		"call tty_write\n\t"
		"addl $8,%%esp\n\t"
		"popl %0\n\t"
		"pop %%fs"
		::"r" (i):"ax","cx","dx");
	return i;
}
