/*
 * Copyright (c) 2015, Mellanox Technologies. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#if defined(CONFIG_X86)
#include <asm/pat.h>
#endif
#include <linux/highmem.h>
#include <rdma/ib_cache.h>
#include <rdma/ib_umem.h>
#include <rdma/ib_user_verbs.h>
#include <rdma/ib_user_verbs_exp.h>
#include "mlx5_ib.h"

#ifdef CONFIG_INFINIBAND_ON_DEMAND_PAGING
static void copy_odp_exp_caps(struct ib_exp_odp_caps *exp_caps,
			      struct ib_odp_caps *caps)
{
	exp_caps->general_odp_caps = caps->general_caps;
	exp_caps->max_size = caps->max_size;
	exp_caps->per_transport_caps.rc_odp_caps = caps->per_transport_caps.rc_odp_caps;
	exp_caps->per_transport_caps.uc_odp_caps = caps->per_transport_caps.uc_odp_caps;
	exp_caps->per_transport_caps.ud_odp_caps = caps->per_transport_caps.ud_odp_caps;
	exp_caps->per_transport_caps.dc_odp_caps = caps->per_transport_caps.dc_odp_caps;
}
#endif

enum {
	MLX5_ATOMIC_SIZE_QP_8BYTES = 1 << 3,
};

static unsigned int dc_cnak_qp_depth = MLX5_DC_CONNECT_QP_DEPTH;
module_param_named(dc_cnak_qp_depth, dc_cnak_qp_depth, uint, 0444);
MODULE_PARM_DESC(dc_cnak_qp_depth, "DC CNAK QP depth");

enum {
	MLX5_STANDARD_ATOMIC_SIZE = 0x8,
};

void mlx5_ib_config_atomic_responder(struct mlx5_ib_dev *dev,
				     struct ib_exp_device_attr *props)
{
	enum ib_atomic_cap cap = props->base.atomic_cap;

	if (cap == IB_ATOMIC_HCA ||
	    cap == IB_ATOMIC_GLOB ||
	    cap == IB_ATOMIC_HCA_REPLY_BE)
		dev->enable_atomic_resp = 1;

	dev->atomic_cap = cap;
}

void mlx5_ib_get_atomic_caps(struct mlx5_ib_dev *dev,
			     struct ib_device_attr *props,
			     int exp)
{
	int tmp;
	u8 atomic_operations;
	u8 atomic_size_qp;
	u8 atomic_req_8B_endianness_mode;

	atomic_operations = MLX5_CAP_ATOMIC(dev->mdev, atomic_operations);
	atomic_size_qp = MLX5_CAP_ATOMIC(dev->mdev, atomic_size_qp);
	atomic_req_8B_endianness_mode =
		MLX5_CAP_ATOMIC(dev->mdev,
				atomic_req_8B_endianness_mode) ||
		!mlx5_host_is_le();

	/* Check if HW supports 8 bytes standard atomic operations and capable
	 * of host endianness respond
	 */
	tmp = MLX5_ATOMIC_OPS_CMP_SWAP | MLX5_ATOMIC_OPS_FETCH_ADD;
	if (((atomic_operations & tmp) == tmp) &&
	    (atomic_size_qp & MLX5_ATOMIC_SIZE_QP_8BYTES)) {
		if (atomic_req_8B_endianness_mode) {
			props->atomic_cap = IB_ATOMIC_HCA;
		} else {
			if (exp)
				props->atomic_cap = IB_ATOMIC_HCA_REPLY_BE;
			else
				props->atomic_cap = IB_ATOMIC_NONE;
		}
	} else {
		props->atomic_cap = IB_ATOMIC_NONE;
	}

	tmp = MLX5_ATOMIC_OPS_EXTENDED_CMP_SWAP | MLX5_ATOMIC_OPS_EXTENDED_FETCH_ADD;
	if (((atomic_operations & tmp) == tmp) &&
	    (atomic_size_qp & MLX5_ATOMIC_SIZE_QP_8BYTES)) {
		if (atomic_req_8B_endianness_mode) {
			props->masked_atomic_cap = IB_ATOMIC_HCA;
		} else {
			if (exp)
				props->masked_atomic_cap = IB_ATOMIC_HCA_REPLY_BE;
			else
				props->masked_atomic_cap = IB_ATOMIC_NONE;
		}
	} else {
		props->masked_atomic_cap = IB_ATOMIC_NONE;
	}
}

static void ext_atomic_caps(struct mlx5_ib_dev *dev,
			    struct ib_exp_device_attr *props)
{
	int tmp;
	unsigned long last;
	unsigned long arg;
	struct ib_exp_masked_atomic_caps *atom_caps =
		&props->masked_atomic_caps;

	/* Legacy extended atomic fields */
	props->max_fa_bit_boudary = 0;
	props->log_max_atomic_inline_arg = 0;
	/* New extended atomic fields */
	atom_caps->max_fa_bit_boudary = 0;
	atom_caps->log_max_atomic_inline_arg = 0;
	atom_caps->masked_log_atomic_arg_sizes = 0;
	atom_caps->masked_log_atomic_arg_sizes_network_endianness = 0;

	tmp = MLX5_ATOMIC_OPS_CMP_SWAP		|
	      MLX5_ATOMIC_OPS_FETCH_ADD		|
	      MLX5_ATOMIC_OPS_EXTENDED_CMP_SWAP |
	      MLX5_ATOMIC_OPS_EXTENDED_FETCH_ADD;

	if ((MLX5_CAP_ATOMIC(dev->mdev, atomic_operations) & tmp) != tmp)
		return;

	props->atomic_arg_sizes = MLX5_CAP_ATOMIC(dev->mdev, atomic_size_qp) &
				  MLX5_CAP_ATOMIC(dev->mdev, atomic_size_dc);
	props->max_fa_bit_boudary = 64;
	arg = (unsigned long)props->atomic_arg_sizes;
	last = find_last_bit(&arg, BITS_PER_LONG);
	if (last < 6)
		props->log_max_atomic_inline_arg = last;
	else
		props->log_max_atomic_inline_arg = 6;

	atom_caps->masked_log_atomic_arg_sizes = props->atomic_arg_sizes;
	if (!mlx5_host_is_le() ||
	    props->base.atomic_cap == IB_ATOMIC_HCA_REPLY_BE)
		atom_caps->masked_log_atomic_arg_sizes_network_endianness =
			props->atomic_arg_sizes;
	else if (props->base.atomic_cap == IB_ATOMIC_HCA)
		atom_caps->masked_log_atomic_arg_sizes_network_endianness =
			atom_caps->masked_log_atomic_arg_sizes &
			~MLX5_STANDARD_ATOMIC_SIZE;

	if (props->base.atomic_cap == IB_ATOMIC_HCA && mlx5_host_is_le())
		props->atomic_arg_sizes &= MLX5_STANDARD_ATOMIC_SIZE;
	atom_caps->max_fa_bit_boudary = props->max_fa_bit_boudary;
	atom_caps->log_max_atomic_inline_arg = props->log_max_atomic_inline_arg;

	props->device_cap_flags2 |= IB_EXP_DEVICE_EXT_ATOMICS |
				    IB_EXP_DEVICE_EXT_MASKED_ATOMICS;
}

enum mlx5_addr_align {
	MLX5_ADDR_ALIGN_0	= 0,
	MLX5_ADDR_ALIGN_64	= 64,
	MLX5_ADDR_ALIGN_128	= 128,
};

static void mlx5_update_ooo_cap(struct mlx5_ib_dev *dev,
				struct ib_exp_device_attr *props)
{
	if (MLX5_CAP_GEN(dev->mdev, multipath_rc_qp))
		props->ooo_caps.rc_caps |=
			IB_EXP_DEVICE_OOO_RW_DATA_PLACEMENT;
	if (MLX5_CAP_GEN(dev->mdev, multipath_xrc_qp))
		props->ooo_caps.xrc_caps |=
			IB_EXP_DEVICE_OOO_RW_DATA_PLACEMENT;
	if (MLX5_CAP_GEN(dev->mdev, multipath_dc_qp))
		props->ooo_caps.dc_caps |=
			IB_EXP_DEVICE_OOO_RW_DATA_PLACEMENT;

	if (MLX5_CAP_GEN(dev->mdev, multipath_rc_qp) ||
	    MLX5_CAP_GEN(dev->mdev, multipath_xrc_qp) ||
	    MLX5_CAP_GEN(dev->mdev, multipath_dc_qp))
		props->exp_comp_mask |= IB_EXP_DEVICE_ATTR_OOO_CAPS;
}

static void mlx5_update_tm_cap(struct mlx5_ib_dev *dev,
			       struct ib_exp_device_attr *props)
{
	if (!MLX5_CAP_GEN(dev->mdev, tag_matching))
		return;

