/*
 *  linux/fs/inode.c
 *
 *  (C) 1991  Linus Torvalds
 */

#include <string.h> 
#include <sys/stat.h>

#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <asm/system.h>

/*
 * 内存中的i节点指针，一共32个
 */
struct m_inode inode_table[NR_INODE]={{0,},};

static void read_inode(struct m_inode * inode);
static void write_inode(struct m_inode * inode);

/*
 * 等待指定的i节点
 */
static inline void wait_on_inode(struct m_inode * inode)
{
	cli();
	while (inode->i_lock)
		sleep_on(&inode->i_wait);
	sti();
}

/*
 * 对指定的i节点上锁
 */
static inline void lock_inode(struct m_inode * inode)
{
	cli();
	while (inode->i_lock)
		sleep_on(&inode->i_wait);
	inode->i_lock=1;
	sti();
}

/*
 * 对指定的i节点解锁
 */
static inline void unlock_inode(struct m_inode * inode)
{
	inode->i_lock=0;
	wake_up(&inode->i_wait);
}

/*
 * 释放内存中设备dev的i节点
 */
void invalidate_inodes(int dev)
{
	int i;
	struct m_inode * inode;

	inode = 0 + inode_table;
	/*
	 * 扫描inode_table
	 */
	for(i = 0; i < NR_INODE; i++, inode++) {
		wait_on_inode(inode);
		if (inode->i_dev == dev) {
			if (inode->i_count)
				printk("inode in use on removed disk\n\r");
			inode->i_dev = inode->i_dirt = 0;
		}
	}
}

void sync_inodes(void)
{
	int i;
	struct m_inode * inode;

	inode = 0 + inode_table;
	for(i=0; i < NR_INODE; i++, inode++) {
		wait_on_inode(inode);
		if (inode->i_dirt && !inode->i_pipe)
			write_inode(inode);
	}
}

/*
 * 文件数据映射到盘块的处理操作
 * 如果create为置位，则对应逻辑快不存在应该申请新的逻辑快
 * 返回根据block数据块对应在设备上的逻辑块号
 */
static int _bmap(struct m_inode * inode, int block, int create)
{
	struct buffer_head * bh;
	int i;

	/*
	 * 判断块大小的有效性
	 */
	if (block < 0)
		panic("_bmap: block<0");
	if (block >= 7+512+512*512)
		panic("_bmap: block>big");
	/*
	 * 如果块号小于7，则用直接块表示
	 */
	if (block < 7) {
		/*
		 * 如果定了create标志并且i节点中对应逻辑块字段为0
		 * 则向设备申请一个块
		 * 最后返回逻辑块号
		 */
		if (create && !inode->i_zone[block])
			if ((inode->i_zone[block] = new_block(inode->i_dev))) {
				inode->i_ctime = CURRENT_TIME;
				inode->i_dirt = 1;
			}
		return inode->i_zone[block];
	}
	/*
	 * 如果块号大于7并且小于512表示是第一次间接块
	 */
	block -= 7;
	if (block < 512) {
		/*
		 * 如果定义了create，并且是首次使用第一次间接块
		 * 则需申请一个块存放间接块信息
		 */
		if (create && !inode->i_zone[7])
			if ((inode->i_zone[7] = new_block(inode->i_dev))) {
				inode->i_dirt = 1;
				inode->i_ctime = CURRENT_TIME;
			}
		/*
		 * 申请块失败
		 */
		if (!inode->i_zone[7])
			return 0;
		/*
		 * 读申请的块数据存放在bh中
		 */
		if (!(bh = bread(inode->i_dev, inode->i_zone[7])))
			return 0;
		/*
		 * 取间接块的block的逻辑块号
		 */
		i = ((unsigned short *) (bh->b_data))[block];
		/*
		 * 如果是create或者逻辑块号为0，则创建
		 * 并设置已修改标记，释放缓冲区
		 */
		if (create && !i)
			if ((i = new_block(inode->i_dev))) {
				((unsigned short *) (bh->b_data))[block] = i;
				bh->b_dirt = 1;
			}
		brelse(bh);
		return i;
	}
	/*
	 * 第二间接块，和第一间接块的处理相同
	 */
	block -= 512;
	if (create && !inode->i_zone[8])
		if ((inode->i_zone[8] = new_block(inode->i_dev))) {
			inode->i_dirt = 1;
			inode->i_ctime = CURRENT_TIME;
		}
	if (!inode->i_zone[8])
		return 0;
	if (!(bh = bread(inode->i_dev,inode->i_zone[8])))
		return 0;
	i = ((unsigned short *)bh->b_data)[block>>9];
	if (create && !i)
		if ((i = new_block(inode->i_dev))) {
			((unsigned short *) (bh->b_data))[block>>9]=i;
			bh->b_dirt=1;
		}
	brelse(bh);
	if (!i)
		return 0;
	if (!(bh = bread(inode->i_dev,i)))
		return 0;
	/*
	 * block&511为为了限制其大小最大为511
	 */
	i = ((unsigned short *)bh->b_data)[block&511];
	if (create && !i)
		if ((i = new_block(inode->i_dev))) {
			((unsigned short *) (bh->b_data))[block&511]=i;
			bh->b_dirt = 1;
		}
	brelse(bh);
	return i;
}

