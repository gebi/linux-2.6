/*
 *  include/linux/vzratelimit.h
 *
 *  Copyright (C) 2005-2009 Parallels Holdings, Ltd.
 *  All rights reserved.
 *  
 *  Licensing governed by "linux/COPYING.Parallels" file.
 *
 */

#ifndef __VZ_RATELIMIT_H__
#define __VZ_RATELIMIT_H__

/*
 * Generic ratelimiting stuff.
 */

struct vz_rate_info {
	int burst;
	int interval; /* jiffy_t per event */
	int bucket; /* kind of leaky bucket */
	unsigned long last; /* last event */
};

/* Return true if rate limit permits. */
int vz_ratelimit(struct vz_rate_info *p);

#endif /* __VZ_RATELIMIT_H__ */
