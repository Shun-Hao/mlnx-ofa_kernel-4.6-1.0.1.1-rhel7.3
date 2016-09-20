#ifndef MLX5_IB_EXP_H
#define MLX5_IB_EXP_H

int mlx5_ib_exp_modify_cq(struct ib_cq *cq, struct ib_cq_attr *cq_attr,
			  int cq_attr_mask);

int mlx5_ib_exp_query_device(struct ib_device *ibdev,
			     struct ib_exp_device_attr *props,
			     struct ib_udata *uhw);

int mlx5_ib_exp_max_inl_recv(struct ib_qp_init_attr *init_attr);

enum mlx5_ib_exp_mmap_cmd {
	MLX5_IB_EXP_MMAP_CORE_CLOCK = 0xFB,
};

#endif