/*
 * 根据i节点信息获取文件数据块block在设备上对应的逻辑块
 */
int bmap(struct m_inode * inode, int block)
{
	return _bmap(inode, block, 0);
}

/*
 * 创建文件数据块block在设备上的逻辑块，并返回逻辑块号
 */
int create_block(struct m_inode * inode, int block)
{
	return _bmap(inode, block, 1);
}
		
/*
 * 释放一个i节点，回写入设备
 */		
void iput(struct m_inode * inode)
{
	if (!inode)
		return;
	wait_on_inode(inode);
	if (!inode->i_count)
		panic("iput: trying to free free inode");
	if (inode->i_pipe) {
		wake_up(&inode->i_wait);
		if (--inode->i_count)
			return;
		free_page(inode->i_size);
		inode->i_count=0;
		inode->i_dirt=0;
		inode->i_pipe=0;
		return;
	}
	if (!inode->i_dev) {
		inode->i_count--;
		return;
	}
	if (S_ISBLK(inode->i_mode)) {
		sync_dev(inode->i_zone[0]);
		wait_on_inode(inode);
	}
repeat:
	if (inode->i_count>1) {
		inode->i_count--;
		return;
	}
	if (!inode->i_nlinks) {
		truncate(inode);
		free_inode(inode);
		return;
	}
	if (inode->i_dirt) {
		write_inode(inode);	/* we can sleep - so do again */
		wait_on_inode(inode);
		goto repeat;
	}
	inode->i_count--;
	return;
}

/*
 * 从i节点列表中获取一个空闲i节点
 */
struct m_inode * get_empty_inode(void)
{
	struct m_inode * inode;
	static struct m_inode * last_inode = inode_table;
	int i;

	do {
		inode = NULL;
		/*
		 * 从最后一项开始
		 */
		for (i = NR_INODE; i; i--) {
			/*
			 * 如果last_inode已经指向i节点表的最后一项
			 * 则让其重新指向i节点的开始处
			 */
			if (++last_inode >= inode_table + NR_INODE)
				last_inode = inode_table;
			/*
			 * i_count为0表示可能是空闲项
			 * 如果i节点的已修改和锁定标志均为0，则退出
			 */
			if (!last_inode->i_count) {
				inode = last_inode;
				if (!inode->i_dirt && !inode->i_lock)
					break;
			}
		}
		/*
		 * 如果没有找到i节点，打印调试信息，然后系统panic
		 */
		if (!inode) {
			for (i=0 ; i<NR_INODE ; i++)
				printk("%04x: %6d\t",inode_table[i].i_dev,
					inode_table[i].i_num);
			panic("No free inodes in mem");
		}
		/*
		 * 等待i节点
		 */
		wait_on_inode(inode);
		while (inode->i_dirt) {
			write_inode(inode);
			wait_on_inode(inode);
		}
	} while (inode->i_count);
	/*
	 * 将i节点的数据清零，并设置计数
	 */
	memset(inode,0,sizeof(*inode));
	inode->i_count = 1;
	return inode;
}

struct m_inode * get_pipe_inode(void)
{
	struct m_inode * inode;

	if (!(inode = get_empty_inode()))
		return NULL;
	if (!(inode->i_size = get_free_page())) {
		inode->i_count = 0;
		return NULL;
	}
	inode->i_count = 2;	/* sum of readers/writers */
	PIPE_HEAD(*inode) = PIPE_TAIL(*inode) = 0;
	inode->i_pipe = 1;
	return inode;
}

/*
 * dev表示设备，nr表示i节点号
 */
struct m_inode * iget(int dev, int nr)
{
	struct m_inode * inode, * empty;

