/*
 *  linux/fs/buffer.c
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 *  'buffer.c' implements the buffer-cache functions. Race-conditions have
 * been avoided by NEVER letting a interrupt change a buffer (except for the
 * data, of course), but instead letting the caller do it. NOTE! As interrupts
 * can wake up a caller, some cli-sti sequences are needed to check for
 * sleep-on-calls. These should be extremely quick, though (I hope).
 */

/*
 * NOTE! There is one discordant note here: checking floppies for
 * disk change. This is where it fits best, I think, as it should
 * invalidate changed floppy-disk-caches.
 */

#include <stdarg.h>
 
#include <linux/config.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <asm/system.h>
#include <asm/io.h>

/*
 * end由链接程序生成，表示内核最后面的地址
 */
extern int end;
extern void put_super(int);
extern void invalidate_inodes(int);

struct buffer_head * start_buffer = (struct buffer_head *) &end;
struct buffer_head * hash_table[NR_HASH];
static struct buffer_head * free_list;
static struct task_struct * buffer_wait = NULL;
int NR_BUFFERS = 0;

/*
 * 等待bh解锁，可能发生进行切换
 */
static inline void wait_on_buffer(struct buffer_head * bh)
{
	cli();
	while (bh->b_lock)
		sleep_on(&bh->b_wait);
	sti();
}

/*
 * sync的系统调用，同步设备和内存高速缓冲中的数据
 */
int sys_sync(void)
{
	int i;
	struct buffer_head * bh;

	sync_inodes();		/* write out inodes into buffers */
	bh = start_buffer;
	for (i=0 ; i<NR_BUFFERS ; i++,bh++) {
		wait_on_buffer(bh);
		if (bh->b_dirt)
			ll_rw_block(WRITE,bh);
	}
	return 0;
}

/*
 * 对指定的设备进行ysnc
 */
int sync_dev(int dev)
{
	int i;
	struct buffer_head * bh;

	bh = start_buffer;
	for (i=0 ; i<NR_BUFFERS ; i++,bh++) {
		if (bh->b_dev != dev)
			continue;
		wait_on_buffer(bh);
		if (bh->b_dev == dev && bh->b_dirt)
			ll_rw_block(WRITE,bh);
	}
	sync_inodes();
	bh = start_buffer;
	for (i=0 ; i<NR_BUFFERS ; i++,bh++) {
		if (bh->b_dev != dev)
			continue;
		wait_on_buffer(bh);
		if (bh->b_dev == dev && bh->b_dirt)
			ll_rw_block(WRITE,bh);
	}
	return 0;
}

/*
 * 使指定设备的高速缓存无效
 * 扫描指定设备的高速缓冲块，复位其有效标志和已修改标志
 */
static inline void invalidate_buffers(int dev)
{
	int i;
	struct buffer_head * bh;

	bh = start_buffer;
	for (i=0 ; i<NR_BUFFERS ; i++,bh++) {
		if (bh->b_dev != dev)
			continue;
		wait_on_buffer(bh);
		if (bh->b_dev == dev)
			bh->b_uptodate = bh->b_dirt = 0;
	}
}

/*
 * This routine checks whether a floppy has been changed, and
 * invalidates all buffer-cache-entries in that case. This
 * is a relatively slow routine, so we have to try to minimize using
 * it. Thus it is called only upon a 'mount' or 'open'. This
 * is the best way of combining speed and utility, I think.
 * People changing diskettes in the middle of an operation deserve
 * to loose :-)
 *
 * NOTE! Although currently this is only for floppies, the idea is
 * that any additional removable block-device will use this routine,
 * and that mount/open needn't know that floppies/whatever are
 * special.
 * 
 * 该子程序检查一个软盘是否已经被更换，如果更换了就使其高速缓存无效
 * 这个程序比较慢，我们要尽可能少用它，所以仅仅在mount和open时才调用
 * 目前这个程序只能用于软盘
 */
void check_disk_change(int dev)
{
	int i;

	/*
	 * 只能用于软盘
	 */
	if (MAJOR(dev) != 2)
		return;
	/*
	 * 如果软盘已经被更换判断
	 */
	if (!floppy_change(dev & 0x03))
		return;

	for (i=0 ; i<NR_SUPER ; i++)
		if (super_block[i].s_dev == dev)
			put_super(super_block[i].s_dev);
	invalidate_inodes(dev);
	invalidate_buffers(dev);
}

