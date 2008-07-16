/*
 * This header is used to share core functionality between the
 * standalone connection tracking module, and the compatibility layer's use
 * of connection tracking.
 *
 * 16 Dec 2003: Yasuyuki Kozakai @USAGI <yasuyuki.kozakai@toshiba.co.jp>
 *	- generalize L3 protocol dependent part.
 *
 * Derived from include/linux/netfiter_ipv4/ip_conntrack_core.h
 */

#ifndef _NF_CONNTRACK_CORE_H
#define _NF_CONNTRACK_CORE_H

#include <linux/netfilter.h>
#include <net/netfilter/nf_conntrack_l3proto.h>
#include <net/netfilter/nf_conntrack_l4proto.h>
#include <net/netfilter/nf_conntrack_ecache.h>

/* This header is used to share core functionality between the
   standalone connection tracking module, and the compatibility layer's use
   of connection tracking. */
extern unsigned int nf_conntrack_in(int pf,
				    unsigned int hooknum,
				    struct sk_buff *skb);

extern int nf_conntrack_init(void);
extern void nf_conntrack_cleanup(void);

extern int nf_conntrack_proto_init(void);
extern void nf_conntrack_proto_fini(void);

extern bool
nf_ct_get_tuple(const struct sk_buff *skb,
		unsigned int nhoff,
		unsigned int dataoff,
		u_int16_t l3num,
		u_int8_t protonum,
		struct nf_conntrack_tuple *tuple,
		const struct nf_conntrack_l3proto *l3proto,
		const struct nf_conntrack_l4proto *l4proto);

extern bool
nf_ct_invert_tuple(struct nf_conntrack_tuple *inverse,
		   const struct nf_conntrack_tuple *orig,
		   const struct nf_conntrack_l3proto *l3proto,
		   const struct nf_conntrack_l4proto *l4proto);

/* Find a connection corresponding to a tuple. */
extern struct nf_conntrack_tuple_hash *
nf_conntrack_find_get(const struct nf_conntrack_tuple *tuple);

extern int __nf_conntrack_confirm(struct sk_buff *skb);

#if defined(CONFIG_VE_IPTABLES)
#include <linux/sched.h>
#define ve_nf_conntrack_hash	(get_exec_env()->_nf_conntrack->_nf_conntrack_hash)
#define ve_nf_conntrack_vmalloc	(get_exec_env()->_nf_conntrack->_nf_conntrack_vmalloc)
#define ve_unconfirmed		(get_exec_env()->_nf_conntrack->_unconfirmed)
#else
#define ve_nf_conntrack_hash		nf_conntrack_hash
#define ve_nf_conntrack_vmalloc		nf_conntrack_vmalloc
#define ve_unconfirmed			unconfirmed
#endif /* CONFIG_VE_IPTABLES */

#if defined(CONFIG_VE_IPTABLES) && defined(CONFIG_SYSCTL)
#define ve_nf_ct_sysctl_header		\
		(get_exec_env()->_nf_conntrack->_nf_ct_sysctl_header)
#define ve_nf_ct_sysctl_table		\
		(get_exec_env()->_nf_conntrack->_nf_ct_sysctl_table)
#define ve_nf_ct_netfilter_table	\
		(get_exec_env()->_nf_conntrack->_nf_ct_netfilter_table)
#define ve_nf_ct_net_table		\
		(get_exec_env()->_nf_conntrack->_nf_ct_net_table)
extern void nf_ct_proto_generic_sysctl_cleanup(void);
extern int nf_ct_proto_generic_sysctl_init(void);
#else
#define ve_nf_ct_sysctl_header		nf_ct_sysctl_header
#define ve_nf_ct_sysctl_table		nf_ct_sysctl_table
#define ve_nf_ct_netfilter_table	nf_ct_netfilter_table
#define ve_nf_ct_net_table		nf_ct_net_table
static inline int nf_ct_proto_generic_sysctl_init(void)
{
	return 0;
}
static inline void nf_ct_proto_generic_sysctl_cleanup(void)
{
}
#endif /* CONFIG_VE_IPTABLES */

/* Confirm a connection: returns NF_DROP if packet must be dropped. */
static inline int nf_conntrack_confirm(struct sk_buff *skb)
{
	struct nf_conn *ct = (struct nf_conn *)skb->nfct;
	int ret = NF_ACCEPT;

	if (ct) {
		if (!nf_ct_is_confirmed(ct) && !nf_ct_is_dying(ct))
			ret = __nf_conntrack_confirm(skb);
		nf_ct_deliver_cached_events(ct);
	}
	return ret;
}

int
print_tuple(struct seq_file *s, const struct nf_conntrack_tuple *tuple,
            const struct nf_conntrack_l3proto *l3proto,
            const struct nf_conntrack_l4proto *proto);

#ifndef CONFIG_VE_IPTABLES
extern struct hlist_head *nf_conntrack_hash;
#endif
extern spinlock_t nf_conntrack_lock ;
extern struct hlist_head unconfirmed;

#endif /* _NF_CONNTRACK_CORE_H */
