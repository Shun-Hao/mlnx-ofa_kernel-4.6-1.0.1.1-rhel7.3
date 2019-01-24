#ifndef _MLNX_LINUX_RADIX_TREE_H
#define _MLNX_LINUX_RADIX_TREE_H

#include "../../compat/config.h"

#include_next <linux/radix-tree.h>


#ifndef HAVE_IDR_PRELOAD_EXPORTED
#define idr_preload LINUX_BACKPORT(idr_preload)
extern void idr_preload(gfp_t gfp_mask);
#endif
#ifndef HAVE_RADIX_TREE_NEXT_CHUNK
#define radix_tree_root         xarray
#define radix_tree_node         xa_node
enum {
	RADIX_TREE_ITER_TAG_MASK = 0x0f,	/* tag index in lower nybble */
	RADIX_TREE_ITER_TAGGED   = 0x10,	/* lookup tagged slots */
	RADIX_TREE_ITER_CONTIG   = 0x20,	/* stop at first hole */
};

struct radix_tree_iter {
	unsigned long	index;
	unsigned long	next_index;
	unsigned long	tags;
	struct radix_tree_node *node;
};

#define ROOT_TAG_SHIFT  (__GFP_BITS_SHIFT)
#ifdef RADIX_TREE_RETRY
#undef RADIX_TREE_RETRY
#endif
#define RADIX_TREE_RETRY        XA_RETRY_ENTRY
#define XA_RETRY_ENTRY		xa_mk_internal(257)

#define RADIX_TREE_MAP_SHIFT    XA_CHUNK_SHIFT
#define RADIX_TREE_MAP_SIZE     (1UL << RADIX_TREE_MAP_SHIFT)
#define RADIX_TREE_MAP_MASK     (RADIX_TREE_MAP_SIZE-1)

//#define RADIX_TREE_MAX_TAGS     XA_MAX_MARKS
#define RADIX_TREE_TAG_LONGS    XA_MARK_LONGS
#ifdef INIT_RADIX_TREE
#undef INIT_RADIX_TREE
#define INIT_RADIX_TREE(root, mask) xa_init_flags(root, mask)
#endif
#define RADIX_TREE_INDEX_BITS  (8 /* CHAR_BIT */ * sizeof(unsigned long))
#define RADIX_TREE_MAX_PATH (DIV_ROUND_UP(RADIX_TREE_INDEX_BITS, \
                                          RADIX_TREE_MAP_SHIFT))
#ifndef XA_CHUNK_SHIFT
#define XA_CHUNK_SHIFT          (CONFIG_BASE_SMALL ? 4 : 6)
#endif
#define XA_CHUNK_SIZE           (1UL << XA_CHUNK_SHIFT)
#define XA_CHUNK_MASK           (XA_CHUNK_SIZE - 1)
#define XA_MAX_MARKS            3
#define XA_MARK_LONGS           DIV_ROUND_UP(XA_CHUNK_SIZE, BITS_PER_LONG)
#define ROOT_IS_IDR	((__force gfp_t)4)
#define IDR_FREE        0
#define ___GFP_DIRECT_RECLAIM   0x200000u
#define __GFP_DIRECT_RECLAIM    ((__force gfp_t)___GFP_DIRECT_RECLAIM) /* Caller can reclaim */
#define __GFP_KSWAPD_RECLAIM    ((__force gfp_t)___GFP_KSWAPD_RECLAIM) /* kswapd can wake */
#define __GFP_RECLAIM ((__force gfp_t)(___GFP_DIRECT_RECLAIM|___GFP_KSWAPD_RECLAIM))
#ifndef __rcu
#define __rcu 
#endif
struct xarray {
	spinlock_t      xa_lock;
		/* private: The rest of the data structure is not to be used
		 * directly. */
        gfp_t           xa_flags;
	void __rcu *    xa_head;
};

struct xa_node {
	unsigned char	shift;		/* Bits remaining in each slot */
	unsigned char	offset;		/* Slot offset in parent */
	unsigned char	count;		/* Total entry count */
	unsigned char	nr_values;	/* Value entry count */
	struct xa_node __rcu *parent;	/* NULL at top of tree */
	struct xarray	*array;		/* The array we belong to */
	union {
		struct list_head private_list;	/* For tree user */
		struct rcu_head	rcu_head;	/* Used when freeing node */
	};
	void __rcu	*slots[XA_CHUNK_SIZE];
	union {
		unsigned long	tags[XA_MAX_MARKS][XA_MARK_LONGS];
		unsigned long	marks[XA_MAX_MARKS][XA_MARK_LONGS];
	};
};


#ifndef HAVE_RADIX_TREE_NEXT_CHUNK
#define radix_tree_next_chunk LINUX_BACKPORT(radix_tree_next_chunk)
extern void __rcu **radix_tree_next_chunk(const struct radix_tree_root *root, struct radix_tree_iter *iter, unsigned flags);

