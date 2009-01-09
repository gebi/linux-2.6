/*
 * Header for use in defining a given L4 protocol for connection tracking.
 *
 * 16 Dec 2003: Yasuyuki Kozakai @USAGI <yasuyuki.kozakai@toshiba.co.jp>
 *	- generalized L3 protocol dependent part.
 *
 * Derived from include/linux/netfiter_ipv4/ip_conntrack_protcol.h
 */

#ifndef _NF_CONNTRACK_L4PROTO_H
#define _NF_CONNTRACK_L4PROTO_H
#include <linux/netlink.h>
#include <net/netlink.h>
#include <net/netfilter/nf_conntrack.h>

struct seq_file;

struct nf_conntrack_l4proto
{
	/* L3 Protocol number. */
	u_int16_t l3proto;

	/* L4 Protocol number. */
	u_int8_t l4proto;

	/* Try to fill in the third arg: dataoff is offset past network protocol
           hdr.  Return true if possible. */
	bool (*pkt_to_tuple)(const struct sk_buff *skb, unsigned int dataoff,
			     struct nf_conntrack_tuple *tuple);

	/* Invert the per-proto part of the tuple: ie. turn xmit into reply.
	 * Some packets can't be inverted: return 0 in that case.
	 */
	bool (*invert_tuple)(struct nf_conntrack_tuple *inverse,
			     const struct nf_conntrack_tuple *orig);

	/* Returns verdict for packet, or -1 for invalid. */
	int (*packet)(struct nf_conn *ct,
		      const struct sk_buff *skb,
		      unsigned int dataoff,
		      enum ip_conntrack_info ctinfo,
		      int pf,
		      unsigned int hooknum);

	/* Called when a new connection for this protocol found;
	 * returns TRUE if it's OK.  If so, packet() called next. */
	bool (*new)(struct nf_conn *ct, const struct sk_buff *skb,
		    unsigned int dataoff);

	/* Called when a conntrack entry is destroyed */
	void (*destroy)(struct nf_conn *ct);

	int (*error)(struct sk_buff *skb, unsigned int dataoff,
		     enum ip_conntrack_info *ctinfo,
		     int pf, unsigned int hooknum);

	/* Print out the per-protocol part of the tuple. Return like seq_* */
	int (*print_tuple)(struct seq_file *s,
			   const struct nf_conntrack_tuple *);

	/* Print out the private part of the conntrack. */
	int (*print_conntrack)(struct seq_file *s, const struct nf_conn *);

	/* convert protoinfo to nfnetink attributes */
	int (*to_nlattr)(struct sk_buff *skb, struct nlattr *nla,
			 const struct nf_conn *ct);

	/* convert nfnetlink attributes to protoinfo */
	int (*from_nlattr)(struct nlattr *tb[], struct nf_conn *ct);

	int (*tuple_to_nlattr)(struct sk_buff *skb,
			       const struct nf_conntrack_tuple *t);
	int (*nlattr_to_tuple)(struct nlattr *tb[],
			       struct nf_conntrack_tuple *t);
	const struct nla_policy *nla_policy;

#ifdef CONFIG_SYSCTL
	struct ctl_table_header	**ctl_table_header;
	struct ctl_table	*ctl_table;
	unsigned int		*ctl_table_users;
#ifdef CONFIG_NF_CONNTRACK_PROC_COMPAT
	struct ctl_table_header	*ctl_compat_table_header;
	struct ctl_table	*ctl_compat_table;
#endif
#endif
	/* Protocol name */
	const char *name;

	/* Module (if any) which this is connected to. */
	struct module *me;
};

/* Existing built-in protocols */
extern struct nf_conntrack_l4proto nf_conntrack_l4proto_tcp6;
extern struct nf_conntrack_l4proto nf_conntrack_l4proto_udp4;
extern struct nf_conntrack_l4proto nf_conntrack_l4proto_udp6;
extern struct nf_conntrack_l4proto nf_conntrack_l4proto_generic;

#define MAX_NF_CT_PROTO 256
extern struct nf_conntrack_l4proto **nf_ct_protos[PF_MAX];

extern struct nf_conntrack_l4proto *
__nf_ct_l4proto_find(u_int16_t l3proto, u_int8_t l4proto);

extern struct nf_conntrack_l4proto *
nf_ct_l4proto_find_get(u_int16_t l3proto, u_int8_t protocol);

extern void nf_ct_l4proto_put(struct nf_conntrack_l4proto *p);

/* Protocol registration. */
extern int nf_conntrack_l4proto_register(struct nf_conntrack_l4proto *proto);
extern void nf_conntrack_l4proto_unregister(struct nf_conntrack_l4proto *proto);

