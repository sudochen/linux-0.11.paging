# 仍然是16位指令，在80386实模式下最多访问1MB内存
	.code16
# rewrite with AT&T syntax by falcon <wuzhangjin@gmail.com> at 081012
#
#	setup.s		(C) 1991 Linus Torvalds
#
# setup.s is responsible for getting the system data from the BIOS,
# and putting them into the appropriate places in system memory.
# both setup.s and system has been loaded by the bootblock.
#
# This code asks the bios for memory/disk/other parameters, and
# puts them in a "safe" place: 0x90000-0x901FF, ie where the
# boot-block used to be. It is then up to the protected mode
# system to read them from there before the area is overwritten
# for buffer-blocks.
#

# NOTE! These had better be the same as in bootsect.s!

	.equ INITSEG, 0x9000	# we move boot here - out of the way
	.equ SYSSEG, 0x1000		# system loaded at 0x10000 (65536).
	.equ SETUPSEG, 0x9020	# this is the current segment

	.global _start, begtext, begdata, begbss, endtext, enddata, endbss
	.text
	begtext:
	.data
	begdata:
	.bss
	begbss:
	.text
#
# 
#	ljmp $SETUPSEG, $_start
# 以上的代码删除掉系统仍然可以运行，以上的代码会修改CS寄存器的内容
# 因为直接跳转到_start地址处运行也可以, 因此我注释了
#
# 
_start:

# ok, the read went well so we get current cursor position and save it for
# posterity.
# address	bytes	name		description
# 0x90000	2		光标位置	列号（0x00最左端），行号（0x00最顶端）
# 0x90002	2		扩展内存数	系统从1M开始的扩展内存数值（KB），实模式下最多访问1M空间	

# 获取当前光标的位置，存放在地址0x90000处，因为bootsect程序此时已经没用了，共512个字节，
# 
	mov	$INITSEG, %ax	# this is done in bootsect already, but...
	mov	%ax, %ds		# DS = 0x9000
	mov	$0x03, %ah		# read cursor pos
	xor	%bh, %bh
	int	$0x10			# save it in known place, con_init fetches
	mov	%dx, %ds:0		# it from 0x90000. save current in 0x90000
#
# 获取mem的大小，并存放在0x90002处
# Get memory size (extended mem, kB)
	mov	$0x88, %ah 
	int	$0x15
	mov	%ax, %ds:2		# mem size save int 0x90002

#
# 获取声卡数据存放在0x90004,0x90006处，共四个字节
# Get video-card data:
	mov	$0x0f, %ah
	int	$0x10
	mov	%bx, %ds:4	# bh = display page, address is 0x90004
	mov	%ax, %ds:6	# al = video mode, ah = window width 0x90006

# 获取EGA/VGA数据，依次存放在0x90008, 0x9000a,  0x9000c处
# check for EGA/VGA and some config parameters

	mov	$0x12, %ah
	mov	$0x10, %bl
	int	$0x10
	mov	%ax, %ds:8
	mov	%bx, %ds:10
	mov	%cx, %ds:12

# 获取hd0的数据，存放在0x90080,128个字节偏移处，共16个字节
# Get hd0 data

	mov	$0x0000, %ax
	mov	%ax, %ds
	lds	%ds:4*0x41, %si
	mov	$INITSEG, %ax			# INITSEG = 0x9000
	mov	%ax, %es				# ES = 0x9000
	mov	$0x0080, %di			# destination address is ES:DI = 0x90080
	mov	$0x10, %cx
	rep
	movsb

# 获取hd1的数据，存放在0x90090,128+16=144个字节偏移处，共16个字节
# Get hd1 data

	mov	$0x0000, %ax
	mov	%ax, %ds
	lds	%ds:4*0x46, %si
	mov	$INITSEG, %ax
	mov	%ax, %es
	mov	$0x0090, %di			# destination ES:DI = 0x90090
	mov	$0x10, %cx
	rep
	movsb

# Check that there IS a hd1 :-)

	mov	$0x01500, %ax
	mov	$0x81, %dl
	int	$0x13
	jc	no_disk1
	cmp	$3, %ah
	je	is_disk1
no_disk1:
	mov	$INITSEG, %ax
	mov	%ax, %es
	mov	$0x0090, %di
	mov	$0x10, %cx
	mov	$0x00, %ax
	rep
	stosb
is_disk1:

# now we want to move to protected mode ...
# 现在，我们要进入保护模式，先关掉终端

	cli							# no interrupts allowed ! 

# first we move the system to it's rightful place
# 下面的代码将0x10000处的代码拷贝到0x0000处，供拷贝0x80000个字节512KB，
# 内核长度最大不能超过512K的假定前提
#
#
	mov	$0x0000, %ax
	cld							# 'direction'=0, movs moves forward
do_move:
	mov	%ax, %es				# destination segment
	add	$0x1000, %ax
	cmp	$0x9000, %ax
	jz	end_move
	mov	%ax, %ds				# source segment
	sub	%di, %di
	sub	%si, %si
	mov $0x8000, %cx
	rep
	movsw
	jmp	do_move

# then we load the segment descriptors
# SETUPSEG 0x9020
# 当前数据段地址为0x9020
# 加载GDT, IDT描述符表，为保护模式做准备
end_move:
	mov	$SETUPSEG, %ax			# right, forgot this at first. didn't work :-)
	mov	%ax, %ds				# DS = 0x9020 以为idt_48是相对DS的偏移地址，在上面的代码中该表了DS，在这个地方恢复
	lidt idt_48					# load idt with 0,0
	lgdt gdt_48					# load gdt with whatever appropriate

