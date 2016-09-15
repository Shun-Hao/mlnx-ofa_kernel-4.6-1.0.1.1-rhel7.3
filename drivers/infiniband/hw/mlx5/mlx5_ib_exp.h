#ifndef MLX5_IB_EXP_H
#define MLX5_IB_EXP_H

int mlx5_ib_exp_modify_cq(struct ib_cq *cq, struct ib_cq_attr *cq_attr,
			  int cq_attr_mask);

#endif
