#ifndef IB_USER_VERBS_EXP_H
#define IB_USER_VERBS_EXP_H

#include <rdma/ib_verbs_exp.h>

enum ibv_exp_start_values {
	IBV_EXP_START_ENUM      = 0x40,
	IBV_EXP_START_FLAG_LOC  = 0x20,
	IBV_EXP_START_FLAG      = (1ULL << IBV_EXP_START_FLAG_LOC),
};

enum {
	IB_USER_VERBS_EXP_CMD_FIRST = 64
};

enum {
	IB_USER_VERBS_EXP_CMD_CREATE_QP,
	IB_USER_VERBS_EXP_CMD_MODIFY_CQ,
	IB_USER_VERBS_EXP_CMD_MODIFY_QP,
	IB_USER_VERBS_EXP_CMD_CREATE_CQ = 3,
	IB_USER_VERBS_EXP_CMD_QUERY_DEVICE = 4,
	IB_USER_VERBS_EXP_CMD_CREATE_DCT,
	IB_USER_VERBS_EXP_CMD_DESTROY_DCT,
	IB_USER_VERBS_EXP_CMD_QUERY_DCT,
	IB_USER_VERBS_EXP_CMD_ARM_DCT,
	IB_USER_VERBS_EXP_CMD_CREATE_MR,
	IB_USER_VERBS_EXP_CMD_QUERY_MKEY,
	IB_USER_VERBS_EXP_CMD_REG_MR = 11,
	IB_USER_VERBS_EXP_CMD_PREFETCH_MR = 12,
	IB_USER_VERBS_EXP_CMD_CREATE_WQ = 14,
	IB_USER_VERBS_EXP_CMD_MODIFY_WQ,
	IB_USER_VERBS_EXP_CMD_DESTROY_WQ,
	IB_USER_VERBS_EXP_CMD_CREATE_RWQ_IND_TBL,
	IB_USER_VERBS_EXP_CMD_DESTROY_RWQ_IND_TBL,
	IB_USER_VERBS_EXP_CMD_CREATE_FLOW = 19,
	IB_USER_VERBS_EXP_CMD_SET_CTX_ATTR,
};

enum ib_uverbs_exp_modify_qp_comp_mask {
	IB_UVERBS_EXP_QP_ATTR_FLOW_ENTROPY	= 1UL << 0,
	IB_UVERBS_EXP_QP_ATTR_RESERVED	= 1UL << 1,
};

/*
 * Flags for exp_attr_mask field in ibv_exp_qp_attr struct
 */
enum ibv_exp_qp_attr_mask {
	IBV_EXP_QP_GROUP_RSS	= IB_QP_GROUP_RSS,
	IBV_EXP_QP_DC_KEY	= IB_QP_DC_KEY,
	IBV_EXP_QP_FLOW_ENTROPY = IB_QP_FLOW_ENTROPY,
	IBV_EXP_QP_ATTR_MASK	= IB_QP_GROUP_RSS | IB_QP_FLOW_ENTROPY |
				  IB_QP_DC_KEY |
				  IB_EXP_QP_OOO_RW_DATA_PLACEMENT,
	IBV_EXP_QP_ATTR_FIRST = IB_QP_GROUP_RSS,
	IBV_EXP_ATTR_MASK_SHIFT = 0x06,
};

struct ib_uverbs_exp_modify_qp {
	__u32 comp_mask;
	struct ib_uverbs_qp_dest dest;
	struct ib_uverbs_qp_dest alt_dest;
	__u32 qp_handle;
	__u32 attr_mask;
	__u32 qkey;
	__u32 rq_psn;
	__u32 sq_psn;
	__u32 dest_qp_num;
	__u32 qp_access_flags;
	__u16 pkey_index;
	__u16 alt_pkey_index;
	__u8  qp_state;
	__u8  cur_qp_state;
	__u8  path_mtu;
	__u8  path_mig_state;
	__u8  en_sqd_async_notify;
	__u8  max_rd_atomic;
	__u8  max_dest_rd_atomic;
	__u8  min_rnr_timer;
	__u8  port_num;
	__u8  timeout;
	__u8  retry_cnt;
	__u8  rnr_retry;
	__u8  alt_port_num;
	__u8  alt_timeout;
	__u8  reserved[6];
	__u64 dct_key;
	__u32 exp_attr_mask;
	__u32 flow_entropy;
	__u32 rate_limit;
	__u32 reserved1;
	__u64 driver_data[0];
};

enum {
	IB_QP_EXP_USER_CREATE_ATOMIC_BE_REPLY = (1<<8),
	IB_QP_EXP_USER_CREATE_RX_END_PADDING = (1<<11),
	IB_QP_EXP_USER_CREATE_SCATTER_FCS = (1 << 12),
};

