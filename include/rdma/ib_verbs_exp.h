#ifndef IB_VERBS_EXP_H
#define IB_VERBS_EXP_H

#include <rdma/ib_verbs.h>

struct ib_exp_umr_caps {
	u32                max_reg_descriptors;
	u32                max_send_wqe_inline_klms;
	u32                max_umr_recursion_depth;
	u32                max_umr_stride_dimenson;
};

struct ib_exp_odp_caps {
	uint64_t	general_odp_caps;
	struct {
		uint32_t	rc_odp_caps;
		uint32_t	uc_odp_caps;
		uint32_t	ud_odp_caps;
		uint32_t	dc_odp_caps;
		uint32_t	xrc_odp_caps;
		uint32_t	raw_eth_odp_caps;
	} per_transport_caps;
};

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

/*
 * RX Hash Function flags.
*/
enum ib_rx_hash_function_flags {
	IB_EXP_RX_HASH_FUNC_TOEPLITZ	= 1 << 0,
	IB_EXP_RX_HASH_FUNC_XOR		= 1 << 1
};

/*
 * RX Hash flags, these flags allows to set which incoming packet field should
 * participates in RX Hash. Each flag represent certain packet's field,
 * when the flag is set the field that is represented by the flag will
 * participate in RX Hash calculation.
 * Notice: *IPV4 and *IPV6 flags can't be enabled together on the same QP
 * and *TCP and *UDP flags can't be enabled together on the same QP.
*/
enum ib_rx_hash_fields {
	IB_RX_HASH_SRC_IPV4		= 1 << 0,
	IB_RX_HASH_DST_IPV4		= 1 << 1,
	IB_RX_HASH_SRC_IPV6		= 1 << 2,
	IB_RX_HASH_DST_IPV6		= 1 << 3,
	IB_RX_HASH_SRC_PORT_TCP	= 1 << 4,
	IB_RX_HASH_DST_PORT_TCP	= 1 << 5,
	IB_RX_HASH_SRC_PORT_UDP	= 1 << 6,
	IB_RX_HASH_DST_PORT_UDP	= 1 << 7
};

struct ib_rx_hash_conf {
	enum ib_rx_hash_function_flags rx_hash_function;
	u8 rx_key_len; /* valid only for Toeplitz */
	u8 *rx_hash_key;
	uint64_t rx_hash_fields_mask; /* enum ib_rx_hash_fields */
	struct ib_rwq_ind_table *rwq_ind_tbl;
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
	struct ib_rx_hash_conf	*rx_hash_conf;
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
	IB_EXP_DEVICE_ATTR_UMR                  = 1ULL << 9,
	IB_EXP_DEVICE_ATTR_ODP			= 1ULL << 10,
	IB_EXP_DEVICE_ATTR_MAX_DCT		= 1ULL << 11,
	IB_EXP_DEVICE_ATTR_MAX_CTX_RES_DOMAIN	= 1ULL << 12,
	IB_EXP_DEVICE_ATTR_RX_HASH		= 1ULL << 13,
	IB_EXP_DEVICE_ATTR_MAX_WQ_TYPE_RQ	= 1ULL << 14,
	IB_EXP_DEVICE_ATTR_MAX_DEVICE_CTX	= 1ULL << 15,
	IB_EXP_DEVICE_ATTR_EXT_MASKED_ATOMICS	= 1ULL << 19,
};

enum ib_exp_device_cap_flags2 {
	IB_EXP_DEVICE_DC_TRANSPORT	= 1 << 0,
	IB_EXP_DEVICE_QPG		= 1 << 1,
	IB_EXP_DEVICE_UD_RSS		= 1 << 2,
	IB_EXP_DEVICE_UD_TSS		= 1 << 3,
	IB_EXP_DEVICE_EXT_ATOMICS	= 1 << 4,
	IB_EXP_DEVICE_UMR		= 1 << 6,
	IB_EXP_DEVICE_ODP               = 1 << 7,
	IB_EXP_DEVICE_EXT_MASKED_ATOMICS	= 1 << 14,
	IB_EXP_DEVICE_CROSS_CHANNEL	= 1 << 28, /* Comapt with user exp area */
	IB_EXP_DEVICE_MASK =	IB_DEVICE_CROSS_CHANNEL,
};

struct ib_exp_rx_hash_caps {
	uint32_t max_rwq_indirection_tables;
	uint32_t max_rwq_indirection_table_size;
	uint8_t  supported_hash_functions; /* from ib_rx_hash_function_flags */
	uint64_t supported_packet_fields;	/* from ib_rx_hash_fields */
	uint32_t supported_qps;  /* from ib_exp_supported_qp_types */
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
	struct ib_exp_umr_caps  umr_caps;
	struct ib_exp_odp_caps	odp_caps;
	uint32_t		max_ctx_res_domain;
	struct ib_exp_masked_atomic_caps masked_atomic_caps;
	uint32_t		max_device_ctx;
	struct ib_exp_rx_hash_caps	rx_hash_caps;
	uint32_t			max_wq_type_rq;
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
 * struct ib_mkey_attr - Memory key attributes
 *
 * @max_reg_descriptors: how many mrs we can we register with this mkey
 */
struct ib_mkey_attr {
	u32 max_reg_descriptors;
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

int ib_exp_query_mkey(struct ib_mr *mr, u64 mkey_attr_mask,
		  struct ib_mkey_attr *mkey_attr);
#endif
