/*
 * IRET
 * 当使用IRET指令返回到相同保护级别的任务时，也就是当前的CS中的DPL和堆栈中CS的DPL相同时
 * IRET会从堆栈弹出代码段选择子及指令指针分别到CS与IP寄存器，
 * 并弹出标志寄存器内容到EFLAGS寄存器。
 * 
 * 当使用IRET指令返回到一个不同的保护级别时，也就是当前的CS中的DPL和堆栈中CS的DPL不同时
 * IRET不仅会从堆栈弹出以上内容，
 * 还会弹出堆栈段选择子及堆栈指针分别到SS与SP寄存器。
 *
 *
 * 我们知道当前系统使用了特权模式0，
 * movl %%esp,%%eax, 将当前的栈顶指针存放进入eax
 * pushl $0x17,17的二进制为00010111，表示权限为3，索引为2的段选择子，为数据段描述符，在局部描述符中
 * pushl %%eax, 将原有的esp入栈
 * pushfl, 将eflags 入栈
 * pushl $0x0f, 0xf的二进制为00001111, 表示权限为3，索引为1的段选择子 为代码段选择符，在局部描述符中
 * pushl $1f, 将如下标号1处的地址入栈
 * iret 由于栈中将要弹出的CS(0x0f)当前的CS(0x08)不同因此依次弹出IP,CS, EFLAGS, SS, SP
 *  根据构造好的栈进行转化到用户空间，此时代码开始从0地址处运行
 *  因为0地址处和3GB地址处的代码映射一致
 *  紧接着执行如下
 * movl $0x17,%%eax，设置数据段的描述符，此时系统继续运行下一跳指令
 * movw %%ax,%%ds,
 * ...
 * 设置ds，es，fs，gs为0x17
 *
 * 由此我们可以看出来，转到用户空间后使用了本地描述符表的代码段描述符和数据段描述符
 * 堆栈仍然使用了内核态的堆栈，但是权限不一样，task0的用户空间栈使用了内核的task_stack
 * 任务0是一个特殊进程，它的数据段和代码段直接映射到内核代码和数据空间
 * 即从物理地址0开始的640KB内存空间，其地址是内核代码使用的堆栈
 * 本地描述表在sched_init函数中已经进行了初始化
 *
 * 需要强调的是，如果下一次进行系统调用，会使用task_start结构中的最高地址作为栈的地址
 *
 */
#define move_to_user_mode() \
__asm__ ("movl %%esp,%%eax\n\t" \
	"pushl $0x17\n\t" \
	"pushl %%eax\n\t" \
	"pushfl\n\t" \
	"pushl $0x0f\n\t" \
	"pushl $1f\n\t" \
	"iret\n" \
	"1:\tmovl $0x17,%%eax\n\t" \
	"movw %%ax,%%ds\n\t" \
	"movw %%ax,%%es\n\t" \
	"movw %%ax,%%fs\n\t" \
	"movw %%ax,%%gs" \
	:::"ax")

/*
 * cli 禁止中断
 * sti 开启中断
 * 这两个指令必须在内核模式下进行
 *
 */
#define cli() __asm__ ("cli"::)
#define sti() __asm__ ("sti"::)


/*
 * local_irq_disable 将原中断状态记录在flags中，然后禁用中断
 * local_irq_restore 使用flags恢复中断状态
 *
 */
#define local_irq_disable(flags) __asm__("pushfl ; popl %0 ; cli":"=r" (flags))
#define local_irq_restore(flags) __asm__("pushl %0 ; popfl"::"r" (flags));

/*
 * 空指令
 */
#define nop() __asm__ ("nop"::)
/*
 * 中断返回指令
 */
