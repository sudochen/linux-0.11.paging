/*
 *  linux/fs/super.c
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 * super.c contains code to handle the super-block tables.
 */
#include <linux/config.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <asm/system.h>

#include <errno.h>
#include <sys/stat.h>

int sync_dev(int dev);
void wait_for_keypress(void);

/* set_bit uses setb, as gas doesn't recognize setc */
/*
 * 原名字为set_bit，但是test_bit更合适
 */
#define test_bit(bitnr,addr) ({ \
register int __res ; \
__asm__("bt %2,%3;setb %%al":"=a" (__res):"a" (0),"r" (bitnr),"m" (*(addr))); \
__res; })

struct super_block super_block[NR_SUPER];
/* this is initialized in init/main.c */
int ROOT_DEV = 0;

/*
 * 获取超级块使用权限
 */
static void lock_super(struct super_block * sb)
{
	cli();
	while (sb->s_lock)
		sleep_on(&(sb->s_wait));
	sb->s_lock = 1;
	sti();
}

/*
 * 释放超级块使用权限
 */
static void free_super(struct super_block * sb)
{
	cli();
	sb->s_lock = 0;
	wake_up(&(sb->s_wait));
	sti();
}

/*
 * 等待超级块使用权限
 */
static void wait_on_super(struct super_block * sb)
{
	cli();
	while (sb->s_lock)
		sleep_on(&(sb->s_wait));
	sti();
}

/*
 * 获取指定设备的超级块，没有则返回空指针
 */
struct super_block * get_super(int dev)
{
	struct super_block * s;

	if (!dev)
		return NULL;
	s = 0 + super_block;
	while (s < NR_SUPER + super_block)
		if (s->s_dev == dev) {
			wait_on_super(s);
			if (s->s_dev == dev)
				return s;
			s = 0 + super_block;
		} else
			s++;
	return NULL;
}

/*
 * 释放指定的超级块
 */
void put_super(int dev)
{
	struct super_block * sb;
	/* struct m_inode * inode;*/
	int i;

	if (dev == ROOT_DEV) {
		printk("root diskette changed: prepare for armageddon\n\r");
		return;
	}
	if (!(sb = get_super(dev)))
		return;
	if (sb->s_imount) {
		printk("Mounted disk changed - tssk, tssk\n\r");
		return;
	}
	lock_super(sb);
	sb->s_dev = 0;
	for(i=0;i<I_MAP_SLOTS;i++)
		brelse(sb->s_imap[i]);
	for(i=0;i<Z_MAP_SLOTS;i++)
		brelse(sb->s_zmap[i]);
	free_super(sb);
	return;
}

static struct super_block * read_super(int dev)
{
	struct super_block * s;
	struct buffer_head * bh;
	int i,block;

	if (!dev)
		return NULL;
	check_disk_change(dev);
	/*
	 * 根据dev获取超级块，没有则进行申请
	 * super_block是一个8个结构体数组
	 * dev是设备号，包含了主设备号和次设备号
	 */
	if ((s = get_super(dev)))
		return s;
	/* 
	 * 如果没有找到，在在super_block数组里找到一个空闲的
	 * 找到一个空闲的超级块
	 */
	for (s = 0 + super_block; ;s++) {
		if (s >= NR_SUPER + super_block)
			return NULL;
		if (!s->s_dev)
			break;
	}
	/*
	 * s_dev设备号，如果是硬盘的第一个分区则是0x301
	 */
	s->s_dev = dev;
	s->s_isup = NULL;
	s->s_imount = NULL;
	s->s_time = 0;
	s->s_rd_only = 0;
	s->s_dirt = 0;

	/*
	 * 下面的代码会从根文件系统读出超级块信息填充到s
	 */
	lock_super(s);
	
