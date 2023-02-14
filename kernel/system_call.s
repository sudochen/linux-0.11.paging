/*
 *  linux/kernel/system_call.s
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 *  system_call.s  contains the system-call low-level handling routines.
 * This also contains the timer-interrupt handler, as some of the code is
 * the same. The hd- and flopppy-interrupts are also here.
 *
 * NOTE: This code handles signal-recognition, which happens every time
 * after a timer-interrupt and after each system call. Ordinary interrupts
 * don't handle signal-recognition, as that would clutter them up totally
 * unnecessarily.
 *
 * Stack layout in 'ret_from_system_call':
 *
 *	 0(%esp) - %eax
 *	 4(%esp) - %ebx
 *	 8(%esp) - %ecx
 *	 C(%esp) - %edx
 *	10(%esp) - %fs
 *	14(%esp) - %es
 *	18(%esp) - %ds
 *	1C(%esp) - %eip
 *	20(%esp) - %cs
 *	24(%esp) - %eflags
 *	28(%esp) - %oldesp
 *	2C(%esp) - %oldss
 */

SIG_CHLD	= 17

EAX		= 0x00
EBX		= 0x04
ECX		= 0x08
EDX		= 0x0C
FS		= 0x10
ES		= 0x14
DS		= 0x18
EIP		= 0x1C
CS		= 0x20
EFLAGS		= 0x24
OLDESP		= 0x28
OLDSS		= 0x2C

# offset within task_struct
state	= 0		# these are offsets into the task-struct.
counter	= 4
priority = 8
signal	= 12
sigaction = 16		# MUST be 16 (=len of sigaction)
blocked = (33*16)
stack_top = (33*16+4)

# offsets within sigaction
sa_handler = 0
sa_mask = 4
sa_flags = 8
sa_restorer = 12

nr_system_calls = 86

/*
 * Ok, I get parallel printer interrupts while using the floppy for some
 * strange reason. Urgel. Now I just ignore them.
 */
.globl system_call,sys_fork,timer_interrupt,sys_execve
.globl hd_interrupt,floppy_interrupt,parallel_interrupt
.globl device_not_available, coprocessor_error
.globl switch_to_by_stack, first_return_from_kernel

.align 4
bad_sys_call:
	movl $-1,%eax					# 错误码
	iret							# 弹出EIP,CS,EFLAGS,ESP,SS恢复到用户空间执行
.align 4
reschedule:
	pushl $ret_from_sys_call		# 保存返回地址
	jmp schedule					# 跳到schedule执行，schedule没有参数，因此schedule执行RET，则跳到ret_form_sys_call执行

.align 4
system_call:
	cmpl $nr_system_calls-1,%eax    # 判断系统调用号是否合法, eax存放的是系统调用号
	ja bad_sys_call                 # 如果不合法则跳到bad_sys_call处，并将eax设置为-1,作为返回值
	push %ds						# 保护ds
	push %es						# 保护es
	push %fs                        # 保存段选择子的内容，SS,ESP,CS,EIP在执行INT指令时都已经压入栈中，IRET时恢复
	pushl %edx						# 保护edx
	pushl %ecx		                # 保护ecx
	pushl %ebx		                # 保护ebx
	movl $0x10,%edx		            # set up ds,es to kernel space，使用内核空间
	mov %dx,%ds                     # 设置数据段为内核数据段
	mov %dx,%es                     # 设置附加段为内核数据段， 代码段在执行INT指令时已经设置了
	movl $0x17,%edx		            # fs points to local data space
	mov %dx,%fs                     # 设置FS为用户段选择子
	call *sys_call_table(,%eax,4)   # call地址sys_call_table + eax * 4, 即调用sys_fork程序，此时会将下一条指令的EIP入栈
	pushl %eax                      # 返回值存放在eax中
	movl current,%eax               # 取当前进程指针存放在eax中
	cmpl $0,state(%eax)		        # state 
	jne reschedule                  # 如果state不等于0则运行重新调度程序
	cmpl $0,counter(%eax)		    # counter，如果在运行状态但是时间片用完了也执行调用程序
	je reschedule
