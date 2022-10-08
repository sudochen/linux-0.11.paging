/*
 *  linux/kernel/blk_dev/ll_rw.c
 *
 * (C) 1991 Linus Torvalds
 */

/*
 * This handles all read/write requests to block devices
 */
#include <errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <asm/system.h>

#include "blk.h"

/*
 * The request-struct contains all necessary data
 * to load a nr of sectors into memory
 */
struct request request[NR_REQUEST];

/*
 * used to wait on when there are no free requests
 */
struct task_struct * wait_for_request = NULL;

/* blk_dev_struct is:
 *	do_request-address
 *	next-request
 */
struct blk_dev_struct blk_dev[NR_BLK_DEV] = {
	{ NULL, NULL },		/* no_dev */
	{ NULL, NULL },		/* dev mem */
	{ NULL, NULL },		/* dev fd */
	{ NULL, NULL },		/* dev hd */
	{ NULL, NULL },		/* dev ttyx */
	{ NULL, NULL },		/* dev tty */
	{ NULL, NULL }		/* dev lp */
};

static inline void lock_buffer(struct buffer_head * bh)
{
	cli();
	while (bh->b_lock)
		sleep_on(&bh->b_wait);
	bh->b_lock=1;
	sti();
}

static inline void unlock_buffer(struct buffer_head * bh)
{
	if (!bh->b_lock)
		printk("ll_rw_block.c: buffer not locked\n\r");
	bh->b_lock = 0;
	wake_up(&bh->b_wait);
}

/*
 * add-request adds a request to the linked list.
 * It disables interrupts so that it can muck with the
 * request-lists in peace.
 */
static void add_request(struct blk_dev_struct * dev, struct request * req)
{
	struct request * tmp;

	req->next = NULL;
	cli();
	if (req->bh)
		req->bh->b_dirt = 0;
	/*
	 * 如果dev当前的请求项为空，表示当前设备没有请求项
	 * 因此将req作为当前请求项，并立即进行request回调
	 */
	if (!(tmp = dev->current_request)) {
		dev->current_request = req;
		sti();
		(dev->request_fn)();
		return;
	}
	for ( ;tmp->next; tmp = tmp->next) {
		/*
		 * 如果tmp的优先级比req高或者tmp比tmp的下一个的优先级高
		 * 并且req比tmp的下一个优先高
		 * 这个算法的目的是让磁头尽可能少的移动从而提高性能
		 * 电梯算法
		 */
		if ((IN_ORDER(tmp, req) || !IN_ORDER(tmp, tmp->next)) &&
		    (IN_ORDER(req, tmp->next))) {
			break;
			}
	}
	/*
	 * 将req插入request链表中
	 */
	req->next=tmp->next;
	tmp->next=req;
	sti();
}

static void make_request(int major, int rw, struct buffer_head * bh)
{
	struct request * req;
	int rw_ahead;

	/* 
	 * WRITEA/READA is special case - it is not really needed, so if the 
	 * buffer is locked, we just forget about it, else it's a normal read
	 *
	 * rw_ahead表示rw是READA或者WRITEA，这个分支根据代码好像是非阻塞的读写
	 * 对于是rw_ahead的，如果缓冲区正在使用，则直接退出
	 * 否则就按照普通的READ, WRITE命令进行
	 *
	 */
	if ((rw_ahead = (rw == READA || rw == WRITEA))) {
		/*
		 * 如果buffer_head已经锁定，直接返回
		 * 否则就按照正常的读写进行
		 *
		 */
		if (bh->b_lock) {
			return;
		}
		/*
		 * 按照正常的读写
		 */
		if (rw == READA) {
			rw = READ;
		} else {
			rw = WRITE;
		}
	}
	/*
	 * 如果既不是读也不是写，直接panic错误
	 */
	if (rw != READ && rw != WRITE) {
		panic("Bad block dev command, must be R/W/RA/WA");
	}

	/*
	 * 锁定buffer
	 *
	 */
	lock_buffer(bh);
	/*
	 * 如果写命令的数据没有被修改过或者读命令的数据没有被更新过直接退出
	 * 不用添加这个请求
	 *
	 */
	if ((rw == WRITE && !bh->b_dirt) || (rw == READ && bh->b_uptodate)) {
		unlock_buffer(bh);
		return;
	}
repeat:
	/* we don't allow the write-requests to fill up the queue completely:
	 * we want some room for reads: they take precedence. The last third
	 * of the requests are only for reads.
	 * 
	 * 读操作优先，从后向前遍历
	 * request的开始给读请求使用
	 * 在reqeust的2/3开始给写请求使用
	 *
	 */
	if (rw == READ)
		req = request + NR_REQUEST;
	else
		req = request + ((NR_REQUEST*2)/3);
	/* find an empty request */
	/*
	 * 找一个空的请求项
	 */
	while (--req >= request)
		if (req->dev < 0)
			break;
	/* if none found, sleep on new requests: check for rw_ahead
	 *
	 * req < request表示没有找到
	 * 如果没有找到一个空的请求项，如果是rw_ahead则直接退出
	 * 否则进行睡眠并再次寻找
	 *
	 */
	if (req < request) {
		if (rw_ahead) {
			unlock_buffer(bh);
			return;
		}
		sleep_on(&wait_for_request);
		goto repeat;
	}
	/* fill up the request-info, and add it to the queue 
	 *
	 * 如果代码走到这说明已经找到了一个request
	 * 然后填充request的信息，并将其添加到队列里
	 *
	 */
	req->dev = bh->b_dev;
	req->cmd = rw;
	req->errors = 0;
	/*
	 * 将块转换成扇区，一个块等于两个扇区，1K空间
	 * nr_sectors读两个扇区，也就是一个块
	 */
	req->sector = bh->b_blocknr<<1;
	req->nr_sectors = 2;
	req->buffer = bh->b_data;
	req->waiting = NULL;
	req->bh = bh;
	req->next = NULL;
	/*
	 * major是主设备号
	 * 将req加入blk_dev[major].current_request
	 */
	add_request(major + blk_dev, req);
}

void ll_rw_block(int rw, struct buffer_head * bh)
{
	unsigned int major;

	if ((major=MAJOR(bh->b_dev)) >= NR_BLK_DEV ||
		(!(blk_dev[major].request_fn))) {
		printk("Trying to read nonexistent block-device\n\r");
		return;
	}
	make_request(major, rw, bh);
}

void blk_dev_init(void)
{
	int i;

	for (i=0 ; i<NR_REQUEST ; i++) {
		request[i].dev = -1;
		request[i].next = NULL;
	}
}