	printk("[%s] bread dev is 0x%x\n", __func__, dev);
	/*
	 * bread从设备上读取第一个块，
	 * 第0个块成为引导块，保留使用
	 * 第1个块是超级块，也就是bread的第二个参数
	 * 
	 */
	if (!(bh = bread(dev, 1))) {
		s->s_dev=0;
		free_super(s);
		return NULL;
	}
	/*
	 * 使用磁盘的超级块数据填充内存的超级块结构体中
	 */
	*((struct d_super_block *) s) =
		*((struct d_super_block *) bh->b_data);
	brelse(bh);
	/*
	 * 判断超级块的magic number是否为0x13f7
	 * 表示是否为MINIX文件系统，
	 * 后来mkfs.minix修改为0x13f8
	 */
	if (s->s_magic != SUPER_MAGIC) {
		s->s_dev = 0;
		free_super(s);
		return NULL;
	}
	/*
	 * 清除i节点位图和逻辑块位图
	 */
	for (i=0; i<I_MAP_SLOTS; i++)
		s->s_imap[i] = NULL;
	for (i=0; i<Z_MAP_SLOTS; i++)
		s->s_zmap[i] = NULL;
	/*
	 * 读i节点和逻辑位图节点信息
	 * s_imap_blocks表示i节点位图使用多个块保存，做多8个
	 * s_zmap_blocks表示逻辑块位图使用多个个块保存，最多8个
	 */
	block = 2;
	printk("%s s_imap_blocks for inodes bit map is %x\n", __func__, s->s_imap_blocks);
	printk("%s s_zmap_blocks for data block bit map is %x\n", __func__, s->s_zmap_blocks);

	for (i=0 ; i < s->s_imap_blocks ; i++)
		if ((s->s_imap[i] = bread(dev, block)))
			block++;
		else
			break;
	for (i=0 ; i < s->s_zmap_blocks ; i++)
		if ((s->s_zmap[i] = bread(dev, block)))
			block++;
		else
			break;
	/*
	 * 检验block数量对还是不对
	 */
	if (block != 2 + s->s_imap_blocks + s->s_zmap_blocks) {
		for(i = 0; i < I_MAP_SLOTS; i++)
			brelse(s->s_imap[i]);
		for(i = 0; i < Z_MAP_SLOTS; i++)
			brelse(s->s_zmap[i]);
		s->s_dev=0;
		free_super(s);
		return NULL;
	}
	/*
	 * i节点位图和逻辑块位图的0位图都不可能为0
	 * 因为很多相关函数中，0意味着失败
	 * 对于申请空闲i节点或者空闲数据块的函数来讲，返回0表示失败
	 * 因此对于imap和zmap都将bit0设置为1，以防止分配0号节点
	 */
	s->s_imap[0]->b_data[0] |= 1;
	s->s_zmap[0]->b_data[0] |= 1;
	free_super(s);
	return s;
}

int sys_umount(char * dev_name)
{
	struct m_inode * inode;
	struct super_block * sb;
	int dev;

	if (!(inode=namei(dev_name)))
		return -ENOENT;
	dev = inode->i_zone[0];
	if (!S_ISBLK(inode->i_mode)) {
		iput(inode);
		return -ENOTBLK;
	}
	iput(inode);
	if (dev==ROOT_DEV)
		return -EBUSY;
	if (!(sb=get_super(dev)) || !(sb->s_imount))
		return -ENOENT;
	if (!sb->s_imount->i_mount)
		printk("Mounted inode has i_mount=0\n");
	for (inode=inode_table+0 ; inode<inode_table+NR_INODE ; inode++)
		if (inode->i_dev==dev && inode->i_count)
				return -EBUSY;
	sb->s_imount->i_mount=0;
	iput(sb->s_imount);
	sb->s_imount = NULL;
	iput(sb->s_isup);
	sb->s_isup = NULL;
	put_super(dev);
	sync_dev(dev);
	return 0;
}

