/*
 * Copyright (C)2003,2004 USAGI/WIDE Project
 *
 * Header for use in defining a given L3 protocol for connection tracking.
 *
 * Author:
 *	Yasuyuki Kozakai @USAGI	<yasuyuki.kozakai@toshiba.co.jp>
 *
 * Derived from include/netfilter_ipv4/ip_conntrack_protocol.h
 */

#ifndef _NF_CONNTRACK_L3PROTO_H
#define _NF_CONNTRACK_L3PROTO_H
#include <linux/netlink.h>
#include <net/netlink.h>
#include <linux/seq_file.h>
#include <net/netfilter/nf_conntrack.h>

struct nf_conntrack_l3proto
{
	/* L3 Protocol Family number. ex) PF_INET */
	u_int16_t l3proto;

	/* Protocol name */
	const char *name;

	/*
	 * Try to fill in the third arg: nhoff is offset of l3 proto
         * hdr.  Return true if possible.
	 */
	bool (*pkt_to_tuple)(const struct sk_buff *skb, unsigned int nhoff,
			     struct nf_conntrack_tuple *tuple);

	/*
	 * Invert the per-proto part of the tuple: ie. turn xmit into reply.
	 * Some packets can't be inverted: return 0 in that case.
	 */
	bool (*invert_tuple)(struct nf_conntrack_tuple *inverse,
			     const struct nf_conntrack_tuple *orig);

	/* Print out the per-protocol part of the tuple. */
	int (*print_tuple)(struct seq_file *s,
			   const struct nf_conntrack_tuple *);

	/* Called when a conntrack entry is destroyed */
	void (*destroy)(struct nf_conn *conntrack);

	/*
	 * Called before tracking. 
	 *	*dataoff: offset of protocol header (TCP, UDP,...) in skb
	 *	*protonum: protocol number
	 */
	int (*get_l4proto)(const struct sk_buff *skb, unsigned int nhoff,
			   unsigned int *dataoff, u_int8_t *protonum);

	int (*tuple_to_nlattr)(struct sk_buff *skb,
			       const struct nf_conntrack_tuple *t);

	int (*nlattr_to_tuple)(struct nlattr *tb[],
			       struct nf_conntrack_tuple *t);
	const struct nla_policy *nla_policy;

#ifdef CONFIG_SYSCTL
	struct ctl_table_header	*ctl_table_header;
	struct ctl_path		*ctl_table_path;
	struct ctl_table	*ctl_table;
#endif /* CONFIG_SYSCTL */

	/* Module (if any) which this is connected to. */
	struct module *me;
};

/* virtualization of l3 protocol's sysctl tables: */
#if defined(CONFIG_VE_IPTABLES)
#include <linux/sched.h>
#define ve_nf_ct3			(get_exec_env()->_nf_conntrack)
#endif

#if defined(CONFIG_VE_IPTABLES) && defined(CONFIG_SYSCTL)
#define ve_nf_ct_l3protos		ve_nf_ct3->_nf_ct_l3protos
#define ve_nf_conntrack_l3proto_ipv4	(ve_nf_ct3->_nf_conntrack_l3proto_ipv4)
#define	ve_nf_conntrack_l3proto_ipv6	(ve_nf_ct3->_nf_conntrack_l3proto_ipv6)
#define ve_nf_conntrack_max		(ve_nf_ct3->_nf_conntrack_max)
#define ve_nf_conntrack_count		(ve_nf_ct3->_nf_conntrack_count)
#define ve_nf_conntrack_checksum	(ve_nf_ct3->_nf_conntrack_checksum)
#else /* !CONFIG_VE_IPTABLES || !CONFIG_SYSCTL: */
#define ve_nf_ct_l3protos		nf_ct_l3protos
#define ve_nf_conntrack_l3proto_ipv4	&nf_conntrack_l3proto_ipv4
#define ve_nf_conntrack_l3proto_ipv6	&nf_conntrack_l3proto_ipv6
#define ve_nf_conntrack_max		nf_conntrack_max
#define ve_nf_conntrack_count		nf_conntrack_count
#define ve_nf_conntrack_checksum	nf_conntrack_checksum
#endif /* CONFIG_VE_IPTABLES && CONFIG_SYSCTL */

extern int init_nf_ct_l3proto_ipv4(void);
extern void fini_nf_ct_l3proto_ipv4(void);
extern int init_nf_ct_l3proto_ipv6(void);
extern void fini_nf_ct_l3proto_ipv6(void);

extern struct nf_conntrack_l3proto *nf_ct_l3protos[AF_MAX];

/* Protocol registration. */
extern int nf_conntrack_l3proto_register(struct nf_conntrack_l3proto *proto);
extern void nf_conntrack_l3proto_unregister(struct nf_conntrack_l3proto *proto);
extern struct nf_conntrack_l3proto *nf_ct_l3proto_find_get(u_int16_t l3proto);
extern void nf_ct_l3proto_put(struct nf_conntrack_l3proto *p);

/* Existing built-in protocols */
extern struct nf_conntrack_l3proto nf_conntrack_l3proto_generic;

static inline struct nf_conntrack_l3proto *
__nf_ct_l3proto_find(u_int16_t l3proto)
{
	if (unlikely(l3proto >= AF_MAX))
		return &nf_conntrack_l3proto_generic;
#ifdef CONFIG_VE_IPTABLES
	if (!get_exec_env()->_nf_conntrack)
		return &nf_conntrack_l3proto_generic;
#endif
	return rcu_dereference(ve_nf_ct_l3protos[l3proto]);
}

#endif /*_NF_CONNTRACK_L3PROTO_H*/
