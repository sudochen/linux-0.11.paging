# 
# 32位指令，在这就进入到保护模式了
# 在本模块中，内核的起始地址为0xC0000000，用户程序的起始地址为0x00000000
# 我对原来的Linux0.11中的内存相关进行了修改，对于一个进程使用1GB的内核空间和3GB的用户空间
#
.code32

# 
# 此处运行在绝对地址0x00000000处，这个页是页目录(page directory)的地址，
# 启动代码运行完毕后会被页目录(page directory)覆盖
#
.text
.globl startup_32,idt,gdt,swapper_pg_dir,tmp_floppy_area,floppy_track_buffer

#
# swapper_pg_dir是主页目录的地址，主页目录地址在0x00000000处
# 根据setup模块我们安装了两个段描述符，分别是代码段和数据段，共8MB的空间
# 0x10的二进制为10000, 
# 我们可以看出DPL为0，T为0表示此描述符在全局描述符中，
# 10表示第二个描述符，权限为0最高权限
# 综上，设置数据段为0x10，全局描述第2个段
# 也就是设置数据段
#
swapper_pg_dir:
startup_32:
	cld
	movl $0x10,%eax			
	mov %ax,%ds
	mov %ax,%es
	mov %ax,%fs
	mov %ax,%gs

# 
# stack_start定义在kernel/sched.c中
# long user_stack[PAGE_SIZE >> 2];
# struct {
# 	long *a;
# 	short b;
# } stack_start { &user_stack, 0x10}
# lss设置了栈顶指针和其栈的段地址，0x10表示数据段，
# 地址为stack_start定义起大小为PAGE_SIZE>>2 = 1024字节
#	
	lss stack_start,%esp
	call setup_idt			# 设置所有的中断描述符为ignore
	
	xorl %eax,%eax
1:	incl %eax		        # check that A20 really IS enabled
	movl %eax,0x000000	    # loop forever if it isn't
	cmpl %eax,0x100000
	je 1b
	

# 
# NOTE! 486 should set bit 16, to check for write-protect in supervisor 
# mode. Then it would be unnecessary with the "verify_area()"-calls. 
# 486 users probably want to set the NE (#5) bit also, so as to use 
# int 16 for math errors. 
# 对486处理器相关的处理	

	movl %cr0,%eax		    # check math chip	
	andl $0x80000011,%eax	# Save PG,PE,ET
	# "orl $0x10020,%eax" here for 486 might be good
	orl $2,%eax		    	# set MP	
	movl %eax,%cr0	
	call check_x87	
	jmp after_page_tables

#
# We depend on ET to be correct. This checks for 287/387.
# 可暂时不关注
#
check_x87:
	fninit
	fstsw %ax
	cmpb $0,%al
	je 1f
	movl %cr0,%eax			# no coprocessor: have to set bits
	xorl $6,%eax			# reset MP, set EM
	movl %eax,%cr0
	ret
.align 4
1:	.byte 0xDB,0xE4			# fsetpm for 287, ignored by 387
	ret

#
# setup_idt
#
# sets up a idt with 256 entries pointing to
# ignore_int, interrupt gates. It doesn't actually load
# idt - that can be done only after paging has been enabled
# and the kernel moved to 0xC0000000. Interrupts
# are enabled elsewhere, when we can be relatively
# sure everything is ok. This routine will be over-
# written by the page tables.
# setup_idt会将256个中断向量设置为ignore_int，但是此处没有加载idt
#
setup_idt:
	lea ignore_int,%edx		# 获取ignore_int的有效地址，edx为中断门的高32位
	movl $0x00080000,%eax	# eax寄存器为终端门的低32位
	movw %dx,%ax			# selector = 0x0008 = cs
	movw $0x8E00,%dx		# interrupt gate - dpl=0, present
#
# 上述的代码是将中断服务子程序处理成如下格式， CS为8表示二进制为00001000表示段选择子为1（代码段）
# +----------+---+-----------+---+---+-----------+--------+--------------+-----------+----------+
# | H 16bits | P | DPL(2bit) | 0 | D | 3bit type | 3bit 0 | 5bit Reserve | 16bits CS | L 16bits |
# +----------+---+-----------+---+---+-----------+--------+--------------+-----------+----------+
# |          | 1 |   00      | 0 | 1 |    110    |   000  |              |     8     |          |
# +----------+---+-----------+---+---+-----------+--------+--------------+-----------+----------+
#

	lea idt,%edi			# 将中断描述符表的地址存放在edi寄存器中，一般作为目的地址寄存器
	mov $256,%ecx			# 计数寄存器，一共256项