int sys_mount(char * dev_name, char * dir_name, int rw_flag)
{
	struct m_inode * dev_i, * dir_i;
	struct super_block * sb;
	int dev;

	if (!(dev_i=namei(dev_name)))
		return -ENOENT;
	dev = dev_i->i_zone[0];
	if (!S_ISBLK(dev_i->i_mode)) {
		iput(dev_i);
		return -EPERM;
	}
	iput(dev_i);
	if (!(dir_i=namei(dir_name)))
		return -ENOENT;
	if (dir_i->i_count != 1 || dir_i->i_num == ROOT_INO) {
		iput(dir_i);
		return -EBUSY;
	}
	if (!S_ISDIR(dir_i->i_mode)) {
		iput(dir_i);
		return -EPERM;
	}
	if (!(sb=read_super(dev))) {
		iput(dir_i);
		return -EBUSY;
	}
	if (sb->s_imount) {
		iput(dir_i);
		return -EBUSY;
	}
	if (dir_i->i_mount) {
		iput(dir_i);
		return -EPERM;
	}
	sb->s_imount=dir_i;
	dir_i->i_mount=1;
	dir_i->i_dirt=1;		/* NOTE! we don't iput(dir_i) */
	return 0;			/* we do that in umount */
}

void mount_root(void)
{
	int i,free;
	struct super_block * p;
	struct m_inode * mi;

	/*
	 * 磁盘上的i节点必须为32个字节
	 */
	if (32 != sizeof(struct d_inode))
		panic("bad i-node size");
	
	/*
	 * 初始化文件表数组
	 */
	for(i = 0; i < NR_FILE; i++)
		file_table[i].f_count=0;

	/*
	 * 如果是软盘，提示用户插入文件系统软盘并输入Enter
	 */
	if (MAJOR(ROOT_DEV) == 2) {
		printk("Insert root floppy and press ENTER");
		wait_for_keypress();
	}

	/*
	 * 初始化超级块数组，一共8项
	 */
	for(p = &super_block[0]; p < &super_block[NR_SUPER]; p++) {
		p->s_dev = 0;
		p->s_lock = 0;
		p->s_wait = NULL;
	}

	/*
	 * 从根文件系统上读取超级块，如果失败则panic 
	 * p是super_block超级块
	 */
	if (!(p = read_super(ROOT_DEV)))
		panic("Unable to mount root");
	
	/*
	 * 从设备上获取根i节点
	 */
	if (!(mi = iget(ROOT_DEV, ROOT_INO)))
		panic("Unable to read root i-node");
	/*
	 * 从逻辑上讲，该i节点引用增加了4次
	 * 下面分析
	 */
	mi->i_count += 4 ;	/* NOTE! it is logically used 4 times, not 1 */
	/*
	 * i节点安装到超级块中，引用+1
	 */
	p->s_isup = p->s_imount = mi;
	/*
	 * 引用+1
	 */
	current->pwd = mi;
	/*
	 * 引用+1
	 */
	current->root = mi;
	free=0;
	/*
	 * 统计该设备上的空闲块数，先让i等于该设备总的逻辑块数
	 * s_nzones表示总逻辑块数
	 * s_zmap逻辑块位图缓冲块指针数组，8个块
	 * s_ninodes表示节点数
	 * s_imap节点位图缓冲块指针数组，8个块
	 * 为什么是8191呢？
	 * 因为一个高速缓冲区是1024个字节，共8192位
	 * i&8191算的是在高速缓冲区的偏移，而i>>13是使用哪个高速缓存作为索引
	 * 下面inodes是一样的道理
	 * 每一个bit代表一个块，一共8192*8个bit，一个块1K
	 * minix文件系统一共可管理8192*8*1K = 64M
	 * minix文件系统最大64M
	 * inode代表一个文件或者文件夹
	 */
	i = p->s_nzones;
	while (--i >= 0)
		if (!test_bit(i&8191, p->s_zmap[i>>13]->b_data))
			free++;
	printk("%s %d/%d free blocks\n\r", __func__, free, p->s_nzones);
	/*
	 * 重置free，计算inode
	 * s_ninodes为什么要加1???不明白，书上说是要把0节点统计进去
	 */
	free=0;
	i = p->s_ninodes + 1;
	while (--i >= 0)
		if (!test_bit(i&8191,p->s_imap[i>>13]->b_data))
			free++;
	printk("%s %d/%d free inodes\n\r", __func__, free, p->s_ninodes);
	printk("%s %d is firstdatazone\n\r", __func__, p->s_firstdatazone);
}
