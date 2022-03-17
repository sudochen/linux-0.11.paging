/*
 *  linux/boot/head.s
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 *  head.s contains the 32-bit startup code.
 *
 * NOTE!!! Startup happens at absolute address 0x00000000, which is also where
 * the page directory will exist. The startup code will be overwritten by
 * the page directory.
 *
 * head被链接至system的开始位置，地址为0
 *
 */
.text
.globl idt,gdt,pg_dir,tmp_floppy_area
pg_dir:
.globl startup_32

/* 
 * 根据setup模块我们安装了两个段描述符，分别是代码段和数据段，共8MB的空间
 * 0x10的二进制为10000, 我们可以看出DPL为0，T为0表示此描述符在全局描述符中的第二个描述符，权限为0最高权限
 * 设置数据段为0x10，全局描述第2个段
 */
startup_32:
	movl $0x10,%eax
	mov %ax,%ds
	mov %ax,%es
	mov %ax,%fs
	mov %ax,%gs
	/*
	 * 设置栈顶指针
	 *
	 * 设置栈顶指针
	 *
	 * stack_start定义在kernel/sched.c中
	 * long user_stack[PAGE_SIZE >> 2];
	 * struct {
	 * 　　long *a;
	 * 　　short b;
	 * } stack_start { &user_stack, 0x10}
	 * lss设置了栈顶指针和其栈的段地址，0x10表示数据段，地址为stack_start定义起大小为PAGE_SIZE>>2 = 1024字节
	 *
	 */
	lss stack_start,%esp
	call setup_idt
	call setup_gdt

	/*
	 * 重新设置数据段，因为在上面的程序中修改gdt，因此需要重新加载，
	 */
	movl $0x10,%eax		    # reload all the segment registers
	mov %ax,%ds		        # after changing gdt. CS was already
	mov %ax,%es		        # reloaded in 'setup_gdt'
	mov %ax,%fs
	mov %ax,%gs
	lss stack_start,%esp
	/*
	 * xorl为异或运算，相同为0，不同为1，此处的意思是清零寄存器
	 */
	xorl %eax,%eax
1:	incl %eax		        # check that A20 really IS enabled
	movl %eax,0x000000	    # loop forever if it isn't
	cmpl %eax,0x100000
	je 1b

/*
 * NOTE! 486 should set bit 16, to check for write-protect in supervisor
 * mode. Then it would be unnecessary with the "verify_area()"-calls.
 * 486 users probably want to set the NE (#5) bit also, so as to use
 * int 16 for math errors.
 */
	movl %cr0,%eax		    # check math chip
	andl $0x80000011,%eax	# Save PG,PE,ET
/* "orl $0x10020,%eax" here for 486 might be good */
	orl $2,%eax		        # set MP
	movl %eax,%cr0
	call check_x87
	jmp after_page_tables

/*
 * We depend on ET to be correct. This checks for 287/387.
 */
check_x87:
	fninit
	fstsw %ax
	cmpb $0,%al
	je 1f			        /* no coprocessor: have to set bits */
	movl %cr0,%eax
	xorl $6,%eax		    /* reset MP, set EM */
	movl %eax,%cr0
	ret
.align 2
1:	.byte 0xDB,0xE4		    /* fsetpm for 287, ignored by 387 */
	ret

/*
 *  setup_idt
 *
 *  sets up a idt with 256 entries pointing to
 *  ignore_int, interrupt gates. It then loads
 *  idt. Everything that wants to install itself
 *  in the idt-table may do so themselves. Interrupts
 *  are enabled elsewhere, when we can be relatively
 *  sure everything is ok. This routine will be over-
 *  written by the page tables.
 */
/* 
 *　setup_idt会将256个中断向量设置为ignore_int， lea执行为取有效地址指令
 *
 */
setup_idt:
	lea ignore_int,%edx     /* address of edx */
	movl $0x00080000,%eax   /* EAX is interrupt gat Blow 32bit */
	movw %dx,%ax		    /* selector = 0x0008 = cs */
	movw $0x8E00,%dx	    /* interrupt gate - dpl=0, present */
	                        /* P = 1, DPL=00 D = 1, 32bits */
	                        /* type = 110 interrupt gate */

    /***********************************************************************************************
    use EAX for interrupt description blow 32bit
    Use EDX for interrupt descirpt high 32bit
    EAX存放的是中断描述符的低32位
    EAX存放的是中断描述符的高32位
    上述的代码是将中断服务子程序处理成如下格式， CS为8表示二进制为00001000表示段选择子为1（代码段）
    +----------+---+-----------+---+---+-----------+--------+--------------+-----------+----------+
    | H 16bits | P | DPL(2bit) | 0 | D | 3bit type | 3bit 0 | 5bit Reserve | 16bits CS | L 16bits |
    +----------+---+-----------+---+---+-----------+--------+--------------+-----------+----------+
    |          | 1 |   00      | 0 | 1 |    110    |   000  |              |     8     |          |
    +----------+---+-----------+---+---+-----------+--------+--------------+-----------+----------+
    *************************************************************************************************/
    
	lea idt,%edi            /*将中断描述符表的地址存放在edi寄存器中，一般作为目的地址寄存器*/
	mov $256,%ecx           /*计数寄存器，一共256项*/
	
    /* 如下代码将中断描述符复制到edi寄存器的地址处，并循环256次
     * 使用lidt加载idt_descr的中断描述符
     */	
