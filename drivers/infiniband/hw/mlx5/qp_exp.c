/*
 * Copyright (c) 2016 Mellanox Technologies. All rights reserved.
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

#include "mlx5_ib.h"
#include <linux/mlx5/qp.h>

int mlx5_ib_exp_max_inl_recv(struct ib_qp_init_attr *init_attr)
{
	return ((struct ib_exp_qp_init_attr *)init_attr)->max_inl_recv;
}

struct ib_qp *mlx5_ib_exp_create_qp(struct ib_pd *pd,
				    struct ib_exp_qp_init_attr *init_attr,
				    struct ib_udata *udata)
{
	int use_inlr;

	use_inlr = (init_attr->qp_type == IB_QPT_RC ||
		    init_attr->qp_type == IB_QPT_UC) &&
		init_attr->max_inl_recv && pd;

	if (use_inlr) {
		int rcqe_sz;
		int scqe_sz;

		rcqe_sz = mlx5_ib_get_cqe_size(init_attr->recv_cq);
		scqe_sz = mlx5_ib_get_cqe_size(init_attr->send_cq);

		if (rcqe_sz == 128)
			init_attr->max_inl_recv = 64;
		else
			init_attr->max_inl_recv = 32;
	} else {
		init_attr->max_inl_recv = 0;
	}

	return _mlx5_ib_create_qp(pd, (struct ib_qp_init_attr *)init_attr,
				  udata, 1);
}
