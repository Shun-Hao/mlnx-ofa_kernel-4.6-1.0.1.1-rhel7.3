/*
 * Copyright (c) 2016, Mellanox Technologies. All rights reserved.
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

#ifndef MLX5_IB_USER_EXP_H
#define MLX5_IB_USER_EXP_H

#include <rdma/mlx5-abi.h>

enum mlx5_exp_ib_create_qp_mask {
	MLX5_EXP_CREATE_QP_MASK_UIDX		= 1 << 0,
	MLX5_EXP_CREATE_QP_MASK_RESERVED	= 1 << 1,
};

struct mlx5_exp_ib_create_qp_data {
	__u32   comp_mask; /* use mlx5_exp_ib_create_qp_mask */
	__u32   uidx;
};

struct mlx5_exp_ib_create_qp {
	/* To allow casting to mlx5_ib_create_qp the prefix is the same as
	 * struct mlx5_ib_create_qp prefix
	 */
	__u64	buf_addr;
	__u64	db_addr;
	__u32	sq_wqe_count;
	__u32	rq_wqe_count;
	__u32	rq_wqe_shift;
	__u32	flags;
	__u32	uidx;
	__u32	reserved0;
	__u64	sq_buf_addr;

	/* Some more reserved fields for future growth of mlx5_ib_create_qp */
	__u64   prefix_reserved[6];

	/* sizeof prefix aligned with mlx5_ib_create_qp */
	__u64   size_of_prefix;

	/* Experimental data
	 * Add new experimental data only inside the exp struct
	 */
	struct mlx5_exp_ib_create_qp_data exp;
};

static inline int get_qp_exp_user_index(struct mlx5_ib_ucontext *ucontext,
					struct mlx5_exp_ib_create_qp *ucmd,
					int inlen,
					u32 *user_index)
{
	if (ucmd->exp.comp_mask & MLX5_EXP_CREATE_QP_MASK_UIDX)
		*user_index = ucmd->exp.uidx;
	else
		*user_index = MLX5_IB_DEFAULT_UIDX;

	return 0;
}

#endif /* MLX5_IB_USER_EXP_H */
