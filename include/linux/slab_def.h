#ifndef _LINUX_SLAB_DEF_H
#define	_LINUX_SLAB_DEF_H

/*
 * Definitions unique to the original Linux SLAB allocator.
 *
 * What we provide here is a way to optimize the frequent kmalloc
 * calls in the kernel by selecting the appropriate general cache
 * if kmalloc was called with a size that can be established at
 * compile time.
 */

#include <linux/init.h>
#include <asm/page.h>		/* kmalloc_sizes.h needs PAGE_SIZE */
#include <asm/cache.h>		/* kmalloc_sizes.h needs L1_CACHE_BYTES */
#include <linux/compiler.h>

/*
 * DEBUG	- 1 for kmem_cache_create() to honour; SLAB_RED_ZONE & SLAB_POISON.
 *		  0 for faster, smaller code (especially in the critical paths).
 *
 * STATS	- 1 to collect stats for /proc/slabinfo.
 *		  0 for faster, smaller code (especially in the critical paths).
 *
 * FORCED_DEBUG	- 1 enables SLAB_RED_ZONE and SLAB_POISON (if possible)
 */

#ifdef CONFIG_DEBUG_SLAB
#define	SLAB_DEBUG		1
#define	SLAB_STATS		1
#define SLAB_FORCED_DEBUG	1
#else
#define	SLAB_DEBUG		0
#define	SLAB_STATS		0
#define SLAB_FORCED_DEBUG	0
#endif

/*
 * struct kmem_cache
 *
 * manages a cache.
 */

struct kmem_cache {
/* 1) per-cpu data, touched during every alloc/free */
	struct array_cache *array[NR_CPUS];
/* 2) Cache tunables. Protected by cache_chain_mutex */
	unsigned int batchcount;
	unsigned int limit;
	unsigned int shared;

	unsigned int buffer_size;
	u32 reciprocal_buffer_size;
/* 3) touched by every alloc & free from the backend */

	unsigned int flags;		/* constant flags */
	unsigned int num;		/* # of objs per slab */

/* 4) cache_grow/shrink */
	/* order of pgs per slab (2^n) */
	unsigned int gfporder;

	/* force GFP flags, e.g. GFP_DMA */
	gfp_t gfpflags;

	size_t colour;			/* cache colouring range */
	unsigned int colour_off;	/* colour offset */
	struct kmem_cache *slabp_cache;
	unsigned int slab_size;
	unsigned int dflags;		/* dynamic flags */

	/* constructor func */
	void (*ctor) (struct kmem_cache *, void *);

/* 5) cache creation/removal */
	const char *name;
	struct list_head next;

/* 6) statistics */
	unsigned long grown;
	unsigned long reaped;
	unsigned long shrunk;
#if SLAB_STATS
	unsigned long num_active;
	unsigned long num_allocations;
	unsigned long high_mark;
	unsigned long errors;
	unsigned long max_freeable;
	unsigned long node_allocs;
	unsigned long node_frees;
	unsigned long node_overflow;
	atomic_t allochit;
	atomic_t allocmiss;
	atomic_t freehit;
	atomic_t freemiss;
#endif
#if SLAB_DEBUG
	/*
	 * If debugging is enabled, then the allocator can add additional
	 * fields and/or padding to every object. buffer_size contains the total
	 * object size including these internal fields, the following two
	 * variables contain the offset to the user object and its size.
	 */
	int obj_offset;
	int obj_size;
#endif
#ifdef CONFIG_BEANCOUNTERS
	int objuse;
#endif
	/*
	 * We put nodelists[] at the end of kmem_cache, because we want to size
	 * this array to nr_node_ids slots instead of MAX_NUMNODES
	 * (see kmem_cache_init())
	 * We still use [MAX_NUMNODES] and not [1] or [0] because cache_cache
	 * is statically defined, so we reserve the max number of nodes.
	 */
	struct kmem_list3 *nodelists[MAX_NUMNODES];
	/*
	 * Do not add fields after nodelists[]
	 */
};

/* Size description struct for general caches. */
struct cache_sizes {
	size_t		 	cs_size;
	struct kmem_cache	*cs_cachep;
#ifdef CONFIG_ZONE_DMA
	struct kmem_cache	*cs_dmacachep;
#endif
};
extern struct cache_sizes malloc_sizes[];
extern int malloc_cache_num;

void *kmem_cache_alloc(struct kmem_cache *, gfp_t);
void *__kmalloc(size_t size, gfp_t flags);

static inline void *kmalloc(size_t size, gfp_t flags)
{
	if (__builtin_constant_p(size)) {
		int i = 0;

		if (!size)
			return ZERO_SIZE_PTR;

#define CACHE(x) \
		if (size <= x) \
			goto found; \
		else \
			i++;
#include <linux/kmalloc_sizes.h>
#undef CACHE
		{
			extern void __you_cannot_kmalloc_that_much(void);
			__you_cannot_kmalloc_that_much();
		}
found:
		if (flags & __GFP_UBC)
			i += malloc_cache_num;
#ifdef CONFIG_ZONE_DMA
		if (flags & GFP_DMA)
			return kmem_cache_alloc(malloc_sizes[i].cs_dmacachep,
						flags);
#endif
		return kmem_cache_alloc(malloc_sizes[i].cs_cachep, flags);
	}
	return __kmalloc(size, flags);
}

#ifdef CONFIG_NUMA
extern void *__kmalloc_node(size_t size, gfp_t flags, int node);
extern void *kmem_cache_alloc_node(struct kmem_cache *, gfp_t flags, int node);

static inline void *kmalloc_node(size_t size, gfp_t flags, int node)
{
	if (__builtin_constant_p(size)) {
		int i = 0;

		if (!size)
			return ZERO_SIZE_PTR;

#define CACHE(x) \
		if (size <= x) \
			goto found; \
		else \
			i++;
#include <linux/kmalloc_sizes.h>
#undef CACHE
		{
			extern void __you_cannot_kmalloc_that_much(void);
			__you_cannot_kmalloc_that_much();
		}
found:
#ifdef CONFIG_ZONE_DMA
		if (flags & GFP_DMA)
			return kmem_cache_alloc_node(malloc_sizes[i].cs_dmacachep,
						flags, node);
#endif
		return kmem_cache_alloc_node(malloc_sizes[i].cs_cachep,
						flags, node);
	}
	return __kmalloc_node(size, flags, node);
}

#endif	/* CONFIG_NUMA */

#endif	/* _LINUX_SLAB_DEF_H */
