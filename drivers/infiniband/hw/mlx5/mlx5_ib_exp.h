/*
 * Copyright (c) 2013-2016, Mellanox Technologies. All rights reserved.
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

#ifndef MLX5_IB_EXP_H
#define MLX5_IB_EXP_H

#include <rdma/ib_verbs.h>

struct mlx5_ib_dev;
struct mlx5_ib_rwq;
struct mlx5_ib_create_wq_data;
struct mlx5_ib_qp;
#define MLX5_DC_CONNECT_QP_DEPTH 8192
#define MLX5_IB_QPT_SW_CNAK	IB_QPT_RESERVED5

enum mlx5_cap_flags {
	MLX5_CAP_COMPACT_AV = 1 << 0,
};

enum {
	MLX5_DCT_CS_RES_64	= 2,
	MLX5_CNAK_RX_POLL_CQ_QUOTA	= 256,
};

struct mlx5_dc_desc {
	dma_addr_t	dma;
	void		*buf;
};

enum mlx5_op {
	MLX5_WR_OP_MLX	= 1,
};

struct mlx5_mlx_wr {
	u8	sl;
	u16	dlid;
	int	icrc;
};

struct mlx5_send_wr {
	struct ib_send_wr	wr;
	union {
		struct mlx5_mlx_wr	mlx;
	} sel;
};

struct mlx5_dc_data {
	struct ib_mr		*mr;
	struct ib_qp		*dcqp;
	struct ib_cq		*rcq;
	struct ib_cq		*scq;
	unsigned int		rx_npages;
	unsigned int		tx_npages;
	struct mlx5_dc_desc	*rxdesc;
	struct mlx5_dc_desc	*txdesc;
	unsigned int		max_wqes;
	unsigned int		cur_send;
	unsigned int		last_send_completed;
	int			tx_pending;
	struct mlx5_ib_dev	*dev;
	int			port;
	int			initialized;
	struct kobject		kobj;
	unsigned long		connects;
	unsigned long		cnaks;
	unsigned long		discards;
	struct ib_wc		wc_tbl[MLX5_CNAK_RX_POLL_CQ_QUOTA];
};

struct mlx5_dc_tracer {
	struct page	*pg;
	dma_addr_t	dma;
	int		size;
	int		order;
};

struct mlx5_ib_dc_target {
	struct ib_dct		ibdct;
	struct mlx5_ib_qp    *qp;
};

struct mlx5_ib_exp_odp_stats {
	/* Debug statistics */
	struct dentry           *odp_debugfs;

	/* Number of ODP MRs currently in use */
	atomic_t                num_odp_mrs;
	/* Total size of ODP MRs in pages */
	atomic_t                num_odp_mr_pages;
	/* Number of instances when the MR couldn't be found during page fault
	 * handling
	 */
	atomic_t                num_mrs_not_found;
	/* Number of instances when the page fault encountered an error */
	atomic_t                num_failed_resolutions;
};

static inline struct mlx5_ib_dc_target *to_mdct(struct ib_dct *ibdct)
{
	return container_of(ibdct, struct mlx5_ib_dc_target, ibdct);
}

struct ib_dct *mlx5_ib_create_dc_target(struct ib_pd *pd,
				  struct ib_dct_init_attr *attr,
				  struct ib_udata *udata);
int mlx5_ib_destroy_dc_target(struct ib_dct *dct);
int mlx5_ib_query_dc_target(struct ib_dct *dct, struct ib_dct_attr *attr);
int mlx5_ib_arm_dc_target(struct ib_dct *dct, struct ib_udata *udata);

int mlx5_ib_exp_modify_cq(struct ib_cq *cq, struct ib_cq_attr *cq_attr,
			  int cq_attr_mask);

int mlx5_ib_exp_query_device(struct ib_device *ibdev,
			     struct ib_exp_device_attr *props,
			     struct ib_udata *uhw);

int mlx5_ib_exp_max_inl_recv(struct ib_qp_init_attr *init_attr);

enum mlx5_ib_exp_mmap_cmd {
	MLX5_IB_MMAP_GET_CONTIGUOUS_PAGES		= 1,
	MLX5_IB_EXP_MMAP_CORE_CLOCK = 0xFB,
	MLX5_IB_EXP_MMAP_GET_CONTIGUOUS_PAGES_CPU_NUMA  = 0xFC,
	MLX5_IB_EXP_MMAP_GET_CONTIGUOUS_PAGES_DEV_NUMA  = 0xFD,
};

int get_pg_order(unsigned long offset);

static inline int is_exp_contig_command(unsigned long command)
{
	if (command == MLX5_IB_MMAP_GET_CONTIGUOUS_PAGES ||
	    command == MLX5_IB_EXP_MMAP_GET_CONTIGUOUS_PAGES_CPU_NUMA ||
	    command == MLX5_IB_EXP_MMAP_GET_CONTIGUOUS_PAGES_DEV_NUMA)
		return 1;

	return 0;
}

int mlx5_ib_exp_contig_mmap(struct ib_ucontext *ibcontext,
			    struct vm_area_struct *vma,
			    unsigned long  command);
struct ib_mr *mlx5_ib_phys_addr(struct ib_pd *pd, u64 length, u64 virt_addr,
				int access_flags);
int mlx5_ib_mmap_dc_info_page(struct mlx5_ib_dev *dev,
			      struct vm_area_struct *vma);
int mlx5_ib_init_dc_improvements(struct mlx5_ib_dev *dev);
void mlx5_ib_cleanup_dc_improvements(struct mlx5_ib_dev *dev);

void mlx5_ib_set_mlx_seg(struct mlx5_mlx_seg *seg, struct mlx5_mlx_wr *wr);

#ifdef CONFIG_INFINIBAND_ON_DEMAND_PAGING
int mlx5_ib_prefetch_mr(struct ib_mr *ibmr, u64 start, u64 length, u32 flags);
#endif

#ifdef CONFIG_INFINIBAND_ON_DEMAND_PAGING
struct mlx5_ib_dev;
int mlx5_ib_exp_odp_init_one(struct mlx5_ib_dev *ibdev);
#endif

void mlx5_ib_get_atomic_caps(struct mlx5_ib_dev *dev,
			     struct ib_device_attr *props,
			     int is_exp);
void mlx5_ib_config_atomic_responder(struct mlx5_ib_dev *dev,
				     struct ib_exp_device_attr *props);
u32 mlx5_ib_atomic_mode_qp(struct mlx5_ib_qp *qp);

int mlx5_ib_exp_query_mkey(struct ib_mr *mr, u64 mkey_attr_mask,
			   struct ib_mkey_attr *mkey_attr);
#endif
