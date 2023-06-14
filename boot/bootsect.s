# 
# BIOS系统调用
# 80386在实模式下虽然是16位的地址，但是经过段映射后可以形成20位的地址共寻址1MB的地址空间
# BOIS会在地址0处建立1KB字节的中断向量表，每个中断向量使用4个字节的空间，
# 前两个字节为段地址，后两个字节为偏移地址，因此一共256个中断向量
# BIOS启动后会建立后中断向量表用于提供一些基本的功能
# 所谓BIOS调用就是使用BIOS的中断功能来执行一些用户想要的操作
# 
# 在AT汇编里，EAX表示32位寄存器，AX表示16位寄存器，AH，AL表示8位寄存器
#
# .code16 表示后面是16位的汇编代码
#
#
	.code16
# 
# rewrite with AT&T syntax by falcon <wuzhangjin@gmail.com> at 081012
#
# SYS_SIZE is the number of clicks (16 bytes) to be loaded.
# 0x3000 is 0x30000 bytes = 196kB, more than enough for current
# versions of linux
#
# SYSSIZE是要加载的节数（16个字节为1节）0x3000*16也就是192KB的大小，
# 对于当前的内核来说已经足够了
# 80386在启动后处于8086,8086内存分段最大可寻址1MB的内存，这1MB的内存排列如下：
# 0x00000 - 0x9FFFF 640KB， 最低1KB位BIOS中断向量表
# 0xA0000 - 0xB7FFF 外围设备
# 0xB8000 - 0xBFFFF 显存
# 0xC0000 - 0xEFFFF 外围设备
# 0xF0000 - 0xFFFFF BIOS
#
	.equ SYSSIZE, 0x3000
#
#	bootsect.s		(C) 1991 Linus Torvalds
#
# 编译系统编译的镜像存放格式为：
# | 512 bootsect | 512*4 setup | system(head,kernel} |
#
# BOIS会将启动设备的前512字节拷贝至内存的0x7c00处，并跳转到此处运行，
# bootsect程序主要将自己（512个字节）搬移到0x90000(576K)处，
# 从启动设备继续读取setup模块，存放在自己后面，也就是0x90200地址处（576.5K）处
# 此时bootsect和setup的结尾地址为0x90a00
# bootsect和setup模块一共占用2.5KB的空间，其中bootsect占用0.5KB，setup占用2KB
#
# 以上的数据读取都使用了BIOS调用
#
# 问：为什么要将bootsect和setup模块存放到0x90000地址处
# 答：这里其实没有任何特殊的考虑，0x90000不会覆盖到外围区域，并且预留了576KB给内核使用
#
# bootsect.s is loaded at 0x7c00 by the bios-startup routines, and moves
# iself out of the way to address 0x90000, and jumps there.
#
# It then loads 'setup' directly after itself (0x90200), and the system
# at 0x10000, using BIOS interrupts. 
#
# NOTE! currently system is at most 8*65536 bytes long. This should be no
# problem, even in the future. I want to keep it simple. This 512 kB
# kernel size should be enough, especially as this doesn't contain the
# buffer cache as in minix
#
# 将system模块存放在0x10000，将bootsect和setup模块存放到0x90000
# 也就是说system模块的范围为0x10000 - 0x90000 一共 0x80000个字节（512KB）空间
#
#
# The loader has been made as simple as possible, and continuos
# read errors will result in a unbreakable loop. Reboot by hand. It
# loads pretty fast by getting whole sectors at a time whenever possible.
#
#

	.global _start, begtext, begdata, begbss, endtext, enddata, endbss
	.text
	begtext:
	.data
	begdata:
	.bss
	begbss:
	.text
