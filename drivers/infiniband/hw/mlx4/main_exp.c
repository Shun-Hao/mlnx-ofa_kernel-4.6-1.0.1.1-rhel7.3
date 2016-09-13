#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/netdevice.h>
#include <linux/inetdevice.h>
#include <linux/mlx4/qp.h>

#include "mlx4_ib.h"

int mlx4_ib_exp_contig_mmap(struct ib_ucontext *context, struct vm_area_struct *vma,
			    unsigned long  command)
{
	int err;
	struct mlx4_ib_dev *dev = to_mdev(context->device);
	/* Getting contiguous physical pages */
	unsigned long total_size = vma->vm_end - vma->vm_start;
	unsigned long page_size_order = (vma->vm_pgoff) >>
					MLX4_IB_EXP_MMAP_CMD_BITS;
	struct ib_cmem *ib_cmem;
	int numa_node;

	if (command == MLX4_IB_EXP_MMAP_GET_CONTIGUOUS_PAGES_CPU_NUMA)
		numa_node = numa_node_id();
	else if (command == MLX4_IB_EXP_MMAP_GET_CONTIGUOUS_PAGES_DEV_NUMA)
		numa_node = dev_to_node(&dev->dev->persist->pdev->dev);
	else
		numa_node = -1;

	ib_cmem = ib_cmem_alloc_contiguous_pages(context, total_size,
						 page_size_order,
						 numa_node);
	if (IS_ERR(ib_cmem)) {
		err = PTR_ERR(ib_cmem);
		return err;
	}

	err = ib_cmem_map_contiguous_pages_to_vma(ib_cmem, vma);
	if (err) {
		ib_cmem_release_contiguous_pages(ib_cmem);
		return err;
	}
	return 0;
}

int mlx4_ib_exp_query_device(struct ib_device *ibdev,
			     struct ib_exp_device_attr *props,
			     struct ib_udata *uhw)
{
	int ret;
	struct mlx4_ib_dev *dev = to_mdev(ibdev);

	ret = mlx4_ib_query_device(ibdev, &props->base, uhw);
	if (ret)
		return ret;

	props->exp_comp_mask = IB_EXP_DEVICE_ATTR_INLINE_RECV_SZ;
	props->inline_recv_sz = dev->dev->caps.max_rq_sg * sizeof(struct mlx4_wqe_data_seg);
	props->device_cap_flags2 = 0;
	props->exp_comp_mask |= IB_EXP_DEVICE_ATTR_CAP_FLAGS2;

	if (dev->dev->caps.hca_core_clock > 0)
		props->exp_comp_mask |= IB_EXP_DEVICE_ATTR_WITH_HCA_CORE_CLOCK;
	props->exp_comp_mask |= IB_EXP_DEVICE_ATTR_WITH_TIMESTAMP_MASK;

	props->device_cap_flags2 |= IB_EXP_DEVICE_QPG;
	if (dev->dev->caps.flags2 & MLX4_DEV_CAP_FLAG2_RSS) {
		props->device_cap_flags2 |= IB_EXP_DEVICE_UD_RSS;
		props->exp_comp_mask |= IB_EXP_DEVICE_ATTR_RSS_TBL_SZ;
		props->max_rss_tbl_sz = dev->dev->caps.max_rss_tbl_sz;
	}

	return 0;
}

int mlx4_ib_exp_ioctl(struct ib_ucontext *context, unsigned int cmd,
		      unsigned long arg)
{
	struct mlx4_ib_dev *dev = to_mdev(context->device);
	int ret;

	switch (cmd) {
	case MLX4_IOCHWCLOCKOFFSET: {
		struct mlx4_clock_params params;

		ret = mlx4_get_internal_clock_params(dev->dev, &params);
		if (!ret)
			return __put_user(params.offset % PAGE_SIZE,
					  (int *)arg);
		else
			return ret;
	}
	default:
		pr_err("mlx4_ib: invalid ioctl %u command with arg %lX\n",
		       cmd, arg);
		ret = -EINVAL;
	}

	return ret;
}

