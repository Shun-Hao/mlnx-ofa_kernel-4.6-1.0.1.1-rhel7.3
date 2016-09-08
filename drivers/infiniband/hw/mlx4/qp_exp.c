/*
 * Copyright (c) 2015 Mellanox Technologies. All rights reserved.
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

#include "mlx4_ib.h"
#include <linux/mlx4/qp.h>

void mlx4_ib_set_exp_attr_flags(struct mlx4_ib_qp *qp, struct ib_qp_init_attr *init_attr)
{
	if (qp->flags & MLX4_IB_QP_CROSS_CHANNEL)
		init_attr->create_flags |= IB_QP_CREATE_CROSS_CHANNEL;

	if (qp->flags & MLX4_IB_QP_MANAGED_SEND)
		init_attr->create_flags |= IB_QP_CREATE_MANAGED_SEND;

	if (qp->flags & MLX4_IB_QP_MANAGED_RECV)
		init_attr->create_flags |= IB_QP_CREATE_MANAGED_RECV;
}

void mlx4_ib_set_exp_qp_flags(struct mlx4_ib_qp *qp, struct ib_qp_init_attr *init_attr)
{
	if (init_attr->create_flags & IB_QP_CREATE_CROSS_CHANNEL)
		qp->flags |= MLX4_IB_QP_CROSS_CHANNEL;

	if (init_attr->create_flags & IB_QP_CREATE_MANAGED_SEND)
		qp->flags |= MLX4_IB_QP_MANAGED_SEND;

	if (init_attr->create_flags & IB_QP_CREATE_MANAGED_RECV)
		qp->flags |= MLX4_IB_QP_MANAGED_RECV;
}

struct ib_qp *mlx4_ib_exp_create_qp(struct ib_pd *pd,
				    struct ib_exp_qp_init_attr *init_attr,
				    struct ib_udata *udata)
{
	struct ib_qp *qp;
	struct ib_device *device;

	device = pd ? pd->device : init_attr->xrcd->device;
	if ((init_attr->create_flags &
		(MLX4_IB_QP_CROSS_CHANNEL |
		 MLX4_IB_QP_MANAGED_SEND |
		 MLX4_IB_QP_MANAGED_RECV)) &&
	     !(to_mdev(device)->dev->caps.flags &
		MLX4_DEV_CAP_FLAG_CROSS_CHANNEL)) {
		pr_debug("%s Does not support cross-channel operations\n",
			 to_mdev(device)->ib_dev.name);
		return ERR_PTR(-EINVAL);
	}

	qp = mlx4_ib_create_qp(pd, (struct ib_qp_init_attr *)init_attr, udata);

	return qp;
}