#define radix_tree_lookup_slot  LINUX_BACKPORT(radix_tree_lookup_slot)
extern void __rcu **radix_tree_lookup_slot(const struct radix_tree_root *root,	unsigned long index);
#define radix_tree_lookup  LINUX_BACKPORT(radix_tree_lookup)
extern void *radix_tree_lookup(const struct radix_tree_root *root, unsigned long index);

#define radix_tree_insert LINUX_BACKPORT(radix_tree_insert)
extern int radix_tree_insert(struct radix_tree_root *root, unsigned long index, void *item);

#define radix_tree_delete LINUX_BACKPORT(radix_tree_delete)
extern void *radix_tree_delete(struct radix_tree_root *root, unsigned long index);

#endif

enum xa_lock_type {
	XA_LOCK_IRQ = 1,
	XA_LOCK_BH = 2,
};

static inline unsigned int xa_lock_type(const struct xarray *xa)
{
	return (__force unsigned int)xa->xa_flags & 3;
}
static inline void xa_init_flags(struct xarray *xa, gfp_t flags)
{
	unsigned int lock_type;
	static struct lock_class_key xa_lock_irq;
	static struct lock_class_key xa_lock_bh;

	spin_lock_init(&xa->xa_lock);
	xa->xa_flags = flags;
	xa->xa_head = NULL;

	lock_type = xa_lock_type(xa);
	if (lock_type == XA_LOCK_IRQ)
		lockdep_set_class(&xa->xa_lock, &xa_lock_irq);
	else if (lock_type == XA_LOCK_BH)
		lockdep_set_class(&xa->xa_lock, &xa_lock_bh);
}
static inline unsigned long
__radix_tree_iter_add(struct radix_tree_iter *iter, unsigned long slots)
{
		return iter->index + slots;
}
static __always_inline long
radix_tree_chunk_size(struct radix_tree_iter *iter)
{
	return iter->next_index - iter->index;
}
static inline bool gfpflags_allow_blocking(const gfp_t gfp_flags)
{
	        return !!(gfp_flags & __GFP_DIRECT_RECLAIM);
}
static inline bool xa_is_value(const void *entry)
{
	        return (unsigned long)entry & 1;
}

static __always_inline void __rcu **
radix_tree_iter_init(struct radix_tree_iter *iter, unsigned long start)
{
	/*
	 *	 * Leave iter->tags uninitialized.
	 *	 radix_tree_next_chunk() will fill it
	 *		 * in the case of a successful tagged chunk
	 *		 lookup.  If the lookup was
	 *			 * unsuccessful or non-tagged then
	 *			 nobody cares about ->tags.
	 *				 *
	 *					 * Set index to zero to
	 *					 bypass next_index
	 *					 overflow protection.
	 *						 * See the
	 *						 comment in
	 *						 radix_tree_next_chunk()
	 *						 for details.
	 *							 */
	iter->index = 0;
	iter->next_index = start;
	return NULL;
}
#ifndef radix_tree_for_each_slot
#define radix_tree_for_each_slot(slot, root, iter, start)		\
	for (slot = radix_tree_iter_init(iter, start) ;			\
			     slot || (slot = radix_tree_next_chunk(root, iter, 0)) ;	\
				     slot = radix_tree_next_slot(slot, iter, 0))
#endif
static __always_inline void __rcu **radix_tree_next_slot(void __rcu **slot,
		struct radix_tree_iter *iter, unsigned flags)
{
	if (flags & RADIX_TREE_ITER_TAGGED) {
		iter->tags >>= 1;
		if (unlikely(!iter->tags))
			return NULL;
		if (likely(iter->tags & 1ul)) {
			iter->index = __radix_tree_iter_add(iter, 1);
			slot++;
			goto found;
		}
		if (!(flags & RADIX_TREE_ITER_CONTIG)) {
			unsigned offset = __ffs(iter->tags);

			iter->tags >>= offset++;
			iter->index = __radix_tree_iter_add(iter, offset);
			slot += offset;
			goto found;
		}
	} else {
		long count = radix_tree_chunk_size(iter);

		while (--count > 0) {
			slot++;
			iter->index = __radix_tree_iter_add(iter, 1);

			if (likely(*slot))
				goto found;
			if (flags & RADIX_TREE_ITER_CONTIG) {
				/* forbid
				 * switching
				 * to
				 * the
				 * next
				 * chunk
				 * */
				iter->next_index = 0;
				break;
			}
		}
	}
	return NULL;

found:
	return slot;
}

#endif /* HAVE_RADIX_TREE_NEXT_CHUNK */
#ifndef HAVE_RADIX_TREE_IS_INTERNAL
#define RADIX_TREE_ENTRY_MASK           3UL
#define RADIX_TREE_INTERNAL_NODE        2UL
static inline bool radix_tree_is_internal_node(void *ptr)
{
	return ((unsigned long)ptr & RADIX_TREE_ENTRY_MASK) ==
		RADIX_TREE_INTERNAL_NODE;
}
#endif
#endif /* _MLNX_LINUX_RADIX_TREE_H */