#define iret() __asm__ ("iret"::)

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
 *
 *
 * 设置门描述符
 * +------------------------------------------------+-------+------------+--------------------+-------------+
 * |              Offset[31:16]                     | P[15] | DPL[14:13] |     TYPE[12:8]     |    Reserve  |
 * +------------------------------------------------+-------+------------+--------------------+-------------+
 * +------------------------------------------------+-------------------------------------------------------+
 * |        Segment Choice Description[31:16]       |                     Offset[15:0]                      |
 * +------------------------------------------------+-------------------------------------------------------+
 * 如上是一个门描述符
 *
 * 如下程序
 * 没有输出项，
 * 输入项为
 * "i" ((short) (0x8000+(dpl<<13)+(type<<8)))表示是一个立即数，编号为%0
 * "o" (*((char *) (gate_addr))) 表示gate_addr的地址
 * "o" (*(4+(char *) (gate_addr))), 表示gate_addr + 4的地址
 * "d" ((char *) (addr)), 表示将addr存放在edx
 * "a" (0x00080000) 表示将0x00080000存放在eax
 *
 * movw %%dx,%%ax 将addr的低16位存放在ax中, 此时根据初始化高16位为8二进制为1000，根据段选择子的定义，选择了代码段
 * movw %0,%%dx 将参数0也就是((short) (0x8000+(dpl<<13)+(type<<8)))存放在dx中，也就是edx的低16位，此时edx的高16位仍然是gate_addr的高16位
 * movl %%eax,%1，将eax的值存放在addr的低32位
 * movl %%edx,%2，将edx的值存放在addr的高32位
 * 通过这样的访问根据不同的参数来构造门
 *
 */
#define _set_gate(gate_addr,type,dpl,addr) \
__asm__ ("pushl %%eax\n\t" \
	"pushl %%edx\n\t" \
	"movw %%dx,%%ax\n\t" \
	"movw %0,%%dx\n\t" \
	"movl %%eax,%1\n\t" \
	"movl %%edx,%2\n\t" \
	"popl %%edx\n\t" \
	"popl %%eax\n\t" \
	: \
	: "i" ((short) (0x8000+(dpl<<13)+(type<<8))), \
	"o" (*((char *) (gate_addr))), \
	"o" (*(4+(char *) (gate_addr))), \
	"d" ((char *) (addr)),\
	"a" (0x00080000))

/*
 * 设置中断门，
 * type = 14 = 1110 表示中断门
 * DPL=0表示特权模式
 *
 */
#define set_intr_gate(n,addr) \
	_set_gate(&idt[n],14,0,addr)

/*
 * 设置陷阱门，
 * type = 14 = 1111 表示陷阱门
 * DPL=0表示特权模式
 *
 */
#define set_trap_gate(n,addr) \
	_set_gate(&idt[n],15,0,addr)
	
/*
 * 设置陷阱门，
 * type = 14 = 1111 表示陷阱门
 * DPL=3表示用户模式下可以访问
 * 系统调用
 *
 */
#define set_system_gate(n,addr) \
	_set_gate(&idt[n],15,3,addr)
	
/*
 * 下面的代码，这个我搜索了全部的代码，没有找到引用的地方
 * 暂时不分析
 *
 */
#if 0
#define _set_seg_desc(gate_addr,type,dpl,base,limit) {\
	*(gate_addr) = ((base) & 0xff000000) | \
		(((base) & 0x00ff0000)>>16) | \
		((limit) & 0xf0000) | \
		((dpl)<<13) | \
		(0x00408000) | \
		((type)<<8); \
	*((gate_addr)+1) = (((base) & 0x0000ffff)<<16) | \
		((limit) & 0x0ffff); }
#endif

