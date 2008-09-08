/* (C) 1999-2001 Paul `Rusty' Russell
 * (C) 2002-2004 Netfilter Core Team <coreteam@netfilter.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/types.h>
#include <linux/jiffies.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/netfilter.h>
#include <net/netfilter/nf_conntrack_l4proto.h>

static unsigned int nf_ct_generic_timeout __read_mostly = 600*HZ;

static bool generic_pkt_to_tuple(const struct sk_buff *skb,
				 unsigned int dataoff,
				 struct nf_conntrack_tuple *tuple)
{
	tuple->src.u.all = 0;
	tuple->dst.u.all = 0;

	return true;
}

static bool generic_invert_tuple(struct nf_conntrack_tuple *tuple,
				 const struct nf_conntrack_tuple *orig)
{
	tuple->src.u.all = 0;
	tuple->dst.u.all = 0;

	return true;
}

/* Print out the per-protocol part of the tuple. */
static int generic_print_tuple(struct seq_file *s,
			       const struct nf_conntrack_tuple *tuple)
{
	return 0;
}

/* Returns verdict for packet, or -1 for invalid. */
static int packet(struct nf_conn *ct,
		  const struct sk_buff *skb,
		  unsigned int dataoff,
		  enum ip_conntrack_info ctinfo,
		  int pf,
		  unsigned int hooknum)
{
	nf_ct_refresh_acct(ct, ctinfo, skb, ve_nf_ct_generic_timeout);
	return NF_ACCEPT;
}

/* Called when a new connection for this protocol found. */
static bool new(struct nf_conn *ct, const struct sk_buff *skb,
		unsigned int dataoff)
{
	return true;
}

#ifdef CONFIG_SYSCTL
static struct ctl_table_header *generic_sysctl_header;
static struct ctl_table generic_sysctl_table[] = {
	{
		.procname	= "nf_conntrack_generic_timeout",
		.data		= &nf_ct_generic_timeout,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_jiffies,
	},
	{
		.ctl_name	= 0
	}
};
#ifdef CONFIG_NF_CONNTRACK_PROC_COMPAT
static struct ctl_table generic_compat_sysctl_table[] = {
	{
		.procname	= "ip_conntrack_generic_timeout",
		.data		= &nf_ct_generic_timeout,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_jiffies,
	},
	{
		.ctl_name	= 0
	}
};
#endif /* CONFIG_NF_CONNTRACK_PROC_COMPAT */
#endif /* CONFIG_SYSCTL */

struct nf_conntrack_l4proto nf_conntrack_l4proto_generic __read_mostly =
{
	.l3proto		= PF_UNSPEC,
	.l4proto		= 0,
	.name			= "unknown",
	.pkt_to_tuple		= generic_pkt_to_tuple,
	.invert_tuple		= generic_invert_tuple,
	.print_tuple		= generic_print_tuple,
	.packet			= packet,
	.new			= new,
#ifdef CONFIG_SYSCTL
	.ctl_table_header	= &generic_sysctl_header,
	.ctl_table		= generic_sysctl_table,
#ifdef CONFIG_NF_CONNTRACK_PROC_COMPAT
	.ctl_compat_table	= generic_compat_sysctl_table,
#endif
#endif
};

#if defined(CONFIG_VE_IPTABLES) && defined(CONFIG_SYSCTL)
int nf_ct_proto_generic_sysctl_init(void)
{
	struct nf_conntrack_l4proto *generic;

	if (ve_is_super(get_exec_env())) {
		generic = &nf_conntrack_l4proto_generic;
		goto out;
	}

	generic = kmemdup(&nf_conntrack_l4proto_generic,
			sizeof(struct nf_conntrack_l4proto), GFP_KERNEL);
	if (generic == NULL)
		goto no_mem_ct;

	generic->ctl_table_header = &ve_generic_sysctl_header;
	generic->ctl_table = kmemdup(generic_sysctl_table,
			sizeof(generic_sysctl_table), GFP_KERNEL);
	if (generic->ctl_table == NULL)
		goto no_mem_sys;

	generic->ctl_table[0].data = &ve_nf_ct_generic_timeout;
#ifdef CONFIG_NF_CONNTRACK_PROC_COMPAT
	generic->ctl_compat_table_header = ve_generic_compat_sysctl_header;
	generic->ctl_compat_table = kmemdup(generic_compat_sysctl_table,
			sizeof(generic_compat_sysctl_table), GFP_KERNEL);
	if (generic->ctl_compat_table == NULL)
		goto no_mem_compat;
	generic->ctl_compat_table[0].data = &ve_nf_ct_generic_timeout;
#endif
out:
	ve_nf_ct_generic_timeout = nf_ct_generic_timeout;

	ve_nf_conntrack_l4proto_generic = generic;
	return 0;

#ifdef CONFIG_NF_CONNTRACK_PROC_COMPAT
no_mem_compat:
	kfree(generic->ctl_table);
#endif
no_mem_sys:
	kfree(generic);
no_mem_ct:
	return -ENOMEM;
}
EXPORT_SYMBOL(nf_ct_proto_generic_sysctl_init);

void nf_ct_proto_generic_sysctl_cleanup(void)
{
	if (!ve_is_super(get_exec_env())) {
#ifdef CONFIG_NF_CONNTRACK_PROC_COMPAT
		kfree(ve_nf_conntrack_l4proto_generic->ctl_compat_table);
#endif
		kfree(ve_nf_conntrack_l4proto_generic->ctl_table);

		kfree(ve_nf_conntrack_l4proto_generic);
	}
}
EXPORT_SYMBOL(nf_ct_proto_generic_sysctl_cleanup);
#endif /* CONFIG_VE_IPTABLES && CONFIG_SYSCTL */