	if (MLX5_CAP_GEN(dev->mdev, rndv_offload_rc))
		props->tm_caps.capability_flags |= IB_EXP_TM_CAP_RC;
	if (MLX5_CAP_GEN(dev->mdev, rndv_offload_dc))
		props->tm_caps.capability_flags |= IB_EXP_TM_CAP_DC;

	if (!props->tm_caps.capability_flags)
		return;

	props->tm_caps.max_rndv_hdr_size = MLX5_TM_MAX_RNDV_MSG_SIZE;
	props->tm_caps.max_num_tags =
		(1 << MLX5_CAP_GEN(dev->mdev,
				   log_tag_matching_list_sz)) - 1;
	props->tm_caps.max_ops =
		1 << MLX5_CAP_GEN(dev->mdev, log_max_qp_sz);
	props->tm_caps.max_sge = MLX5_TM_MAX_SGE;
	props->exp_comp_mask |= IB_EXP_DEVICE_ATTR_TM_CAPS;
}

static void mlx5_update_tunnel_offloads_caps(struct mlx5_ib_dev *dev,
					     struct ib_exp_device_attr *props)
{
	props->tunnel_offloads_caps = 0;

	if (MLX5_CAP_ETH(dev->mdev, tunnel_stateless_vxlan))
		props->tunnel_offloads_caps |=
			IBV_EXP_RAW_PACKET_CAP_TUNNELED_OFFLOAD_VXLAN;
	if (MLX5_CAP_ETH(dev->mdev, tunnel_stateless_geneve_rx))
		props->tunnel_offloads_caps |=
			IBV_EXP_RAW_PACKET_CAP_TUNNELED_OFFLOAD_GENEVE;
	if (MLX5_CAP_ETH(dev->mdev, tunnel_stateless_gre))
		props->tunnel_offloads_caps|=
			IBV_EXP_RAW_PACKET_CAP_TUNNELED_OFFLOAD_GRE;
	props->exp_comp_mask |= IB_EXP_DEVICE_ATTR_TUNNEL_OFFLOADS_CAPS;
}

int mlx5_ib_exp_query_device(struct ib_device *ibdev,
			     struct ib_exp_device_attr *props,
			     struct ib_udata *uhw)
{
	struct mlx5_ib_dev *dev = to_mdev(ibdev);
	u32 uar_sz_shift;
	u32 max_tso;
	int ret;

	ret = mlx5_ib_query_device(ibdev, &props->base, uhw);
	if (ret)
		return ret;

	props->exp_comp_mask = 0;
	props->device_cap_flags2 = 0;
	props->exp_comp_mask |= IB_EXP_DEVICE_ATTR_CAP_FLAGS2;

	props->exp_comp_mask |= IB_EXP_DEVICE_ATTR_MAX_CTX_RES_DOMAIN;
	props->max_ctx_res_domain = MLX5_IB_MAX_CTX_DYNAMIC_UARS *
		MLX5_NON_FP_BFREGS_PER_UAR;
#ifdef CONFIG_INFINIBAND_ON_DEMAND_PAGING
	props->exp_comp_mask |= IB_EXP_DEVICE_ATTR_ODP;
	props->exp_comp_mask |= IB_EXP_DEVICE_ATTR_ODP_MAX_SIZE;
	props->device_cap_flags2 |= IB_EXP_DEVICE_ODP;
	copy_odp_exp_caps(&props->odp_caps, &to_mdev(ibdev)->odp_caps);
#endif
	props->exp_comp_mask |= IB_EXP_DEVICE_ATTR_WITH_TIMESTAMP_MASK |
		IB_EXP_DEVICE_ATTR_WITH_HCA_CORE_CLOCK;

	props->exp_comp_mask |= IB_EXP_DEVICE_ATTR_DC_REQ_RD;
	props->exp_comp_mask |= IB_EXP_DEVICE_ATTR_DC_RES_RD;
	props->exp_comp_mask |= IB_EXP_DEVICE_ATTR_MAX_DCT;
	if (MLX5_CAP_GEN(dev->mdev, dct)) {
		props->device_cap_flags2 |= IB_EXP_DEVICE_DC_TRANSPORT;
		props->dc_rd_req = 1 << MLX5_CAP_GEN(dev->mdev, log_max_ra_req_dc);
		props->dc_rd_res = 1 << MLX5_CAP_GEN(dev->mdev, log_max_ra_res_dc);
		props->max_dct = props->base.max_qp;
	} else {
		props->dc_rd_req = 0;
		props->dc_rd_res = 0;
		props->max_dct = 0;
	}

	props->exp_comp_mask |= IB_EXP_DEVICE_ATTR_INLINE_RECV_SZ;
	if (MLX5_CAP_GEN(dev->mdev, sctr_data_cqe))
		props->inline_recv_sz = MLX5_MAX_INLINE_RECEIVE_SIZE;
	else
		props->inline_recv_sz = 0;

	mlx5_ib_get_atomic_caps(dev, &props->base, 1);
	ext_atomic_caps(dev, props);
	props->exp_comp_mask |= IB_EXP_DEVICE_ATTR_EXT_ATOMIC_ARGS |
		IB_EXP_DEVICE_ATTR_EXT_MASKED_ATOMICS;

	props->device_cap_flags2 |= IB_EXP_DEVICE_UMR;
	props->umr_caps.max_reg_descriptors = 1 << MLX5_CAP_GEN(dev->mdev, log_max_klm_list_size);
	props->umr_caps.max_send_wqe_inline_klms = 20;
	props->umr_caps.max_umr_recursion_depth = MLX5_CAP_GEN(dev->mdev, max_indirection);
	props->umr_caps.max_umr_stride_dimenson = 1;
	props->exp_comp_mask |= IB_EXP_DEVICE_ATTR_UMR;

	props->exp_comp_mask |= IB_EXP_DEVICE_ATTR_RX_HASH;
	props->exp_comp_mask |= IB_EXP_DEVICE_ATTR_MAX_WQ_TYPE_RQ;
	if (MLX5_CAP_GEN(dev->mdev, port_type) == MLX5_CAP_PORT_TYPE_ETH) {
		props->rx_hash_caps.max_rwq_indirection_tables = 1 << MLX5_CAP_GEN(dev->mdev, log_max_rqt);
		props->rx_hash_caps.max_rwq_indirection_table_size = 1 << MLX5_CAP_GEN(dev->mdev, log_max_rqt_size);
		props->rx_hash_caps.supported_hash_functions = IB_EXP_RX_HASH_FUNC_TOEPLITZ;
		props->rx_hash_caps.supported_packet_fields = IB_RX_HASH_SRC_IPV4 |
			IB_RX_HASH_DST_IPV4 |
			IB_RX_HASH_SRC_IPV6 |
			IB_RX_HASH_DST_IPV6 |
			IB_RX_HASH_SRC_PORT_TCP |
			IB_RX_HASH_DST_PORT_TCP |
			IB_RX_HASH_SRC_PORT_UDP |
			IB_RX_HASH_DST_PORT_UDP;
		props->rx_hash_caps.supported_qps = IB_QPT_RAW_PACKET;
		props->max_wq_type_rq = 1 << MLX5_CAP_GEN(dev->mdev, log_max_rq);
	} else {
		memset(&props->rx_hash_caps, 0, sizeof(props->rx_hash_caps));
		props->max_wq_type_rq = 0;
	}
	props->exp_comp_mask |= IB_EXP_DEVICE_ATTR_MP_RQ;
	if (MLX5_CAP_GEN(dev->mdev, striding_rq)) {
		props->mp_rq_caps.allowed_shifts =  IB_MP_RQ_2BYTES_SHIFT;
		props->mp_rq_caps.supported_qps = IB_EXP_QPT_RAW_PACKET;
		props->mp_rq_caps.max_single_stride_log_num_of_bytes =  MLX5_MAX_SINGLE_STRIDE_LOG_NUM_BYTES;
		props->mp_rq_caps.min_single_stride_log_num_of_bytes =  MLX5_MIN_SINGLE_STRIDE_LOG_NUM_BYTES;
		props->mp_rq_caps.max_single_wqe_log_num_of_strides =  MLX5_MAX_SINGLE_WQE_LOG_NUM_STRIDES;
		props->mp_rq_caps.min_single_wqe_log_num_of_strides =  MLX5_MIN_SINGLE_WQE_LOG_NUM_STRIDES;
	} else {
		props->mp_rq_caps.supported_qps = 0;
	}