/*
 * 补充一些基础知识 
 * 	段描述符
 *		数据段描述符
 *		代码段描述符
 *		系统段描述符（LDT段描述符，TSS段描述符）
 *	门描述符，门描述符见_set_gate函数的注释
 *		调用门描述符
 *		中断门描述符
 *		陷阱门描述符
 *		任务门描述符
 *	
 *	一个段描述符定义如下
 *	占用8个字节，从高地址向低地址排列
 *	+-----------------+-------+-------+--------------+-------+------------+-------+------------+-------------+
 *	|   ADDR[31:24]   | G[23] | L[21] | Limit[19:16] | P[15] | DPL[14:13] | S[12] | TYPE[11:8] | ADDR[23:16] |
 *	+-----------------+-------+-------+--------------+-------+------------+-------+------------+-------------+
 *	+------------------------------------------------+-------------------------------------------------------+
 *	|                    ADDR[15:0]                  |                     Limit[15:0]                       |
 *	+------------------------------------------------+-------------------------------------------------------+	
 *
 *	Limit表示段限长度，处理器会将两个字段组合成20位的值，并根据G来指定其实际含义
 *	G如果G=0，单位为1B, Limit的长度为1B-1MB，如果G=1，单位为4KB, Limit的长度为4KB-4GB
 *	L表示是32位还是64位，我们使用32位
 *	P表示这个段存在还是不存在
 *	DPL表示描述符的优先级
 *	S表示系统段描述符还是存储段描述符，为0表示系统段，为1表示存储段
 *	TYPE制定段或者门的类型
 *
 *
 *	设置任务段，局部段描述符，我们先看一个例子，这个例子是全局段描述符
 *	gdt:
 *	.word	0,0,0,0				# dummy
 *
 *	.word	0x07FF				# 8Mb - limit=2047 (2048*4096=8Mb)
 *	.word	0x0000				# base address=0
 *	.word	0x9A00				# code read/exec
 *	.word	0x00C0				# granularity=4096, 386
 *
 *	.word	0x07FF				# 8Mb - limit=2047 (2048*4096=8Mb)
 *	.word	0x0000				# base address=0
 *	.word	0x9200				# data read/write
 *	.word	0x00C0				# granularity=4096, 386
 *   n 表示全局描述符n所对应的地址
 *   addr 表示状态段/局部段所在的内存地址
 *   type 类型
 *   由内联汇编的知识我们知道如下信息
 *	1 没有输出项
 *	2 有输入项
 *	3 没有影响的寄存器列表
 *	
 *	以上为一个段描述符结构，右下为低位，左上为高位
 *	参数如下
 *	%0 addr存放在eax寄存器中
 *	%1 描述符n的起始地址
 *	%2 描述符n的起始地址+2
 *	%3 描述符n的起始地址+4
 *	%4 描述符n的起始地址+5
 *	%5 描述符n的起始地址+6
 *	%6 描述符n的地址地址+7
 *
 *	movw $104,%1，将立即数104存放在描述符的低16位，如果是一个LDT其长度为8个字节，TSS段的长度为104个字节，这里使用最大的长度作为限制
 *	movw %%ax,%2，eax存放的是addr的地址，因此这个意思是将addr的低16位存放到描述符的bit[16:31]
 *	rorl $16,%%eax, eax右移16位，也就是addr的高16位存放在ax中，
 *	movb %%al,%3，将addr的bit[16:23]存放的描述符的bit[32:39]
 *	movb $" type ",%4 将type存放描述符到bit[40:47]中
 *	movb $0x00,%5, 描述符bit[48:55]设置为0
 *	movb %%ah,%6, 将addr的bit[24:31]存放到描述付的bit[56:63]中
 */
#define _set_tssldt_desc(n,addr,type) \
__asm__ ("pushl %%eax\n\t" \
	"movw $104,%1\n\t" \
	"movw %%ax,%2\n\t" \
	"rorl $16,%%eax\n\t" \
	"movb %%al,%3\n\t" \
	"movb $" type ",%4\n\t" \
	"movb $0x00,%5\n\t" \
	"movb %%ah,%6\n\t" \
	"rorl $16,%%eax\n\t" \
	"popl %%eax\n\t" \
	::"a" (addr+0xc0000000), "m" (*(n)), "m" (*(n+2)), "m" (*(n+4)), \
	 "m" (*(n+5)), "m" (*(n+6)), "m" (*(n+7)) \
	)

/*
 * 在全局描述符n地址处，设置一个TSS段描述符或者LDT描述符，TSS或LDT的地址为addr
 */
#define set_tss_desc(n,addr) _set_tssldt_desc(((char *)(n)), ((int)(addr)), "0x89")
#define set_ldt_desc(n,addr) _set_tssldt_desc(((char *)(n)), ((int)(addr)), "0x82")

