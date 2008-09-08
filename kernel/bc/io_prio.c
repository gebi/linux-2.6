/*
 *  kernel/bc/io_prio.c
 *
 *  Copyright (C) 2007 SWsoft
 *  All rights reserved.
 *
 *  Licensing governed by "linux/COPYING.SWsoft" file.
 *
 *  Vasily Tarasov <vtaras@openvz.org>
 *
 */

#include <linux/module.h>
#include <linux/cfq-iosched.h>
#include <bc/io_prio.h>
#include <bc/beancounter.h>
#include <bc/hash.h>
#include <bc/io_acct.h>
#include <linux/blkdev.h>

struct cfq_bc_data *__find_cfq_bc(struct ub_iopriv *iopriv,
							struct cfq_data *cfqd)
{
	struct cfq_bc_data *cfq_bc;

	list_for_each_entry(cfq_bc, &iopriv->cfq_bc_head, cfq_bc_list)
		if (cfq_bc->cfqd == cfqd)
			return cfq_bc;

	return NULL;
}

struct cfq_bc_data *bc_find_cfq_bc(struct ub_iopriv *iopriv,
					struct cfq_data *cfqd)
{
	struct cfq_bc_data *cfq_bc;
	unsigned long flags;

	read_lock_irqsave(&iopriv->cfq_bc_list_lock, flags);
	cfq_bc = __find_cfq_bc(iopriv, cfqd);
	read_unlock_irqrestore(&iopriv->cfq_bc_list_lock, flags);
	return cfq_bc;
}
struct cfq_bc_data *bc_findcreate_cfq_bc(struct ub_iopriv *iopriv,
					struct cfq_data *cfqd, gfp_t gfp_mask)
{
	struct cfq_bc_data *cfq_bc_new;
	struct cfq_bc_data *cfq_bc;
	unsigned long flags;

	cfq_bc = bc_find_cfq_bc(iopriv, cfqd);
	if (cfq_bc)
		return cfq_bc;

	cfq_bc_new = kzalloc(sizeof(*cfq_bc_new), gfp_mask);
	if (!cfq_bc_new)
		return NULL;

	cfq_init_cfq_bc(cfq_bc_new);
	cfq_bc_new->cfqd = cfqd;
	cfq_bc_new->ub_iopriv = iopriv;

	write_lock_irqsave(&iopriv->cfq_bc_list_lock, flags);
	cfq_bc = __find_cfq_bc(iopriv, cfqd);
	if (cfq_bc)
		kfree(cfq_bc_new);
	else {
		list_add_tail(&cfq_bc_new->cfq_bc_list,
					&iopriv->cfq_bc_head);
		cfq_bc = cfq_bc_new;
	}
	write_unlock_irqrestore(&iopriv->cfq_bc_list_lock, flags);

	return cfq_bc;
}

void bc_init_ioprio(struct ub_iopriv *iopriv)
{
	INIT_LIST_HEAD(&iopriv->cfq_bc_head);
	rwlock_init(&iopriv->cfq_bc_list_lock);
	iopriv->ioprio = UB_IOPRIO_BASE;
}

static void inline bc_cfq_bc_check_empty(struct cfq_bc_data *cfq_bc)
{
	BUG_ON(!RB_EMPTY_ROOT(&cfq_bc->service_tree.rb));
}

static void bc_release_cfq_bc(struct cfq_bc_data *cfq_bc)
{
	struct cfq_data *cfqd;
	elevator_t *eq;
	int i;

	cfqd = cfq_bc->cfqd;
	eq = cfqd->queue->elevator;

	for (i = 0; i < CFQ_PRIO_LISTS; i++) {
		if (cfq_bc->async_cfqq[0][i]) {
			eq->ops->put_queue(cfq_bc->async_cfqq[0][i]);
			cfq_bc->async_cfqq[0][i] = NULL;
		}
		if (cfq_bc->async_cfqq[1][i]) {
			eq->ops->put_queue(cfq_bc->async_cfqq[1][i]);
			cfq_bc->async_cfqq[1][i] = NULL;
		}
	}
	if (cfq_bc->async_idle_cfqq) {
		eq->ops->put_queue(cfq_bc->async_idle_cfqq);
		cfq_bc->async_idle_cfqq = NULL;
	}
	/* 
	 * Note: this cfq_bc is already not in active list,
	 * but can be still pointed from cfqd as active.
	 */
	cfqd->active_cfq_bc = NULL;

	bc_cfq_bc_check_empty(cfq_bc);
	list_del(&cfq_bc->cfq_bc_list);
	kfree(cfq_bc);
}

void bc_fini_ioprio(struct ub_iopriv *iopriv)
{
	struct cfq_bc_data *cfq_bc;
	struct cfq_bc_data *cfq_bc_tmp;
	unsigned long flags;
	spinlock_t *queue_lock;

	/* 
	 * Don't get cfq_bc_list_lock since ub is already dead,
	 * but async cfqqs are still in hash list, consequently
	 * queue_lock should be hold.
	 */
	list_for_each_entry_safe(cfq_bc, cfq_bc_tmp,
			&iopriv->cfq_bc_head, cfq_bc_list) {
		queue_lock = cfq_bc->cfqd->queue->queue_lock;
		spin_lock_irqsave(queue_lock, flags);
		bc_release_cfq_bc(cfq_bc);
		spin_unlock_irqrestore(queue_lock, flags);
	}
}