	props->vlan_offloads = 0;
	if (MLX5_CAP_GEN(dev->mdev, eth_net_offloads)) {
		if (MLX5_CAP_ETH(dev->mdev, csum_cap))
			props->device_cap_flags2 |=
				IB_EXP_DEVICE_RX_CSUM_IP_PKT |
				IB_EXP_DEVICE_RX_CSUM_TCP_UDP_PKT |
				IB_EXP_DEVICE_RX_TCP_UDP_PKT_TYPE;
		if (MLX5_CAP_ETH(dev->mdev, vlan_cap)) {
			props->exp_comp_mask |=
				IB_EXP_DEVICE_ATTR_VLAN_OFFLOADS;
			props->vlan_offloads |= IB_WQ_CVLAN_STRIPPING |
						IB_WQ_CVLAN_INSERTION;
		}
		if (MLX5_CAP_ETH(dev->mdev, scatter_fcs))
			props->device_cap_flags2 |=
				IB_EXP_DEVICE_SCATTER_FCS;

		if (MLX5_CAP_ETH(dev->mdev, swp)) {
			props->sw_parsing_caps.sw_parsing_offloads |=
				IB_RAW_PACKET_QP_SW_PARSING;
			props->sw_parsing_caps.supported_qpts |=
				BIT(IB_QPT_RAW_PACKET);
			props->exp_comp_mask |=
				IB_EXP_DEVICE_ATTR_SW_PARSING_CAPS;
		}

		if (MLX5_CAP_ETH(dev->mdev, swp_csum)) {
			props->sw_parsing_caps.sw_parsing_offloads |=
				IB_RAW_PACKET_QP_SW_PARSING_CSUM;
			props->sw_parsing_caps.supported_qpts |=
				BIT(IB_QPT_RAW_PACKET);
			props->exp_comp_mask |=
				IB_EXP_DEVICE_ATTR_SW_PARSING_CAPS;
		}

		if (MLX5_CAP_ETH(dev->mdev, swp_lso)) {
			props->sw_parsing_caps.sw_parsing_offloads |=
				IB_RAW_PACKET_QP_SW_PARSING_LSO;
			props->sw_parsing_caps.supported_qpts |=
				BIT(IB_QPT_RAW_PACKET);
			props->exp_comp_mask |=
				IB_EXP_DEVICE_ATTR_SW_PARSING_CAPS;
		}
	}

	if (MLX5_CAP_GEN(dev->mdev, ipoib_enhanced_offloads)) {
		if (MLX5_CAP_IPOIB_ENHANCED(dev->mdev, csum_cap)) {
			props->device_cap_flags2 |=
				IB_EXP_DEVICE_RX_CSUM_IP_PKT |
				IB_EXP_DEVICE_RX_CSUM_TCP_UDP_PKT |
				IB_EXP_DEVICE_RX_TCP_UDP_PKT_TYPE;
		}
	}

	props->rx_pad_end_addr_align = MLX5_ADDR_ALIGN_0;
	if (MLX5_CAP_GEN(dev->mdev, end_pad)) {
		if (MLX5_CAP_GEN(dev->mdev, cache_line_128byte) &&
		    (cache_line_size() == 128))
			props->rx_pad_end_addr_align = MLX5_ADDR_ALIGN_128;
		else
			props->rx_pad_end_addr_align = MLX5_ADDR_ALIGN_64;
		props->exp_comp_mask |= IB_EXP_DEVICE_ATTR_RX_PAD_END_ALIGN;
	}

		max_tso = MLX5_CAP_ETH(dev->mdev, max_lso_cap);
		if (max_tso) {
			props->tso_caps.max_tso = 1 << max_tso;
			props->tso_caps.supported_qpts |=
				1 << IB_QPT_RAW_PACKET;
			props->exp_comp_mask |= IB_EXP_DEVICE_ATTR_TSO_CAPS;
		}

	props->exp_comp_mask |= IB_EXP_DEVICE_ATTR_EC_CAPS;
	if (MLX5_CAP_GEN(dev->mdev, vector_calc)) {
		if (MLX5_CAP_VECTOR_CALC(dev->mdev, calc_matrix)  &&
		    MLX5_CAP_VECTOR_CALC(dev->mdev, calc0.op_xor) &&
		    MLX5_CAP_VECTOR_CALC(dev->mdev, calc1.op_xor) &&
		    MLX5_CAP_VECTOR_CALC(dev->mdev, calc2.op_xor) &&
		    MLX5_CAP_VECTOR_CALC(dev->mdev, calc3.op_xor)) {
			props->device_cap_flags2 |= IB_EXP_DEVICE_EC_OFFLOAD;
			props->ec_caps.max_ec_data_vector_count =
				MLX5_CAP_VECTOR_CALC(dev->mdev, max_vec_count);
			/* XXX: Should be MAX_SQ_SIZE / (11 * WQE_BB) */
			props->ec_caps.max_ec_calc_inflight_calcs = 1024;
			props->ec_w_mask = 1 << 0 | 1 << 1 | 1 << 3;
			if (MLX5_CAP_VECTOR_CALC(dev->mdev, calc_matrix_type_8bit))
				props->ec_w_mask |= 1 << 7;
		}
	}

	if (MLX5_CAP_QOS(dev->mdev, packet_pacing) &&
	    MLX5_CAP_GEN(dev->mdev, qos)) {
		props->packet_pacing_caps.qp_rate_limit_max =
			MLX5_CAP_QOS(dev->mdev, packet_pacing_max_rate);
		props->packet_pacing_caps.qp_rate_limit_min =
			MLX5_CAP_QOS(dev->mdev, packet_pacing_min_rate);
		props->packet_pacing_caps.supported_qpts |=
			1 << IB_QPT_RAW_PACKET;
		props->exp_comp_mask |= IB_EXP_DEVICE_ATTR_PACKET_PACING_CAPS;
	}

	props->device_cap_flags2 |= IB_EXP_DEVICE_NOP;

	props->exp_comp_mask |= IB_EXP_DEVICE_ATTR_MAX_DEVICE_CTX;

	if (MLX5_CAP_GEN(dev->mdev, uar_4k))
		uar_sz_shift = MLX5_ADAPTER_PAGE_SHIFT;
	else
		uar_sz_shift = PAGE_SHIFT;

	/* mlx5_core uses MLX5_NUM_DRIVER_UARS uar pages. Each ucontext uses
	 * 8 uars (see MLX5_DEF_TOT_UUARS and MLX5_NUM_NON_FP_BFREGS_PER_UAR
	 * in libmlx5). Note that the CAP's uar_sz is in MB unit.
	 */
	props->max_device_ctx =
		(1 << (MLX5_CAP_GEN(dev->mdev, uar_sz) + 20 - uar_sz_shift))
		/ (MLX5_DEF_TOT_BFREGS / MLX5_NON_FP_BFREGS_PER_UAR)
		- MLX5_NUM_DRIVER_UARS;

	if (MLX5_CAP_GEN(dev->mdev, rq_delay_drop) &&
	    MLX5_CAP_GEN(dev->mdev, general_notification_event))
		props->device_cap_flags2 |= IB_EXP_DEVICE_DELAY_DROP;

	mlx5_update_ooo_cap(dev, props);
	mlx5_update_tm_cap(dev, props);
	mlx5_update_tunnel_offloads_caps(dev, props);

	props->device_cap_flags2 |= IB_EXP_DEVICE_PHYSICAL_RANGE_MR;

	if (MLX5_CAP_DEVICE_MEM(dev->mdev, memic)) {
		props->max_dm_size =
			MLX5_CAP_DEVICE_MEM(dev->mdev, max_memic_size);
		props->exp_comp_mask |= IB_EXP_DEVICE_ATTR_MAX_DM_SIZE;
	}

	return 0;
}

static void mlx5_ib_enable_dc_tracer(struct mlx5_ib_dev *dev)
{
	struct device *device = dev->ib_dev.dma_device;
	struct mlx5_dc_tracer *dct = &dev->dctr;
	int order;
	void *tmp;
	int size;
	int err;

	size = MLX5_CAP_GEN(dev->mdev, num_ports) * 4096;
	if (size <= PAGE_SIZE)
		order = 0;
	else
		order = 1;

	dct->pg = alloc_pages(GFP_KERNEL, order);
	if (!dct->pg) {
		mlx5_ib_err(dev, "failed to allocate %d pages\n", order);
		return;
	}

	tmp = kmap(dct->pg);
	if (!tmp) {
		mlx5_ib_err(dev, "failed to kmap one page\n");
		err = -ENOMEM;
		goto map_err;
	}

	memset(tmp, 0xff, size);
	kunmap(dct->pg);

	dct->size = size;
	dct->order = order;
	dct->dma = dma_map_page(device, dct->pg, 0, size, DMA_FROM_DEVICE);
	if (dma_mapping_error(device, dct->dma)) {
		mlx5_ib_err(dev, "dma mapping error\n");
		goto map_err;
	}

	err = mlx5_core_set_dc_cnak_trace(dev->mdev, 1, dct->dma);
	if (err) {
		mlx5_ib_warn(dev, "failed to enable DC tracer\n");
		goto cmd_err;
	}

	return;

cmd_err:
	dma_unmap_page(device, dct->dma, size, DMA_FROM_DEVICE);
map_err:
	__free_pages(dct->pg, dct->order);
	dct->pg = NULL;
}