rp_sidt:
	movl %eax,(%edi)        /* *edi == eax */
	movl %edx,4(%edi)       /* *(edi + 4) = eds */
	addl $8,%edi            /* edi = edi + 8, meas next interrupt select descript */
	dec %ecx                /* ecs -- */
	jne rp_sidt             /* if not 0 goto rp_sidt */
	lidt idt_descr          /* load interrupt select descript */
	ret

/*
 *  setup_gdt
 *
 *  This routines sets up a new gdt and loads it.
 *  Only two entries are currently built, the same
 *  ones that were built in init.s. The routine
 *  is VERY complicated at two whole lines, so this
 *  rather long comment is certainly needed :-).
 *  This routine will beoverwritten by the page tables.
 */
setup_gdt:
	lgdt gdt_descr
	ret

/*
 * I put the kernel page tables right after the page directory,
 * using 4 of them to span 16 Mb of physical memory. People with
 * more than 16MB will have to expand this.
 */
.org 0x1000
pg0:

.org 0x2000
pg1:

.org 0x3000
pg2:

.org 0x4000
pg3:

.org 0x5000
/*
 * tmp_floppy_area is used by the floppy-driver when DMA cannot
 * reach to a buffer-block. It needs to be aligned, so that it isn't
 * on a 64kB border.
 */
tmp_floppy_area:
	.fill 1024,1,0

/* 
 * 程序运行到此处时会调用了setup_paging在setup_paging返回时由于将main函数进行压堆栈将执行main函数
 * L6的地址为main函数的返回地址
 * $1, $2, $3为main函数的参数
 */
after_page_tables:
	pushl $1		# These are the parameters to main :-)
	pushl $2
	pushl $3
	pushl $L6		# return address for main, if it decides to.
	pushl $main
	jmp setup_paging
L6:
	jmp L6			# main should never return here, but
				    # just in case, we know what happens.

/* This is the default interrupt "handler" :-) */
int_msg:
	.asciz "Unknown interrupt\n\r"
.align 2
ignore_int:
	pushl %eax
	pushl %ecx
	pushl %edx
	push %ds
	push %es
	push %fs
	movl $0x10,%eax
	mov %ax,%ds
	mov %ax,%es
	mov %ax,%fs
	pushl $int_msg
	call printk
	popl %eax
	pop %fs
	pop %es
	pop %ds
	popl %edx
	popl %ecx
	popl %eax
	iret


/*
 * Setup_paging
 *
 * This routine sets up paging by setting the page bit
 * in cr0. The page tables are set up, identity-mapping
 * the first 16MB. The pager assumes that no illegal
 * addresses are produced (ie >4Mb on a 4Mb machine).
 *
 * NOTE! Although all physical memory should be identity
 * mapped by this routine, only the kernel page functions
 * use the >1Mb addresses directly. All "normal" functions
 * use just the lower 1Mb, or the local data space, which
 * will be mapped to some other place - mm keeps track of
 * that.
 *
 * For those with more memory than 16 Mb - tough luck. I've
 * not got it, why should you :-) The source is here. Change
 * it. (Seriously - it shouldn't be too difficult. Mostly
 * change some constants etc. I left it at 16Mb, as my machine
 * even cannot be extended past that (ok, but it was cheap :-)
 * I've tried to show which constants to change by having
 * some kind of marker at them (search for "16Mb"), but I
 * won't guarantee that's all :-( )
 */
