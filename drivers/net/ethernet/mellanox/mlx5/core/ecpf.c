/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2019 Mellanox Technologies. */

#include "ecpf.h"
#include <linux/mlx5/driver.h>
#include "mlx5_core.h"
#include "eswitch.h"
#include "en.h"

bool mlx5_read_embedded_cpu(struct mlx5_core_dev *dev)
{
	return (ioread32be(&dev->iseg->initializing) >> MLX5_ECPU_BIT_NUM) & 1;
}

static int mlx5_peer_pf_enable_hca(struct mlx5_core_dev *dev)
{
	u32 out[MLX5_ST_SZ_DW(enable_hca_out)] = {};
	u32 in[MLX5_ST_SZ_DW(enable_hca_in)]   = {};

	MLX5_SET(enable_hca_in, in, opcode, MLX5_CMD_OP_ENABLE_HCA);
	MLX5_SET(enable_hca_in, in, function_id, 0);
	MLX5_SET(enable_hca_in, in, embedded_cpu_function, 0);
	return mlx5_cmd_exec(dev, &in, sizeof(in), &out, sizeof(out));
}

static int mlx5_peer_pf_disable_hca(struct mlx5_core_dev *dev)
{
	u32 out[MLX5_ST_SZ_DW(disable_hca_out)] = {};
	u32 in[MLX5_ST_SZ_DW(disable_hca_in)]   = {};

	MLX5_SET(disable_hca_in, in, opcode, MLX5_CMD_OP_DISABLE_HCA);
	MLX5_SET(disable_hca_in, in, function_id, 0);
	MLX5_SET(enable_hca_in, in, embedded_cpu_function, 0);
	return mlx5_cmd_exec(dev, in, sizeof(in), out, sizeof(out));
}

static int mlx5_peer_pf_init(struct mlx5_core_dev *dev)
{
	int err;

	err = mlx5_peer_pf_enable_hca(dev);
	if (err)
		mlx5_core_err(dev, "Failed to enable peer PF HCA err(%d)\n",
			      err);

	return err;
}

static void mlx5_peer_pf_cleanup(struct mlx5_core_dev *dev)
{
	int err;

	err = mlx5_peer_pf_disable_hca(dev);
	if (err) {
		mlx5_core_err(dev, "Failed to disable peer PF HCA err(%d)\n",
			      err);
		return;
	}

	err = mlx5_wait_for_pages(dev, &dev->priv.peer_pf_pages);
	if (err)
		mlx5_core_warn(dev, "Timeout reclaiming peer PF pages err(%d)\n",
			       err);
}

int mlx5_ec_init(struct mlx5_core_dev *dev)
{
	int err = 0;

	if (!mlx5_core_is_ecpf(dev))
		return 0;

	/* ECPF shall enable HCA for peer PF in the same way a PF
	 * does this for its VFs.
	 */
	err = mlx5_peer_pf_init(dev);
	if (err)
		return err;

	return 0;
}

void mlx5_ec_cleanup(struct mlx5_core_dev *dev)
{
	if (!mlx5_core_is_ecpf(dev))
		return;

	mlx5_peer_pf_cleanup(dev);
}

static ssize_t max_tx_rate_store(struct kobject *kobj,
				 struct kobj_attribute *attr,
				 const char *buf,
				 size_t count)
{
	struct mlx5_smart_nic_vport *tmp =
		container_of(kobj, struct mlx5_smart_nic_vport, kobj);
	struct mlx5_eswitch *esw = tmp->esw;
	u32 max_tx_rate;
	u32 min_tx_rate;
	int err;

	mutex_lock(&esw->state_lock);
	min_tx_rate = esw->vports[0].info.min_rate;
	mutex_unlock(&esw->state_lock);

	err = kstrtou32(buf, 0, &max_tx_rate);
	if (err)
		return err;

	err = mlx5_eswitch_set_vport_rate(esw, tmp->vport,
					  max_tx_rate, min_tx_rate);

	return err ? err : count;
}

static ssize_t max_tx_rate_show(struct kobject *kobj,
				struct kobj_attribute *attr,
				char *buf)
{
	return sprintf(buf,
		       "usage: write <Rate (Mbit/s)> to set max transmit rate\n");
}

static int mlx5_query_host_params_context(struct mlx5_core_dev *dev,
					  u32 *out, int outlen)
{
	u32 in[MLX5_ST_SZ_DW(query_host_params_in)] = {};

	MLX5_SET(query_host_params_in, in, opcode,
		 MLX5_CMD_OP_QUERY_HOST_PARAMS);

	return mlx5_cmd_exec(dev, in, sizeof(in), out, outlen);
}