static void mlx5_ib_disable_dc_tracer(struct mlx5_ib_dev *dev)
{
	struct device *device = dev->ib_dev.dma_device;
	struct mlx5_dc_tracer *dct = &dev->dctr;
	int err;

	if (!dct->pg)
		return;

	err = mlx5_core_set_dc_cnak_trace(dev->mdev, 0, dct->dma);
	if (err) {
		mlx5_ib_warn(dev, "failed to disable DC tracer\n");
		return;
	}

	dma_unmap_page(device, dct->dma, dct->size, DMA_FROM_DEVICE);
	__free_pages(dct->pg, dct->order);
	dct->pg = NULL;
}

int mlx5_ib_mmap_dc_info_page(struct mlx5_ib_dev *dev,
			      struct vm_area_struct *vma)
{
	struct mlx5_dc_tracer *dct;
	phys_addr_t pfn;
	int err;

	if ((MLX5_CAP_GEN(dev->mdev, port_type) !=
	     MLX5_CAP_PORT_TYPE_IB) ||
	    (!mlx5_core_is_pf(dev->mdev)) ||
	    (!MLX5_CAP_GEN(dev->mdev, dc_cnak_trace)))
		return -ENOTSUPP;

	dct = &dev->dctr;
	if (!dct->pg) {
		mlx5_ib_err(dev, "mlx5_ib_mmap DC no page\n");
		return -ENOMEM;
	}

	pfn = page_to_pfn(dct->pg);
	err = remap_pfn_range(vma, vma->vm_start, pfn, dct->size, vma->vm_page_prot);
	if (err) {
		mlx5_ib_err(dev, "mlx5_ib_mmap DC remap_pfn_range failed\n");
		return err;
	}
	return 0;
}


enum {
	MLX5_DC_CNAK_SIZE		= 128,
	MLX5_NUM_BUF_IN_PAGE		= PAGE_SIZE / MLX5_DC_CNAK_SIZE,
	MLX5_CNAK_TX_CQ_SIGNAL_FACTOR	= 128,
	MLX5_DC_CNAK_SL			= 0,
	MLX5_DC_CNAK_VL			= 0,
};

static void dump_buf(void *buf, int size)
{
	__be32 *p = buf;
	int offset;
	int i;

	for (i = 0, offset = 0; i < size; i += 16) {
		pr_info("%03x: %08x %08x %08x %08x\n", offset, be32_to_cpu(p[0]),
			be32_to_cpu(p[1]), be32_to_cpu(p[2]), be32_to_cpu(p[3]));
		p += 4;
		offset += 16;
	}
	pr_info("\n");
}

enum {
	CNAK_LENGTH_WITHOUT_GRH	= 32,
	CNAK_LENGTH_WITH_GRH	= 72,
};

static struct mlx5_dc_desc *get_desc_from_index(struct mlx5_dc_desc *desc, u64 index, unsigned *offset)
{
	struct mlx5_dc_desc *d;

	int i;
	int j;

	i = index / MLX5_NUM_BUF_IN_PAGE;
	j = index % MLX5_NUM_BUF_IN_PAGE;
	d = desc + i;
	*offset = j * MLX5_DC_CNAK_SIZE;
	return d;
}

static void build_cnak_msg(void *rbuf, void *sbuf, u32 *length, u16 *dlid)
{
	void *rdceth, *sdceth;
	void *rlrh, *slrh;
	void *rgrh, *sgrh;
	void *rbth, *sbth;
	int is_global;
	void *saeth;

	memset(sbuf, 0, MLX5_DC_CNAK_SIZE);
	rlrh = rbuf;
	is_global = MLX5_GET(lrh, rlrh, lnh) == 0x3;
	rgrh = is_global ? rlrh + MLX5_ST_SZ_BYTES(lrh) : NULL;
	rbth = rgrh ? rgrh + MLX5_ST_SZ_BYTES(grh) : rlrh + MLX5_ST_SZ_BYTES(lrh);
	rdceth = rbth + MLX5_ST_SZ_BYTES(bth);

	slrh = sbuf;
	sgrh = is_global ? slrh + MLX5_ST_SZ_BYTES(lrh) : NULL;
	sbth = sgrh ? sgrh + MLX5_ST_SZ_BYTES(grh) : slrh + MLX5_ST_SZ_BYTES(lrh);
	sdceth = sbth + MLX5_ST_SZ_BYTES(bth);
	saeth = sdceth + MLX5_ST_SZ_BYTES(dceth);

	*dlid = MLX5_GET(lrh, rlrh, slid);
	MLX5_SET(lrh, slrh, vl, MLX5_DC_CNAK_VL);
	MLX5_SET(lrh, slrh, lver, MLX5_GET(lrh, rlrh, lver));
	MLX5_SET(lrh, slrh, sl, MLX5_DC_CNAK_SL);
	MLX5_SET(lrh, slrh, lnh, MLX5_GET(lrh, rlrh, lnh));
	MLX5_SET(lrh, slrh, dlid, MLX5_GET(lrh, rlrh, slid));
	MLX5_SET(lrh, slrh, pkt_len, 0x9 + ((is_global ? MLX5_ST_SZ_BYTES(grh) : 0) >> 2));
	MLX5_SET(lrh, slrh, slid, MLX5_GET(lrh, rlrh, dlid));

	if (is_global) {
		void *rdgid, *rsgid;
		void *ssgid, *sdgid;

		MLX5_SET(grh, sgrh, ip_version, MLX5_GET(grh, rgrh, ip_version));
		MLX5_SET(grh, sgrh, traffic_class, MLX5_GET(grh, rgrh, traffic_class));
		MLX5_SET(grh, sgrh, flow_label, MLX5_GET(grh, rgrh, flow_label));
		MLX5_SET(grh, sgrh, payload_length, 0x1c);
		MLX5_SET(grh, sgrh, next_header, 0x1b);
		MLX5_SET(grh, sgrh, hop_limit, MLX5_GET(grh, rgrh, hop_limit));

		rdgid = MLX5_ADDR_OF(grh, rgrh, dgid);
		rsgid = MLX5_ADDR_OF(grh, rgrh, sgid);
		ssgid = MLX5_ADDR_OF(grh, sgrh, sgid);
		sdgid = MLX5_ADDR_OF(grh, sgrh, dgid);
		memcpy(ssgid, rdgid, 16);
		memcpy(sdgid, rsgid, 16);
		*length = CNAK_LENGTH_WITH_GRH;
	} else {
		*length = CNAK_LENGTH_WITHOUT_GRH;
	}

	MLX5_SET(bth, sbth, opcode, 0x51);
	MLX5_SET(bth, sbth, migreq, 0x1);
	MLX5_SET(bth, sbth, p_key, MLX5_GET(bth, rbth, p_key));
	MLX5_SET(bth, sbth, dest_qp, MLX5_GET(dceth, rdceth, dci_dct));
	MLX5_SET(bth, sbth, psn, MLX5_GET(bth, rbth, psn));

	MLX5_SET(dceth, sdceth, dci_dct, MLX5_GET(bth, rbth, dest_qp));

	MLX5_SET(aeth, saeth, syndrome, 0x64);

	if (0) {
		pr_info("===dump packet ====\n");
		dump_buf(sbuf, *length);
	}
}

static int reduce_tx_pending(struct mlx5_dc_data *dcd, int num)
{
	struct mlx5_ib_dev *dev = dcd->dev;
	struct ib_cq *cq = dcd->scq;
	unsigned int send_completed;
	unsigned int polled;
	struct ib_wc wc;
	int n;

	while (num > 0) {
		n = ib_poll_cq(cq, 1, &wc);
		if (unlikely(n < 0)) {
			mlx5_ib_warn(dev, "error polling cnak send cq\n");
			return n;
		}
		if (unlikely(!n))
			return -EAGAIN;

		if (unlikely(wc.status != IB_WC_SUCCESS)) {
			mlx5_ib_warn(dev, "cnak send completed with error, status %d vendor_err %d\n",
				     wc.status, wc.vendor_err);
			dcd->last_send_completed++;
			dcd->tx_pending--;
			num--;
		} else {
			send_completed = wc.wr_id;
			polled = send_completed - dcd->last_send_completed;
			dcd->tx_pending = (unsigned int)(dcd->cur_send - send_completed);
			num -= polled;
			dcd->last_send_completed = send_completed;
		}
	}

	return 0;
}

static bool signal_wr(int wr_count, struct mlx5_dc_data *dcd)
{
	return !(wr_count % dcd->tx_signal_factor);
}