# 
# 如下代码将中断描述符复制到edi寄存器的地址处，并循环256次
# 使用lidt加载idt_descr的中断描述符
#
rp_sidt:
	movl %eax,(%edi)		# 将eax的值存放在edi地指处
	movl %edx,4(%edi)		# 将edx的值存放在edi+4地址处
	addl $8,%edi			# 将edi的值加8，一个中断描述符占用8个字节
	dec %ecx				# 将ecx减一
	jne rp_sidt				# 如果ecx不为0则跳转到rp_sidt处
	ret

#
# I put the kernel page tables right after the page directory,
# using 4 of them to span 16 Mb of physical memory. People with
# more than 16MB will have to expand this.
#
.org 0x1000
pg0:

.org 0x2000
pg1:

.org 0x3000
pg2:

.org 0x4000
pg3:

.org 0x5000


#
# tmp_floppy_area is used by the floppy-driver when DMA cannot
# reach to a buffer-block. It needs to be aligned, so that it isn't
# on a 64kB border.
#
tmp_floppy_area:
	.fill 1024,1,0
#
# floppy_track_buffer is used to buffer one track of floppy data: it
# has to be separate from the tmp_floppy area, as otherwise a single-
# sector read/write can mess it up. It can contain one full track of
# data (18*2*512 bytes).
#
floppy_track_buffer:
	.fill 512*2*18,1,0

after_page_tables:
	call setup_paging		# 设置页式映射
	lgdt gdt_descr			# 加载全局描述符
	lidt idt_descr			# 加载中断描述符
	ljmp $0x08,$1f			# 跳转到代码段的1处运行，也就是下面的1处，强制更改了CS
1:	movl $0x10,%eax			# reload all the segment registers
	mov %ax,%ds				# after changing gdt.
	mov %ax,%es				# relaod的目的在于刷新描述符高速缓存
	mov %ax,%fs
	mov %ax,%gs
	lss stack_start,%esp	# 重新设置堆栈
	pushl $8				# These are the parameters to main :-)
	pushl $7
	pushl $6
	cld						# gcc2 wants the direction flag cleared at all times, 递增
	call start_kernel
L6:
	jmp L6					# main should never return here, but
							# just in case, we know what happens.
int_msg:
	.asciz "Unknown interrupt\n\r"
.align 4
ignore_int:
	cld
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


#
# Setup_paging
# 
# This routine sets up paging by setting the page bit
# in cr0. The page tables are set up, identity-mapping
# the first 16MB. The pager assumes that no illegal
# addresses are produced (ie >4Mb on a 4Mb machine).
#
# NOTE! Although all physical memory should be identity
# mapped by this routine, only the kernel page functions
# use the >1Mb addresses directly. All "normal" functions
# use just the lower 1Mb, or the local data space, which
# will be mapped to some other place - mm keeps track of
# that.
# 
# For those with more memory than 16 Mb - tough luck. I've
# not got it, why should you :-) The source is here. Change
# it. (Seriously - it shouldn't be too difficult. Mostly
# change some constants etc. I left it at 16Mb, as my machine
# even cannot be extended past that (ok, but it was cheap :-)
# I've tried to show which constants to change by having
# some kind of marker at them (search for "16Mb"), but I
# won't guarantee that's all :-( )
# 
.align 4
setup_paging:

	movl $1024*5,%ecx		    # 5 pages - swapper_pg_dir+4 page tables 
	xorl %eax,%eax				# eax = 0
	xorl %edi,%edi			    # swapper_pg_dir is at 0x000
	cld;rep;stosl				# stosl *EDI = EAX; 
#
# 上面的代码将0地址开始的20K空间清零，
# 在这里插入一点知识，X86首先进行段式映射，然后在进行页式映射，
# X86指令中的称为偏移地址
# 偏移地址经过段式映射后成为线性地址，我们可以知道Linux中，线性地址就等于偏移地址
# 线性地址经过页式映射后才是物理地址
# 分页是怎么进行的？
# 分页机制将线性地址分成三个部分进行查表，高10位表示页表目录，中10位表示页表项，低12位表示偏移，
# 在寻址时，根据高10位找到页表目录，页表目录存放了页表的起始地址
# 根据中10位在页面中找到对应的物理页，在加上低12位的偏移形成物理地址
# +---------------------------------------+------+ 
# | 页表目录或者页表物理地址的高位BIT(12-31) |   7  | 
# +---------------------------------------+------+
# 我们不用关心其低12位，低12位有其特殊的意义，具体可查询intel的数据手册，
# 页表目录和页表以4KB对齐
# 
# 这20K中前4K为主页表目录，主目录中一共有1024个页表目录，每个页表目录指向一个页表，每一个页表也有1024项
# 现在我们算一个算，一个页是4KB，一个页表都1024项，那个一个页表可以维护的内存是4M，
# 一个页表目录项对应一个页表，也就是说一个页表目录可以维护4M的内存
# 那么有多个页表目录项呢，答案是1024个，那么很容易知道一个页表目录维护4GB的内存
# 由于当前系统中只用16M的内存，因此我们只需要4个目录项即可
#
#
	