void bc_cfq_exit_queue(struct cfq_data *cfqd)
{
	struct cfq_bc_data *cfq_bc;
	struct user_beancounter *ub;

	local_irq_disable();
	for_each_beancounter(ub) {
		write_lock(&ub->iopriv.cfq_bc_list_lock);
		cfq_bc = __find_cfq_bc(&ub->iopriv, cfqd);
		if (!cfq_bc) {
			write_unlock(&ub->iopriv.cfq_bc_list_lock);
			continue;
		}
		bc_release_cfq_bc(cfq_bc);
		write_unlock(&ub->iopriv.cfq_bc_list_lock);
	}
	local_irq_enable();
}

int bc_expired(struct cfq_data *cfqd)
{
	return time_after(jiffies, cfqd->slice_end) ?  1 : 0;
}

static inline int bc_empty(struct cfq_bc_data *cfq_bc)
{
	/*
	 * consider BC as empty only if there is no requests
	 * in elevator _and_ in driver
	 */
	if (!cfq_bc->rqnum && !cfq_bc->on_dispatch)
		return 1;

	return 0;
}
 
static inline unsigned long bc_time_slice_by_ioprio(unsigned int ioprio,
 							unsigned int base_slice)
{
 	return	base_slice +
 		(base_slice * (ioprio - UB_IOPRIO_MIN))
 		/ (UB_IOPRIO_MAX - UB_IOPRIO_MIN - 1);
}
 
static inline void bc_set_active(struct cfq_data *cfqd)
{
	if (list_empty(&cfqd->act_cfq_bc_head)) {
		cfqd->active_cfq_bc = NULL;
		return;
	}

	cfqd->active_cfq_bc = list_first_entry(&cfqd->act_cfq_bc_head,
					struct cfq_bc_data, act_cfq_bc_list);
	list_move_tail(&cfqd->active_cfq_bc->act_cfq_bc_list,
						&cfqd->act_cfq_bc_head);
	cfqd->slice_end = jiffies +
		bc_time_slice_by_ioprio(cfqd->active_cfq_bc->ub_iopriv->ioprio,
							cfqd->cfq_ub_slice);
}

void bc_schedule_active(struct cfq_data *cfqd)
{
	if (bc_expired(cfqd) || !cfqd->active_cfq_bc ||
				bc_empty(cfqd->active_cfq_bc))
		bc_set_active(cfqd);
}

void bc_inc_rqnum(struct cfq_queue *cfqq)
{
	struct cfq_bc_data *cfq_bc;

	cfq_bc = cfqq->cfq_bc;

	if (!cfq_bc->rqnum)
		list_add_tail(&cfq_bc->act_cfq_bc_list,
				&cfqq->cfqd->act_cfq_bc_head);

	cfq_bc->rqnum++;
}

void bc_dec_rqnum(struct cfq_queue *cfqq)
{
	struct cfq_bc_data *cfq_bc;

	cfq_bc = cfqq->cfq_bc;

	cfq_bc->rqnum--;

	if (!cfq_bc->rqnum)
		list_del(&cfq_bc->act_cfq_bc_list);
}

unsigned long bc_set_ioprio(int ubid, int ioprio)
{
	struct user_beancounter *ub;

	if (ioprio < UB_IOPRIO_MIN || ioprio >= UB_IOPRIO_MAX)
		return -ERANGE;

	ub = get_beancounter_byuid(ubid, 0);
 	if (!ub)
		return -ESRCH;

	ub->iopriv.ioprio = ioprio;
	put_beancounter(ub);
 
	return 0;
}

struct user_beancounter *bc_io_switch_context(struct page *page)
{
	struct page_beancounter *pb;
	struct user_beancounter *old_ub = NULL;

	pb = page_iopb(page);
	pb = iopb_to_pb(pb);
	if (pb) {
		get_beancounter(pb->ub);
		old_ub = set_exec_ub(pb->ub);
	}
	
	return old_ub;
}

void bc_io_restore_context(struct user_beancounter *ub)
{
	struct user_beancounter *old_ub;

	if (ub) {
		old_ub = set_exec_ub(ub);
		put_beancounter(old_ub);
	}
}

EXPORT_SYMBOL(bc_io_switch_context);
EXPORT_SYMBOL(bc_io_restore_context);
EXPORT_SYMBOL(__find_cfq_bc);
EXPORT_SYMBOL(bc_fini_ioprio);
EXPORT_SYMBOL(bc_init_ioprio);
EXPORT_SYMBOL(bc_findcreate_cfq_bc);
EXPORT_SYMBOL(bc_cfq_exit_queue);
EXPORT_SYMBOL(bc_expired);
EXPORT_SYMBOL(bc_schedule_active);
EXPORT_SYMBOL(bc_inc_rqnum);
EXPORT_SYMBOL(bc_dec_rqnum);
