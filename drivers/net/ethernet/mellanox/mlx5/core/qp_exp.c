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

#include <linux/mlx5/qp.h>
#include <linux/mlx5/qp_exp.h>
#include <linux/mlx5/driver.h>
#include "mlx5_core.h"

void mlx5_init_dct_table(struct mlx5_core_dev *dev)
{
	mlx5_dct_debugfs_init(dev);
}

void mlx5_cleanup_dct_table(struct mlx5_core_dev *dev)
{
	mlx5_dct_debugfs_cleanup(dev);
}

int mlx5_core_arm_dct_common(struct mlx5_core_dev *dev, struct mlx5_core_dct *dct)
{
	u32 out[MLX5_ST_SZ_DW(arm_dct_out)] = {0};
	u32 in[MLX5_ST_SZ_DW(arm_dct_in)]   = {0};

	MLX5_SET(arm_dct_in, in, opcode, MLX5_CMD_OP_ARM_DCT_FOR_KEY_VIOLATION);
	MLX5_SET(arm_dct_in, in, dct_number, dct->mqp.qpn);
	return mlx5_cmd_exec(dev, (void *)&in, sizeof(in),
			     (void *)&out, sizeof(out));
}

int mlx5_core_arm_lag_dct(struct mlx5_core_dev *dev, struct mlx5_core_dct *dct_array)
{
	int err, i;

	for (i = 0; i < 2; i++) {
		err = mlx5_core_arm_dct_common(dev, &dct_array[i]);
		if (err)
			return err;
	}

	return 0;
}

int mlx5_core_arm_dct(struct mlx5_core_dev *dev, struct mlx5_core_dct *dct)
{
	if (mlx5_lag_is_active(dev)) {
		switch (MLX5_CAP_GEN(dev, lag_dct)) {
		case MLX5_DC_LAG_NOT_SUPPORTED:
			return -EOPNOTSUPP;
		case MLX5_DC_LAG_SW_ASSISTED:
			return mlx5_core_arm_lag_dct(dev, dct);
		case MLX5_DC_LAG_HW_ONLY:
			return mlx5_core_arm_dct_common(dev, &dct[0]);
		default:
			return -EINVAL;
		}
	} else {
		return mlx5_core_arm_dct_common(dev, &dct[0]);
	}
}
EXPORT_SYMBOL_GPL(mlx5_core_arm_dct);