# Identity-map the kernel in low 4MB memory for ease of transition
# 下面的代码创建了如下映射
# 虚拟地址0x00000000+4MB映射到物理地址0x00000000+4MB
# 虚拟地址0xC0000000+16MB映射到物理地址0xC0000000+16MB
# 由此可见我们代码在0地址和0xC0000000都能很好的运行
#
	movl $pg0+7,swapper_pg_dir				# set present bit/user r/w
# But the real place is at 0xC0000000
# 为什么是3072，一个页表目录项占用4个字节，那么3072正好对应的是第（3072/4 = ）个目录项
# 要给页表目录项维护4M内存
# 那第768个目录项维护内存的起始地址是 768*4M = 3072M = 3G = 0xC0000000地址处
	movl $pg0+7,swapper_pg_dir+3072	   	 	# set present bit/user r/w */
	movl $pg1+7,swapper_pg_dir+3076
	movl $pg2+7,swapper_pg_dir+3080
	movl $pg3+7,swapper_pg_dir+3084	
#
# 上面的代码执行完毕后内存如下
# 地址0x00000000
#  0x00001 007
# 地址0x00000C00
#  0x00001 007
#  0x00002 007
#  0x00003 007
#  0x00004 007
# 一共5个页表目录   
#
#
	
	movl $pg3+4092,%edi			# 最后一个页表
	movl $0xfff007,%eax		    # 16Mb - 4096 + 7 (r/w user,p)
	std
1:	stosl			            # fill pages backwards - more efficient :-)
	subl $0x1000,%eax
	jge 1b
	cld
# 
# 上面的代码执行完成后内存如下
# 地址0x00000000 swapper_pg_dir
#  0x00001 007
# 地址0x00000C00
#  0x00001 007
#  0x00002 007
#  0x00003 007
#  0x00004 007
# 地址0x00001000 pg0
#  0x00000 007
#  0x00001 007
# 地址0x00002000 pg1
#  0x00400 007
#  0x00401 007
# 地址0x00003000 pg2
#  0x00800 007
#  0x00801 007
# 地址0x00001000 pg3
#  0x00c00 007
#  0x00c01 007
#  ...
#  0x00ffe 007
#  0x00fff 007					# 这个FFF007就是上面eax寄存器的由来
#
# 地址0x00FFF111地址，我们根据规则将其分解，
# 我们知道系统需先经过段映射由于段映射后线性地址和逻辑地址一样
# 高10位为0000000011即3，第三项页面目录项其对应的页面项为pg3
# 中10位为1111111111即1023，每一个页面占4个地址即1023*4 = 4092，也就是pg3的最后一项，对应为0xfff
# 低12位为偏移地址0x111
# 合并起来后地址为0xfff111
# 我们可以看出页管理后系统还是平坦映射
#
	xorl %eax,%eax		        # swapper_pg_dir is at 0x0000
	movl %eax,%cr3		        # cr3 - page directory start 
	movl %cr0,%eax
	orl $0x80000000,%eax
	movl %eax,%cr0		        # set paging (PG) bit
	ret			                # this also flushes prefetch-queue

#
# The interrupt descriptor table has room for 256 idt's
#
.align 4
.word 0
idt_descr:
	.word 256*8-1		        # idt contains 256 entries
	.long 0xc0000000+idt

.align 4
idt:
	.fill 256,8,0		        # idt is uninitialized

#
# The real GDT is also 256 entries long - no real reason
# 
.align 4
.word 0
gdt_descr:
	.word 256*8-1
	.long 0xc0000000+gdt

.align 4
gdt:
	.quad 0x0000000000000000	# NULL descriptor
	.quad 0xc0c09a0000000fff	# 16Mb at 0xC0000000
	.quad 0xc0c0920000000fff	# 16Mb
	.quad 0x0000000000000000	# TEMPORARY - don't use
	.fill 252,8,0			    # space for LDT's and TSS

