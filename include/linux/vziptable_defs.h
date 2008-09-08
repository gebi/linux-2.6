#ifndef _LINUX_VZIPTABLE_DEFS_H
#define _LINUX_VZIPTABLE_DEFS_H

/* these masks represent modules */
#define VE_IP_IPTABLES_MOD		(1U<<0)
#define VE_IP_FILTER_MOD		(1U<<1)
#define VE_IP_MANGLE_MOD		(1U<<2)
#define VE_IP_CONNTRACK_MOD		(1U<<14)
#define VE_IP_CONNTRACK_FTP_MOD		(1U<<15)
#define VE_IP_CONNTRACK_IRC_MOD		(1U<<16)
#define VE_IP_NAT_MOD			(1U<<20)
#define VE_IP_NAT_FTP_MOD		(1U<<21)
#define VE_IP_NAT_IRC_MOD		(1U<<22)
#define VE_IP_IPTABLES6_MOD		(1U<<26)
#define VE_IP_FILTER6_MOD		(1U<<27)
#define VE_IP_MANGLE6_MOD		(1U<<28)
#define VE_IP_IPTABLE_NAT_MOD		(1U<<29)
#define VE_NF_CONNTRACK_MOD		(1U<<30)

/* these masks represent modules with their dependences */
#define VE_IP_IPTABLES		(VE_IP_IPTABLES_MOD)
#define VE_IP_FILTER		(VE_IP_FILTER_MOD		\
					| VE_IP_IPTABLES)
#define VE_IP_MANGLE		(VE_IP_MANGLE_MOD		\
					| VE_IP_IPTABLES)
#define VE_IP_IPTABLES6		(VE_IP_IPTABLES6_MOD)
#define VE_IP_FILTER6		(VE_IP_FILTER6_MOD | VE_IP_IPTABLES6)
#define VE_IP_MANGLE6		(VE_IP_MANGLE6_MOD | VE_IP_IPTABLES6)
#define VE_NF_CONNTRACK		(VE_NF_CONNTRACK_MOD | VE_IP_IPTABLES)
#define VE_IP_CONNTRACK		(VE_IP_CONNTRACK_MOD		\
					| VE_IP_IPTABLES)
#define VE_IP_CONNTRACK_FTP	(VE_IP_CONNTRACK_FTP_MOD	\
					| VE_IP_CONNTRACK)
#define VE_IP_CONNTRACK_IRC	(VE_IP_CONNTRACK_IRC_MOD	\
					| VE_IP_CONNTRACK)
#define VE_IP_NAT		(VE_IP_NAT_MOD			\
					| VE_IP_CONNTRACK)
#define VE_IP_NAT_FTP		(VE_IP_NAT_FTP_MOD		\
					| VE_IP_NAT | VE_IP_CONNTRACK_FTP)
#define VE_IP_NAT_IRC		(VE_IP_NAT_IRC_MOD		\
					| VE_IP_NAT | VE_IP_CONNTRACK_IRC)
#define VE_IP_IPTABLE_NAT	(VE_IP_IPTABLE_NAT_MOD | VE_IP_CONNTRACK)

/* safe iptables mask to be used by default */
#define VE_IP_DEFAULT					\
	(VE_IP_IPTABLES |				\
	VE_IP_FILTER | VE_IP_MANGLE)

#define VE_IPT_CMP(x, y)		(((x) & (y)) == (y))

#endif /* _LINUX_VZIPTABLE_DEFS_H */