# that was painless, now we enable A20

	#call	empty_8042			# 8042 is the keyboard controller
	#mov	$0xD1, %al			# command write
	#out	%al, $0x64
	#call	empty_8042
	#mov	$0xDF, %al			# A20 on
	#out	%al, $0x60
	#call	empty_8042
	inb     $0x92, %al			# open A20 line(Fast Gate A20).
	orb     $0b00000010, %al
	outb    %al, $0x92

# well, that went ok, I hope. Now we have to reprogram the interrupts :-(
# we put them right after the intel-reserved hardware interrupts, at
# int 0x20-0x2F. There they won't mess up anything. Sadly IBM really
# messed this up with the original PC, and they haven't been able to
# rectify it afterwards. Thus the bios puts interrupts at 0x08-0x0f,
# which is used for the internal hardware interrupts as well. We just
# have to reprogram the 8259's, and it isn't fun.

	mov	$0x11, %al				# initialization sequence(ICW1)
								# ICW4 needed(1),CASCADE mode,Level-triggered
	out	%al, $0x20				# send it to 8259A-1
	.word	0x00eb,0x00eb		# jmp $+2, jmp $+2
	out	%al, $0xA0				# and to 8259A-2
	.word	0x00eb,0x00eb
	mov	$0x20, %al				# start of hardware int's (0x20)(ICW2)
	out	%al, $0x21				# from 0x20-0x27
	.word	0x00eb,0x00eb
	mov	$0x28, %al				# start of hardware int's 2 (0x28)
	out	%al, $0xA1				# from 0x28-0x2F
	.word	0x00eb,0x00eb		# IR 7654 3210
	mov	$0x04, %al				# 8259-1 is master(0000 0100) --\
	out	%al, $0x21				# |
	.word	0x00eb,0x00eb		# INT	/
	mov	$0x02, %al				# 8259-2 is slave(010 --> 2)
	out	%al, $0xA1
	.word	0x00eb,0x00eb
	mov	$0x01, %al				# 8086 mode for both
	out	%al, $0x21
	.word	0x00eb,0x00eb
	out	%al, $0xA1
	.word	0x00eb,0x00eb
	mov	$0xFF, %al				# mask off all interrupts for now
	out	%al, $0x21
	.word	0x00eb,0x00eb
	out	%al, $0xA1

# well, that certainly wasn't fun :-(. Hopefully it works, and we don't
# need no steenking BIOS anyway (except for the initial loading :-).
# The BIOS-routine wants lots of unnecessary data, and it's less
# "interesting" anyway. This is how REAL programmers do it.
#
# Well, now's the time to actually move into protected mode. To make
# things as simple as possible, we do no register set-up or anything,
# we let the gnu-compiled 32-bit programs do that. We just jump to
# absolute address 0x00000, in 32-bit protected mode.
# 启用32位保护模式，寻址方式发生变化

	mov	$0x0001, %ax			# protected mode (PE) bit
	lmsw %ax					# This is it!
#
# 在实模式中段寄存器的值为段地址
# 在保护模式中段寄存器称为段选择器，里面存放的值为段选择子
# 在保护模式中$8表示段选择子的为8，其定义如下
# +--------------------+---------------+------------------+
# |    索引（13bits）  | 权限（2bits） | 全局/局部(1bits) |
# +--------------------+---------------+------------------+
# $8的二进制为00001000,根据以上可以看出是全局描述符（1），最高权限（00），索引为1
#
# 根据后面的全局描述符表gdt可以知道，此处是代码段，基地址为0，也是Image模块的Head的地址
#
	ljmp $8, $0					# jmp offset 0 of code segment 0 in gdt

# This routine checks that the keyboard command queue is empty
# No timeout is used - if this hangs there is something wrong with
# the machine, and we probably couldn't proceed anyway.
empty_8042:
	.word	0x00eb,0x00eb
	in	$0x64, %al				# 8042 status port
	test $2, %al				# is input buffer full?
	jnz	empty_8042				# yes - loop
	ret
#
# 从0地址开始的两个段，数据段和代码段，基地址为0，长度为8MB
#
gdt:
	.word	0,0,0,0				# dummy

	.word	0x07FF				# 8Mb - limit=2047 (2048*4096=8Mb)
	.word	0x0000				# base address=0
	.word	0x9A00				# code read/exec
	.word	0x00C0				# granularity=4096, 386

	.word	0x07FF				# 8Mb - limit=2047 (2048*4096=8Mb)
	.word	0x0000				# base address=0
	.word	0x9200				# data read/write
	.word	0x00C0				# granularity=4096, 386

idt_48:
	.word	0					# idt limit=0
	.word	0,0					# idt base=0L

#
# 0x800表示限制大小在0x800
# 512+gdt可表示0x200+gdt，表示setup模块所在的的地址+gdt偏移，也就是gdt存放数据信息
# LGDT, LIDT指令后面根据地址是线性地址，这两个指令是仅有的能够加载线性地址的指令
# 也就是说不管段寄存器是什么值，都看作0，这两个指令通过在实模式中使用
# 以便于处理器在切换到保护模式之前进行初始化
#
#
gdt_48:
	.word	0x800				# gdt limit=2048, 256 GDT entries
# 512+gdt is the real gdt after setup is moved to 0x9020 * 0x10
	.word   512+gdt, 0x9		# gdt base = 0X9xxxx, 
	
.text
endtext:
.data
enddata:
.bss
endbss:

