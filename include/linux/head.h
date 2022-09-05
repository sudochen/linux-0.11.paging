#ifndef _HEAD_H
#define _HEAD_H

typedef struct desc_struct {
	unsigned long a,b;
} desc_table[256];

/*
 * 页表目录
 * 这些都在heas模块中定义
 */
extern unsigned long swapper_pg_dir[1024];
extern desc_table idt,gdt;

/*
 * 定义的全局符描述表
 * 在head模块中预定义了四个段描述符
 * 第一个是NULL
 * 第二个是代码段
 * 第三个是数据段
 * 第四个是保留段
 * 
 */
#define GDT_NUL 0
#define GDT_CODE 1
#define GDT_DATA 2
#define GDT_TMP 3

/*
 * 每个进程使用了第三个局部描述符
 * 第一个是NULL
 * 第二个是代码段
 * 第三个是数据段
 * 
 */
#define LDT_NUL 0
#define LDT_CODE 1
#define LDT_DATA 2

#endif