ret_from_sys_call:
	movl current,%eax		        # task[0] cannot have signals
	cmpl task,%eax                  # 判断是不是任务0，如果是跳到标号3处运行，任务0不执行信号处理
	je 3f
	cmpw $0x0f,CS(%esp)		        # was old code segment supervisor ? 如果是调用者是内核程序也不进行信号处理
	jne 3f
	cmpw $0x17,OLDSS(%esp)		    # was stack segment = 0x17 ?  如果原堆栈也是内核也退出
	jne 3f
	movl signal(%eax),%ebx          # 取信号位图
	movl blocked(%eax),%ecx         # 取信号屏蔽位图
	notl %ecx                       # 信号屏蔽位图取反
	andl %ebx,%ecx                  # 获得信号位图
	bsfl %ecx,%ecx                  # 如果为0则表示没有信号处理
	je 3f                           #   
	btrl %ecx,%ebx
	movl %ebx,signal(%eax)          # 信号
	incl %ecx
	pushl %ecx						# 信号值
	call do_signal                  # 调用信号处理函数do_signal(ecx)为参数
	popl %eax						# 弹出信号值
3:	popl %eax						# 恢复寄存器并恢复到用户空间执行
	popl %ebx
	popl %ecx
	popl %edx
	pop %fs
	pop %es
	pop %ds
	iret

.align 4
coprocessor_error:
	push %ds
	push %es
	push %fs
	pushl %edx
	pushl %ecx
	pushl %ebx
	pushl %eax
	movl $0x10,%eax
	mov %ax,%ds
	mov %ax,%es
	movl $0x17,%eax
	mov %ax,%fs
	pushl $ret_from_sys_call
	jmp math_error

# 因为当前内核堆栈不不会引起内核任务切换
# 但如果是可抢占内核，如果在任务切换时发生中断，而引发内核任务切换
# 后果未知
.align 4
switch_to_by_stack:
	pushl %ebp                      # 入栈保存ebp
	movl %esp,%ebp                  # 将当前的栈指针存放在ebp中，C语言函数调用规范
	pushl %edx						# 入栈保存edx
	pushl %ecx                      # 入栈保存ecx
	pushl %ebx                      # 入栈保存ebx
	pushl %eax                      # 入栈保存eax
	pushl %edi						# 入栈保存edi
	pushl %esi						# 入栈保存esi
	pushfl							# 入栈保存eflags
	push %gs						# 入栈保存gs
	push %fs						# 入栈保存fs
	push %es						# 入栈保存es
	push %ds						# 入栈保存ds
	#
	# 以上代码就是常说的保存现场
	#
	movl 8(%ebp),%ebx               # *(ebp + 8)存放的是pnext, ebp = [EIP, CS, pnext, ldt, cr]
	cmpl %ebx,current               # 判断要切换的任务和当前任务是不是一样
	je 1f                           # 如果一样跳转到1处

	# switch_to PCB
	# 在当前的Linux0.11中，因为不涉及内核抢占，因此可以不用禁止中断
	# 可以注释掉这句话重新编译进行测试，注释掉cli，操作系统仍然运行正常
	# 在进程切换完成后也不用开启中断，因为会通过popfl进行中断标志恢复
	# 如果加上cli，则进程切换不会被打断
	#
	cli
	movl %ebx,%eax                  # pnext赋值给ebx
	xchgl %eax,current              # 交换current和eax，current目前是pnext了
	# rewrite TSS pointer
	movl tss,%ecx                   # 当前tss段地址
	addl $4096,%ebx                 # ebx是pnext（task struct）的顶端，也就是栈顶
	movl %ebx,4(%ecx)               # 4表示esp0的偏移，设置tss的内核态指针为task的顶端
	# switch_to system core stack
	movl %esp,stack_top(%eax)       # 当前esp存放到old task->stack_stop
	movl 8(%ebp),%ebx               # ebx为pnext
	movl stack_top(%ebx),%esp       # 使用pnext恢复栈
	# switch_to LDT
	movl 12(%ebp), %ecx             # 获取局部描述符
	lldt %cx                        # 加载局部描述符
	movl $0x17,%ecx                 # 设置fs为0x17
	mov %cx,%fs
	# get pnext->page dir base
	movl 16(%ebp), %ecx             # 获取CR3的地址
	movl %ecx,%cr3                  # 设置CR3
	
	# nonsense
	cmpl %eax,last_task_used_math 
	jne 1f
	clts
1:   
	#
	# 为什么不保存SS，SP，CS，IP
	# 切换肯定发生内核态，SS不发生变化，而SP在保存在进程的stack_top中
	# CS, IP 通过函数调用call指令已经保存
	# 因此不用保存
	# 为什么要保存eflags，因此eflags保存了一些溢出参数等
	# 如下代码就是常说的恢复现场
	#
	pop %ds
	pop %es
	pop %fs
	pop %gs
	popfl
	# popfl根据进程切换后的进程的eflags进行中断标志及其他标识的恢复
	popl %esi
	popl %edi
	popl %eax
	popl %ebx
	popl %ecx
	popl %edx
	popl %ebp
	ret

.align 4
first_return_from_kernel: 
	iret
	
