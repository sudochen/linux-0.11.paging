# Linux0.11
将Linux0.11修改为4G的虚拟内存模式，0-3G用户空间，3G-4G内核空间.  
可以挂载oldlinux上的文件系统.  
源代码基线是在网上找的，具体已经不知道出处了.  
纯粹是为了学习记录，如果有侵权请联系删除sudochen@163.com. 
可以在vmware中运行，使用命令如下命令制作vmdisk
qemu-img convert -f raw -O vmdk rootfs/hdc-0.11.img rootfs/hdc-0.11.vmdk
# 编译
进入rootfs目录，将所有的tar.bz2解压  
执行make命令编译，默认使用内核堆栈进行任务切换，serial作为stdio等  
执行make qemu使用qemu仿真，serial重定向到当前终端的标准输入输出上  
执行make help获取更多信息  
# 启动信息 make qemu
```
qemu-system-i386 -nographic -serial mon:stdio -m 64M -boot a -fda Image -fdb ./rootfs/rootimage-0.11.img -hda ./rootfs/hdc-0.11.img 
WARNING: Image format was not specified for 'Image' and probing guessed raw.
         Automatically detecting the format is dangerous for raw images, write operations on block 0 will be restricted.
         Specify the 'raw' format explicitly to remove the restrictions.
WARNING: Image format was not specified for './rootfs/rootimage-0.11.img' and probing guessed raw.
         Automatically detecting the format is dangerous for raw images, write operations on block 0 will be restricted.
         Specify the 'raw' format explicitly to remove the restrictions.
WARNING: Image format was not specified for './rootfs/hdc-0.11.img' and probing guessed raw.
         Automatically detecting the format is dangerous for raw images, write operations on block 0 will be restricted.
         Specify the 'raw' format explicitly to remove the restrictions.
[0000000000] params a=6 b=7 c=8
[0000000000] mem_start is 6MB
[0000000000] men_end is 16MB
[0000000000] system has 2560 pages omg
[0000000000] ramdisk size is 2MB
[0000000000] task switch use KERNEL STACK
[0000000000] init_task use GTD[4] for TSS
[0000000000] init_task use GTD[5] for LDT
[0000000000] Enable timer_interrupt
[0000000001] Enable system_call
[0000000001] Mem-info 4096 pages:
[0000000001] Buffer blocks: 3413 blocks(1KB) 3MB
[0000000001] Tatal pages: 4096 pages(4KB) 16MB
[0000000001] Free pages: 2560 pages(4KB) 10MB
[0000000001] Reserved pages: 1536 pages(4KB) 6MB
[0000000002] Shared pages: 0 pages(4KB) 0MB
[0000000002] HD_TYPE undefined, Query by BIOS
[0000000003] Query 0 HardDisk
[0000000003] Query 1 HardDisk
[0000000003] hd[0] start_sect 0 nr_sects 121968
[0000000004]    hd[1] start_sect 1 nr_sects 120959
[0000000004]    hd[2] start_sect 0 nr_sects 0
[0000000005]    hd[3] start_sect 0 nr_sects 0
[0000000005]    hd[4] start_sect 0 nr_sects 0
[0000000005] Partition table ok.
[0000000006] Ram disk: 2048 KB, starting at 0x400000
[0000000006] Ram disk: not floppy
[0000000006] read_super bread dev is 0x301
[0000000006] SuperBlock sectors is 3 0x600 offset in disk
[0000000006] read_super s_imap_blocks for inodes bit map is 3
[0000000006] read_super s_zmap_blocks for data block bit map is 8
[0000000007] mount_root 47933/60000 free blocks
[0000000007] mount_root 19305/20000 free inodes
[0000000008] mount_root 638 is firstdatazone
3413 buffers = 3494912 bytes buffer space
Free mem: 10485760 bytes
init current pid is 1
init fork current pid is 2
Ok. Rootfs is HD
[/usr/root]# ls
README	      gcclib140	    hello.c	  mtools.howto
```