enum ib_uverbs_exp_create_qp_flags {
	IBV_UVERBS_EXP_CREATE_QP_FLAGS = IB_QP_CREATE_CROSS_CHANNEL  |
					 IB_QP_CREATE_MANAGED_SEND   |
					 IB_QP_CREATE_MANAGED_RECV	|
					 IB_QP_EXP_USER_CREATE_ATOMIC_BE_REPLY |
					 IB_QP_EXP_USER_CREATE_RX_END_PADDING |
					 IB_QP_EXP_USER_CREATE_SCATTER_FCS
};

enum ib_uverbs_exp_create_qp_comp_mask {
	IB_UVERBS_EXP_CREATE_QP_CAP_FLAGS          = (1ULL << 0),
	IB_UVERBS_EXP_CREATE_QP_INL_RECV           = (1ULL << 1),
	IB_UVERBS_EXP_CREATE_QP_QPG		= (1ULL << 2),
};

struct ib_uverbs_qpg_init_attrib {
	__u32 tss_child_count;
	__u32 rss_child_count;
};

struct ib_uverbs_qpg {
	__u32 qpg_type;
	union {
		struct {
			__u32 parent_handle;
			__u32 reserved;
		};
		struct ib_uverbs_qpg_init_attrib parent_attrib;
	};
	__u32 reserved2;
};

struct ib_uverbs_exp_masked_atomic_caps {
	__u32  max_fa_bit_boudary;
	__u32  log_max_atomic_inline_arg;
	__u64  masked_log_atomic_arg_sizes;
	__u64  masked_log_atomic_arg_sizes_network_endianness;
};

struct ib_uverbs_exp_hash_conf {
	/* enum ib_rx_hash_fields */
	__u64 rx_hash_fields_mask;
	__u32 rwq_ind_tbl_handle;
	__u8 rx_hash_function; /* enum ib_rx_hash_function_flags */
	__u8 rx_key_len; /* valid only for Toeplitz */
	__u8 rx_hash_key[128]; /* valid only for Toeplitz */
	__u8 reserved[2];
};

struct ib_uverbs_exp_create_qp {
	__u64 comp_mask;
	__u64 user_handle;
	__u32 pd_handle;
	__u32 send_cq_handle;
	__u32 recv_cq_handle;
	__u32 srq_handle;
	__u32 max_send_wr;
	__u32 max_recv_wr;
	__u32 max_send_sge;
	__u32 max_recv_sge;
	__u32 max_inline_data;
	__u8  sq_sig_all;
	__u8  qp_type;
	__u8  is_srq;
	__u8  reserved;
	__u64 qp_cap_flags;
	__u32 max_inl_recv;
	__u32 reserved1;
	struct ib_uverbs_qpg qpg;
	__u64 reserved2;
	struct ib_uverbs_exp_hash_conf rx_hash_conf;
	uint8_t     port_num;
	__u8  reserved3[7];
	__u64 driver_data[0];
};

enum ib_uverbs_exp_create_qp_resp_comp_mask {
	IB_UVERBS_EXP_CREATE_QP_RESP_INL_RECV	= (1ULL << 0),
};

struct ib_uverbs_exp_create_qp_resp {
	__u64 comp_mask;
	__u32 qp_handle;
	__u32 qpn;
	__u32 max_send_wr;
	__u32 max_recv_wr;
	__u32 max_send_sge;
	__u32 max_recv_sge;
	__u32 max_inline_data;
	__u32 max_inl_recv;
};

enum ib_uverbs_exp_modify_cq_comp_mask {
	/* set supported bits for validity check */
	IB_UVERBS_EXP_CQ_ATTR_RESERVED	= 1 << 0
};

struct ib_uverbs_exp_modify_cq {
	__u32 cq_handle;
	__u32 attr_mask;
	__u16 cq_count;
	__u16 cq_period;
	__u32 cq_cap_flags;
	__u32 comp_mask;
	__u32 rsvd;
};

struct ib_uverbs_exp_ec_caps {
	__u32        max_ec_data_vector_count;
	__u32        max_ec_calc_inflight_calcs;
};

struct ib_uverbs_exp_query_device {
	__u64 comp_mask;
	__u64 driver_data[0];
};

struct ib_uverbs_exp_umr_caps {
	__u32                                   max_reg_descriptors;
	__u32                                   max_send_wqe_inline_klms;
	__u32                                   max_umr_recursion_depth;
	__u32                                   max_umr_stride_dimenson;
};

struct ib_uverbs_exp_odp_caps {
	__u64   general_odp_caps;
	struct {
		__u32   rc_odp_caps;
		__u32   uc_odp_caps;
		__u32   ud_odp_caps;
		__u32   dc_odp_caps;
		__u32   xrc_odp_caps;
		__u32   raw_eth_odp_caps;
	} per_transport_caps;
};