.align 4
device_not_available:
	push %ds
	push %es
	push %fs
	pushl %edx
	pushl %ecx
	pushl %ebx
	pushl %eax
	movl $0x10,%eax
	mov %ax,%ds
	mov %ax,%es
	movl $0x17,%eax
	mov %ax,%fs
	pushl $ret_from_sys_call
	clts				            # clear TS so that we can use math
	movl %cr0,%eax
	testl $0x4,%eax			        # EM (math emulation bit)
	je math_state_restore
	pushl %ebp
	pushl %esi
	pushl %edi
	call math_emulate
	popl %edi
	popl %esi
	popl %ebp
	ret

.align 4
timer_interrupt:
	push %ds		                # save ds,es and put kernel data space
	push %es		                # into them. %fs is used by _system_call
	push %fs
	pushl %edx		                # we save %eax,%ecx,%edx as gcc doesn't
	pushl %ecx		                # save those across function calls. %ebx
	pushl %ebx		                # is saved as we use that in ret_sys_call
	pushl %eax                      # 以上保持各种寄存器，因为后续调用了ret_from_sys_call因此需要构造一个栈
	movl $0x10,%eax                 # 内核数据段
	mov %ax,%ds                     # ds设置为内核数据段
	mov %ax,%es                     # es设置为内核数据段
	movl $0x17,%eax                 #
	mov %ax,%fs                     # fs设置为用户数据断
	incl jiffies                    # 增加jiffies计数
	movb $0x20,%al		            # EOI to interrupt controller #1，结束中断指令
	outb %al,$0x20                  #
	movl CS(%esp),%eax              # 从堆栈中取出CS的值
	andl $3,%eax		            # %eax is CPL (0 or 3, 0=supervisor)
	pushl %eax                      # eax作为参数入栈
	call do_timer		            # 'do_timer(long CPL)' does everything from
	addl $4,%esp		            # task switching to accounting ... 恢复参数
	jmp ret_from_sys_call

.align 4
sys_execve:
	lea EIP(%esp),%eax              # 取系统调用返回地址的地址
	pushl %eax                      # 将系统调用返回地址的地址入栈作为第一个参数
	call do_execve                  # 执行do_execve调用
	addl $4,%esp                    # 修复栈
	ret                             # 返回

.align 4
sys_fork:
	call find_empty_process         # 寻找一个空的task_struct
	testl %eax,%eax                 # 测试eax是负数还是0，如果是负数或者0，则跳转至1标号
	js 1f
	push %gs                        # push gs esi edi ebp没什么实际意思，只是想将当前被中断的用户进程的数据作为参数传递到copy_process中
	pushl %esi
	pushl %edi
	pushl %ebp
	pushl %eax                      # eax是进程号，也就是find_empty_process的返回值，为数组的下标
	call copy_process               # 入栈，为什么后面是20, 我猜测应该push %gs也是占用4个字节，只是高地址数据无效
	addl $20,%esp                   # 还原栈指针, 因为前面通过栈传递了copy_process的参数
1:	ret                             # 子程序返回

hd_interrupt:
	pushl %eax
	pushl %ecx
	pushl %edx
	push %ds
	push %es
	push %fs
	movl $0x10,%eax
	mov %ax,%ds
	mov %ax,%es
	movl $0x17,%eax
	mov %ax,%fs
	movb $0x20,%al
	outb %al,$0xA0		# EOI to interrupt controller #1
	jmp 1f			    # give port chance to breathe
1:	jmp 1f
1:	xorl %edx,%edx
	xchgl do_hd,%edx
	testl %edx,%edx
	jne 1f
	movl $unexpected_hd_interrupt,%edx
1:	outb %al,$0x20
	call *%edx		    # "interesting" way of handling intr.
	pop %fs
	pop %es
	pop %ds
	popl %edx
	popl %ecx
	popl %eax
	iret

floppy_interrupt:
	pushl %eax
	pushl %ecx
	pushl %edx
	push %ds
	push %es
	push %fs
	movl $0x10,%eax
	mov %ax,%ds
	mov %ax,%es
	movl $0x17,%eax
	mov %ax,%fs
	movb $0x20,%al
	outb %al,$0x20		# EOI to interrupt controller #1
	xorl %eax,%eax
	xchgl do_floppy,%eax
	testl %eax,%eax
	jne 1f
	movl $unexpected_floppy_interrupt,%eax
1:	call *%eax		    # "interesting" way of handling intr.
	pop %fs
	pop %es
	pop %ds
	popl %edx
	popl %ecx
	popl %eax
	iret

parallel_interrupt:
	pushl %eax
	movb $0x20,%al
	outb %al,$0x20
	popl %eax
	iret
	
