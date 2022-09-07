/*
 *  linux/fs/bitmap.c
 *
 *  (C) 1991  Linus Torvalds
 */

/* bitmap.c contains the code that handles the inode and block bitmaps */
#include <string.h>

#include <linux/sched.h>
#include <linux/kernel.h>

/*
 * 将addr处的数据清零，清256个字节
 */
#define clear_block(addr) \
__asm__ __volatile__ ("cld\n\t" \
	"rep\n\t" \
	"stosl" \
	::"a" (0),"c" (BLOCK_SIZE/4),"D" ((long) (addr)))

/*
 * 将addr地址处的第nr个bit置位
 */
#define set_bit(nr,addr) ({\
register int res ; \
__asm__ __volatile__("btsl %2,%3\n\tsetb %%al": \
"=a" (res):"0" (0),"r" (nr),"m" (*(addr))); \
res;})

/*
 * 将addr地址处的第nr个bit清零
 */
#define clear_bit(nr,addr) ({\
register int res ; \
__asm__ __volatile__("btrl %2,%3\n\tsetnb %%al": \
"=a" (res):"0" (0),"r" (nr),"m" (*(addr))); \
res;})

/*
 * 在addr地址处寻找第一个为0的bit，返回nr
 */
#define find_first_zero(addr) ({ \
int __res; \
__asm__ __volatile__ ("cld\n" \
	"1:\tlodsl\n\t" \
	"notl %%eax\n\t" \
	"bsfl %%eax,%%edx\n\t" \
	"je 2f\n\t" \
	"addl %%edx,%%ecx\n\t" \
	"jmp 3f\n" \
	"2:\taddl $32,%%ecx\n\t" \
	"cmpl $8192,%%ecx\n\t" \
	"jl 1b\n" \
	"3:" \
	:"=c" (__res):"c" (0),"S" (addr)); \
__res;})

/*
 * 释放设备dev上数据区中的逻辑块block
 * 复位指定逻辑块block的逻辑块位图bit
 */
void free_block(int dev, int block)
{
	struct super_block * sb;
	struct buffer_head * bh;

	/*
	 * 获取设备的超级块，不存在在panic
	 * 超级块存放了文件系统的所有信息
	 */
	if (!(sb = get_super(dev)))
		panic("trying to free block on nonexistent device");
	/*
	 * 如果逻辑块小于首个数据逻辑块或者大于设备的总逻辑块，panic
	 */
	if (block < sb->s_firstdatazone || block >= sb->s_nzones)
		panic("trying to free block not in datazone");
	/*
	 * 从高速缓存hash表中寻找该数据，如果找到了则判断其有效性
	 * 并清除其已修改和更新标志，释放该数据块
	 * 该代码的作用是如果该逻辑块在当前高速缓存中，就释放对应的缓冲块
	 */
	bh = get_hash_table(dev,block);
	if (bh) {
		if (bh->b_count != 1) {
			printk("trying to free block (%04x:%d), count=%d\n",
				dev,block,bh->b_count);
			return;
		}
		bh->b_dirt=0;
		bh->b_uptodate=0;
		brelse(bh);
	}
	/*
	 * 计算block在数据区开始算起的数据逻辑块号
	 * 然后对逻辑块位图进行操作
	 * 复位对应的比特位，如果原来就是0，则panic
	 */
	block -= sb->s_firstdatazone - 1 ;
	if (clear_bit(block&8191,sb->s_zmap[block/8192]->b_data)) {
		printk("block (%04x:%d) ",dev,block+sb->s_firstdatazone-1);
		panic("free_block: bit already cleared");
	}
	/*
	 * 置相应逻辑块位图所在缓冲区已修改标志
	 */
	sb->s_zmap[block/8192]->b_dirt = 1;
}

/*
 * 向设备dev申请一个逻辑块，返回逻辑块号
 * 置位相应逻辑块号的bit
 */