static ssize_t config_show(struct kobject *kobj,
			   struct kobj_attribute *attr,
			   char *buf)
{
	struct mlx5_smart_nic_vport *tmp =
		container_of(kobj, struct mlx5_smart_nic_vport, kobj);
	struct mlx5_eswitch *esw = tmp->esw;
	struct mlx5_vport_info *ivi;
	int vport = tmp->vport;
	char *p = buf;

	mutex_lock(&esw->state_lock);
	ivi = &esw->vports[vport].info;
	p += sprintf(p, buf, "MAC        : %pM\n", ivi->mac);
	p += sprintf(p, buf, "MaxTxRate  : %d\n", ivi->max_rate);
	mutex_unlock(&esw->state_lock);

	return (ssize_t)(p - buf);
}

static struct kobj_attribute attr_max_tx_rate = {
	.attr = {.name = "max_tx_rate",
		 .mode = 0644 },
	.show = max_tx_rate_show,
	.store = max_tx_rate_store,
};

static struct kobj_attribute attr_config = {
	.attr = {.name = "config",
		 .mode = 0444 },
	.show = config_show,
};

static struct attribute *smart_nic_attrs[] = {
	&attr_config.attr,
	&attr_max_tx_rate.attr,
	NULL,
};

static struct kobj_type smart_nic_type = {
	.sysfs_ops     = &kobj_sysfs_ops,
	.default_attrs = smart_nic_attrs
};

int mlx5_query_host_params_num_vfs(struct mlx5_core_dev *dev, int *num_vf)
{
	u32 out[MLX5_ST_SZ_DW(query_host_params_out)] = {};
	int err;

	err = mlx5_query_host_params_context(dev, out, sizeof(out));
	if (err)
		return err;

	*num_vf = MLX5_GET(query_host_params_out, out,
			   host_params_context.host_num_of_vfs);
	mlx5_core_dbg(dev, "host_num_of_vfs %d\n", *num_vf);

	return 0;
}

void mlx5_smartnic_sysfs_init(struct net_device *dev)
{
	struct mlx5e_priv *priv = netdev_priv(dev);
	struct mlx5_core_dev *mdev = priv->mdev;
	struct mlx5_smart_nic_vport *tmp;
	struct mlx5_eswitch *esw;
	int err;

	if (!mlx5_core_is_ecpf(mdev))
		return;

	esw = mdev->priv.eswitch;
	esw->smart_nic_sysfs.kobj =
		kobject_create_and_add("smart_nic", &dev->dev.kobj);
	if (!esw->smart_nic_sysfs.kobj)
		return;

	/* Only PF setting for now */
	esw->smart_nic_sysfs.vport =
		kcalloc(1, sizeof(struct mlx5_smart_nic_vport), GFP_KERNEL);
	if (!esw->smart_nic_sysfs.vport)
		goto err_attr_mem;

	/* PF setting */
	tmp = &esw->smart_nic_sysfs.vport[0];
	tmp->esw = esw;
	tmp->vport = 0;
	err = kobject_init_and_add(&tmp->kobj, &smart_nic_type,
				   esw->smart_nic_sysfs.kobj, "%s", "pf");
	if (err)
		goto err_attr_pf;

	return;

err_attr_pf:
	kfree(esw->smart_nic_sysfs.vport);
	esw->smart_nic_sysfs.vport = NULL;

err_attr_mem:
	kobject_put(esw->smart_nic_sysfs.kobj);
	esw->smart_nic_sysfs.kobj = NULL;
}

void mlx5_smartnic_sysfs_cleanup(struct net_device *dev)
{
	struct mlx5e_priv *priv = netdev_priv(dev);
	struct mlx5_core_dev *mdev = priv->mdev;
	struct mlx5_smart_nic_vport *tmp;
	struct mlx5_eswitch *esw;

	if (!mlx5_core_is_ecpf(mdev))
		return;

	esw = mdev->priv.eswitch;

	if (!esw->smart_nic_sysfs.kobj || !esw->smart_nic_sysfs.vport)
		return;

	tmp = &esw->smart_nic_sysfs.vport[0];
	kobject_put(&tmp->kobj);

	kfree(esw->smart_nic_sysfs.vport);
	esw->smart_nic_sysfs.vport = NULL;

	kobject_put(esw->smart_nic_sysfs.kobj);
	esw->smart_nic_sysfs.kobj = NULL;
}
