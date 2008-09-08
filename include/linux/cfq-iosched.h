#ifndef _LINUX_CFQ_IOSCHED_H
#define _LINUX_CFQ_IOSCHED_H

#include <linux/ioprio.h>
#include <linux/rbtree.h>
#include <linux/blkdev.h>

extern struct kmem_cache *cfq_pool;

#define CFQ_PRIO_LISTS		IOPRIO_BE_NR

/*
 * Most of our rbtree usage is for sorting with min extraction, so
 * if we cache the leftmost node we don't have to walk down the tree
 * to find it. Idea borrowed from Ingo Molnars CFS scheduler. We should
 * move this into the elevator for the rq sorting as well.
 */
struct cfq_rb_root {
	struct rb_root rb;
	struct rb_node *left;
};
#define CFQ_RB_ROOT	(struct cfq_rb_root) { RB_ROOT, NULL, }

/*
 * Per (Device, UBC) queue data
 */
struct cfq_bc_data {
	/* for ub.iopriv->cfq_bc_head */
	struct list_head	cfq_bc_list;
	/* for cfqd->act_cfq_bc_head */
	struct list_head	act_cfq_bc_list;

	struct cfq_data		*cfqd;
	struct ub_iopriv	*ub_iopriv;

	/*
	 * rr list of queues with requests and the count of them
	 */
	struct cfq_rb_root	service_tree;

	int			cur_prio;
	int			cur_end_prio;

	unsigned long		rqnum;
	unsigned long		on_dispatch;

	/*
	 * async queue for each priority case
	 */
	struct cfq_queue	*async_cfqq[2][CFQ_PRIO_LISTS];
	struct cfq_queue	*async_idle_cfqq;
};

/*
 * Per block device queue structure
 */
struct cfq_data {
	struct request_queue *queue;

#ifndef CONFIG_BC_IO_SCHED
	struct cfq_bc_data cfq_bc;
#endif
	unsigned int busy_queues;

	int rq_in_driver;
	int sync_flight;
	int hw_tag;

	/*
	 * idle window management
	 */
	struct timer_list idle_slice_timer;
	struct work_struct unplug_work;

	struct cfq_queue *active_queue;
	struct cfq_io_context *active_cic;

	sector_t last_position;
	unsigned long last_end_request;

	/*
	 * tunables, see top of file
	 */
	unsigned int cfq_quantum;
	unsigned int cfq_fifo_expire[2];
	unsigned int cfq_back_penalty;
	unsigned int cfq_back_max;
	unsigned int cfq_slice[2];
	unsigned int cfq_slice_async_rq;
	unsigned int cfq_slice_idle;

	struct list_head cic_list;

	/* list of ub that have requests */
	struct list_head act_cfq_bc_head;
	/* ub that owns a timeslice at the moment */
	struct cfq_bc_data *active_cfq_bc;
	unsigned int cfq_ub_slice;
	unsigned long slice_end;
	int virt_mode;
	int write_virt_mode;
};

/*
 * Per process-grouping structure
 */
struct cfq_queue {
	/* reference count */
	atomic_t ref;
	/* various state flags, see below */
	unsigned int flags;
	/* parent cfq_data */
	struct cfq_data *cfqd;
	/* service_tree member */
	struct rb_node rb_node;
	/* service_tree key */
	unsigned long rb_key;
	/* sorted list of pending requests */
	struct rb_root sort_list;
	/* if fifo isn't expired, next request to serve */
	struct request *next_rq;
	/* requests queued in sort_list */
	int queued[2];
	/* currently allocated requests */
	int allocated[2];
	/* fifo list of requests in sort_list */
	struct list_head fifo;

	unsigned long slice_end;
	long slice_resid;

	/* pending metadata requests */
	int meta_pending;
	/* number of requests that are on the dispatch list or inside driver */
	int dispatched;

	/* io prio of this group */
	unsigned short ioprio, org_ioprio;
	unsigned short ioprio_class, org_ioprio_class;

	pid_t pid;
	struct cfq_bc_data *cfq_bc;
};

static void inline cfq_init_cfq_bc(struct cfq_bc_data *cfq_bc)
{
	cfq_bc->service_tree = CFQ_RB_ROOT;
}
#endif /* _LINUX_CFQ_IOSCHED_H */