int new_block(int dev)
{
	struct buffer_head * bh;
	struct super_block * sb;
	int i,j;

	/*
	 * 获取超级块
	 */
	if (!(sb = get_super(dev)))
		panic("trying to get new block from nonexistant device");
	
	/*
	 * 遍历逻辑块位图，找到第一个为0的bit
	 */
	j = 8192;
	for (i=0 ; i<8 ; i++)
		if ((bh=sb->s_zmap[i]))
			if ((j=find_first_zero(bh->b_data))<8192)
				break;
	if (i>=8 || !bh || j>=8192)
		return 0;
	/*
	 * 将逻辑块bit位置位，如果原来就是1则panic
	 */
	if (set_bit(j,bh->b_data))
		panic("new_block: bit already set");
	/*
	 * 设置缓冲区脏标记
	 */
	bh->b_dirt = 1;
	/*
	 * 如果逻辑块大于设备总的逻辑块则失败
	 */
	j += i*8192 + sb->s_firstdatazone-1;
	if (j >= sb->s_nzones)
		return 0;
	/*
	 * 获取该设备的该新逻辑块数据，如果失败则panic
	 * 并判断引用标志必须为1
	 */
	if (!(bh=getblk(dev,j)))
		panic("new_block: cannot get block");
	if (bh->b_count != 1)
		panic("new block: count is != 1");
	/*
	 * 设置更新标志和已修改标志
	 * 然后释放对应的缓冲区
	 */
	clear_block(bh->b_data);
	bh->b_uptodate = 1;
	bh->b_dirt = 1;
	brelse(bh);
	return j;
}

/*
 * 释放制定的i节点
 */
void free_inode(struct m_inode * inode)
{
	struct super_block * sb;
	struct buffer_head * bh;

	if (!inode)
		return;
	/*
	 * 如果i节点的设备号字段为0，说明该节点无用，将i节点对应的内存清除
	 */
	if (!inode->i_dev) {
		memset(inode,0,sizeof(*inode));
		return;
	}
	/*
	 * 如果i节点还有其他程序引用，则不能释放
	 */
	if (inode->i_count>1) {
		printk("trying to free inode with count=%d\n",inode->i_count);
		panic("free_inode");
	}
	/*
	 * 如果链接数部位0，说明还有其他文件或者目录使用该节点，不释放
	 */
	if (inode->i_nlinks)
		panic("trying to free inode with links");
	/*
	 * 获取i节点对应设备的超级块
	 */
	if (!(sb = get_super(inode->i_dev)))
		panic("trying to free inode on nonexistent device");
	/*
	 * 如果i节点等于0或者大于该文件系统i节点的总数则panic
	 */
	if (inode->i_num < 1 || inode->i_num > sb->s_ninodes)
		panic("trying to free inode 0 or nonexistant inode");
	/*
	 * 如果i节点对应的bit所在的高速缓存不存在则出错
	 */
	if (!(bh=sb->s_imap[inode->i_num>>13]))
		panic("nonexistent imap in superblock");
	/*
	 * 清零bit
	 */
	if (clear_bit(inode->i_num&8191,bh->b_data))
		printk("free_inode: bit already cleared.\n\r");
	/*
	 * 设置已修改标志，并清空该i节点所占的内存
	 */
	bh->b_dirt = 1;
	memset(inode,0,sizeof(*inode));
}

/*
 * 创建一个i节点
 */
struct m_inode * new_inode(int dev)
{
	struct m_inode * inode;
	struct super_block * sb;
	struct buffer_head * bh;
	int i,j;

	/*
	 * 获取一个i节点
	 */
	if (!(inode=get_empty_inode()))
		return NULL;
	/*
	 * 获取超级块
	 */
	if (!(sb = get_super(dev)))
		panic("new_inode with unknown device");
	/*
	 * 寻找第一个为0的i节点的bit
	 */
	j = 8192;
	for (i=0 ; i<8 ; i++)
		if ((bh=sb->s_imap[i]))
			if ((j=find_first_zero(bh->b_data))<8192)
				break;
	if (!bh || j >= 8192 || j+i*8192 > sb->s_ninodes) {
		iput(inode);
		return NULL;
	}
	/*
	 * 置位
	 */
	if (set_bit(j,bh->b_data))
		panic("new_inode: bit already set");
	/*
	 * 设置该i节点位图所在的高速缓存已修改标志
	 */
	bh->b_dirt = 1;
	inode->i_count=1;
	inode->i_nlinks=1;
	inode->i_dev=dev;
	inode->i_uid=current->euid;
	inode->i_gid=current->egid;
	inode->i_dirt=1;
	/*
	 * 将该i节点的i_num指向该i节点所在的位图的bit
	 */
	inode->i_num = j + i*8192;
	inode->i_mtime = inode->i_atime = inode->i_ctime = CURRENT_TIME;
	return inode;
}
