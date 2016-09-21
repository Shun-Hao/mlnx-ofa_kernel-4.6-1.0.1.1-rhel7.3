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

	props->exp_comp_mask |= IB_EXP_DEVICE_ATTR_EXT_ATOMIC_ARGS;
	props->atomic_arg_sizes = 1 << 3;
	props->max_fa_bit_boudary = 64;
	props->log_max_atomic_inline_arg = 3;
	props->device_cap_flags2 |= IB_EXP_DEVICE_EXT_ATOMICS;
	props->exp_comp_mask |= IB_EXP_DEVICE_ATTR_MAX_CTX_RES_DOMAIN;
	props->max_ctx_res_domain = MLX4_IB_MAX_CTX_UARS * dev->dev->caps.bf_regs_per_page;


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

int mlx4_ib_exp_uar_mmap(struct ib_ucontext *context, struct vm_area_struct *vma,
			    unsigned long  command)
{
	struct mlx4_ib_user_uar *uar;
	unsigned long  parm = vma->vm_pgoff >> MLX4_IB_EXP_MMAP_CMD_BITS;
	struct mlx4_ib_ucontext *mucontext = to_mucontext(context);
	struct mlx4_ib_dev *dev = to_mdev(context->device);
	int err;

	if (vma->vm_end - vma->vm_start != PAGE_SIZE)
		return -EINVAL;

	if (parm >= MLX4_IB_MAX_CTX_UARS)
		return -EINVAL;

	/* We prevent double mmaping on same context */
	list_for_each_entry(uar, &mucontext->user_uar_list, list)
		if (uar->user_idx == parm) {
			return -EINVAL;
		}

	uar = kzalloc(sizeof(*uar), GFP_KERNEL);
	uar->user_idx = parm;

	err = mlx4_uar_alloc(dev->dev, &uar->uar);
	if (err) {
		kfree(uar);
		return -ENOMEM;
	}

	rdma_user_mmap_io(context, vma,
				uar->uar.pfn, PAGE_SIZE,
				pgprot_noncached(vma->vm_page_prot), NULL);
        //We need it as in use in find_user_uar func
	(&uar->hw_bar_info[HW_BAR_DB])->vma = vma;
	
	mutex_lock(&mucontext->user_uar_mutex);
	list_add(&uar->list, &mucontext->user_uar_list);
	mutex_unlock(&mucontext->user_uar_mutex);
	return 0;
}

int mlx4_ib_exp_bf_mmap(struct ib_ucontext *context, struct vm_area_struct *vma,
			    unsigned long  command)
{
	struct mlx4_ib_user_uar *uar;
	unsigned long  parm = vma->vm_pgoff >> MLX4_IB_EXP_MMAP_CMD_BITS;
	struct mlx4_ib_ucontext *mucontext = to_mucontext(context);
	struct mlx4_ib_dev *dev = to_mdev(context->device);

	if (vma->vm_end - vma->vm_start != PAGE_SIZE)
		return -EINVAL;

	if (parm >= MLX4_IB_MAX_CTX_UARS)
		return -EINVAL;

	/*
	 * BlueFlame pages are affiliated with the UAR pages by their
	 * indexes. A QP can only use a BlueFlame page with the index
	 * equal to the QP UAR. Therefore BF may be mapped to user
	 * only after the related UAR is already mapped to the user.
	 */
	uar = NULL;
	list_for_each_entry(uar, &mucontext->user_uar_list, list)
		if (uar->user_idx == parm)
			break;
	if (!uar || uar->user_idx != parm)
		return -EINVAL;
	
	/* We prevent double mmaping on same context */
	if (uar->hw_bar_info[HW_BAR_BF].vma)
		return -EINVAL;

	rdma_user_mmap_io(context, vma, 
				uar->uar.pfn + dev->dev->caps.num_uars, PAGE_SIZE, 
				pgprot_noncached(vma->vm_page_prot), NULL);
	(&uar->hw_bar_info[HW_BAR_BF])->vma = vma;

	return 0;
}