static int send_cnak(struct mlx5_dc_data *dcd, struct mlx5_send_wr *mlx_wr,
		     u64 rcv_buff_id)
{
	struct ib_send_wr *wr = &mlx_wr->wr;
	struct mlx5_ib_dev *dev = dcd->dev;
	const struct ib_send_wr *bad_wr;
	struct mlx5_dc_desc *rxd;
	struct mlx5_dc_desc *txd;
	unsigned int offset;
	unsigned int cur;
	__be32 *sbuf;
	void *rbuf;
	int err;

	if (unlikely(dcd->tx_pending > dcd->max_wqes)) {
		mlx5_ib_warn(dev, "SW error in cnak send: tx_pending(%d) > max_wqes(%d)\n",
			     dcd->tx_pending, dcd->max_wqes);
		return -EFAULT;
	}

	if (unlikely(dcd->tx_pending == dcd->max_wqes)) {
		err = reduce_tx_pending(dcd, 1);
		if (err)
			return err;
		if (dcd->tx_pending == dcd->max_wqes)
			return -EAGAIN;
	}

	cur = dcd->cur_send;
	txd = get_desc_from_index(dcd->txdesc, cur % dcd->max_wqes, &offset);
	sbuf = txd->buf + offset;

	wr->sg_list[0].addr = txd->dma + offset;
	wr->sg_list[0].lkey = dcd->mr->lkey;
	wr->opcode = IB_WR_SEND;
	wr->num_sge = 1;
	wr->wr_id = cur;
	if (!signal_wr(cur, dcd))
		wr->send_flags &= ~IB_SEND_SIGNALED;
	else
		wr->send_flags |= IB_SEND_SIGNALED;

	rxd = get_desc_from_index(dcd->rxdesc, rcv_buff_id, &offset);
	rbuf = rxd->buf + offset;
	build_cnak_msg(rbuf, sbuf, &wr->sg_list[0].length, &mlx_wr->sel.mlx.dlid);

	mlx_wr->sel.mlx.sl = MLX5_DC_CNAK_SL;
	mlx_wr->sel.mlx.icrc = 1;

	err = ib_post_send(dcd->dcqp, wr, &bad_wr);
	if (likely(!err)) {
		dcd->tx_pending++;
		dcd->cur_send++;
		atomic64_inc(&dcd->dev->dc_stats[dcd->port - 1].cnaks);
	}

	return err;
}

static int mlx5_post_one_rxdc(struct mlx5_dc_data *dcd, int index)
{
	const struct ib_recv_wr *bad_wr;
	struct ib_recv_wr wr;
	struct ib_sge sge;
	u64 addr;
	int err;
	int i;
	int j;

	i = index / (PAGE_SIZE / MLX5_DC_CNAK_SIZE);
	j = index % (PAGE_SIZE / MLX5_DC_CNAK_SIZE);
	addr = dcd->rxdesc[i].dma + j * MLX5_DC_CNAK_SIZE;

	memset(&wr, 0, sizeof(wr));
	wr.num_sge = 1;
	sge.addr = addr;
	sge.length = MLX5_DC_CNAK_SIZE;
	sge.lkey = dcd->mr->lkey;
	wr.sg_list = &sge;
	wr.num_sge = 1;
	wr.wr_id = index;
	err = ib_post_recv(dcd->dcqp, &wr, &bad_wr);
	if (unlikely(err))
		mlx5_ib_warn(dcd->dev, "failed to post dc rx buf at index %d\n", index);

	return err;
}

static void dc_cnack_rcv_comp_handler(struct ib_cq *cq, void *cq_context)
{
	struct mlx5_dc_data *dcd = cq_context;
	struct mlx5_ib_dev *dev = dcd->dev;
	struct mlx5_send_wr mlx_wr;
	struct ib_send_wr *wr = &mlx_wr.wr;
	struct ib_wc *wc = dcd->wc_tbl;
	struct ib_sge sge;
	int err;
	int n;
	int i;

	memset(&mlx_wr, 0, sizeof(mlx_wr));
	wr->sg_list = &sge;

	n = ib_poll_cq(cq, MLX5_CNAK_RX_POLL_CQ_QUOTA, wc);
	if (unlikely(n < 0)) {
		/* mlx5 never returns negative values but leave a message just in case */
		mlx5_ib_warn(dev, "DC cnak[%d]: failed to poll cq (%d), aborting\n",
			     dcd->index, n);
		return;
	}
	if (likely(n > 0)) {
		for (i = 0; i < n; i++) {
			if (dev->mdev->state == MLX5_DEVICE_STATE_INTERNAL_ERROR)
				return;

			if (unlikely(wc[i].status != IB_WC_SUCCESS)) {
				mlx5_ib_warn(dev, "DC cnak[%d]: completed with error, status = %d vendor_err = %d\n",
					     wc[i].status, wc[i].vendor_err, dcd->index);
			} else {
				atomic64_inc(&dcd->dev->dc_stats[dcd->port - 1].connects);
				dev->dc_stats[dcd->port - 1].rx_scatter[dcd->index]++;
				if (unlikely(send_cnak(dcd, &mlx_wr, wc[i].wr_id)))
					mlx5_ib_warn(dev, "DC cnak[%d]: failed to allocate send buf - dropped\n",
						     dcd->index);
			}

			if (unlikely(mlx5_post_one_rxdc(dcd, wc[i].wr_id))) {
				atomic64_inc(&dcd->dev->dc_stats[dcd->port - 1].discards);
				mlx5_ib_warn(dev, "DC cnak[%d]: repost rx failed, will leak rx queue\n",
					     dcd->index);
			}
		}
	}

	err = ib_req_notify_cq(cq, IB_CQ_NEXT_COMP);
	if (unlikely(err))
		mlx5_ib_warn(dev, "DC cnak[%d]: failed to re-arm receive cq (%d)\n",
			     dcd->index, err);
}

static int alloc_dc_buf(struct mlx5_dc_data *dcd, int rx)
{
	struct mlx5_ib_dev *dev = dcd->dev;
	struct mlx5_dc_desc **desc;
	struct mlx5_dc_desc *d;
	struct device *ddev;
	int max_wqes;
	int err = 0;
	int npages;
	int totsz;
	int i;

	ddev = &dev->mdev->pdev->dev;
	max_wqes = dcd->max_wqes;
	totsz = max_wqes * MLX5_DC_CNAK_SIZE;
	npages = DIV_ROUND_UP(totsz, PAGE_SIZE);
	desc = rx ? &dcd->rxdesc : &dcd->txdesc;
	*desc = kcalloc(npages, sizeof(*dcd->rxdesc), GFP_KERNEL);
	if (!*desc) {
		err = -ENOMEM;
		goto out;
	}

	for (i = 0; i < npages; i++) {
		d = *desc + i;
		d->buf = dma_alloc_coherent(ddev, PAGE_SIZE, &d->dma, GFP_KERNEL);
		if (!d->buf) {
			mlx5_ib_err(dev, "dma alloc failed at %d\n", i);
			goto out_free;
		}
	}
	if (rx)
		dcd->rx_npages = npages;
	else
		dcd->tx_npages = npages;

	return 0;

out_free:
	for (i--; i >= 0; i--) {
		d = *desc + i;
		dma_free_coherent(ddev, PAGE_SIZE, d->buf, d->dma);
	}
	kfree(*desc);
out:
	return err;
}

static int alloc_dc_rx_buf(struct mlx5_dc_data *dcd)
{
	return alloc_dc_buf(dcd, 1);
}

static int alloc_dc_tx_buf(struct mlx5_dc_data *dcd)
{
	return alloc_dc_buf(dcd, 0);
}

static void free_dc_buf(struct mlx5_dc_data *dcd, int rx)
{
	struct mlx5_ib_dev *dev = dcd->dev;
	struct mlx5_dc_desc *desc;
	struct mlx5_dc_desc *d;
	struct device *ddev;
	int npages;
	int i;

	ddev = &dev->mdev->pdev->dev;
	npages = rx ? dcd->rx_npages : dcd->tx_npages;
	desc = rx ? dcd->rxdesc : dcd->txdesc;
	for (i = 0; i < npages; i++) {
		d = desc + i;
		dma_free_coherent(ddev, PAGE_SIZE, d->buf, d->dma);
	}
	kfree(desc);
}

static void free_dc_rx_buf(struct mlx5_dc_data *dcd)
{
	free_dc_buf(dcd, 1);
}

static void free_dc_tx_buf(struct mlx5_dc_data *dcd)
{
	free_dc_buf(dcd, 0);
}

struct dc_attribute {
	struct attribute attr;
	ssize_t (*show)(struct mlx5_dc_stats *, struct dc_attribute *, char *buf);
	ssize_t (*store)(struct mlx5_dc_stats *, struct dc_attribute *,
			 const char *buf, size_t count);
};

static ssize_t qp_count_show(struct mlx5_dc_stats *dc_stats,
			     struct dc_attribute *unused,
			     char *buf)
{
	return sprintf(buf, "%u\n", dc_stats->dev->num_dc_cnak_qps);
}

