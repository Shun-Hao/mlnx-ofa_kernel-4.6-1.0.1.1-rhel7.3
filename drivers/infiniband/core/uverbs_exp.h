#ifndef UVERBS_EXP_H
#define UVERBS_EXP_H

#include <linux/kref.h>
#include <linux/idr.h>
#include <linux/mutex.h>
#include <linux/completion.h>
#include <linux/cdev.h>

#include <rdma/ib_verbs.h>
#include <rdma/ib_umem.h>
#include <rdma/ib_user_verbs.h>
#include <rdma/ib_user_verbs_exp.h>

typedef int (*uverbs_ex_cmd)(struct ib_uverbs_file *file,
					struct ib_udata *ucore,
					struct ib_udata *uhw);

#define IB_UVERBS_DECLARE_EXP_CMD(name)				\
	int ib_uverbs_exp_##name(struct ib_uverbs_file *file,	\
				 struct ib_udata *ucore,	\
				 struct ib_udata *uhw)

IB_UVERBS_DECLARE_EXP_CMD(create_qp);

uverbs_ex_cmd uverbs_exp_cmd_table[] = {
	[IB_USER_VERBS_EXP_CMD_CREATE_QP]	= ib_uverbs_exp_create_qp,
};

unsigned long ib_uverbs_exp_get_unmapped_area(struct file *filp,
					      unsigned long addr,
					      unsigned long len, unsigned long pgoff,
					      unsigned long flags);
#endif
