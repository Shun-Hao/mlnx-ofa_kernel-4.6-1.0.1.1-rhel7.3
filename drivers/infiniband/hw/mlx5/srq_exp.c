/*
 * Copyright (c) 2017 Mellanox Technologies. All rights reserved.
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

#include <linux/mlx5/qp.h>
#include <linux/mlx5/qp_exp.h>
#include <rdma/ib_verbs_exp.h>

#include "mlx5_ib.h"
#include "user_exp.h"

int mlx5_ib_exp_create_srq_user(struct mlx5_ib_dev *dev,
				struct mlx5_srq_attr *in,
				struct ib_udata *udata,
				struct mlx5_ib_create_srq *ucmd)
{
	struct mlx5_ib_exp_create_srq ucmd_exp = {};
	size_t ucmdlen;

	ucmdlen = min(udata->inlen, sizeof(ucmd_exp));
	if (ib_copy_from_udata(&ucmd_exp, udata, ucmdlen)) {
		mlx5_ib_dbg(dev, "failed copy udata\n");
		return -EFAULT;
	}

	if (ucmd_exp.reserved0 || ucmd_exp.reserved1 || ucmd_exp.comp_mask)
		return -EINVAL;

	if (in->type == IB_EXP_SRQT_TAG_MATCHING) {
		if (!ucmd_exp.max_num_tags)
			return -EINVAL;
		in->tm_log_list_size = ilog2(ucmd_exp.max_num_tags) + 1;
		if (in->tm_log_list_size >
		    MLX5_CAP_GEN(dev->mdev, log_tag_matching_list_sz)) {
			mlx5_ib_dbg(dev, "TM SRQ max_num_tags exceeding limit\n");
			return -EINVAL;
		}
		in->flags |= MLX5_SRQ_FLAG_RNDV;
	}

	ucmdlen = offsetof(typeof(*ucmd), reserved1) + sizeof(ucmd->reserved1);
	ucmdlen = min(udata->inlen, ucmdlen);
	memcpy(ucmd, &ucmd_exp, ucmdlen);

	return 0;
}

