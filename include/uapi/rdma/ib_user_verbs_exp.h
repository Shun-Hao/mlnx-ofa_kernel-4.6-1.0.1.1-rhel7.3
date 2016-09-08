#ifndef IB_USER_VERBS_EXP_H
#define IB_USER_VERBS_EXP_H

#include <rdma/ib_verbs_exp.h>

enum {
	IB_USER_VERBS_EXP_CMD_FIRST = 64
};

enum {
	IB_USER_VERBS_EXP_CMD_CREATE_QP,
};

enum ib_uverbs_exp_create_qp_flags {
	IBV_UVERBS_EXP_CREATE_QP_FLAGS = IB_QP_CREATE_CROSS_CHANNEL  |
					 IB_QP_CREATE_MANAGED_SEND   |
					 IB_QP_CREATE_MANAGED_RECV
};

enum ib_uverbs_exp_create_qp_comp_mask {
	IB_UVERBS_EXP_CREATE_QP_CAP_FLAGS          = (1ULL << 0),
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
	__u64 driver_data[0];
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
	__u32 reserved;
};

#endif