/* Generic netlink helpers */
extern int nf_ct_port_tuple_to_nlattr(struct sk_buff *skb,
				      const struct nf_conntrack_tuple *tuple);
extern int nf_ct_port_nlattr_to_tuple(struct nlattr *tb[],
				      struct nf_conntrack_tuple *t);
extern const struct nla_policy nf_ct_port_nla_policy[];

#ifdef CONFIG_SYSCTL
/* Log invalid packets */
extern unsigned int nf_ct_log_invalid;
#endif

#ifdef CONFIG_VE_IPTABLES
#include <linux/sched.h>
#define ve_nf_ct4			(get_exec_env()->_nf_conntrack)
#define ve_nf_ct_initialized()		(get_exec_env()->_nf_conntrack != NULL)
#else
#define ve_nf_ct_initialized()		1
#endif

#if defined(CONFIG_VE_IPTABLES) && defined(CONFIG_SYSCTL)

#define ve_nf_ct_protos			(ve_nf_ct4->_nf_ct_protos)
#define ve_nf_conntrack_l4proto_icmp	(ve_nf_ct4->_nf_conntrack_l4proto_icmp)
#define ve_nf_conntrack_l4proto_icmpv6	\
				(ve_nf_ct4->_nf_conntrack_l4proto_icmpv6)
#define ve_nf_conntrack_l4proto_tcp4	(ve_nf_ct4->_nf_conntrack_l4proto_tcp4)
#define ve_nf_conntrack_l4proto_tcp6	(ve_nf_ct4->_nf_conntrack_l4proto_tcp6)
#define ve_nf_conntrack_l4proto_udp4	(ve_nf_ct4->_nf_conntrack_l4proto_udp4)
#define ve_nf_conntrack_l4proto_udp6	(ve_nf_ct4->_nf_conntrack_l4proto_udp6)
#define ve_nf_conntrack_l4proto_generic		\
				(ve_nf_ct4->_nf_conntrack_l4proto_generic)
#define ve_nf_ct_log_invalid		(ve_nf_ct4->_nf_ct_log_invalid)
/* TCP: */
#define ve_nf_ct_tcp_timeouts		(ve_nf_ct4->_nf_ct_tcp_timeouts)
#define ve_nf_ct_tcp_timeout_max_retrans	\
				(ve_nf_ct4->_nf_ct_tcp_timeout_max_retrans)
#define ve_nf_ct_tcp_timeout_unacknowledged	\
				(ve_nf_ct4->_nf_ct_tcp_timeout_unacknowledged)
#define ve_nf_ct_tcp_max_retrans	(ve_nf_ct4->_nf_ct_tcp_max_retrans)
#define ve_nf_ct_tcp_loose		(ve_nf_ct4->_nf_ct_tcp_loose)
#define ve_nf_ct_tcp_be_liberal		(ve_nf_ct4->_nf_ct_tcp_be_liberal)
#define ve_tcp_sysctl_table_users	(ve_nf_ct4->_tcp_sysctl_table_users)
#define ve_tcp_sysctl_header		(ve_nf_ct4->_tcp_sysctl_header)
#define ve_tcp_compat_sysctl_header	(ve_nf_ct4->_tcp_compat_sysctl_header)
/* UDP: */
#define ve_nf_ct_udp_timeout		(ve_nf_ct4->_nf_ct_udp_timeout)
#define ve_nf_ct_udp_timeout_stream	(ve_nf_ct4->_nf_ct_udp_timeout_stream)
#define ve_udp_sysctl_table_users	(ve_nf_ct4->_udp_sysctl_table_users)
#define ve_udp_sysctl_header		(ve_nf_ct4->_udp_sysctl_header)
#define ve_udp_compat_sysctl_header	(ve_nf_ct4->_udp_compat_sysctl_header)
/* ICMP: */
#define ve_nf_ct_icmp_timeout		(ve_nf_ct4->_nf_ct_icmp_timeout)
#define ve_icmp_sysctl_header		(ve_nf_ct4->_icmp_sysctl_header)
#define ve_icmp_compat_sysctl_header	(ve_nf_ct4->_icmp_compat_sysctl_header)
/* ICMPV6: */
#define ve_nf_ct_icmpv6_timeout		(ve_nf_ct4->_nf_ct_icmpv6_timeout)
#define ve_icmpv6_sysctl_header		(ve_nf_ct4->_icmpv6_sysctl_header)
/* GENERIC: */
#define ve_nf_ct_generic_timeout	(ve_nf_ct4->_nf_ct_generic_timeout)
#define ve_generic_sysctl_header	(ve_nf_ct4->_generic_sysctl_header)
#define ve_generic_compat_sysctl_header	(ve_nf_ct4->_generic_compat_sysctl_header)

