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

struct mlx5_ib_dev;
enum {
	MLX5_DCT_CS_RES_64	= 2,
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
void mlx5_ib_disable_dc_tracer(struct mlx5_ib_dev *dev);
void mlx5_ib_enable_dc_tracer(struct mlx5_ib_dev *dev);

#endif