.align 2
setup_paging:
	movl $1024*5,%ecx		    /* 5 pages - pg_dir+4 page tables */
	xorl %eax,%eax              /* EAX is 0 */
	xorl %edi,%edi			    /* pg_dir is at 0x000, EDI is 0, start address*/
	cld;rep;stosl               /* stosl *EDI = EAX */
	/**********************************************************************
    以上代码的意思是从0-1024*5的地址空间清零，由于此时0地址处的代码已经执行完毕没有什么用了
    为什么是1024*5的长度呢，我们在上面的代码中可以看到.org 0x1000 pg0: 从0x1000为第一个页表一共建立了4个
    在加上0地址到0x1000一共5个
    问题1：分段是怎么用的？
    答：执行发出逻辑地址后经分段部件形成线性地址，根据前面的分析我们支持逻辑地址等于线性地址
    问题2：分页是怎么进行地址映射的？
    答：分页机制将线性地址分成三个部分进行查表，高10位表示页表目录，中10位表示页表项，低12位表示偏移，在寻址时
    根据高10位找到页表目录，页表目录存放了页表的起始地址，根据中10位在页面中找到对应的物理页，在加上低12位的偏移形成物理地址
    页表目录和页表的格式如下（页表目录和页表是4KB对齐的）：
    +----------------------------------------------+---------------------+ 
    |页表目录或者页表物理地址的高位BIT(12-31)      |          7          | 
    +----------------------------------------------+---------------------+  
    由于页表目录和页表以4KB对齐，因此我们不用关心其低12位，低12位有其特殊的意义，具体可查询intel的数据手册

    pg_dir表示页表的地址，7表示为该页存在用户可读可写
    由于目前系统的最大内存为16MB，因此只用到也变目录的前4项（为什么）
    一个页表管理1024项4KB的地址空间既4MB，那么16MB需要多少个页表目录呢，答案是4个
    ***********************************************************************/
    
	movl $pg0+7,pg_dir		    /* set present bit/user r/w */
	movl $pg1+7,pg_dir+4		/*  --------- " " --------- */
	movl $pg2+7,pg_dir+8		/*  --------- " " --------- */
	movl $pg3+7,pg_dir+12		/*  --------- " " --------- */

    /*
     上面的代码执行完毕后内存如下
    --起始地址0x0000
        0x00001 007
        0x00002 007
        0x00003 007
        0x00004 007
    一共四项页表目录
    */
     
	movl $pg3+4092,%edi         /* end of this page */
	movl $0xfff007,%eax		    /* 16Mb - 4096 + 7 (r/w user,p) */
	std                         /* derection -4 */
1:	stosl			            /* fill pages backwards - more efficient :-) *EDI=EAS */
	subl $0x1000,%eax
	jge 1b
	cld
    /*
    以上的代码倒序从pg3的最后一项开始填充直到eax为0，这样填充下来后如下图
        页表目录
        -------------------------0x0000
        0x00001 007
        -------------------------0x0004
        0x00002 007
        -------------------------0x0008
        0x00003 007
        -------------------------0x000C
        0x00004 007
        ...
        ...
        ...
　　    pg0页表0
        -------------------------0x1000
        0x00000 007
        -------------------------0x1004
        0x00001 007
        -------------------------0x1008
        ...
        ...
        ...
　　    pg1页表1
        -------------------------0x2000
        0x00400 007
        -------------------------0x2004
        0x00401 007
        -------------------------0x2008
        ...
        ...
        ...
　　    pg2页表2
        -------------------------0x3000
        0x00800 007
        -------------------------0x3004
        0x00801 007
        -------------------------0x3008
        ...
        ...
        ...
　　    pg3页表3
        -------------------------0x4000
        0x00c00 007
        -------------------------0x4004
        0x00c01 007
        -------------------------0x4008
        ...
        ...
        ...
        -------------------------0x4ff4
        0x00ffd 007
        -------------------------0x4ff8
        0x00ffe 007
        -------------------------0x4ffc
        0x00fff 007
        -------------------------0x5000
    我们举一个例子，如果要访问0x00401555地址处的数据，我们知道系统需先经过段映射由于段映射后线性地址和逻辑地址一样
    因此访问的地址还是0x00401555,段映射后需要进行页映射，我们根据上面的问题2对地址进行分解
    0x00401555的高10位为1寻找第一个页表目录值为0x00002 007，中10位为1在页表1中的第一项值为0x00401 007，低12位为555，
    根据0x00401 007去掉007，再加上555，最终的地址为0x00401555，我们发现饶了一圈逻辑地址和物理地址一样
    这种映射方式成为平坦映射
    
    我们再来一个例子，地址0x00FFF111地址，我们根据规则将其分解
    高10位为0000000011即3
    中10位为1111111111即1023，每一个页面占4个地址即1023*4 = 4092 = 0xFFC
    低12位为111
    如上面蓝色字体的映射过程，其最终地址还是0x00fff111
    我们可以看出页管理后系统还是平坦映射，从0地址开始一共16MB
    */

	
	xorl %eax,%eax		        /* pg_dir is at 0x0000 */
	movl %eax,%cr3		        /* cr3 - page directory start */
	movl %cr0,%eax
	orl $0x80000000,%eax
	movl %eax,%cr0		        /* set paging (PG) bit */
	ret			                /* this also flushes prefetch-queue */

.align 2
.word 0
idt_descr:
	.word 256*8-1		        # idt contains 256 entries
	.long idt
.align 2
.word 0
gdt_descr:
	.word 256*8-1		        # so does gdt (not that that's any
	.long gdt		            # magic number, but it works for me :^)

	.align 8
idt:	.fill 256,8,0		    # idt is uninitialized

/*
 * 第二次的全局描述符地址，这个地址是从0地址开始的, 一共16MB的空间
 */
gdt:	
    .quad 0x0000000000000000	/* NULL descriptor */
	.quad 0x00c09a0000000fff	/* 16Mb */
	.quad 0x00c0920000000fff	/* 16Mb */
	.quad 0x0000000000000000	/* TEMPORARY - don't use */
	.fill 252,8,0			    /* space for LDT's and TSS's etc */