#
# SETUPLEN 表示setup模块占用sectors的数量，一个sectors为512字节
# BOOTSEG 表示bootsect模块的原始运行地址
# INITSEG 表示bootsect模块搬移后的运行地址
# SETUPSEG 表示setup模块的运行地址
# SYSEG 表示system模块的运行地址，system最终会被搬移到0地址
# ENDSEG 表示system模块的结束地址
#
	.equ SETUPLEN, 4				# nr of setup-sectors
	.equ BOOTSEG, 0x07c0			# original address of boot-sector
	.equ INITSEG, 0x9000			# we move boot here - out of the way
	.equ SETUPSEG, 0x9020			# setup starts here
	.equ SYSSEG, 0x1000				# system loaded at 0x10000 (65536).
	.equ ENDSEG, SYSSEG + SYSSIZE	# where to stop loading

# ROOT_DEV:	0x000 - same type of floppy as boot.
#		0x301 - first partition on first drive etc
# 0x301 表示硬盘的第一个分区
# 0x21D 表示软盘的第一个分区
# 0x21C 表示RAMDISK
#
	.equ ROOT_DEV, 0x301
#	.equ ROOT_DEV, 0x21D
#	.equ ROOT_DEV, 0x21C
#

#
# 系统启动后，BIOS会将启动设备的前512字节拷贝至0x7c00处并运行
# 在编译bootsect模块中，我们发现了链接参数-Ttext 0 -e _start表示起始地址为0，程序入口为_start
# 
# 设置DS为0x07c0，设置ES为0x9000
# 将SI和DI清零
# movsw将DS:SI地址处的数据拷贝到ES:DI处，SI和DI会自动递增，拷贝的次数存放在CX寄存器中
# 因此下面的代码意思是，
# 将0x7c00的数据拷贝至0x90000(576K)处, 每次拷贝2个字节，共拷贝256次，512个字节
# 也就是将bootsect从0x07c00拷贝到0x90000(576K)处
#
# 问：为什么要拷贝到0x90000(576K)处
# 答：因为system会被拷贝到0x10000(64K)处，而Linus在写这个版本的Linux的时候假设内核的大小为512K,
# 这个可以在后面的注释里看到，64K+512K就是576K
#
# 问：那什么system要拷贝到0x10000而不是直接拷贝到0x00000地址呢，
# 答：这是因为在setup模块中需要用到BIOS调用获取一些硬件参数，而BIOS中断向量表和服务程序可能占用了64K的地址
# 这就是为什么在setup的最后又将system模块拷贝到0x00000地址的原因
#
_start:
	mov	$BOOTSEG, %ax				# BOOTSEG 0x07c0
	mov	%ax, %ds					# DS = 0x07c0
	mov	$INITSEG, %ax				# INITSEC 0x9000
	mov	%ax, %es					# ES = 0x9000
	mov	$256, %cx					# CX = 256
	sub	%si, %si					# SI = 0
	sub	%di, %di					# DI = 0
	rep								# execute repeat util CX == 0, total 512 bytes
	movsw							# copy 2Bytes from DS:SI(0x07c00) to ES:DI(0x90000) 512 bytes

# 
# 跳转至$INITSEC:go处运行，INITSEG定义为0x9000
# 因此也就是跳转至下面的标号“go”的地方开始运行，这条语句会将CS设置为INITSEG
#
#	
	ljmp $INITSEG, $go				# jump 0x9000:go
go:	mov	%cs, %ax					# CS = 0x9000
	mov	%ax, %ds					# DS = 0x9000
	mov	%ax, %es					# ES = 0x9000
	mov	%ax, %ss					# SS = 0x9000, put stack top at 0x9ff00
#
# 此处设置栈顶地址为0x9ff00
# 因为bootsec占用512字节，setup占用512*4个字节，从0x90000开始存放bootsect和setup，末尾地址为0x90a00
# 而x86的栈为FD栈，递减满栈，因此从0x90a00到0x9feff的空间都是可以用，
# 栈顶指针初始值为0x9ff00，下一次入栈的最近一个字节的地址为0x9feff
# 
#
	mov	$0xff00, %sp				# x86 FD stack [full decrease stack]
	                                # we will copy 4 sectors(2048) form boot device
	                                # code in 0x90000->0x90a00 and stack top 0x9ff00