	if (!dev)
		panic("iget with dev==0");
	/*
	 * 从inodetabale中获取一个空的inode
	 */
	empty = get_empty_inode();
	inode = inode_table;
	/*
	 * 扫描inode_table找到i节点的dev和nr为特定值的i节点
	 */
	while (inode < NR_INODE + inode_table) {
		/*
		 * 判断设备号和i节点编号是否为指定的值
		 * 如果不是，继续尝试下一个inode
		 */
		if (inode->i_dev != dev || inode->i_num != nr) {
			inode++;
			continue;
		}
		/*
		 * 在这里说明已经找到了指定的i节点
		 * 等待inode解锁，这里可能发生睡眠
		 */
		wait_on_inode(inode);
		/*
		 * 确保在等待期间i节点信息没有发生变化
		 * 如果发生变化，将inode_table赋值给inode并继续寻找
		 */
		if (inode->i_dev != dev || inode->i_num != nr) {
			inode = inode_table;
			continue;
		}
		/*
		 * 增加引用计数
		 */
		inode->i_count++;
		/*
		 * 如果inode的i_mount有值说明这个i节点挂载了其他分区
		 */
		if (inode->i_mount) {
			int i;
			/*
			 * 扫描超级块并找到inode挂载的超级块
			 */
			for (i = 0; i < NR_SUPER; i++)
				if (super_block[i].s_imount == inode)
					break;
			if (i >= NR_SUPER) {
				printk("Mounted inode hasn't got sb\n");
				if (empty)
					iput(empty);
				return inode;
			}
			/*
			 * 将该i节点写盘
			 * 从安装在此i节点的超级块中获取设备号
			 * 从新使用新的设备从开始进行扫描
			 */
			iput(inode);
			dev = super_block[i].s_dev;
			nr = ROOT_INO;
			inode = inode_table;
			continue;
		}
		/*
		 * 放弃临时i节点
		 */
		if (empty)
			iput(empty);
		return inode;
	}
	if (!empty)
		return (NULL);
	inode = empty;
	inode->i_dev = dev;
	inode->i_num = nr;
	read_inode(inode);
	return inode;
}

/*
 * 从设备上读取指定i节点信息到缓冲区中
 */
static void read_inode(struct m_inode * inode)
{
	struct super_block * sb;
	struct buffer_head * bh;
	int block;

	/*
	 * 锁定i节点并获取超级块
	 */
	lock_inode(inode);
	if (!(sb = get_super(inode->i_dev)))
		panic("trying to read inode without dev");
	/*
	 * 该i节点所在的逻辑块号
	 * 启动块+超级块+imap_blocks+zmap_blocks
	 * (inode->i_num-1)/INODES_PER_BLOCK表示一个块中存放多个inode
	 * 这个意思是这个indo在第几个块中
	 */
	block = 2 + sb->s_imap_blocks + sb->s_zmap_blocks +
		(inode->i_num-1)/INODES_PER_BLOCK;
	/*
	 * 从逻辑块block读取一块数据，返回值为高速缓存结构体
	 */
	if (!(bh = bread(inode->i_dev, block)))
		panic("unable to read i-node block");
	/*
	 * 使用磁盘的i节点信息填充inode结构体
	 */
	*(struct d_inode *)inode =
		((struct d_inode *)bh->b_data)
			[(inode->i_num-1)%INODES_PER_BLOCK];
	brelse(bh);
	unlock_inode(inode);
}

/*
 * 将i节点信息写入设备，写入缓冲区中，缓冲区刷新时会写入磁盘中
 */
static void write_inode(struct m_inode * inode)
{
	struct super_block * sb;
	struct buffer_head * bh;
	int block;

	lock_inode(inode);
	/*
	 * 如果该i节点没有被修改过，或者i节点对应的设备号为0，则直接退出
	 */
	if (!inode->i_dirt || !inode->i_dev) {
		unlock_inode(inode);
		return;
	}
	if (!(sb = get_super(inode->i_dev)))
		panic("trying to write inode without device");
	/*
	 * 参考read_inode或者改inode对应的逻辑块号
	 */
	block = 2 + sb->s_imap_blocks + sb->s_zmap_blocks +
		(inode->i_num-1)/INODES_PER_BLOCK;
	/*
	 * 从该逻辑块读数据
	 */
	if (!(bh = bread(inode->i_dev, block)))
		panic("unable to read i-node block");
	/*
	 * 使用内存的inode数据填充将要刷磁盘的高速缓存中
	 */
	((struct d_inode *)bh->b_data)
		[(inode->i_num-1)%INODES_PER_BLOCK] =
			*(struct d_inode *)inode;
	/*
	 * 设置告诉缓存已修改标记，后续后刷到磁盘中
	 */
	bh->b_dirt=1;
	inode->i_dirt=0;
	brelse(bh);
	unlock_inode(inode);
}
