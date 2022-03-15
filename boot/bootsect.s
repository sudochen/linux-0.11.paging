# 
# NOTICE:
# BOIS system call
# there is 20bits address length in 80389, and 20bits addressing 1MB memory space
# BOIS create interrupt table in address 0x00000, use 1KB memory space
#
# in AT assemble, EAX is 32bits resgister, AX for 16bits, AH, AL for 8bits
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
# NOTICE:
# SYS_SIZE is the (system size bytes)/16
#
	.equ SYSSIZE, SYS_SIZE
#
#	bootsect.s		(C) 1991 Linus Torvalds
#
# NOTICE:
# the kernel format like this
# | 512 bootsect | 512*4 setup | {head} {system} kernel |
# BOIS copy the first sector of booting device to 0x7c00 in memory, one sector has 512 bytes
# and jump 0x7c00 running
#
# bootsect copy itself from 0x7c00 to 0x90000(576KB), copy 512 bytes
# bootsect jump to 0x90000 and copy setup module after itsef, address is 0x90200(576.5KB),
# setup module has 4 sectors, 2048 bytes
# so the end of [bootsect + setup] is 0x90a00
#
#
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
# The loader has been made as simple as possible, and continuos
# read errors will result in a unbreakable loop. Reboot by hand. It
# loads pretty fast by getting whole sectors at a time whenever possible.
#
# 系统上电后，BOIS会将启动扇区的第一个扇区读到0x7c00(31KB)处并跳转到此处运行
# 第一个扇区的名称为bootsect模块，bootsect会将自己拷贝到0x90000(576KB)处运行
# 紧接着bootsect会从启动设备的第二个分区拷贝四个分区的数据到0x90200处
#
#
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

	.equ SETUPLEN, 4				# nr of setup-sectors
	.equ BOOTSEG, 0x07c0			# original address of boot-sector
	.equ INITSEG, 0x9000			# we move boot here - out of the way
	.equ SETUPSEG, 0x9020			# setup starts here
	.equ SYSSEG, 0x1000				# system loaded at 0x10000 (65536).
	.equ ENDSEG, SYSSEG + SYSSIZE	# where to stop loading

# ROOT_DEV:	0x000 - same type of floppy as boot.
#		0x301 - first partition on first drive etc
#	.equ ROOT_DEV, 0x301
	.equ ROOT_DEV, 0x21D
#
# the code will be copy to 0x7c00 and running
#
# I delete blow code
# ljmp    $BOOTSEG, $_start
# 
#
# 程序开始运行
# 设置DS为0x07c0，设置ES为0x9000
# 将SI和DI清零
# movsw将DS:SI[0x07c00]地址处的数据拷贝到ES:DI[90000]处，SI和DI会自动递增，拷贝的次数存放在CX寄存器中
# 因此下面的代码意思是，
# 将0x7c00的数据拷贝至0x90000(576K)处, 每次拷贝2个字节，共拷贝256次，512个字节
# 也就是将bootsect从0x07c00拷贝到0x90000(576K)处
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
# 而x86的栈为FD栈，满减栈，因此从0x90a00到0x9ff00的空间都是可以用，栈顶指针初始值为0x9ff00
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
#
load_setup:
	mov	$0x0000, %dx				# drive 0, head 0
	mov	$0x0002, %cx				# sector 2, track 0
	mov	$0x0200, %bx				# address = 512, in INITSEG
	mov $0x200+SETUPLEN, %ax		# service 2, nr of sectors, SETUPLEN is 4
	int	$0x13						# read it
	jnc	ok_load_setup				# ok - continue
	mov	$0x0000, %dx				
	mov	$0x0000, %ax				# reset the diskette
	int	$0x13
	jmp	load_setup

ok_load_setup:

# Get disk drive parameters, specifically nr of sectors/track

	mov	$0x00, %dl
	mov	$0x0800, %ax				# AH=8 is get drive parameters
	int	$0x13
	mov	$0x00, %ch
	#seg cs
	mov	%cx, %cs:sectors+0			# %cs means sectors is in %cs
	mov	$INITSEG, %ax
	mov	%ax, %es					# restore ES
#
# 使用系统掉打印：Loading system ...
# Print some inane message
	mov	$0x03, %ah					# read cursor pos
	xor	%bh, %bh
	int	$0x10
	
	mov	$24, %cx
	mov	$0x0007, %bx				# page 0, attribute 7 (normal)
	mov $msg1, %bp
	mov	$0x1301, %ax				# write string, move cursor
	int	$0x10

# 读取SYS模块，存放在地址0x10000（64K）开始的地方
# ok, we've written the message, now
# we want to load the system (at 0x10000)

	mov	$SYSSEG, %ax				# AX = 0x1000
	mov	%ax, %es					# ES = 0x1000 segment of 0x010000
	call read_it
	call kill_motor

# After that we check which root-device to use. If the device is
# defined (#= 0), nothing is done and the given device is used.
# Otherwise, either /dev/PS0 (2,28) or /dev/at0 (2,8), depending
# on the number of sectors that the BIOS reports currently.

	#seg cs
	mov	%cs:root_dev+0, %ax
	cmp	$0, %ax
	jne	root_defined
	#seg cs
	mov	%cs:sectors+0, %bx
	mov	$0x0208, %ax				# /dev/ps0 - 1.2Mb
	cmp	$15, %bx
	je	root_defined
	mov	$0x021c, %ax				# /dev/PS0 - 1.44Mb
	cmp	$18, %bx
	je	root_defined
undef_root:
	jmp undef_root
root_defined:
	#seg cs
	mov	%ax, %cs:root_dev+0
# 
# 跳转到 SETUPSEG的 0 偏移开始运行，SETUPSEG为0x9020，即地址0x90200，
# 而0x90200刚好为setup模块的起始地址
# NOTICE
# 目前，bootsect和setup在0x90000地址处
# system模块在0x10000（64K）地址处
#
# after that (everyting loaded), we jump to
# the setup-routine loaded directly after
# the bootblock:

	ljmp $SETUPSEG, $0				# setup code

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
	#seg cs
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
	#seg cs
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

#/*
# * This procedure turns off the floppy drive motor, so
# * that we enter the kernel in a known state, and
# * don't have to worry about it later.
# */
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