extern void nf_ct_proto_icmp_sysctl_cleanup(void);
extern int nf_ct_proto_icmp_sysctl_init(void);
extern void nf_ct_proto_icmpv6_sysctl_cleanup(void);
extern int nf_ct_proto_icmpv6_sysctl_init(void);
extern void nf_ct_proto_tcp_sysctl_cleanup(void);
extern int nf_ct_proto_tcp_sysctl_init(void);
extern void nf_ct_proto_udp_sysctl_cleanup(void);
extern int nf_ct_proto_udp_sysctl_init(void);

#else /* !CONFIG_VE_IPTABLES || !CONFIG_SYSCTL: */

#define ve_nf_ct_protos			nf_ct_protos
#define ve_nf_conntrack_l4proto_icmp	&nf_conntrack_l4proto_icmp
#define ve_nf_conntrack_l4proto_icmpv6	&nf_conntrack_l4proto_icmpv6
#define ve_nf_conntrack_l4proto_tcp4	&nf_conntrack_l4proto_tcp4
#define ve_nf_conntrack_l4proto_tcp6	&nf_conntrack_l4proto_tcp6
#define ve_nf_conntrack_l4proto_udp4	&nf_conntrack_l4proto_udp4
#define ve_nf_conntrack_l4proto_udp6	&nf_conntrack_l4proto_udp6
#define ve_nf_conntrack_l4proto_generic	&nf_conntrack_l4proto_generic

#if defined(CONFIG_SYSCTL)

#define ve_nf_ct_log_invalid		nf_ct_log_invalid
/* TCP: */
#define ve_nf_ct_tcp_timeouts		*tcp_timeouts
#define ve_nf_ct_tcp_timeout_max_retrans	\
					nf_ct_tcp_timeout_max_retrans
#define ve_nf_ct_tcp_timeout_unacknowledged	\
					nf_ct_tcp_timeout_unacknowledged
#define ve_nf_ct_tcp_max_retrans	nf_ct_tcp_max_retrans
#define ve_nf_ct_tcp_loose		nf_ct_tcp_loose
#define ve_nf_ct_tcp_be_liberal		nf_ct_tcp_be_liberal
#define ve_tcp_sysctl_table_users	tcp_sysctl_table_users
#define ve_tcp_sysctl_header		tcp_sysctl_header
/* UDP:*/
#define ve_nf_ct_udp_timeout		nf_ct_udp_timeout
#define ve_nf_ct_udp_timeout_stream	nf_ct_udp_timeout_stream
#define ve_udp_sysctl_table_users	udp_sysctl_table_users
#define ve_udp_sysctl_header		udp_sysctl_header
/* ICMP: */
#define ve_nf_ct_icmp_timeout		nf_ct_icmp_timeout
#define ve_icmp_sysctl_header		icmp_sysctl_header
/* ICMPV6: */
#define ve_nf_ct_icmpv6_timeout		nf_ct_icmpv6_timeout
#define ve_icmpv6_sysctl_header		icmpv6_sysctl_header
/* GENERIC: */
#define ve_nf_ct_generic_timeout	nf_ct_generic_timeout
#define ve_generic_sysctl_header	generic_sysctl_header
#endif /* CONFIG_SYSCTL */

static inline int nf_ct_proto_icmp_sysctl_init(void)
{
	return 0;
}
static inline void nf_ct_proto_icmp_sysctl_cleanup(void)
{
}
static inline int nf_ct_proto_tcp_sysctl_init(void)
{
	return 0;
}
static inline void nf_ct_proto_tcp_sysctl_cleanup(void)
{
}
static inline int nf_ct_proto_udp_sysctl_init(void)
{
	return 0;
}
static inline void nf_ct_proto_udp_sysctl_cleanup(void)
{
}
static inline int nf_ct_proto_icmpv6_sysctl_init(void)
{
	return 0;
}
static inline void nf_ct_proto_icmpv6_sysctl_cleanup(void)
{
}
#endif /* CONFIG_VE_IPTABLES && CONFIG_SYSCTL */

#ifdef CONFIG_SYSCTL
#ifdef DEBUG_INVALID_PACKETS
#define LOG_INVALID(proto) \
	(ve_nf_ct_log_invalid == (proto) || ve_nf_ct_log_invalid == IPPROTO_RAW)
#else
#define LOG_INVALID(proto) \
	((ve_nf_ct_log_invalid == (proto) || ve_nf_ct_log_invalid == IPPROTO_RAW) \
	 && net_ratelimit())
#endif
#else
#define LOG_INVALID(proto) 0
#endif /* CONFIG_SYSCTL */

#endif /*_NF_CONNTRACK_PROTOCOL_H*/