struct ib_uverbs_exp_mp_rq_caps {
	__u32	supported_qps; /* use ib_exp_supported_qp_types */
	__u32	allowed_shifts; /* use ib_mp_rq_shifts */
	__u8	min_single_wqe_log_num_of_strides;
	__u8	max_single_wqe_log_num_of_strides;
	__u8	min_single_stride_log_num_of_bytes;
	__u8	max_single_stride_log_num_of_bytes;
	__u32	reserved;
};


struct ib_uverbs_exp_rx_hash_caps {
	__u32	max_rwq_indirection_tables;
	__u32	max_rwq_indirection_table_size;
	__u64	supported_packet_fields;
	__u32	supported_qps;
	__u8	supported_hash_functions;
	__u8	reserved[3];
};

struct ib_uverbs_exp_tso_caps {
	__u32 max_tso; /* Maximum tso payload size in bytes */

	/* Corresponding bit will be set if qp type from
	 * 'enum ib_qp_type' is supported, e.g.
	 * supported_qpts |= 1 << IB_QPT_RAW
	 */
	__u32 supported_qpts;
};

struct ib_uverbs_exp_packet_pacing_caps {
	__u32 qp_rate_limit_min;
	__u32 qp_rate_limit_max; /* In kpbs */

	/* Corresponding bit will be set if qp type from
	 * 'enum ib_qp_type' is supported, e.g.
	 * supported_qpts |= 1 << IB_QPT_RAW_PACKET
	 */
	__u32 supported_qpts;
	__u32 reserved;
};

struct ib_uverbs_exp_ooo_caps {
	__u32 rc_caps;
	__u32 xrc_caps;
	__u32 dc_caps;
	__u32 ud_caps;
};

struct ib_uverbs_exp_sw_parsing_caps {
	__u32 sw_parsing_offloads;
	__u32 supported_qpts;
};

struct ib_uverbs_exp_query_device_resp {
	__u64					comp_mask;
	struct ib_uverbs_query_device_resp	base;
	__u64					timestamp_mask;
	__u64					hca_core_clock;
	__u64					device_cap_flags2;
	__u32					dc_rd_req;
	__u32					dc_rd_res;
	__u32					inline_recv_sz;
	__u32					max_rss_tbl_sz;
	__u64					atomic_arg_sizes;
	__u32					max_fa_bit_boudary;
	__u32					log_max_atomic_inline_arg;
	struct ib_uverbs_exp_umr_caps		umr_caps;
	struct ib_uverbs_exp_odp_caps		odp_caps;
	__u32					max_dct;
	__u32					max_ctx_res_domain;
	struct ib_uverbs_exp_rx_hash_caps	rx_hash;
	__u32					max_wq_type_rq;
	__u32					max_device_ctx;
	struct ib_uverbs_exp_mp_rq_caps		mp_rq_caps;
	__u16					vlan_offloads;
	__u8					reserved1[2];
	__u32					ec_w_mask;
	struct ib_uverbs_exp_ec_caps            ec_caps;
	struct ib_uverbs_exp_masked_atomic_caps masked_atomic_caps;
	__u16					rx_pad_end_addr_align;
	__u8					reserved2[6];
	struct ib_uverbs_exp_tso_caps		tso_caps;
	struct ib_uverbs_exp_packet_pacing_caps packet_pacing_caps;
	struct ib_uverbs_exp_ooo_caps		ooo_caps;
	struct ib_uverbs_exp_sw_parsing_caps	sw_parsing_caps;
};

enum ib_uverbs_exp_create_cq_comp_mask {
	IB_UVERBS_EXP_CREATE_CQ_CAP_FLAGS	= (u64)1 << 0,
	IB_UVERBS_EXP_CREATE_CQ_ATTR_RESERVED	= (u64)1 << 1,
};

struct ib_uverbs_exp_create_cq {
	__u64 comp_mask;
	__u64 user_handle;
	__u32 cqe;
	__u32 comp_vector;
	__s32 comp_channel;
	__u32 reserved;
	__u64 create_flags;
	__u64 driver_data[0];
};

struct ib_uverbs_exp_create_mr {
	__u64 comp_mask;
	__u32 pd_handle;
	__u32 max_reg_descriptors;
	__u64 exp_access_flags;
	__u32 create_flags;
	__u32 reserved;
	__u64 driver_data[0];
};

struct ib_uverbs_exp_create_mr_resp {
	__u64 comp_mask;
	__u32 handle;
	__u32 lkey;
	__u32 rkey;
	__u32 reserved;
	__u64 driver_data[0];
};

struct ib_uverbs_exp_query_mkey {
	__u64 comp_mask;
	__u32 handle;
	__u32 lkey;
	__u32 rkey;
	__u32 reserved;
	__u64 driver_data[0];
};

