/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2019 Mellanox Technologies. */

#ifndef __MLX5_ECPF_H__
#define __MLX5_ECPF_H__

#include <linux/mlx5/driver.h>
#include "mlx5_core.h"

#ifdef CONFIG_MLX5_ESWITCH

enum {
	MLX5_ECPU_BIT_NUM = 23,
};

bool mlx5_read_embedded_cpu(struct mlx5_core_dev *dev);
int mlx5_ec_init(struct mlx5_core_dev *dev);
void mlx5_ec_cleanup(struct mlx5_core_dev *dev);
int mlx5_query_host_params_num_vfs(struct mlx5_core_dev *dev, int *num_vf);
int mlx5_query_host_params_total_vfs(struct mlx5_core_dev *dev, int *total_vfs);
void mlx5_smartnic_sysfs_init(struct net_device *dev);
void mlx5_smartnic_sysfs_cleanup(struct net_device *dev);

#else  /* CONFIG_MLX5_ESWITCH */

static inline bool
mlx5_read_embedded_cpu(struct mlx5_core_dev *dev) { return false; }
static inline int mlx5_ec_init(struct mlx5_core_dev *dev) { return 0; }
static inline void mlx5_ec_cleanup(struct mlx5_core_dev *dev) {}
static inline int
mlx5_query_host_params_num_vfs(struct mlx5_core_dev *dev, int *num_vf)
{ return -EOPNOTSUPP; }
static inline int
mlx5_query_host_params_total_vfs(struct mlx5_core_dev *dev, int *total_vfs)
{ return -EOPNOTSUPP; }

#endif /* CONFIG_MLX5_ESWITCH */

#endif /* __MLX5_ECPF_H__ */