#define _hashfn(dev,block) (((unsigned)(dev^block))%NR_HASH)
#define hash(dev,block) hash_table[_hashfn(dev,block)]

/*
 * 从hash表项中移除bh
 */
static inline void remove_from_queues(struct buffer_head * bh)
{
	/* remove from hash-queue */
	if (bh->b_next)
		bh->b_next->b_prev = bh->b_prev;
	if (bh->b_prev)
		bh->b_prev->b_next = bh->b_next;
	if (hash(bh->b_dev,bh->b_blocknr) == bh)
		hash(bh->b_dev,bh->b_blocknr) = bh->b_next;
	/* remove from free list */
	if (!(bh->b_prev_free) || !(bh->b_next_free))
		panic("Free block list corrupted");
	bh->b_prev_free->b_next_free = bh->b_next_free;
	bh->b_next_free->b_prev_free = bh->b_prev_free;
	if (free_list == bh)
		free_list = bh->b_next_free;
}

/*
 * 将bh加入hash表
 */
static inline void insert_into_queues(struct buffer_head * bh)
{
	/* put at end of free list */
	bh->b_next_free = free_list;
	bh->b_prev_free = free_list->b_prev_free;
	free_list->b_prev_free->b_next_free = bh;
	free_list->b_prev_free = bh;
	/* put the buffer in new hash-queue if it has a device */
	bh->b_prev = NULL;
	bh->b_next = NULL;
	if (!bh->b_dev)
		return;
	bh->b_next = hash(bh->b_dev,bh->b_blocknr);
	hash(bh->b_dev,bh->b_blocknr) = bh;
	bh->b_next->b_prev = bh;
}

/*
 * 在高速缓冲区寻找指定设备和块的缓冲区
 */
static struct buffer_head * find_buffer(int dev, int block)
{		
	struct buffer_head * tmp;

	for (tmp = hash(dev, block); tmp != NULL; tmp = tmp->b_next)
		if (tmp->b_dev == dev && tmp->b_blocknr == block)
			return tmp;
	return NULL;
}

/*
 * Why like this, I hear you say... The reason is race-conditions.
 * As we don't lock buffers (unless we are readint them, that is),
 * something might happen to it while we sleep (ie a read-error
 * will force it bad). This shouldn't really happen currently, but
 * the code is ready.
 */
struct buffer_head * get_hash_table(int dev, int block)
{
	struct buffer_head * bh;

	for (;;) {
		/*
		 * 在高速缓冲区中寻找dev和block对应的高速缓存
		 */
		if (!(bh=find_buffer(dev,block)))
			return NULL;
		/*
		 * 增加引用计数
		 */
		bh->b_count++;
		/*
		 * 等待解锁
		 */
		wait_on_buffer(bh);
		/*
		 * 由于可能经过睡眠，因此有必要在检查一下缓冲区的正确性
		 */
		if (bh->b_dev == dev && bh->b_blocknr == block)
			return bh;
		/*
		 * 如果缓冲区不正确，则减少引用计数，重新寻找
		 */
		bh->b_count--;
	}
}

/*
 * Ok, this is getblk, and it isn't very clear, again to hinder
 * race-conditions. Most of the code is seldom used, (ie repeating),
 * so it should be much more efficient than it looks.
 *
 * The algoritm is changed: hopefully better, and an elusive bug removed.
 */
/*
 * 同时判断缓冲区的修改标志和锁定标志
 */
#define BADNESS(bh) (((bh)->b_dirt<<1)+(bh)->b_lock)
/*
 * 检查所指定的缓冲区是否已经在告诉缓冲中
 * 如果不在，需要建立
 */
struct buffer_head * getblk(int dev, int block)
{
	struct buffer_head * tmp, * bh;

repeat:
	/*
	 * 先根据dev和block在高速缓存hash表中获取，如果存在直接返回
	 */
	if ((bh = get_hash_table(dev, block)))
		return bh;