static int init_driver_cnak(struct mlx5_ib_dev *dev, int port, int index);
static ssize_t qp_count_store(struct mlx5_dc_stats *dc_stats,
			      struct dc_attribute *unused,
			      const char *buf, size_t count)
{
	struct mlx5_ib_dev *dev = dc_stats->dev;
	int port = dc_stats->port;
	unsigned long var;
	int i;
	int err = 0;
	int qp_add = 0;

	if (kstrtol(buf, 0, &var)) {
		err = -EINVAL;
		goto err;
	}
	if ((var > dev->max_dc_cnak_qps) ||
	    (dev->num_dc_cnak_qps >= var)) {
		err = -EINVAL;
		goto err;
	}

	for (i = dev->num_dc_cnak_qps; i < var; i++) {
		err = init_driver_cnak(dev, port, i);
		if (err) {
			mlx5_ib_warn(dev, "Fail to set %ld CNAK QPs. Only %d were added\n",
				     var, qp_add);
			break;
		}
		dev->num_dc_cnak_qps++;
		qp_add++;
	}
err:

	return err ? err : count;
}

#define DC_ATTR(_name, _mode, _show, _store) \
struct dc_attribute dc_attr_##_name = __ATTR(_name, _mode, _show, _store)

static DC_ATTR(qp_count, 0644, qp_count_show, qp_count_store);

static ssize_t rx_connect_show(struct mlx5_dc_stats *dc_stats,
			       struct dc_attribute *unused,
			       char *buf)
{
	unsigned long num;

	num = atomic64_read(&dc_stats->connects);

	return sprintf(buf, "%lu\n", num);
}

static ssize_t tx_cnak_show(struct mlx5_dc_stats *dc_stats,
			    struct dc_attribute *unused,
			    char *buf)
{
	unsigned long num;

	num = atomic64_read(&dc_stats->cnaks);

	return sprintf(buf, "%lu\n", num);
}

static ssize_t tx_discard_show(struct mlx5_dc_stats *dc_stats,
			       struct dc_attribute *unused,
			       char *buf)
{
	unsigned long num;

	num = atomic64_read(&dc_stats->discards);

	return sprintf(buf, "%lu\n", num);
}

static ssize_t rx_scatter_show(struct mlx5_dc_stats *dc_stats,
			       struct dc_attribute *unused,
			       char *buf)
{
	int i;
	int ret;
	int res = 0;

	buf[0] = 0;

	for (i = 0; i < dc_stats->dev->max_dc_cnak_qps ; i++) {
		unsigned long num = dc_stats->rx_scatter[i];

		if (!dc_stats->dev->dcd[dc_stats->port - 1][i].initialized)
			continue;
		ret = sprintf(buf + strlen(buf), "%d:%lu\n", i, num);
		if (ret < 0) {
			res = ret;
			break;
		}
		res += ret;
	}
	return res;
}

#define DC_ATTR_RO(_name) \
struct dc_attribute dc_attr_##_name = __ATTR_RO(_name)

static DC_ATTR_RO(rx_connect);
static DC_ATTR_RO(tx_cnak);
static DC_ATTR_RO(tx_discard);
static DC_ATTR_RO(rx_scatter);

static struct attribute *dc_attrs[] = {
	&dc_attr_rx_connect.attr,
	&dc_attr_tx_cnak.attr,
	&dc_attr_tx_discard.attr,
	&dc_attr_rx_scatter.attr,
	&dc_attr_qp_count.attr,
	NULL
};

static ssize_t dc_attr_show(struct kobject *kobj,
			    struct attribute *attr, char *buf)
{
	struct dc_attribute *dc_attr = container_of(attr, struct dc_attribute, attr);
	struct mlx5_dc_stats *d = container_of(kobj, struct mlx5_dc_stats, kobj);

	if (!dc_attr->show)
		return -EIO;

	return dc_attr->show(d, dc_attr, buf);
}

static ssize_t dc_attr_store(struct kobject *kobj,
			     struct attribute *attr, const char *buf, size_t size)
{
	struct dc_attribute *dc_attr = container_of(attr, struct dc_attribute, attr);
	struct mlx5_dc_stats *d = container_of(kobj, struct mlx5_dc_stats, kobj);

	if (!dc_attr->store)
		return -EIO;

	return dc_attr->store(d, dc_attr, buf, size);
}

static const struct sysfs_ops dc_sysfs_ops = {
	.show = dc_attr_show,
	.store = dc_attr_store
};

static struct kobj_type dc_type = {
	.sysfs_ops     = &dc_sysfs_ops,
	.default_attrs = dc_attrs
};

static int init_dc_sysfs(struct mlx5_ib_dev *dev)
{
	struct device *device = &dev->ib_dev.dev;

	dev->dc_kobj = kobject_create_and_add("dct", &device->kobj);
	if (!dev->dc_kobj) {
		mlx5_ib_err(dev, "failed to register DCT sysfs object\n");
		return -ENOMEM;
	}

	return 0;
}

static void cleanup_dc_sysfs(struct mlx5_ib_dev *dev)
{
	if (dev->dc_kobj) {
		kobject_put(dev->dc_kobj);
		dev->dc_kobj = NULL;
	}
}

static int init_dc_port_sysfs(struct mlx5_dc_stats *dc_stats,
			      struct mlx5_ib_dev *dev, int port)
{
	int ret;

	dc_stats->dev = dev;
	dc_stats->port = port;
	ret = kobject_init_and_add(&dc_stats->kobj, &dc_type,
				   dc_stats->dev->dc_kobj, "%d", dc_stats->port);
	if (!ret)
		dc_stats->initialized = 1;
	return ret;
}

static void cleanup_dc_port_sysfs(struct mlx5_dc_stats *dc_stats)
{
	if (!dc_stats->initialized)
		return;
	kobject_put(&dc_stats->kobj);
}

static int comp_vector(struct ib_device *dev, int port, int index)
{
	int comp_per_port = dev->num_comp_vectors / dev->phys_port_cnt;

	return (port - 1) * comp_per_port + (index % comp_per_port);
}

static int init_driver_cnak(struct mlx5_ib_dev *dev, int port, int index)
{
	struct mlx5_dc_data *dcd = &dev->dcd[port - 1][index];
	struct mlx5_ib_resources *devr = &dev->devr;
	struct ib_cq_init_attr cq_attr = {};
	struct ib_qp_init_attr init_attr;
	struct ib_pd *pd = devr->p0;
	struct ib_qp_attr attr;
	int ncqe;
	int nwr;
	int err;
	int i;

	dcd->dev = dev;
	dcd->port = port;
	dcd->index = index;
	dcd->mr = pd->device->get_dma_mr(pd,  IB_ACCESS_LOCAL_WRITE);
	if (IS_ERR(dcd->mr)) {
		mlx5_ib_warn(dev, "failed to create dc DMA MR\n");
		err = PTR_ERR(dcd->mr);
		goto error1;
	}

	dcd->mr->device      = pd->device;
	dcd->mr->pd          = pd;
	dcd->mr->uobject     = NULL;
	dcd->mr->need_inval  = false;

	ncqe = min_t(int, dc_cnak_qp_depth,
		     BIT(MLX5_CAP_GEN(dev->mdev, log_max_cq_sz)));
	nwr = min_t(int, ncqe,
		    BIT(MLX5_CAP_GEN(dev->mdev, log_max_qp_sz)));

	if (dc_cnak_qp_depth > nwr) {
		mlx5_ib_warn(dev, "Can't set DC CNAK QP size to %d. Set to default %d\n",
			     dc_cnak_qp_depth, nwr);
		dc_cnak_qp_depth = nwr;
	}

	cq_attr.cqe = ncqe;
	cq_attr.comp_vector = comp_vector(&dev->ib_dev, port, index);
	dcd->rcq = ib_create_cq(&dev->ib_dev, dc_cnack_rcv_comp_handler, NULL,
				dcd, &cq_attr);
	if (IS_ERR(dcd->rcq)) {
		err = PTR_ERR(dcd->rcq);
		mlx5_ib_warn(dev, "failed to create dc cnack rx cq (%d)\n", err);
		goto error2;
	}

	err = ib_req_notify_cq(dcd->rcq, IB_CQ_NEXT_COMP);
	if (err) {
		mlx5_ib_warn(dev, "failed to setup dc cnack rx cq (%d)\n", err);
		goto error3;
	}

	dcd->scq = ib_create_cq(&dev->ib_dev, NULL, NULL,
				dcd, &cq_attr);
	if (IS_ERR(dcd->scq)) {
		err = PTR_ERR(dcd->scq);
		mlx5_ib_warn(dev, "failed to create dc cnack tx cq (%d)\n", err);
		goto error3;
	}

	memset(&init_attr, 0, sizeof(init_attr));
	init_attr.qp_type = MLX5_IB_QPT_SW_CNAK;
	init_attr.cap.max_recv_wr = nwr;
	init_attr.cap.max_recv_sge = 1;
	init_attr.cap.max_send_wr = nwr;
	init_attr.cap.max_send_sge = 1;
	init_attr.sq_sig_type = IB_SIGNAL_REQ_WR;
	init_attr.recv_cq = dcd->rcq;
	init_attr.send_cq = dcd->scq;
	dcd->dcqp = ib_create_qp(pd, &init_attr);
	if (IS_ERR(dcd->dcqp)) {
		mlx5_ib_warn(dev, "failed to create qp (%d)\n", err);
		err = PTR_ERR(dcd->dcqp);
		goto error4;
	}

	memset(&attr, 0, sizeof(attr));
	attr.qp_state = IB_QPS_INIT;
	attr.port_num = port;
	err = ib_modify_qp(dcd->dcqp, &attr,
			   IB_QP_STATE | IB_QP_PKEY_INDEX | IB_QP_PORT);
	if (err) {
		mlx5_ib_warn(dev, "failed to modify qp to init\n");
		goto error5;
	}

	memset(&attr, 0, sizeof(attr));
	attr.qp_state = IB_QPS_RTR;
	attr.path_mtu = IB_MTU_4096;
	err = ib_modify_qp(dcd->dcqp, &attr, IB_QP_STATE);
	if (err) {
		mlx5_ib_warn(dev, "failed to modify qp to rtr\n");
		goto error5;
	}

	memset(&attr, 0, sizeof(attr));
	attr.qp_state = IB_QPS_RTS;
	err = ib_modify_qp(dcd->dcqp, &attr, IB_QP_STATE);
	if (err) {
		mlx5_ib_warn(dev, "failed to modify qp to rts\n");
		goto error5;
	}

	dcd->max_wqes = nwr;
	err = alloc_dc_rx_buf(dcd);
	if (err) {
		mlx5_ib_warn(dev, "failed to allocate rx buf\n");
		goto error5;
	}

	err = alloc_dc_tx_buf(dcd);
	if (err) {
		mlx5_ib_warn(dev, "failed to allocate tx buf\n");
		goto error6;
	}

	for (i = 0; i < nwr; i++) {
		err = mlx5_post_one_rxdc(dcd, i);
		if (err)
			goto error7;
	}

	dcd->tx_signal_factor = min_t(int, DIV_ROUND_UP(dcd->max_wqes, 2),
				      MLX5_CNAK_TX_CQ_SIGNAL_FACTOR);

	dcd->initialized = 1;
	return 0;

error7:
	free_dc_tx_buf(dcd);
error6:
	free_dc_rx_buf(dcd);
error5:
	if (ib_destroy_qp(dcd->dcqp))
		mlx5_ib_warn(dev, "failed to destroy dc qp\n");
error4:
	if (ib_destroy_cq(dcd->scq))
		mlx5_ib_warn(dev, "failed to destroy dc scq\n");
error3:
	if (ib_destroy_cq(dcd->rcq))
		mlx5_ib_warn(dev, "failed to destroy dc rcq\n");
error2:
	ib_dereg_mr(dcd->mr);
error1:
	return err;
}