# load the setup-sectors directly after the bootblock.
# Note that 'es' is already set up.
#
# 下面一段代码使用BOIS系统调用从第二个扇区读，共读取4个扇区，2048个字节，我们
# 通过前面的注释可是直到setup模块，刚好占用4个扇区，下面代码的左右就是从第二个
# 扇区开始读取数据，存放在当前数据段的0x200处，也就是0x90200处，读取成功后挑战至
# ok_load_setup处开始运行，读取失败后继续进行尝试读取
# 目前我们只需知道其含义即可，具体可参考<Linux内核完全注释的讲解>
# sector扇区从1开始编号
# drive（驱动控制器），head（磁头），track（磁道）都是从0开始编号
# 还有一个概念就是柱面（Cylinder）多个磁盘的同一个磁道是一个柱面
# 一般一个磁道有63个扇区，每个扇区512个字节
#
#
load_setup:
	mov	$0x0000, %dx				# drive 0, head 0
	mov	$0x0002, %cx				# sector 2, track 0
	mov	$0x0200, %bx				# address = 512, in INITSEG
	mov $0x0200+SETUPLEN, %ax		# service 2, nr of sectors, SETUPLEN is 4
	int	$0x13						# read it
	jnc	ok_load_setup				# ok - continue
	mov	$0x0000, %dx				
	mov	$0x0000, %ax				# reset the diskette
	int	$0x13
	jmp	load_setup
    
ok_load_setup:

#
# Get disk drive parameters, specifically nr of sectors/track
# 获取当前软盘驱动的参数放在sectors处
# cs和ds是一样的，因此cs:sectors是可以修改为sectors+0
# 
#
	mov	$0x00, %dl
	mov	$0x0800, %ax				# AH=8 is get drive parameters
	int	$0x13
	mov	$0x00, %ch
	mov	%cx, %cs:sectors+0			# %cs means sectors is in %cs
	mov	$INITSEG, %ax
	mov	%ax, %es					# restore ES
#
# 使用系统调用打印：Loading system ...
# Print some inane message
#
	mov	$0x03, %ah					# read cursor pos
	xor	%bh, %bh
	int	$0x10
	
	mov	$24, %cx
	mov	$0x0007, %bx				# page 0, attribute 7 (normal)
	mov $msg1, %bp
	mov	$0x1301, %ax				# write string, move cursor
	int	$0x10

# 
# 读取SYS模块，存放在地址0x10000（64K）开始的地方，
# 根据前面的SYSSIZE定义我们知道一共读取0x3000*16个字节也就是192KB的内容
# 对于我们来说已经够了，我们可以计算出当前的最大地址为64 + 192 = 256KB，
# 不能覆盖到bootsect和setup模块的起始地址
# ok, we've written the message, now
# we want to load the system (at 0x10000)
#
	mov	$SYSSEG, %ax				# AX = 0x1000
	mov	%ax, %es					# ES = 0x1000 segment of 0x010000
	call read_it					# 读取system模块到0x10000处,知道就行，暂时不关注
	call kill_motor					# 关闭驱动

#
# After that we check which root-device to use. If the device is
# defined (#= 0), nothing is done and the given device is used.
# Otherwise, either /dev/PS0 (2,28) or /dev/at0 (2,8), depending
# on the number of sectors that the BIOS reports currently.
#

	mov	%cs:root_dev+0, %ax			# 获取root_dev的参数
	cmp	$0, %ax						# 和0进行对比
	jne	root_defined				# 如果不为0说明，root_dev已经定义，跳转到root_defined处，root_dev的值存放在ax寄存器中
	mov	%cs:sectors+0, %bx			# 否则获取sectors处的数据，根据sectors的数据判断是1.2M还是1.44M的软盘
	mov	$0x0208, %ax				# /dev/ps0 - 1.2Mb
	cmp	$15, %bx					# 1.2M的sectors数目是15个
	je	root_defined				# 如果相等跳转到root_defined处
	mov	$0x021c, %ax				# /dev/PS0 - 1.44Mb
	cmp	$18, %bx					# 1.44M的sectors的数目是18个
	je	root_defined				# 如果相等跳转到root_defined处