	/*
	 * tmp指向缓冲区头部
	 * 下面的代码意思是需要找到一个b_count为0的高速缓存
	 * 一直对BANDNESS不理解，再次解释一下
	 *
	 * BANDNESS = bh->b_dirt*2 + bh->b_lock
	 * 分析如下
	 * 
	 * 从free_list开始遍历
	 * 第一次的时候bh为空因此可以进入if (!bh || BADNESS(tmp) < BADNESS(bh))
	 * 将从free_list找到的可用tmp赋值为bh
	 * 如果此bh的lock和dirt都为0，则直接可以用这个bh，否则继续进入循环
	 * 此时又找到一个tmp，然后利用BADNESS宏进行对比
	 * 可以发现系统倾向于选择一个lock和dirt都为0的缓冲区，如果实在找不到
	 * 那就选择一个lock置位的，最后如果实在找不到那就找一个lock和dirt都为1的
	 * 如果所有的b_count都在用，则睡眠一会在重新查找
	 * BADNESS值越小表示使用该块的系统开销越小，优先选择该块。
	 * 是否标记dirt对BADNES的计算结果有很大影响。
	 * 如果块既“未锁定”又是“干净的”，则可以使用直接退出循环
	 */	
	tmp = free_list;
	do {
		if (tmp->b_count)
			continue;
		/*
		 * 如果bh为空，第一次肯定为空
		 */
		if (!bh || BADNESS(tmp) < BADNESS(bh)) {
			bh = tmp;
			if (!BADNESS(tmp))
				break;
		}
	/* and repeat until we find something good */
	} while ((tmp = tmp->b_next_free) != free_list);
	/*
	 * 如果没有找到，则睡眠一会儿，然后继续找
	 */
	if (!bh) {
		sleep_on(&buffer_wait);
		goto repeat;
	}
	/*
	 * 根据上面的BADNESS分析，获取的缓冲区如果上锁，需求等待解锁
	 * wait_on_buffer会引起任务切换，因此需要再次检查该缓冲区的使用情况
	 *
	 */
	wait_on_buffer(bh);
	if (bh->b_count)
		goto repeat;
	/*
	 * 如果该缓冲区已经被修改，则需要和磁盘进行同步并等待该缓冲区解锁
	 *
	 */
	while (bh->b_dirt) {
		sync_dev(bh->b_dev);
		wait_on_buffer(bh);
		if (bh->b_count)
			goto repeat;
	}
	/* NOTE!! While we slept waiting for this block, somebody else might */
	/* already have added "this" block to the cache. check it */
	/*
	 * 在上面的等待可能发生进程切换，也有可能导致指定的设备和块已经被加进入了
	 */
	if (find_buffer(dev, block))
		goto repeat;
	/* OK, FINALLY we know that this buffer is the only one of it's kind, */
	/* and that it's unused (b_count=0), unlocked (b_lock=0), and clean */
	bh->b_count = 1;
	bh->b_dirt = 0;
	bh->b_uptodate = 0;
	remove_from_queues(bh);
	bh->b_dev = dev;
	bh->b_blocknr = block;
	insert_into_queues(bh);
	return bh;
}
/*
 * 释放指定的缓冲区
 * 等待缓冲区解锁，引用计数递减1，唤醒等待空闲缓冲区的进程
 */
void brelse(struct buffer_head * buf)
{
	if (!buf)
		return;
	wait_on_buffer(buf);
	if (!(buf->b_count--))
		panic("Trying to free free buffer");
	wake_up(&buffer_wait);
}

/*
 * bread() reads a specified block and returns the buffer that contains
 * it. It returns NULL if the block was unreadable.
 * 从指定的设备上读取指定的数据块
 */
struct buffer_head * bread(int dev,int block)
{
	struct buffer_head * bh;
	/*
	 * 获取一个高速缓存块
	 */
	if (!(bh = getblk(dev, block)))
		panic("bread: getblk returned NULL\n");
	/*
	 * 如果该高速缓冲块是有效的，直接返回
	 */
	if (bh->b_uptodate)
		return bh;
	/*
	 * 产生读设备请求
	 */
	ll_rw_block(READ, bh);
	/*
	 * 等待缓冲区解锁
	 */
	wait_on_buffer(bh);
	/*
	 * 如果有效，直接返回，否则返回NULL
	 */
	if (bh->b_uptodate)
		return bh;
	brelse(bh);
	return NULL;
}

