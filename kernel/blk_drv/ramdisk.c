/*
 *  linux/kernel/blk_drv/ramdisk.c
 *
 *  Written by Theodore Ts'o, 12/2/91
 */

#include <string.h>

#include <linux/config.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <asm/system.h>
#include <asm/segment.h>
#include <asm/memory.h>

/*
 * 虚拟内存盘主设备号为1，这个宏必须在blk.h之前定义
 * 0x100表示虚拟内存盘
 * 
 */
#define MAJOR_NR 1
#include "blk.h"

char *rd_start;
int	rd_length = 0;

void do_rd_request(void)
{
	int	len;
	char *addr;

	INIT_REQUEST;
	/*
	 * 左移8位，表示乘以512
	 */
	addr = rd_start + (CURRENT->sector << 9);
	len = CURRENT->nr_sectors << 9;
	/*
	 * 如果子设备号不是1，或者地址不在RAMDISK地址空间内，结束该请求
	 */
	if ((MINOR(CURRENT->dev) != 1) || (addr+len > rd_start+rd_length)) {
		end_request(0);
		goto repeat;
	}
	/*
	 * 如果是写，将buffer数据拷贝到addr处
	 * 如果是读，将addr数据拷贝到buffer处
	 * 一共拷贝len个字节
	 */
	if (CURRENT-> cmd == WRITE) {
		(void) memcpy(addr, CURRENT->buffer, len);
	} else if (CURRENT->cmd == READ) {
		(void) memcpy(CURRENT->buffer, addr, len);
	} else
		panic("unknown ramdisk-command");
	/*
	 * 成功后设置更新标志
	 */
	end_request(1);
	goto repeat;
}

/*
 * Returns amount of memory which needs to be reserved.
 * 确定虚拟盘在内存中的起始地址，长度，并对整个空间清零
 * 对于16MB的系统，mem_start为4MB
 * length由RAMDIST_SIZE确定，RAMDIST_SIZE的单位为KB
 */
long rd_init(long mem_start, int length)
{
	int	i;
	char *cp;

	blk_dev[MAJOR_NR].request_fn = DEVICE_REQUEST;
	rd_start = (char *) mem_start;
	rd_length = length;
	cp = rd_start;
	/*
	 * 每个字节都设置为0
	 */
	for (i=0; i < length; i++)
		*cp++ = '\0';
	return(length);
}

#ifdef RAMDISK_START
#define ramdisk_start RAMDISK_START
#else
#define ramdisk_start 256 /* Start at block 256 by default */
#endif

/*
 * If the root device is the ram disk, try to load it.
 * In order to do this, the root device is originally set to the
 * floppy, and we later change it to be ram disk.
 * 如果是软盘启动，并且定了RAMDISK，则尝试从软盘中加载数据到RAMDISK中
 * 经过实验RAMDISK功能在bochs上可以成功，但是使用qemu不能成功，提示如下
 * [0000000003] Ram disk: 2048 KB, starting at 0x400000
 * [0000000054] Loading 1474560 bytes into ram disk...
 * [0000000055] bad_flp_intr
 * [0000000056] Reset-floppy called
 * [0000000056] bad_flp_intr
 * [0000000056] Reset-floppy called
 * 先不管了，记录一下
 */
void rd_load(void)
{
	struct buffer_head *bh;
	struct super_block s;
	int	 block = ramdisk_start;
	int	 i = 1;
	int	 nblocks;
	char *cp;		/* Move pointer */
	
	if (!rd_length)
		return;
	printk("Ram disk: %d KB, starting at 0x%x\n", rd_length/1024, (int) rd_start);
	/*
	 * 如果不是软盘，软盘的主设备号为2
	 */
	if (MAJOR(ROOT_DEV) != 2) {
		printk("Ram disk: not floppy\n");
		return;
	}
	/*
	 * 读取256+1，256+2
	 */
	bh = breada(ROOT_DEV, block+1, block, block+2, -1);
	if (!bh) {
		printk("Disk error while looking for ramdisk!\n");
		return;
	}
	*((struct d_super_block *) &s) = *((struct d_super_block *) bh->b_data);
	brelse(bh);
	/* No ram disk image present, assume normal floppy boot */
	/*
	 * 不是软盘镜像
	 */
	if (s.s_magic != SUPER_MAGIC) {
		printk("Ram disk: magic error %x\n", s.s_magic);
		return;
	}
	/*
	 * 如果数据块数大于虚拟内存所能容纳的数量，也不能进行RAMDISK
	 */
	nblocks = s.s_nzones << s.s_log_zone_size;
	if (nblocks > (rd_length >> BLOCK_SIZE_BITS)) {
		printk("Ram disk image too big!  (%d blocks, %d avail)\n", 
			nblocks, rd_length >> BLOCK_SIZE_BITS);
		return;
	}
	printk("Loading %d bytes into ram disk...\n", 
		nblocks << BLOCK_SIZE_BITS);
	/*
	 * cp指向虚拟盘起始地址，然后将软盘的根文件系统拷贝到RAMDISK上
	 */
	cp = rd_start;
	while (nblocks) {
		if (nblocks > 2) 
			bh = breada(ROOT_DEV, block, block+1, block+2, -1);
		else
			bh = bread(ROOT_DEV, block);
		if (!bh) {
			printk("I/O error on block %d, aborting load\n", 
				block);
			return;
		}
		(void) memcpy(cp, bh->b_data, BLOCK_SIZE);
		brelse(bh);
		printk("Loading %d blocks\n", i); 
		cp += BLOCK_SIZE;
		block++;
		nblocks--;
		i++;
	}
	printk("Loading %d bytes into ram disk... Done\n", 
		nblocks << BLOCK_SIZE_BITS);
	ROOT_DEV=0x0101;
}
