#ifndef IB_USER_VERBS_EXP_H
#define IB_USER_VERBS_EXP_H

#include <rdma/ib_verbs_exp.h>

enum {
	IB_USER_VERBS_EXP_CMD_FIRST = 64
};

enum {
	IB_USER_VERBS_EXP_CMD_CREATE_QP,
	IB_USER_VERBS_EXP_CMD_MODIFY_CQ,
	IB_USER_VERBS_EXP_CMD_MODIFY_QP,
	IB_USER_VERBS_EXP_CMD_CREATE_CQ = 3,
	IB_USER_VERBS_EXP_CMD_QUERY_DEVICE = 4
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
	IBV_EXP_QP_FLOW_ENTROPY = IB_QP_FLOW_ENTROPY,
	IBV_EXP_QP_ATTR_MASK	= IB_QP_GROUP_RSS | IB_QP_FLOW_ENTROPY,
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
	__u64 reserved1;
	__u32 exp_attr_mask;
	__u32 flow_entropy;
	__u64 driver_data[0];
};

enum {
	IB_QP_EXP_USER_CREATE_ATOMIC_BE_REPLY = (1<<8)
};

enum ib_uverbs_exp_create_qp_flags {
	IBV_UVERBS_EXP_CREATE_QP_FLAGS = IB_QP_CREATE_CROSS_CHANNEL  |
					 IB_QP_CREATE_MANAGED_SEND   |
					 IB_QP_CREATE_MANAGED_RECV	|
					 IB_QP_EXP_USER_CREATE_ATOMIC_BE_REPLY
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

struct ib_uverbs_exp_query_device {
	__u64 comp_mask;
	__u64 driver_data[0];
};

struct ib_uverbs_exp_query_device_resp {
	__u64					comp_mask;
	struct ib_uverbs_query_device_resp	base;
	__u64					timestamp_mask;
	__u64					hca_core_clock;
	__u64					device_cap_flags2;
	__u32					reserved;
	__u32					reserved2;
	__u32					inline_recv_sz;
	__u32					max_rss_tbl_sz;
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

#endif
