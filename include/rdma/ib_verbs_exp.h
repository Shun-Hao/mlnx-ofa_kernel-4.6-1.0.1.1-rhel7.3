#ifndef IB_VERBS_EXP_H
#define IB_VERBS_EXP_H

#include <rdma/ib_verbs.h>

enum ib_cq_cap_flags {
	IB_CQ_IGNORE_OVERRUN           = (1 << 0)
};

struct ib_cq_attr {
	struct {
		u16     cq_count;
		u16     cq_period;
	} moderation;
	u32     cq_cap_flags;
};

struct ib_qpg_init_attrib {
	u32 tss_child_count;
	u32 rss_child_count;
};

struct ib_exp_qp_init_attr {
	void                  (*event_handler)(struct ib_event *, void *);
	void		       *qp_context;
	struct ib_cq	       *send_cq;
	struct ib_cq	       *recv_cq;
	struct ib_srq	       *srq;
	struct ib_xrcd	       *xrcd;     /* XRC TGT QPs only */
	struct ib_qp_cap	cap;
	enum ib_sig_type	sq_sig_type;
	enum ib_qp_type		qp_type;
	enum ib_qp_create_flags	create_flags;
	u8			port_num;
	struct ib_rwq_ind_table *rwq_ind_tbl;
	enum ib_qpg_type        qpg_type;
	union {
		struct ib_qp *qpg_parent; /* see qpg_type */
		struct ib_qpg_init_attrib parent_attrib;
	};
	u32			max_inl_recv;
};

struct ib_exp_masked_atomic_caps {
	u32 max_fa_bit_boudary;
	u32 log_max_atomic_inline_arg;
	u64 masked_log_atomic_arg_sizes;
	u64 masked_log_atomic_arg_sizes_network_endianness;
};

enum ib_exp_device_attr_comp_mask {
	IB_EXP_DEVICE_ATTR_WITH_TIMESTAMP_MASK = 1ULL << 1,
	IB_EXP_DEVICE_ATTR_WITH_HCA_CORE_CLOCK = 1ULL << 2,
	IB_EXP_DEVICE_ATTR_CAP_FLAGS2		= 1ULL << 3,
	IB_EXP_DEVICE_ATTR_DC_REQ_RD		= 1ULL << 4,
	IB_EXP_DEVICE_ATTR_DC_RES_RD		= 1ULL << 5,
	IB_EXP_DEVICE_ATTR_INLINE_RECV_SZ	= 1ULL << 6,
	IB_EXP_DEVICE_ATTR_RSS_TBL_SZ		= 1ULL << 7,
	IB_EXP_DEVICE_ATTR_EXT_ATOMIC_ARGS	= 1ULL << 8,
	IB_EXP_DEVICE_ATTR_MAX_DCT		= 1ULL << 11,
	IB_EXP_DEVICE_ATTR_MAX_CTX_RES_DOMAIN	= 1ULL << 12,
	IB_EXP_DEVICE_ATTR_EXT_MASKED_ATOMICS	= 1ULL << 19,
};

enum ib_exp_device_cap_flags2 {
	IB_EXP_DEVICE_DC_TRANSPORT	= 1 << 0,
	IB_EXP_DEVICE_QPG		= 1 << 1,
	IB_EXP_DEVICE_UD_RSS		= 1 << 2,
	IB_EXP_DEVICE_UD_TSS		= 1 << 3,
	IB_EXP_DEVICE_EXT_ATOMICS	= 1 << 4,
	IB_EXP_DEVICE_EXT_MASKED_ATOMICS	= 1 << 14,
	IB_EXP_DEVICE_CROSS_CHANNEL	= 1 << 28, /* Comapt with user exp area */
	IB_EXP_DEVICE_MASK =	IB_DEVICE_CROSS_CHANNEL,
};

struct ib_exp_device_attr {
	struct ib_device_attr	base;
	/* Use IB_EXP_DEVICE_ATTR_... for exp_comp_mask */
	uint32_t		exp_comp_mask;
	uint64_t		device_cap_flags2;
	u32			dc_rd_req;
	u32			dc_rd_res;
	uint32_t		inline_recv_sz;
	u32			max_dct;
	uint32_t		max_rss_tbl_sz;
	/*
	  * This field is a bit mask for the supported atomic argument sizes.
	  * A bit set signifies an argument of size of 2 ^ bit_nubmer bytes is
	  * supported.
	  */
	u64                     atomic_arg_sizes;
	u32                     max_fa_bit_boudary;
	u32                     log_max_atomic_inline_arg;
	uint32_t		max_ctx_res_domain;
	struct ib_exp_masked_atomic_caps masked_atomic_caps;
};

enum {
	IB_DCT_CREATE_FLAGS_MASK		= 0,
};

struct ib_dct_init_attr {
	struct ib_pd	       *pd;
	struct ib_cq	       *cq;
	struct ib_srq	       *srq;
	u64			dc_key;
	u8			port;
	u32			access_flags;
	u8			min_rnr_timer;
	u8			tclass;
	u32			flow_label;
	enum ib_mtu		mtu;
	u8			pkey_index;
	u8			gid_index;
	u8			hop_limit;
	u32			create_flags;
	u32			inline_size;
	void		      (*event_handler)(struct ib_event *, void *);
	void		       *dct_context;
};

struct ib_dct_attr {
	u64			dc_key;
	u8			port;
	u32			access_flags;
	u8			min_rnr_timer;
	u8			tclass;
	u32			flow_label;
	enum ib_mtu		mtu;
	u8			pkey_index;
	u8			gid_index;
	u8			hop_limit;
	u32			key_violations;
	u8			state;
};

struct ib_dct {
	struct ib_device       *device;
	struct ib_uobject      *uobject;
	struct ib_pd	       *pd;
	struct ib_cq	       *cq;
	struct ib_srq	       *srq;
	void		      (*event_handler)(struct ib_event *, void *);
	void		       *dct_context;
	u32			dct_num;
};

/**
 * ib_exp_modify_cq - Modifies the attributes for the specified CQ and then
 *   transitions the CQ to the given state.
 * @cq: The CQ to modify.
 * @cq_attr: specifies the CQ attributes to modify.
 * @cq_attr_mask: A bit-mask used to specify which attributes of the CQ
 *   are being modified.
 */
int ib_exp_modify_cq(struct ib_cq *cq,
		     struct ib_cq_attr *cq_attr,
		     int cq_attr_mask);
int ib_exp_query_device(struct ib_device *device,
			struct ib_exp_device_attr *device_attr,
			struct ib_udata *uhw);

struct ib_dct *ib_exp_create_dct(struct ib_pd *pd,
				 struct ib_dct_init_attr *attr,
				 struct ib_udata *udata);
int ib_exp_destroy_dct(struct ib_dct *dct);
int ib_exp_query_dct(struct ib_dct *dct, struct ib_dct_attr *attr);

#endif
