#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/netdevice.h>
#include <linux/inetdevice.h>

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