static void cleanup_driver_cnak(struct mlx5_ib_dev *dev, int port, int index)
{
	struct mlx5_dc_data *dcd = &dev->dcd[port - 1][index];

	if (!dcd->initialized)
		return;

	if (ib_destroy_qp(dcd->dcqp))
		mlx5_ib_warn(dev, "destroy qp failed\n");

	if (ib_destroy_cq(dcd->scq))
		mlx5_ib_warn(dev, "destroy scq failed\n");

	if (ib_destroy_cq(dcd->rcq))
		mlx5_ib_warn(dev, "destroy rcq failed\n");

	ib_dereg_mr(dcd->mr);
	free_dc_tx_buf(dcd);
	free_dc_rx_buf(dcd);
	dcd->initialized = 0;
}

int mlx5_ib_init_dc_improvements(struct mlx5_ib_dev *dev)
{
	int port;
	int err;
	int i;
	struct mlx5_core_dev *mdev = dev->mdev;
	int max_dc_cnak_qps;
	int ini_dc_cnak_qps;

	if (!mlx5_core_is_pf(dev->mdev) ||
	    !(MLX5_CAP_GEN(dev->mdev, dc_cnak_trace)))
		return 0;

	mlx5_ib_enable_dc_tracer(dev);

	max_dc_cnak_qps = min_t(int, 1 << MLX5_CAP_GEN(mdev, log_max_dc_cnak_qps),
			      dev->ib_dev.num_comp_vectors / MLX5_CAP_GEN(mdev, num_ports));
	err = init_dc_sysfs(dev);
	if (err)
		return err;

	if (!MLX5_CAP_GEN(dev->mdev, dc_connect_qp))
		return 0;

	/* start with 25% of maximum CNAK QPs */
	ini_dc_cnak_qps = DIV_ROUND_UP(max_dc_cnak_qps, 4);

	for (port = 1; port <= MLX5_CAP_GEN(dev->mdev, num_ports); port++) {
		dev->dcd[port - 1] =
			kcalloc(max_dc_cnak_qps, sizeof(struct mlx5_dc_data), GFP_KERNEL);
		if (!dev->dcd[port - 1]) {
			err = -ENOMEM;
			goto err;
		}
		dev->dc_stats[port - 1].rx_scatter =
			kcalloc(max_dc_cnak_qps, sizeof(int), GFP_KERNEL);
		if (!dev->dc_stats[port - 1].rx_scatter) {
			err = -ENOMEM;
			goto err;
		}
		for (i = 0; i < ini_dc_cnak_qps; i++) {
			err = init_driver_cnak(dev, port, i);
			if (err)
				goto err;
		}
		err = init_dc_port_sysfs(&dev->dc_stats[port - 1], dev, port);
		if (err) {
			mlx5_ib_warn(dev, "failed to initialize DC cnak sysfs\n");
			goto err;
		}
	}
	dev->num_dc_cnak_qps = ini_dc_cnak_qps;
	dev->max_dc_cnak_qps = max_dc_cnak_qps;


	return 0;

err:
	for (port = 1; port <= MLX5_CAP_GEN(dev->mdev, num_ports); port++) {
		for (i = 0; i < ini_dc_cnak_qps; i++)
			cleanup_driver_cnak(dev, port, i);
		cleanup_dc_port_sysfs(&dev->dc_stats[port - 1]);
		kfree(dev->dc_stats[port - 1].rx_scatter);
		kfree(dev->dcd[port - 1]);
	}
	cleanup_dc_sysfs(dev);

	return err;
}

void mlx5_ib_cleanup_dc_improvements(struct mlx5_ib_dev *dev)
{
	int port;
	int i;

	for (port = 1; port <= MLX5_CAP_GEN(dev->mdev, num_ports); port++) {
		for (i = 0; i < dev->num_dc_cnak_qps; i++)
			cleanup_driver_cnak(dev, port, i);
		cleanup_dc_port_sysfs(&dev->dc_stats[port - 1]);
		kfree(dev->dc_stats[port - 1].rx_scatter);
		kfree(dev->dcd[port - 1]);
	}
	cleanup_dc_sysfs(dev);

	mlx5_ib_disable_dc_tracer(dev);
}

struct tc_attribute {
	struct attribute attr;
	ssize_t (*show)(struct mlx5_tc_data *, struct tc_attribute *, char *buf);
	ssize_t (*store)(struct mlx5_tc_data *, struct tc_attribute *,
			 const char *buf, size_t count);
};

#define TC_ATTR(_name, _mode, _show, _store) \
struct tc_attribute tc_attr_##_name = __ATTR(_name, _mode, _show, _store)

static ssize_t traffic_class_show(struct mlx5_tc_data *tcd, struct tc_attribute *unused, char *buf)
{
	return sprintf(buf, "%d\n", tcd->val);
}

static ssize_t traffic_class_store(struct mlx5_tc_data *tcd, struct tc_attribute *unused,
				   const char *buf, size_t count)
{
	long var;

	if (kstrtol(buf, 0, &var))
		return -EINVAL;

	if (var > 0xff)
		return -EINVAL;

	tcd->val = var;
	return count;
}

static TC_ATTR(traffic_class, 0644, traffic_class_show, traffic_class_store);

static struct attribute *tc_attrs[] = {
	&tc_attr_traffic_class.attr,
	NULL
};

static ssize_t tc_attr_show(struct kobject *kobj,
			    struct attribute *attr, char *buf)
{
	struct tc_attribute *tc_attr = container_of(attr, struct tc_attribute, attr);
	struct mlx5_tc_data *d = container_of(kobj, struct mlx5_tc_data, kobj);

	if (!tc_attr->show)
		return -EIO;

	return tc_attr->show(d, tc_attr, buf);
}

static ssize_t tc_attr_store(struct kobject *kobj,
			     struct attribute *attr, const char *buf, size_t count)
{
	struct tc_attribute *tc_attr = container_of(attr, struct tc_attribute, attr);
	struct mlx5_tc_data *d = container_of(kobj, struct mlx5_tc_data, kobj);

	if (!tc_attr->store)
		return -EIO;

	return tc_attr->store(d, tc_attr, buf, count);
}

static const struct sysfs_ops tc_sysfs_ops = {
	.show = tc_attr_show,
	.store = tc_attr_store
};

