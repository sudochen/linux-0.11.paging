#ifndef _MM_H
#define _MM_H

extern unsigned long get_free_page(void);
extern unsigned long put_page(unsigned long page,unsigned long address);
extern unsigned long put_dirty_page(unsigned long page,unsigned long address);
extern void free_page(unsigned long addr);
#ifndef PAGE_SIZE
#define PAGE_SIZE 			4096
#endif
#endif