/*
 * 从from地址复制一块数据到to
 */
#define COPYBLK(from,to) \
__asm__("cld\n\t" \
	"rep\n\t" \
	"movsl\n\t" \
	::"c" (BLOCK_SIZE/4),"S" (from),"D" (to) \
	)

/*
 * bread_page reads four buffers into memory at the desired address. It's
 * a function of its own, as there is some speed to be got by reading them
 * all at the same time, not waiting for one to be read, and then another
 * etc.
 * 从设备上读取4个缓冲块到地址address处
 */
void bread_page(unsigned long address,int dev,int b[4])
{
	struct buffer_head * bh[4];
	int i;

	for (i=0; i<4; i++)
		/*
		 * b[]里面存放的块号
		 */
		if (b[i]) {
			if ((bh[i] = getblk(dev,b[i])))
				if (!bh[i]->b_uptodate)
					ll_rw_block(READ,bh[i]);
		} else
			bh[i] = NULL;

	/*
	 * 将缓冲区的内容复制到address，一个缓冲区1024字节，一共4个，就是一个页
	 */		
	for (i=0 ; i<4 ; i++,address += BLOCK_SIZE)
		if (bh[i]) {
			wait_on_buffer(bh[i]);
			if (bh[i]->b_uptodate)
				COPYBLK((unsigned long) bh[i]->b_data,address);
			brelse(bh[i]);
		}
}

/*
 * Ok, breada can be used as bread, but additionally to mark other
 * blocks for reading as well. End the argument list with a negative
 * number.
 * 这个函数可以想bread一样使用，但是需要一个负数进行结束
 */
struct buffer_head * breada(int dev,int first, ...)
{
	va_list args;
	struct buffer_head * bh, *tmp;

	va_start(args,first);
	if (!(bh=getblk(dev,first)))
		panic("bread: getblk returned NULL\n");
	if (!bh->b_uptodate)
		ll_rw_block(READ,bh);
	while ((first=va_arg(args,int))>=0) {
		tmp=getblk(dev,first);
		if (tmp) {
			if (!tmp->b_uptodate)
				ll_rw_block(READA,tmp);
			tmp->b_count--;
		}
	}
	va_end(args);
	wait_on_buffer(bh);
	if (bh->b_uptodate)
		return bh;
	brelse(bh);
	return (NULL);
}

void buffer_init(long buffer_end)
{
	struct buffer_head * h = start_buffer;
	void * b;
	int i;
	/*
	 * 如果缓冲区高端为1M，则从640KB到1MB被显示内核和BIOS占用，实际上应该是640KB
	 * 
	 */
	if (buffer_end == 1<<20)
		b = (void *) (640*1024);
	else
		b = (void *) buffer_end;
	/*
	 * h为start_buffer在实际跟踪过程中为end，end由链接程序生成，内核代码最末端
	 * while里面保证h和b不重合
	 */
	while ((b -= BLOCK_SIZE) >= ((void *) (h+1))) {
		h->b_dev = 0;
		h->b_dirt = 0;
		h->b_count = 0;
		h->b_lock = 0;
		h->b_uptodate = 0;
		h->b_wait = NULL;
		h->b_next = NULL;
		h->b_prev = NULL;
		h->b_data = (char *) b;
		h->b_prev_free = h-1;
		h->b_next_free = h+1;
		h++;
		NR_BUFFERS++;
		if (b == (void *) 0x100000)		//如果地址递减到1MB，则跳过显存和BIOS
			b = (void *) 0xA0000;		//b设置为640KB
	}
	/* h--指向最后的buffer_head
	 * 如下的语句就是将buffer_head组成一个双向循环链表
	 * 头部为start_buffer
	 */
	h--;
	free_list = start_buffer;
	free_list->b_prev_free = h;
	h->b_next_free = free_list;
	/*
	 * 初始化哈希表
	 */
	for (i=0; i < NR_HASH; i++)
		hash_table[i]=NULL;
}	
