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

#include "mlx5_ib.h"

int mlx5_ib_exp_query_device(struct ib_device *ibdev,
			     struct ib_exp_device_attr *props,
			     struct ib_udata *uhw)
{
	struct mlx5_ib_dev *dev = to_mdev(ibdev);
	int ret;

	ret = mlx5_ib_query_device(ibdev, &props->base, uhw);
	if (ret)
		return ret;

	props->exp_comp_mask = 0;
	props->device_cap_flags2 = 0;
	props->exp_comp_mask |= IB_EXP_DEVICE_ATTR_CAP_FLAGS2;

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

	return 0;
}
