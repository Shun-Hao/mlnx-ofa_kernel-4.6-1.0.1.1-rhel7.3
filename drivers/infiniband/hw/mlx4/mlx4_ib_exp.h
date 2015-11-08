#ifndef MLX4_IB_EXP_H
#define MLX4_IB_EXP_H

#include <linux/compiler.h>
#include <linux/list.h>
#include <rdma/ib_verbs.h>
#include <rdma/ib_cmem.h>
#include <linux/mlx4/device.h>

#define MLX4_IB_EXP_MMAP_CMD_MASK 0xFF
#define MLX4_IB_EXP_MMAP_CMD_BITS 8

enum mlx4_ib_exp_mmap_cmd {
	MLX4_IB_EXP_MMAP_GET_CONTIGUOUS_PAGES = 2,

	/* Use EXP mmap commands until it is pushed to upstream */
	MLX4_IB_EXP_MMAP_GET_CONTIGUOUS_PAGES_CPU_NUMA	= 0xFC,
	MLX4_IB_EXP_MMAP_GET_CONTIGUOUS_PAGES_DEV_NUMA	= 0xFD,
};

int mlx4_ib_exp_contig_mmap(struct ib_ucontext *context, struct vm_area_struct *vma,
			    unsigned long  command);
static inline int is_exp_contig_command(unsigned long  command)
{
	if (command == MLX4_IB_EXP_MMAP_GET_CONTIGUOUS_PAGES ||
	    command == MLX4_IB_EXP_MMAP_GET_CONTIGUOUS_PAGES_CPU_NUMA ||
	    command == MLX4_IB_EXP_MMAP_GET_CONTIGUOUS_PAGES_DEV_NUMA)
		return 1;

	return 0;
}

int mlx4_ib_exp_modify_cq(struct ib_cq *cq, struct ib_cq_attr *cq_attr, int cq_attr_mask);
#endif