static struct kobj_type tc_type = {
	.sysfs_ops     = &tc_sysfs_ops,
	.default_attrs = tc_attrs
};

int init_tc_sysfs(struct mlx5_ib_dev *dev)
{
	struct device *device = &dev->ib_dev.dev;
	int port;
	int err;

	dev->tc_kobj = kobject_create_and_add("tc", &device->kobj);
	if (!dev->tc_kobj)
		return -ENOMEM;
	for (port = 1; port <= MLX5_CAP_GEN(dev->mdev, num_ports); port++) {
		struct mlx5_tc_data *tcd = &dev->tcd[port - 1];

		err = kobject_init_and_add(&tcd->kobj, &tc_type, dev->tc_kobj, "%d", port);
		if (err)
			goto err;
		tcd->val = -1;
		tcd->initialized = true;
	}
	return 0;
err:
	cleanup_tc_sysfs(dev);
	return err;
}

void cleanup_tc_sysfs(struct mlx5_ib_dev *dev)
{
	if (dev->tc_kobj) {
		int port;

		kobject_put(dev->tc_kobj);
		dev->tc_kobj = NULL;
		for (port = 1; port <= MLX5_CAP_GEN(dev->mdev, num_ports); port++) {
			struct mlx5_tc_data *tcd = &dev->tcd[port - 1];

			if (tcd->initialized)
				kobject_put(&tcd->kobj);
		}
	}
}

static phys_addr_t idx2pfn(struct mlx5_ib_dev *dev, int idx)
{
	int fw_uars_per_page = MLX5_CAP_GEN(dev->mdev, uar_4k) ? MLX5_UARS_IN_PAGE : 1;

	return (pci_resource_start(dev->mdev->pdev, 0) >> PAGE_SHIFT) + idx /
	       fw_uars_per_page;
}

int alloc_and_map_wc(struct mlx5_ib_dev *dev,
		     struct mlx5_ib_ucontext *context, u32 indx,
		     struct vm_area_struct *vma)
{
	int uars_per_page = get_uars_per_sys_page(dev,
					          context->bfregi.lib_uar_4k);
	u32 sys_page_idx = indx / uars_per_page;
	phys_addr_t pfn;
	u32 uar_index;
	size_t map_size = vma->vm_end - vma->vm_start;
	struct rdma_umap_priv *vma_prv;
	int err;

	if (indx % uars_per_page) {
		mlx5_ib_warn(dev, "invalid uar index %d, should be system page aligned and there are %d uars per page.\n",
			     indx, uars_per_page);
		return -EINVAL;
	}

#if defined(CONFIG_X86)
	if (!pat_enabled()) {
		mlx5_ib_dbg(dev, "write combine not available\n");
		return -EPERM;
	}
#elif !(defined(CONFIG_PPC) || ((defined(CONFIG_ARM) || defined(CONFIG_ARM64)) && defined(CONFIG_MMU)))
	return -EPERM;
#endif

	if (vma->vm_end - vma->vm_start != PAGE_SIZE) {
		mlx5_ib_warn(dev, "wrong size, expected PAGE_SIZE(%ld) got %ld\n",
			     PAGE_SIZE, vma->vm_end - vma->vm_start);
		return -EINVAL;
	}

	if (indx >= MLX5_IB_MAX_CTX_DYNAMIC_UARS) {
		mlx5_ib_warn(dev, "wrong offset, idx:%d max:%d\n",
			     indx, MLX5_IB_MAX_CTX_DYNAMIC_UARS);
		return -EINVAL;
	}

	/* Fail if uar already allocated */
	if (context->dynamic_wc_uar_index[sys_page_idx] != MLX5_IB_INVALID_UAR_INDEX) {
		mlx5_ib_warn(dev, "wrong offset, idx %d is busy\n", indx);
		return -EINVAL;
	}

	err = mlx5_cmd_alloc_uar(dev->mdev, &uar_index);
	if (err) {
		mlx5_ib_warn(dev, "UAR alloc failed\n");
		return err;
	}

	vma_prv = kzalloc(sizeof(struct rdma_umap_priv), GFP_KERNEL);
	if (!vma_prv) {
		mlx5_cmd_free_uar(dev->mdev, uar_index);
		return -ENOMEM;
	}
	pfn = idx2pfn(dev, uar_index);

/*
	vma->vm_page_prot = pgprot_writecombine(vma->vm_page_prot);
	if (io_remap_pfn_range(vma, vma->vm_start, pfn,
			       PAGE_SIZE, vma->vm_page_prot)) {
		mlx5_ib_err(dev, "io remap failed\n");
		mlx5_cmd_free_uar(dev->mdev, uar_index);
		kfree(vma_prv);
		return -EAGAIN;
	}
	context->dynamic_wc_uar_index[sys_page_idx] = uar_index;
*/
	rdma_user_mmap_io(&context->ibucontext, vma, pfn, map_size, pgprot_writecombine(vma->vm_page_prot),vma_prv);
//	mlx5_ib_set_vma_data(vma, context, vma_prv);

	return 0;
}

struct ib_dm *mlx5_ib_exp_alloc_dm(struct ib_device *ibdev,
				   struct ib_ucontext *context,
				   u64 length, u64 uaddr,
				   struct ib_udata *uhw)
{
	u32 map_size = DIV_ROUND_UP(length, PAGE_SIZE) * PAGE_SIZE;
	phys_addr_t memic_addr, memic_pfn;
	u64 act_size = roundup(length, 64);
	struct vm_area_struct *vma;
	struct mlx5_ib_dm *dm;
	pgprot_t prot;
	int ret;

	dm = kzalloc(sizeof(*dm), GFP_KERNEL);
	if (!dm)
		return ERR_PTR(-ENOMEM);

	if (mlx5_core_alloc_memic(to_mdev(ibdev)->mdev, &memic_addr,
				  act_size)) {
		ret = -EFAULT;
		goto err_free;
	}

	if (context) {
		down_read(&current->mm->mmap_sem);
		vma = find_vma(current->mm, uaddr & PAGE_MASK);
		if (!vma || (vma->vm_end - vma->vm_start < map_size)) {
			ret = -EINVAL;
			goto err_vma;
		}

		if (vma->vm_flags & VM_LOCKED) {
			ret = -EACCES;
			goto err_vma;
		}

		prot = pgprot_writecombine(vma->vm_page_prot);
		vma->vm_page_prot = prot;
		memic_pfn = memic_addr >> PAGE_SHIFT;
		if (io_remap_pfn_range(vma, vma->vm_start,
				       memic_pfn, map_size,
				       vma->vm_page_prot)) {
			ret = -EAGAIN;
			goto err_vma;
		}

		up_read(&current->mm->mmap_sem);
	} else {
		dm->dm_base_addr = ioremap(memic_addr, length);
		if (!dm->dm_base_addr) {
			ret = -ENOMEM;
			goto err_map;
		}
	}

	dm->ibdm.dev_addr = memic_addr;

	return &dm->ibdm;

err_vma:
	up_read(&current->mm->mmap_sem);

err_map:
	mlx5_core_dealloc_memic(to_mdev(ibdev)->mdev, memic_addr, act_size);

err_free:
	kfree(dm);

	return ERR_PTR(ret);
}

int mlx5_ib_exp_free_dm(struct ib_dm *ibdm)
{
	struct mlx5_ib_dev *dev = to_mdev(ibdm->device);
	struct mlx5_ib_dm *dm = to_mdm(ibdm);
	int ret;

	if (dm->dm_base_addr)
		iounmap(dm->dm_base_addr);

	ret = mlx5_core_dealloc_memic(dev->mdev, ibdm->dev_addr,
				      roundup(ibdm->length, 64));

	kfree(dm);

	return ret;
}

int mlx5_ib_exp_memcpy_dm(struct ib_dm *ibdm,
			  struct ib_exp_memcpy_dm_attr *attr)
{
	struct mlx5_ib_dm *dm = to_mdm(ibdm);
	uint64_t dm_addr = (uint64_t)dm->dm_base_addr + attr->dm_offset;

	if ((attr->dm_offset + attr->length > ibdm->length) || (dm_addr & 0x3))
		return -EINVAL;

	if (attr->memcpy_dir == IBV_EXP_DM_CPY_TO_DEVICE)
		memcpy_toio((void *)dm_addr, attr->host_addr, attr->length);
	else
		memcpy_fromio(attr->host_addr, (void *)dm_addr, attr->length);

	mb();

	return 0;
}

int mlx5_ib_exp_set_context_attr(struct ib_device *device,
				 struct ib_ucontext *context,
				 struct ib_exp_context_attr *attr)
{
	int ret;

	if (attr->comp_mask & IB_UVERBS_EXP_SET_CONTEXT_PEER_INFO) {
		ret = ib_get_peer_private_data(context, attr->peer_id,
					       attr->peer_name);
		if (ret)
			return ret;
	} else {
		return -EINVAL;
	}

	return 0;
}