undef_root:
	jmp undef_root
root_defined:
	mov	%ax, %cs:root_dev+0			# 将ax的值存放在root_dev处
# 
# 当所有的模块都加载完成后，跳转到0x09200地址处运行，我们知道此处是setup的地址
# 跳转到 SETUPSEG的 0 偏移开始运行，SETUPSEG为0x9020，即地址0x90200，
#
# 目前，
# bootsect在0x90000地址处共512字节
# setup在0x90200地址处共2KB
# system模块在0x10000(64KB)地址处共192KB字节
# 以上都在实模式的1MB访问空间内
#
# after that (everyting loaded), we jump to
# the setup-routine loaded directly after
# the bootblock:
# SETUPSEG定义为0x9020
# 此时CS为0x9020，也就是说跳转到0x90200地址运行
# 这个地址为setup模块的起始地址
#

	ljmp $SETUPSEG, $0				# setup code

# 
# This routine loads the system at address 0x10000, making sure
# no 64kB boundaries are crossed. We try to load it as fast as
# possible, loading whole tracks whenever we can.
#
# in:	es - starting address segment (normally 0x1000)
#
sread:	.word 1+ SETUPLEN			# sectors read of current track
head:	.word 0						# current head
track:	.word 0						# current track

read_it:
	mov	%es, %ax					# AX = 0x1000
	test $0x0fff, %ax				# ES must be 64KB boundary
die:	
	jne die							# es must be at 64kB boundary
	xor %bx, %bx					# bx is starting address within segment
rp_read:
	mov %es, %ax
 	cmp $ENDSEG, %ax				# have we loaded all yet?
	jb ok1_read
	ret
ok1_read:
	mov	%cs:sectors+0, %ax
	sub	sread, %ax
	mov	%ax, %cx
	shl	$9, %cx
	add	%bx, %cx
	jnc ok2_read
	je 	ok2_read
	xor %ax, %ax
	sub %bx, %ax
	shr $9, %ax
ok2_read:
	call read_track
	mov %ax, %cx
	add sread, %ax
	cmp %cs:sectors+0, %ax
	jne ok3_read
	mov $1, %ax
	sub head, %ax
	jne ok4_read
	incw track 
ok4_read:
	mov	%ax, head
	xor	%ax, %ax
ok3_read:
	mov	%ax, sread
	shl	$9, %cx
	add	%cx, %bx
	jnc	rp_read
	mov	%es, %ax
	add	$0x1000, %ax
	mov	%ax, %es
	xor	%bx, %bx
	jmp	rp_read

read_track:
	push %ax
	push %bx
	push %cx
	push %dx
	mov	track, %dx
	mov	sread, %cx
	inc	%cx
	mov	%dl, %ch
	mov	head, %dx
	mov	%dl, %dh
	mov	$0, %dl
	and	$0x0100, %dx
	mov	$2, %ah
	int	$0x13
	jc	bad_rt
	pop	%dx
	pop	%cx
	pop	%bx
	pop	%ax
	ret
bad_rt:	mov	$0, %ax
	mov	$0, %dx
	int	$0x13
	pop	%dx
	pop	%cx
	pop	%bx
	pop	%ax
	jmp	read_track

#
# This procedure turns off the floppy drive motor, so
# that we enter the kernel in a known state, and
# don't have to worry about it later.
# 
kill_motor:
	push %dx
	mov	$0x3f2, %dx
	mov	$0, %al
	outsb
	pop	%dx
	ret

sectors:
	.word 0

msg1:
	.byte 13,10
	.ascii "Loading system ..."
	.byte 13,10,13,10

	.org 508
root_dev:
	.word ROOT_DEV
boot_flag:
	.word 0xAA55
	
	.text
	endtext:
	.data
	enddata:
	.bss
	endbss:
	