struct ib_uverbs_exp_query_mkey_resp {
	__u64 comp_mask;
	__u32 max_reg_descriptors;
	__u32 reserved;
	__u64 driver_data[0];
};

enum ib_uverbs_exp_access_flags {
	IB_UVERBS_EXP_ACCESS_ON_DEMAND     = (IBV_EXP_START_FLAG << 14),
	IB_UVERBS_EXP_ACCESS_PHYSICAL_ADDR = (IBV_EXP_START_FLAG << 16),
};

enum ib_uverbs_exp_reg_mr_ex_comp_mask {
	IB_UVERBS_EXP_REG_MR_EX_RESERVED		= (u64)1 << 0,
};

struct ib_uverbs_exp_reg_mr {
	__u64 start;
	__u64 length;
	__u64 hca_va;
	__u32 pd_handle;
	__u32 reserved;
	__u64 exp_access_flags;
	__u64 comp_mask;
};

struct ib_uverbs_exp_reg_mr_resp {
	__u32 mr_handle;
	__u32 lkey;
	__u32 rkey;
	__u32 reserved;
	__u64 comp_mask;
};

struct ib_uverbs_exp_prefetch_mr {
	__u64 comp_mask;
	__u32 mr_handle;
	__u32 flags;
	__u64 start;
	__u64 length;
};

struct ib_uverbs_create_dct {
	__u64	comp_mask;
	__u64	user_handle;
	__u32	pd_handle;
	__u32	cq_handle;
	__u32	srq_handle;
	__u32	access_flags;
	__u64	dc_key;
	__u32	flow_label;
	__u8	min_rnr_timer;
	__u8	tclass;
	__u8	port;
	__u8	pkey_index;
	__u8	gid_index;
	__u8	hop_limit;
	__u8	mtu;
	__u8	rsvd0;
	__u32	create_flags;
	__u32	inline_size;
	__u32	rsvd1;
	__u64	driver_data[0];
};

struct ib_uverbs_create_dct_resp {
	__u32 dct_handle;
	__u32 dctn;
	__u32 inline_size;
	__u32 rsvd;
};

struct ib_uverbs_destroy_dct {
	__u64 comp_mask;
	__u32 dct_handle;
	__u32 reserved;
};

struct ib_uverbs_destroy_dct_resp {
	__u32	events_reported;
	__u32	reserved;
};

struct ib_uverbs_query_dct {
	__u64	comp_mask;
	__u32	dct_handle;
	__u32	reserved;
	__u64	driver_data[0];
};

struct ib_uverbs_query_dct_resp {
	__u64	dc_key;
	__u32	access_flags;
	__u32	flow_label;
	__u32	key_violations;
	__u8	port;
	__u8	min_rnr_timer;
	__u8	tclass;
	__u8	mtu;
	__u8	pkey_index;
	__u8	gid_index;
	__u8	hop_limit;
	__u8	state;
	__u32	rsvd;
	__u64	driver_data[0];
};

struct ib_uverbs_arm_dct {
	__u64	comp_mask;
	__u32	dct_handle;
	__u32	reserved;
	__u64	driver_data[0];
};

struct ib_uverbs_arm_dct_resp {
	__u64	driver_data[0];
};

struct ib_uverbs_exp_kern_ib_filter {
	__be32	l3_type_qpn;
	__u8	dst_gid[16];
};

struct ib_uverbs_exp_flow_spec_ib {
	union {
		struct ib_uverbs_flow_spec_hdr hdr;
		struct {
			__u32 type;
			__u16 size;
			__u16 reserved;
		};
	};
	struct ib_uverbs_exp_kern_ib_filter val;
	struct ib_uverbs_exp_kern_ib_filter mask;
};

struct ib_uverbs_exp_flow_spec {
	union {
		union {
			struct ib_uverbs_flow_spec_hdr hdr;
			struct {
				__u32 type;
				__u16 size;
				__u16 reserved;
			};
		};
		struct ib_uverbs_flow_spec_eth     eth;
		struct ib_uverbs_exp_flow_spec_ib      ib;
		struct ib_uverbs_flow_spec_ipv4    ipv4;
		struct ib_uverbs_flow_spec_tcp_udp tcp_udp;
		struct ib_uverbs_flow_spec_ipv6    ipv6;
	};
};

enum ib_uverbs_exp_set_context_attr_comp_mask {
	IB_UVERBS_EXP_SET_CONTEXT_PEER_INFO	= (1UL << 0),
	IB_UVERBS_EXP_SET_CONTEXT_ATTR_RESERVED	= (1UL << 1),
};

struct ib_uverbs_exp_set_context_attr {
	__u64	peer_id;
	__u8	peer_name[64];
	__u32	comp_mask;
	__u32	reserved;
};

#endif
