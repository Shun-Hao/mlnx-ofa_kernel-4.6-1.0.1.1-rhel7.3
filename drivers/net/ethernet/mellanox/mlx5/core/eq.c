/*
 * Copyright (c) 2013-2015, Mellanox Technologies. All rights reserved.
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

#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/mlx5/driver.h>
#include <linux/mlx5/cmd.h>
#include <linux/mlx5/qp.h>
#ifdef CONFIG_RFS_ACCEL
#include <linux/cpu_rmap.h>
#endif
#include "mlx5_core.h"
#include "fpga/core.h"
#include "eswitch.h"
#include "lib/clock.h"
#include "diag/fw_tracer.h"

enum {
	MLX5_EQE_SIZE		= sizeof(struct mlx5_eqe),
	MLX5_EQE_OWNER_INIT_VAL	= 0x1,
};

enum {
	MLX5_EQ_STATE_ARMED		= 0x9,
	MLX5_EQ_STATE_FIRED		= 0xa,
	MLX5_EQ_STATE_ALWAYS_ARMED	= 0xb,
};

enum {
	MLX5_NUM_SPARE_EQE	= 0x80,
	MLX5_NUM_ASYNC_EQE	= 0x1000,
	MLX5_NUM_CMD_EQE	= 32,
	MLX5_NUM_PF_DRAIN	= 64,
};

enum {
	MLX5_EQ_DOORBEL_OFFSET	= 0x40,
};

enum {
	MLX5_PCI_POWER_COULD_NOT_BE_READ = 0x0,
	MLX5_PCI_POWER_SUFFICIENT_REPORTED = 0x1,
	MLX5_PCI_POWER_INSUFFICIENT_REPORTED = 0x2,
};

#define MLX5_ASYNC_EVENT_MASK ((1ull << MLX5_EVENT_TYPE_PATH_MIG)	    | \
			       (1ull << MLX5_EVENT_TYPE_COMM_EST)	    | \
			       (1ull << MLX5_EVENT_TYPE_SQ_DRAINED)	    | \
			       (1ull << MLX5_EVENT_TYPE_CQ_ERROR)	    | \
			       (1ull << MLX5_EVENT_TYPE_WQ_CATAS_ERROR)	    | \
			       (1ull << MLX5_EVENT_TYPE_PATH_MIG_FAILED)    | \
			       (1ull << MLX5_EVENT_TYPE_WQ_INVAL_REQ_ERROR) | \
			       (1ull << MLX5_EVENT_TYPE_WQ_ACCESS_ERROR)    | \
			       (1ull << MLX5_EVENT_TYPE_PORT_CHANGE)	    | \
			       (1ull << MLX5_EVENT_TYPE_SRQ_CATAS_ERROR)    | \
			       (1ull << MLX5_EVENT_TYPE_SRQ_LAST_WQE)	    | \
			       (1ull << MLX5_EVENT_TYPE_SRQ_RQ_LIMIT))

struct map_eq_in {
	u64	mask;
	u32	reserved;
	u32	unmap_eqn;
};

struct cre_des_eq {
	u8	reserved[15];
	u8	eqn;
};

static int mlx5_cmd_destroy_eq(struct mlx5_core_dev *dev, u8 eqn)
{
	u32 out[MLX5_ST_SZ_DW(destroy_eq_out)] = {0};
	u32 in[MLX5_ST_SZ_DW(destroy_eq_in)]   = {0};

	MLX5_SET(destroy_eq_in, in, opcode, MLX5_CMD_OP_DESTROY_EQ);
	MLX5_SET(destroy_eq_in, in, eq_number, eqn);
	return mlx5_cmd_exec(dev, in, sizeof(in), out, sizeof(out));
}

static struct mlx5_eqe *get_eqe(struct mlx5_eq *eq, u32 entry)
{
	return mlx5_buf_offset(&eq->buf, entry * MLX5_EQE_SIZE);
}

static struct mlx5_eqe *next_eqe_sw(struct mlx5_eq *eq)
{
	struct mlx5_eqe *eqe = get_eqe(eq, eq->cons_index & (eq->nent - 1));

	return ((eqe->owner & 1) ^ !!(eq->cons_index & eq->nent)) ? NULL : eqe;
}

static const char *eqe_type_str(u8 type)
{
	switch (type) {
	case MLX5_EVENT_TYPE_COMP:
		return "MLX5_EVENT_TYPE_COMP";
	case MLX5_EVENT_TYPE_PATH_MIG:
		return "MLX5_EVENT_TYPE_PATH_MIG";
	case MLX5_EVENT_TYPE_COMM_EST:
		return "MLX5_EVENT_TYPE_COMM_EST";
	case MLX5_EVENT_TYPE_SQ_DRAINED:
		return "MLX5_EVENT_TYPE_SQ_DRAINED";
	case MLX5_EVENT_TYPE_SRQ_LAST_WQE:
		return "MLX5_EVENT_TYPE_SRQ_LAST_WQE";
	case MLX5_EVENT_TYPE_SRQ_RQ_LIMIT:
		return "MLX5_EVENT_TYPE_SRQ_RQ_LIMIT";
	case MLX5_EVENT_TYPE_CQ_ERROR:
		return "MLX5_EVENT_TYPE_CQ_ERROR";
	case MLX5_EVENT_TYPE_WQ_CATAS_ERROR:
		return "MLX5_EVENT_TYPE_WQ_CATAS_ERROR";
	case MLX5_EVENT_TYPE_PATH_MIG_FAILED:
		return "MLX5_EVENT_TYPE_PATH_MIG_FAILED";
	case MLX5_EVENT_TYPE_WQ_INVAL_REQ_ERROR:
		return "MLX5_EVENT_TYPE_WQ_INVAL_REQ_ERROR";
	case MLX5_EVENT_TYPE_WQ_ACCESS_ERROR:
		return "MLX5_EVENT_TYPE_WQ_ACCESS_ERROR";
	case MLX5_EVENT_TYPE_SRQ_CATAS_ERROR:
		return "MLX5_EVENT_TYPE_SRQ_CATAS_ERROR";
	case MLX5_EVENT_TYPE_INTERNAL_ERROR:
		return "MLX5_EVENT_TYPE_INTERNAL_ERROR";
	case MLX5_EVENT_TYPE_PORT_CHANGE:
		return "MLX5_EVENT_TYPE_PORT_CHANGE";
	case MLX5_EVENT_TYPE_GPIO_EVENT:
		return "MLX5_EVENT_TYPE_GPIO_EVENT";
	case MLX5_EVENT_TYPE_PORT_MODULE_EVENT:
		return "MLX5_EVENT_TYPE_PORT_MODULE_EVENT";
	case MLX5_EVENT_TYPE_TEMP_WARN_EVENT:
		return "MLX5_EVENT_TYPE_TEMP_WARN_EVENT";
	case MLX5_EVENT_TYPE_REMOTE_CONFIG:
		return "MLX5_EVENT_TYPE_REMOTE_CONFIG";
	case MLX5_EVENT_TYPE_DB_BF_CONGESTION:
		return "MLX5_EVENT_TYPE_DB_BF_CONGESTION";
	case MLX5_EVENT_TYPE_STALL_EVENT:
		return "MLX5_EVENT_TYPE_STALL_EVENT";
	case MLX5_EVENT_TYPE_CMD:
		return "MLX5_EVENT_TYPE_CMD";
	case MLX5_EVENT_TYPE_HOST_PARAMS_CHANGE:
		return "MLX5_EVENT_TYPE_HOST_PARAMS_CHANGE";
	case MLX5_EVENT_TYPE_PAGE_REQUEST:
		return "MLX5_EVENT_TYPE_PAGE_REQUEST";
	case MLX5_EVENT_TYPE_PAGE_FAULT:
		return "MLX5_EVENT_TYPE_PAGE_FAULT";
	case MLX5_EVENT_TYPE_PPS_EVENT:
		return "MLX5_EVENT_TYPE_PPS_EVENT";
	case MLX5_EVENT_TYPE_NIC_VPORT_CHANGE:
		return "MLX5_EVENT_TYPE_NIC_VPORT_CHANGE";
	case MLX5_EVENT_TYPE_FPGA_ERROR:
		return "MLX5_EVENT_TYPE_FPGA_ERROR";
	case MLX5_EVENT_TYPE_FPGA_QP_ERROR:
		return "MLX5_EVENT_TYPE_FPGA_QP_ERROR";
	case MLX5_EVENT_TYPE_GENERAL_EVENT:
		return "MLX5_EVENT_TYPE_GENERAL_EVENT";
	case MLX5_EVENT_TYPE_DEVICE_TRACER:
		return "MLX5_EVENT_TYPE_DEVICE_TRACER";
	case MLX5_EVENT_TYPE_DCT_DRAINED:
		return "MLX5_EVENT_TYPE_DCT_DRAINED";
	case MLX5_EVENT_TYPE_DCT_KEY_VIOLATION:
		return "MLX5_EVENT_TYPE_DCT_KEY_VIOLATION";
	case MLX5_EVENT_TYPE_XRQ_ERROR:
		return "MLX5_EVENT_TYPE_XRQ_ERROR";
	default:
		return "Unrecognized event";
	}
}

static enum mlx5_dev_event port_subtype_event(u8 subtype)
{
	switch (subtype) {
	case MLX5_PORT_CHANGE_SUBTYPE_DOWN:
		return MLX5_DEV_EVENT_PORT_DOWN;
	case MLX5_PORT_CHANGE_SUBTYPE_ACTIVE:
		return MLX5_DEV_EVENT_PORT_UP;
	case MLX5_PORT_CHANGE_SUBTYPE_INITIALIZED:
		return MLX5_DEV_EVENT_PORT_INITIALIZED;
	case MLX5_PORT_CHANGE_SUBTYPE_LID:
		return MLX5_DEV_EVENT_LID_CHANGE;
	case MLX5_PORT_CHANGE_SUBTYPE_PKEY:
		return MLX5_DEV_EVENT_PKEY_CHANGE;
	case MLX5_PORT_CHANGE_SUBTYPE_GUID:
		return MLX5_DEV_EVENT_GUID_CHANGE;
	case MLX5_PORT_CHANGE_SUBTYPE_CLIENT_REREG:
		return MLX5_DEV_EVENT_CLIENT_REREG;
	}
	return -1;
}

static void eq_update_ci(struct mlx5_eq *eq, int arm)
{
	__be32 __iomem *addr = eq->doorbell + (arm ? 0 : 2);
	u32 val = (eq->cons_index & 0xffffff) | (eq->eqn << 24);

	__raw_writel((__force u32)cpu_to_be32(val), addr);
	/* We still want ordering, just not swabbing, so add a barrier */
	mb();
}

#ifdef CONFIG_INFINIBAND_ON_DEMAND_PAGING
static void eqe_pf_action(struct work_struct *work)
{
	struct mlx5_pagefault *pfault = container_of(work,
						     struct mlx5_pagefault,
						     work);
	struct mlx5_eq *eq = pfault->eq;

	mlx5_core_page_fault(eq->dev, pfault);
}

static void eq_pf_process(struct mlx5_eq *eq)
{
	struct mlx5_core_dev *dev = eq->dev;
	struct mlx5_eqe_page_fault *pf_eqe;
	struct mlx5_pagefault *pfault;
	struct mlx5_eqe *eqe;
	int set_ci = 0;

	while ((eqe = next_eqe_sw(eq))) {
		pfault = mempool_alloc(eq->pf_ctx.pool, GFP_ATOMIC);
		if (!pfault) {
			schedule_work(&eq->pf_ctx.work);
			break;
		}

#ifdef dma_rmb
		dma_rmb();
#else
		rmb();
#endif
		pf_eqe = &eqe->data.page_fault;
		pfault->event_subtype = eqe->sub_type;
		pfault->bytes_committed = be32_to_cpu(pf_eqe->bytes_committed);
		pfault->wqe.common = NULL;

		pfault->start = jiffies;
		mlx5_core_dbg(dev,
			      "PAGE_FAULT: subtype: 0x%02x, bytes_committed: 0x%06x\n",
			      eqe->sub_type, pfault->bytes_committed);

		switch (eqe->sub_type) {
		case MLX5_PFAULT_SUBTYPE_RDMA:
			/* RDMA based event */
			pfault->type =
				(be32_to_cpu(pf_eqe->wqe.pftype_wq) >> 24) & 0xf;
			pfault->token =
				be32_to_cpu(pf_eqe->rdma.pftype_token) &
				MLX5_24BIT_MASK;
			pfault->rdma.r_key =
				be32_to_cpu(pf_eqe->rdma.r_key);
			pfault->rdma.packet_size =
				be16_to_cpu(pf_eqe->rdma.packet_length);
			pfault->rdma.rdma_op_len =
				be32_to_cpu(pf_eqe->rdma.rdma_op_len);
			pfault->rdma.rdma_va =
				be64_to_cpu(pf_eqe->rdma.rdma_va);
			mlx5_core_dbg(dev,
				      "PAGE_FAULT: type:0x%x, token: 0x%06x, r_key: 0x%08x\n",
				      pfault->type, pfault->token,
				      pfault->rdma.r_key);
			mlx5_core_dbg(dev,
				      "PAGE_FAULT: rdma_op_len: 0x%08x, rdma_va: 0x%016llx\n",
				      pfault->rdma.rdma_op_len,
				      pfault->rdma.rdma_va);
			break;

		case MLX5_PFAULT_SUBTYPE_WQE:
			/* WQE based event */
			pfault->type =
				(be32_to_cpu(pf_eqe->wqe.pftype_wq) >> 24) & 0x7;
			pfault->token =
				be32_to_cpu(pf_eqe->wqe.token);
			pfault->wqe.wq_num =
				be32_to_cpu(pf_eqe->wqe.pftype_wq) &
				MLX5_24BIT_MASK;
			pfault->wqe.wqe_index =
				be16_to_cpu(pf_eqe->wqe.wqe_index);
			pfault->wqe.packet_size =
				be16_to_cpu(pf_eqe->wqe.packet_length);
			mlx5_core_dbg(dev,
				      "PAGE_FAULT: type:0x%x, token: 0x%06x, wq_num: 0x%06x, wqe_index: 0x%04x\n",
				      pfault->type, pfault->token,
				      pfault->wqe.wq_num,
				      pfault->wqe.wqe_index);
			pfault->wqe.common = mlx5_core_get_rsc(eq->dev,
							       pfault->wqe.wq_num);

			pfault->wqe.ignore = false;
			if (pfault->wqe.common  &&
			    pfault->wqe.common->res == MLX5_RES_QP) {
				int requestor = pfault->type & MLX5_PFAULT_REQUESTOR;
				struct mlx5_core_qp *mqp = (struct mlx5_core_qp *)pfault->wqe.common;
				unsigned long flags;

				/* after QP is modified to RESET it is possible that new
				 * pagefault event arrives. This implies that the pending pagefault
				 * is no long relevant
				 */
				spin_lock_irqsave(&mqp->common.lock, flags);
				if (requestor) {
					if (mqp->pfault_req)
						mqp->pfault_req->wqe.ignore = true;
					mqp->pfault_req = pfault;
				} else {
					if (mqp->pfault_res)
						mqp->pfault_res->wqe.ignore = true;
					mqp->pfault_res = pfault;
				}
				spin_unlock_irqrestore(&mqp->common.lock, flags);
			}
			break;

		default:
			mlx5_core_warn(dev,
				       "Unsupported page fault event sub-type: 0x%02hhx\n",
				       eqe->sub_type);
			/* Unsupported page faults should still be
			 * resolved by the page fault handler
			 */
		}

		pfault->done_cb = NULL;
		pfault->eq = eq;
		INIT_WORK(&pfault->work, eqe_pf_action);
		queue_work(eq->pf_ctx.wq, &pfault->work);

		++eq->cons_index;
		++set_ci;

		if (unlikely(set_ci >= MLX5_NUM_SPARE_EQE)) {
			eq_update_ci(eq, 0);
			set_ci = 0;
		}
	}

	eq_update_ci(eq, 1);
}

static irqreturn_t mlx5_eq_pf_int(int irq, void *eq_ptr)
{
	struct mlx5_eq *eq = eq_ptr;
	unsigned long flags;

	if (spin_trylock_irqsave(&eq->pf_ctx.lock, flags)) {
		eq_pf_process(eq);
		spin_unlock_irqrestore(&eq->pf_ctx.lock, flags);
	} else {
		schedule_work(&eq->pf_ctx.work);
	}

	return IRQ_HANDLED;
}

/* mempool_refill() was proposed but unfortunately wasn't accepted
 * http://lkml.iu.edu/hypermail/linux/kernel/1512.1/05073.html
 * Chip workaround.
 */
static void mempool_refill(mempool_t *pool)
{
	while (pool->curr_nr < pool->min_nr)
		mempool_free(mempool_alloc(pool, GFP_KERNEL), pool);
}

static void eq_pf_action(struct work_struct *work)
{
	struct mlx5_eq *eq = container_of(work, struct mlx5_eq, pf_ctx.work);

	mempool_refill(eq->pf_ctx.pool);

	spin_lock_irq(&eq->pf_ctx.lock);
	eq_pf_process(eq);
	spin_unlock_irq(&eq->pf_ctx.lock);
}

static int init_pf_ctx(struct mlx5_eq_pagefault *pf_ctx, const char *name, bool capi_enabled)
{
	spin_lock_init(&pf_ctx->lock);
	INIT_WORK(&pf_ctx->work, eq_pf_action);

	pf_ctx->wq = alloc_workqueue(name,
				     WQ_HIGHPRI | WQ_CPU_INTENSIVE | WQ_UNBOUND,
				     MLX5_NUM_CMD_EQE);

	if (!pf_ctx->wq)
		return -ENOMEM;

	pf_ctx->pool = mempool_create_kmalloc_pool
		(MLX5_NUM_PF_DRAIN, sizeof(struct mlx5_pagefault));
	if (!pf_ctx->pool)
		goto err_wq;

	return 0;
err_wq:
	destroy_workqueue(pf_ctx->wq);
	return -ENOMEM;
}

static void page_fault_resume_callback(int status, void *context)
{
	struct mlx5_pagefault *pfault = context;

	if (pfault->done_cb)
		pfault->done_cb(pfault, pfault->done_context);

	if (status)
		mlx5_core_err(pfault->eq->dev, "Resolve the page fault failed with status %d\n",
			      status);
	if (pfault->event_subtype == MLX5_PFAULT_SUBTYPE_WQE &&
	    pfault->wqe.common)
		mlx5_core_put_rsc(pfault->wqe.common);

	mempool_free(pfault, pfault->eq->pf_ctx.pool);
}

int mlx5_core_page_fault_resume(struct mlx5_core_dev *dev,
				struct mlx5_pagefault *pfault,
				bool drop,
				int error)
{
	int ret;

	if (likely(!drop)) {
		u32 *out = pfault->out_pf_resume;
		u32 *in = pfault->in_pf_resume;
		u32 token = pfault->token;
		int wq_num = pfault->event_subtype == MLX5_PFAULT_SUBTYPE_WQE ?
			pfault->wqe.wq_num : pfault->token;
		u8 type = pfault->type;

		memset(out, 0, MLX5_ST_SZ_BYTES(page_fault_resume_out));
		memset(in, 0, MLX5_ST_SZ_BYTES(page_fault_resume_in));

		MLX5_SET(page_fault_resume_in, in, opcode,
			 MLX5_CMD_OP_PAGE_FAULT_RESUME);
		MLX5_SET(page_fault_resume_in, in, error, !!error);
		MLX5_SET(page_fault_resume_in, in, page_fault_type, type);
		MLX5_SET(page_fault_resume_in, in, wq_number, wq_num);
		MLX5_SET(page_fault_resume_in, in, token, token);

		ret = mlx5_cmd_exec_cb_legacy(dev,
				       in, MLX5_ST_SZ_BYTES(page_fault_resume_in),
				       out, MLX5_ST_SZ_BYTES(page_fault_resume_out),
				       page_fault_resume_callback, pfault);
	} else {
		page_fault_resume_callback(0, pfault);
		ret = 0;
	}

	return ret;
}
EXPORT_SYMBOL_GPL(mlx5_core_page_fault_resume);
#endif /* CONFIG_INFINIBAND_ON_DEMAND_PAGING */

void mlx5_pcie_event_work(struct work_struct *work)
{
	u32 out[MLX5_ST_SZ_DW(mpein_reg)] = {0};
	u32 in[MLX5_ST_SZ_DW(mpein_reg)] = {0};
	struct mlx5_pcie_power_state *pcie;
	struct mlx5_core_dev *dev;
	struct mlx5_priv *priv;
	u8 power_status;
	u16 pci_power;

	pcie = container_of(work, struct mlx5_pcie_power_state, work);
	priv = container_of(pcie, struct mlx5_priv, pcie_power);
	dev  = container_of(priv, struct mlx5_core_dev, priv);

	if (!MLX5_CAP_MCAM_FEATURE(dev, pci_status_and_power))
		return;
	if (!printk_ratelimit())
		return;

	mlx5_core_access_reg(dev, in, sizeof(in), out, sizeof(out),
			     MLX5_REG_MPEIN, 0, 0);
	power_status = MLX5_GET(mpein_reg, out, pwr_status);
	pci_power = MLX5_GET(mpein_reg, out, pci_power);

	switch (power_status) {
	case MLX5_PCI_POWER_COULD_NOT_BE_READ:
		mlx5_core_info(dev,
			       "PCIe slot power capability was not advertised.\n");
		break;
	case MLX5_PCI_POWER_INSUFFICIENT_REPORTED:
		mlx5_core_warn(dev,
			       "Detected insufficient power on the PCIe slot (%uW).\n",
			       pci_power);
		break;
	case MLX5_PCI_POWER_SUFFICIENT_REPORTED:
		mlx5_core_info(dev,
			       "PCIe slot advertised sufficient power (%uW).\n",
			       pci_power);
		break;
	}
}

void mlx5_pcie_event_handler(struct mlx5_core_dev *dev)
{
	struct mlx5_priv *priv = &dev->priv;

	queue_work(priv->pcie_power.wq, &priv->pcie_power.work);
}

static void general_event_handler(struct mlx5_core_dev *dev,
				  struct mlx5_eqe *eqe)
{
	switch (eqe->sub_type) {
	case MLX5_GENERAL_SUBTYPE_DELAY_DROP_TIMEOUT:
		if (dev->event &&
		    MLX5_CAP_GEN(dev, port_type) == MLX5_CAP_PORT_TYPE_ETH)
			dev->event(dev, MLX5_DEV_EVENT_DELAY_DROP_TIMEOUT, 0);
		break;
	case MLX5_GENERAL_SUBTYPE_PCIE_POWER_CHANGE_EVENT:
		mlx5_pcie_event_handler(dev);
		break;
#ifdef CONFIG_BF_POWER_FAILURE_EVENT
	case MLX5_GENERAL_SUBTYPE_POWER_FAILURE_EVENT:
		if (dev->event)
			dev->event(dev, MLX5_DEV_EVENT_POWER_FAILURE_EVENT, 1);
		break;
#endif
	default:
		mlx5_core_dbg(dev, "General event with unrecognized subtype: sub_type %d\n",
			      eqe->sub_type);
	}
}

static void mlx5_temp_warning_event(struct mlx5_core_dev *dev,
				    struct mlx5_eqe *eqe)
{
	u64 value_lsb;
	u64 value_msb;

	value_lsb = be64_to_cpu(eqe->data.temp_warning.sensor_warning_lsb);
	value_msb = be64_to_cpu(eqe->data.temp_warning.sensor_warning_msb);

	mlx5_core_warn(dev,
		       "High temperature on sensors with bit set %llx %llx",
		       value_msb, value_lsb);
}

/* caller must eventually call mlx5_cq_put on the returned cq */
static struct mlx5_core_cq *mlx5_eq_cq_get(struct mlx5_eq *eq, u32 cqn)
{
	struct mlx5_cq_table *table = &eq->cq_table;
	struct mlx5_core_cq *cq = NULL;

	spin_lock(&table->lock);
	cq = radix_tree_lookup(&table->tree, cqn);
	if (likely(cq))
		mlx5_cq_hold(cq);
	spin_unlock(&table->lock);

	return cq;
}

static void mlx5_eq_cq_completion(struct mlx5_eq *eq, u32 cqn)
{
	struct mlx5_core_cq *cq = mlx5_eq_cq_get(eq, cqn);

	if (unlikely(!cq)) {
		mlx5_core_dbg(eq->dev, "Completion event for bogus CQ 0x%x\n", cqn);
		return;
	}

	++cq->arm_sn;

	cq->comp(cq);

	mlx5_cq_put(cq);
}

static void mlx5_eq_cq_event(struct mlx5_eq *eq, u32 cqn, int event_type)
{
	struct mlx5_core_cq *cq = mlx5_eq_cq_get(eq, cqn);

	if (unlikely(!cq)) {
		mlx5_core_warn(eq->dev, "Async event for bogus CQ 0x%x\n", cqn);
		return;
	}

	cq->event(cq, event_type);

	mlx5_cq_put(cq);
}

enum {
	EC_FUNCTION_MASK = 0x8000,
};

static irqreturn_t mlx5_eq_int(int irq, void *eq_ptr)
{
	struct mlx5_eq *eq = eq_ptr;
	struct mlx5_core_dev *dev = eq->dev;
	struct mlx5_eqe *eqe;
	int set_ci = 0;
	u32 cqn = -1;
	u32 rsn;
	u8 port;

	while ((eqe = next_eqe_sw(eq))) {
		/*
		 * Make sure we read EQ entry contents after we've
		 * checked the ownership bit.
		 */
#ifdef dma_rmb
		dma_rmb();
#else
		rmb();
#endif

		mlx5_core_dbg(eq->dev, "eqn %d, eqe type %s\n",
			      eq->eqn, eqe_type_str(eqe->type));
		switch (eqe->type) {
		case MLX5_EVENT_TYPE_COMP:
			cqn = be32_to_cpu(eqe->data.comp.cqn) & 0xffffff;
			mlx5_eq_cq_completion(eq, cqn);
			break;
		case MLX5_EVENT_TYPE_DCT_DRAINED:
		case MLX5_EVENT_TYPE_DCT_KEY_VIOLATION:
			rsn = be32_to_cpu(eqe->data.dct.dctn) & 0xffffff;
			rsn |= (MLX5_RES_DCT << MLX5_USER_INDEX_LEN);
			mlx5_rsc_event(dev, rsn, eqe->type);
			break;
		case MLX5_EVENT_TYPE_PATH_MIG:
		case MLX5_EVENT_TYPE_COMM_EST:
		case MLX5_EVENT_TYPE_SQ_DRAINED:
		case MLX5_EVENT_TYPE_SRQ_LAST_WQE:
		case MLX5_EVENT_TYPE_WQ_CATAS_ERROR:
		case MLX5_EVENT_TYPE_PATH_MIG_FAILED:
		case MLX5_EVENT_TYPE_WQ_INVAL_REQ_ERROR:
		case MLX5_EVENT_TYPE_WQ_ACCESS_ERROR:
			rsn = be32_to_cpu(eqe->data.qp_srq.qp_srq_n) & 0xffffff;
			rsn |= (eqe->data.qp_srq.type << MLX5_USER_INDEX_LEN);
			mlx5_core_dbg(dev, "event %s(%d) arrived on resource 0x%x\n",
				      eqe_type_str(eqe->type), eqe->type, rsn);
			mlx5_rsc_event(dev, rsn, eqe->type);
			break;

		case MLX5_EVENT_TYPE_SRQ_RQ_LIMIT:
		case MLX5_EVENT_TYPE_SRQ_CATAS_ERROR:
			rsn = be32_to_cpu(eqe->data.qp_srq.qp_srq_n) & 0xffffff;
			mlx5_core_dbg(dev, "SRQ event %s(%d): srqn 0x%x\n",
				      eqe_type_str(eqe->type), eqe->type, rsn);
			mlx5_srq_event(dev, rsn, eqe->type);
			break;

		case MLX5_EVENT_TYPE_XRQ_ERROR:
			{
				u32 xrqn;
				u32 qpn_id_handle;
				u8 error_type;

				error_type = be32_to_cpu(eqe->data.xrq.type_xrqn) >> 24;
				xrqn = be32_to_cpu(eqe->data.xrq.type_xrqn) &
				      MLX5_24BIT_MASK;
				qpn_id_handle = be32_to_cpu(eqe->data.xrq.qpn_id_handle) &
					MLX5_24BIT_MASK;
				mlx5_core_dbg(dev,
					      "XRQ event %s(%d): xrqn 0x%x\n",
					      eqe_type_str(eqe->type),
					      eqe->type,
					      xrqn);
				mlx5_xrq_event(dev, xrqn, eqe->type, qpn_id_handle, error_type);
			}
			break;

		case MLX5_EVENT_TYPE_CMD:
			mlx5_cmd_comp_handler(dev, be32_to_cpu(eqe->data.cmd.vector), false);
			break;

		case MLX5_EVENT_TYPE_PORT_CHANGE:
			port = (eqe->data.port.port >> 4) & 0xf;
			switch (eqe->sub_type) {
			case MLX5_PORT_CHANGE_SUBTYPE_DOWN:
			case MLX5_PORT_CHANGE_SUBTYPE_ACTIVE:
			case MLX5_PORT_CHANGE_SUBTYPE_LID:
			case MLX5_PORT_CHANGE_SUBTYPE_PKEY:
			case MLX5_PORT_CHANGE_SUBTYPE_GUID:
			case MLX5_PORT_CHANGE_SUBTYPE_CLIENT_REREG:
			case MLX5_PORT_CHANGE_SUBTYPE_INITIALIZED:
				if (dev->event)
					dev->event(dev, port_subtype_event(eqe->sub_type),
						   (unsigned long)port);
				break;
			default:
				mlx5_core_warn(dev, "Port event with unrecognized subtype: port %d, sub_type %d\n",
					       port, eqe->sub_type);
			}
			break;
		case MLX5_EVENT_TYPE_CQ_ERROR:
			cqn = be32_to_cpu(eqe->data.cq_err.cqn) & 0xffffff;
			mlx5_core_warn(dev, "CQ error on CQN 0x%x, syndrome 0x%x\n",
				       cqn, eqe->data.cq_err.syndrome);
			mlx5_eq_cq_event(eq, cqn, eqe->type);
			break;

		case MLX5_EVENT_TYPE_PAGE_REQUEST:
			{
				u16 func_id = be16_to_cpu(eqe->data.req_pages.func_id);
				s32 npages = be32_to_cpu(eqe->data.req_pages.num_pages);
				bool ec_function = be16_to_cpu(eqe->data.req_pages.ec_function) & EC_FUNCTION_MASK;

				mlx5_core_dbg(dev, "page request for func 0x%x, npages %d\n",
					      func_id, npages);
				mlx5_core_req_pages_handler(dev, func_id, npages, ec_function);
			}
			break;

		case MLX5_EVENT_TYPE_NIC_VPORT_CHANGE:
			mlx5_eswitch_vport_event(dev->priv.eswitch, eqe);
			break;

		case MLX5_EVENT_TYPE_PORT_MODULE_EVENT:
			mlx5_port_module_event(dev, eqe);
			break;

#if defined (HAVE_PTP_CLOCK_INFO_N_PINS) && defined (HAVE_PTP_CLOCK_INFO) && (defined (CONFIG_PTP_1588_CLOCK) || defined(CONFIG_PTP_1588_CLOCK_MODULE))
		case MLX5_EVENT_TYPE_PPS_EVENT:
			mlx5_pps_event(dev, eqe);
			break;
#endif

		case MLX5_EVENT_TYPE_FPGA_ERROR:
		case MLX5_EVENT_TYPE_FPGA_QP_ERROR:
			mlx5_fpga_event(dev, eqe->type, &eqe->data.raw);
			break;

		case MLX5_EVENT_TYPE_TEMP_WARN_EVENT:
			mlx5_temp_warning_event(dev, eqe);
			break;

		case MLX5_EVENT_TYPE_GENERAL_EVENT:
			general_event_handler(dev, eqe);
			break;

		case MLX5_EVENT_TYPE_DEVICE_TRACER:
			mlx5_fw_tracer_event(dev, eqe);
			break;

		case MLX5_EVENT_TYPE_HOST_PARAMS_CHANGE:
			esw_host_params_event(dev->priv.eswitch);
			break;

		default:
			mlx5_core_warn(dev, "Unhandled event 0x%x on EQ 0x%x\n",
				       eqe->type, eq->eqn);
			break;
		}

		++eq->cons_index;
		++set_ci;

		/* The HCA will think the queue has overflowed if we
		 * don't tell it we've been processing events.  We
		 * create our EQs with MLX5_NUM_SPARE_EQE extra
		 * entries, so we must update our consumer index at
		 * least that often.
		 */
		if (unlikely(set_ci >= MLX5_NUM_SPARE_EQE)) {
			eq_update_ci(eq, 0);
			set_ci = 0;
		}
	}

	eq_update_ci(eq, 1);

	if (cqn != -1)
		tasklet_schedule(&eq->tasklet_ctx.task);

	return IRQ_HANDLED;
}

static irqreturn_t mlx5_eq_shared_int(int irq, void *eq_ptr)
{
	struct mlx5_eq *eq = eq_ptr;
	struct mlx5_priv *priv = &eq->dev->priv;
	struct mlx5_eq_table *eq_table = &priv->eq_table;
	struct mlx5_eq *eq_arr = eq_table->ctrl_eqs;
	struct mlx5_irq_info *irq_info;
	irqreturn_t ret = IRQ_NONE;
	int i;

	irq_info = &priv->irq_info[eq->vecidx];

	for_each_set_bit(i, irq_info->active_eqs, MLX5_EQ_MAX_ASYNC_EQS)
#ifdef CONFIG_INFINIBAND_ON_DEMAND_PAGING
		if (eq_arr[i].type == MLX5_EQ_TYPE_PF)
			ret = mlx5_eq_pf_int(irq, eq_arr + i);
		else
#endif
			ret = mlx5_eq_int(irq, eq_arr + i);

	return ret;
}

/* Some architectures don't latch interrupts when they are disabled, so using
 * mlx5_eq_poll_irq_disabled could end up losing interrupts while trying to
 * avoid losing them.  It is not recommended to use it, unless this is the last
 * resort.
 */
u32 mlx5_eq_poll_irq_disabled(struct mlx5_eq *eq)
{
	u32 count_eqe;

	disable_irq(eq->irqn);
	count_eqe = eq->cons_index;
	mlx5_eq_int(eq->irqn, eq);
	count_eqe = eq->cons_index - count_eqe;
	enable_irq(eq->irqn);

	return count_eqe;
}

static void init_eq_buf(struct mlx5_eq *eq)
{
	struct mlx5_eqe *eqe;
	int i;

	for (i = 0; i < eq->nent; i++) {
		eqe = get_eqe(eq, i);
		eqe->owner = MLX5_EQE_OWNER_INIT_VAL;
	}
}

void mlx5_add_pci_to_irq_name(struct mlx5_core_dev *dev, const char *src_name,
			      char *dest_name)
{
	snprintf(dest_name, MLX5_MAX_IRQ_NAME, "%s@pci:%s", src_name,
		 pci_name(dev->pdev));
}

int mlx5_create_map_eq(struct mlx5_core_dev *dev, struct mlx5_eq *eq, u8 idx,
		       int nent, u64 mask, const char *name,
		       enum mlx5_eq_type type,
		       bool is_shared)
{
	struct mlx5_cq_table *cq_table = &eq->cq_table;
	u8 vecidx = !is_shared ? idx : MLX5_EQ_VEC_SHARED_CTRL;
	u32 out[MLX5_ST_SZ_DW(create_eq_out)] = {0};
	struct mlx5_priv *priv = &dev->priv;
	irq_handler_t handler;
	__be64 *pas;
	void *eqc;
	int inlen;
	u32 *in;
	int err;

	/* Init CQ table */
	memset(cq_table, 0, sizeof(*cq_table));
	spin_lock_init(&cq_table->lock);
	INIT_RADIX_TREE(&cq_table->tree, GFP_ATOMIC);

	eq->type = type;
	eq->eq_idx = idx;
	eq->nent = roundup_pow_of_two(nent + MLX5_NUM_SPARE_EQE);
	eq->cons_index = 0;
	err = mlx5_buf_alloc(dev, eq->nent * MLX5_EQE_SIZE, &eq->buf);
	if (err)
		return err;

	if (is_shared)
		handler = mlx5_eq_shared_int;
#ifdef CONFIG_INFINIBAND_ON_DEMAND_PAGING
	else if (type == MLX5_EQ_TYPE_PF) {
		handler = mlx5_eq_pf_int;
		if (!mlx5_core_is_pf(dev))
			vecidx = MLX5_EQ_VEC_SHARED_PF;
	}
#endif
	else
		handler = mlx5_eq_int;

	eq->vecidx = vecidx;
	init_eq_buf(eq);

	inlen = MLX5_ST_SZ_BYTES(create_eq_in) +
		MLX5_FLD_SZ_BYTES(create_eq_in, pas[0]) * eq->buf.npages;

	in = kvzalloc(inlen, GFP_KERNEL);
	if (!in) {
		err = -ENOMEM;
		goto err_buf;
	}

	pas = (__be64 *)MLX5_ADDR_OF(create_eq_in, in, pas);
	mlx5_fill_page_array(&eq->buf, pas);

	MLX5_SET(create_eq_in, in, opcode, MLX5_CMD_OP_CREATE_EQ);
	MLX5_SET(create_eq_in, in, uid,
		 (mask || !MLX5_CAP_GEN(dev, log_max_uctx)) ? 0 :
		 MLX5_SHARED_RESOURCE_UID);
	MLX5_SET64(create_eq_in, in, event_bitmask, mask);

	eqc = MLX5_ADDR_OF(create_eq_in, in, eq_context_entry);
	MLX5_SET(eqc, eqc, log_eq_size, ilog2(eq->nent));
	MLX5_SET(eqc, eqc, uar_page, priv->uar->index);
	MLX5_SET(eqc, eqc, intr, vecidx);
	MLX5_SET(eqc, eqc, log_page_size,
		 eq->buf.page_shift - MLX5_ADAPTER_PAGE_SHIFT);

	err = mlx5_cmd_exec(dev, in, inlen, out, sizeof(out));
	if (err)
		goto err_in;

	eq->eqn = MLX5_GET(create_eq_out, out, eq_number);
#ifdef HAVE_PCI_IRQ_API
	eq->irqn = pci_irq_vector(dev->pdev, vecidx);
#else
	eq->irqn = priv->msix_arr[vecidx].vector;
#endif
	eq->dev = dev;
	eq->doorbell = priv->uar->map + MLX5_EQ_DOORBEL_OFFSET;

	if (!is_shared ||
	    bitmap_empty(priv->irq_info[vecidx].active_eqs, MLX5_EQ_MAX_ASYNC_EQS)) {
		mlx5_add_pci_to_irq_name(dev, !is_shared ? name : "mlx5_ctrl_eqs",
					 priv->irq_info[vecidx].name);
		priv->irq_info[vecidx].is_shared = is_shared;
		err = request_irq(eq->irqn, handler, 0,
				  priv->irq_info[vecidx].name,
				  eq);
		if (err)
			goto err_eq;
	}

	if (is_shared)
		set_bit(idx, priv->irq_info[vecidx].active_eqs);

	err = mlx5_debug_eq_add(dev, eq);
	if (err)
		goto err_irq;

#ifdef CONFIG_INFINIBAND_ON_DEMAND_PAGING
	if (type == MLX5_EQ_TYPE_PF) {
		err = init_pf_ctx(&eq->pf_ctx, name, dev->capi.enabled);
		if (err)
			goto err_irq;
	} else
#endif
	{
		INIT_LIST_HEAD(&eq->tasklet_ctx.list);
		INIT_LIST_HEAD(&eq->tasklet_ctx.process_list);
		spin_lock_init(&eq->tasklet_ctx.lock);
		tasklet_init(&eq->tasklet_ctx.task, mlx5_cq_tasklet_cb,
			     (unsigned long)&eq->tasklet_ctx);
	}

	/* EQs are created in ARMED state
	 */
	eq_update_ci(eq, 1);

	kvfree(in);
	return 0;

err_irq:
	if (is_shared)
		clear_bit(idx, priv->irq_info[vecidx].active_eqs);
	if (!is_shared ||
	    bitmap_empty(priv->irq_info[vecidx].active_eqs, MLX5_EQ_MAX_ASYNC_EQS))
		#ifdef HAVE_PCI_IRQ_API
		free_irq(eq->irqn, eq);
		#else
		free_irq(priv->msix_arr[vecidx].vector, eq);
		#endif

err_eq:
	mlx5_cmd_destroy_eq(dev, eq->eqn);

err_in:
	kvfree(in);

err_buf:
	mlx5_buf_free(dev, &eq->buf);
	return err;
}

int mlx5_destroy_unmap_eq(struct mlx5_core_dev *dev, struct mlx5_eq *eq)
{
	struct mlx5_irq_info *irq_info;
	int err;

	irq_info = &dev->priv.irq_info[eq->vecidx];
	mlx5_debug_eq_remove(dev, eq);

	if (irq_info->is_shared)
		clear_bit(eq->eq_idx, irq_info->active_eqs);

	if (!irq_info->is_shared ||
	    bitmap_empty(irq_info->active_eqs, MLX5_EQ_MAX_ASYNC_EQS))
		free_irq(eq->irqn, eq);

	err = mlx5_cmd_destroy_eq(dev, eq->eqn);
	if (err)
		mlx5_core_warn(dev, "failed to destroy a previously created eq: eqn %d\n",
			       eq->eqn);
	synchronize_irq(eq->irqn);

	if (eq->type == MLX5_EQ_TYPE_COMP) {
		tasklet_disable(&eq->tasklet_ctx.task);
#ifdef CONFIG_INFINIBAND_ON_DEMAND_PAGING
	} else if (eq->type == MLX5_EQ_TYPE_PF) {
		cancel_work_sync(&eq->pf_ctx.work);
		destroy_workqueue(eq->pf_ctx.wq);
		mempool_destroy(eq->pf_ctx.pool);
#endif
	}
	mlx5_buf_free(dev, &eq->buf);

	return err;
}

int mlx5_eq_add_cq(struct mlx5_eq *eq, struct mlx5_core_cq *cq)
{
	struct mlx5_cq_table *table = &eq->cq_table;
	int err;

	spin_lock_irq(&table->lock);
	err = radix_tree_insert(&table->tree, cq->cqn, cq);
	spin_unlock_irq(&table->lock);

	return err;
}

int mlx5_eq_del_cq(struct mlx5_eq *eq, struct mlx5_core_cq *cq)
{
	struct mlx5_cq_table *table = &eq->cq_table;
	struct mlx5_core_cq *tmp;

	spin_lock_irq(&table->lock);
	tmp = radix_tree_delete(&table->tree, cq->cqn);
	spin_unlock_irq(&table->lock);

	if (!tmp) {
		mlx5_core_warn(eq->dev, "cq 0x%x not found in eq 0x%x tree\n", eq->eqn, cq->cqn);
		return -ENOENT;
	}

	if (tmp != cq) {
		mlx5_core_warn(eq->dev, "corruption on cqn 0x%x in eq 0x%x\n", eq->eqn, cq->cqn);
		return -EINVAL;
	}

	return 0;
}

#ifndef HAVE_PCI_IRQ_API
u32 mlx5_get_msix_vec(struct mlx5_core_dev *dev, int vecidx)
{
	return dev->priv.msix_arr[MLX5_EQ_VEC_ASYNC].vector;
}
#endif

int mlx5_eq_init(struct mlx5_core_dev *dev)
{
	int err;

	spin_lock_init(&dev->priv.eq_table.lock);

	err = mlx5_eq_debugfs_init(dev);

	return err;
}

void mlx5_eq_cleanup(struct mlx5_core_dev *dev)
{
	mlx5_eq_debugfs_cleanup(dev);
}

int mlx5_start_eqs(struct mlx5_core_dev *dev)
{
	struct mlx5_eq_table *table = &dev->priv.eq_table;
	u64 async_event_mask = MLX5_ASYNC_EVENT_MASK;
	int is_pf = mlx5_core_is_pf(dev);
	bool is_shared = is_pf ? false : true;
	int err;

	if (MLX5_VPORT_MANAGER(dev))
		async_event_mask |= (1ull << MLX5_EVENT_TYPE_NIC_VPORT_CHANGE);

	if (MLX5_CAP_GEN(dev, general_notification_event))
		async_event_mask |= (1ull << MLX5_EVENT_TYPE_GENERAL_EVENT);

	if (MLX5_CAP_GEN(dev, port_module_event))
		async_event_mask |= (1ull << MLX5_EVENT_TYPE_PORT_MODULE_EVENT);
	else
		mlx5_core_dbg(dev, "port_module_event is not set\n");

	if (MLX5_PPS_CAP(dev))
		async_event_mask |= (1ull << MLX5_EVENT_TYPE_PPS_EVENT);

	if (MLX5_CAP_GEN(dev, fpga))
		async_event_mask |= (1ull << MLX5_EVENT_TYPE_FPGA_ERROR) |
				    (1ull << MLX5_EVENT_TYPE_FPGA_QP_ERROR);

	if (MLX5_CAP_GEN(dev, nvmf_target_offload))
		async_event_mask |= (1ull << MLX5_EVENT_TYPE_XRQ_ERROR);

	if (MLX5_CAP_GEN_MAX(dev, dct))
		async_event_mask |= (1ull << MLX5_EVENT_TYPE_DCT_DRAINED);

	if (MLX5_CAP_GEN(dev, temp_warn_event))
		async_event_mask |= (1ull << MLX5_EVENT_TYPE_TEMP_WARN_EVENT);

	if (MLX5_CAP_MCAM_REG(dev, tracer_registers))
		async_event_mask |= (1ull << MLX5_EVENT_TYPE_DEVICE_TRACER);

	if (mlx5_core_is_ecpf_esw_manager(dev))
		async_event_mask |= (1ull << MLX5_EVENT_TYPE_HOST_PARAMS_CHANGE);

	err = mlx5_create_map_eq(dev, &table->ctrl_eqs[MLX5_EQ_VEC_CMD],
				 MLX5_EQ_VEC_CMD, MLX5_NUM_CMD_EQE,
				 1ull << MLX5_EVENT_TYPE_CMD,
				 "mlx5_cmd_eq", MLX5_EQ_TYPE_ASYNC, is_shared);
	if (err) {
		mlx5_core_warn(dev, "failed to create cmd EQ %d\n", err);
		return err;
	}

	mlx5_cmd_use_events(dev);

	err = mlx5_create_map_eq(dev, &table->ctrl_eqs[MLX5_EQ_VEC_ASYNC],
				 MLX5_EQ_VEC_ASYNC, MLX5_NUM_ASYNC_EQE,
				 dev->async_events_mask | async_event_mask,
				 "mlx5_async_eq", MLX5_EQ_TYPE_ASYNC, is_shared);
	if (err) {
		mlx5_core_warn(dev, "failed to create async EQ %d\n", err);
		goto err1;
	}

	err = mlx5_create_map_eq(dev, &table->ctrl_eqs[MLX5_EQ_VEC_PAGES],
				 MLX5_EQ_VEC_PAGES,
				 /* TODO: sriov max_vf + */ 1,
				 1 << MLX5_EVENT_TYPE_PAGE_REQUEST, "mlx5_pages_eq",
				 MLX5_EQ_TYPE_ASYNC, is_shared);
	if (err) {
		mlx5_core_warn(dev, "failed to create pages EQ %d\n", err);
		goto err2;
	}

#ifdef CONFIG_INFINIBAND_ON_DEMAND_PAGING
	if (MLX5_CAP_GEN(dev, pg)) {
		err = mlx5_create_map_eq(dev, &table->ctrl_eqs[MLX5_EQ_VEC_PFAULT],
					 MLX5_EQ_VEC_PFAULT,
					 MLX5_NUM_ASYNC_EQE,
					 1 << MLX5_EVENT_TYPE_PAGE_FAULT,
					 "mlx5_page_fault_eq",
					 MLX5_EQ_TYPE_PF, 0);
		if (err) {
			mlx5_core_warn(dev, "failed to create page fault EQ %d\n",
				       err);
			goto err3;
		}
	}

	return err;
err3:
	mlx5_destroy_unmap_eq(dev, &table->ctrl_eqs[MLX5_EQ_VEC_PAGES]);
#else
	return err;
#endif

err2:
	mlx5_destroy_unmap_eq(dev, &table->ctrl_eqs[MLX5_EQ_VEC_ASYNC]);

err1:
	mlx5_cmd_use_polling(dev);
	mlx5_destroy_unmap_eq(dev, &table->ctrl_eqs[MLX5_EQ_VEC_CMD]);
	return err;
}

void mlx5_stop_eqs(struct mlx5_core_dev *dev)
{
	struct mlx5_eq_table *table = &dev->priv.eq_table;
	int err;

#ifdef CONFIG_INFINIBAND_ON_DEMAND_PAGING
	if (MLX5_CAP_GEN(dev, pg)) {
		err = mlx5_destroy_unmap_eq(dev, &table->ctrl_eqs[MLX5_EQ_VEC_PFAULT]);
		if (err)
			mlx5_core_err(dev, "failed to destroy page fault eq, err(%d)\n",
				      err);
	}
#endif

	err = mlx5_destroy_unmap_eq(dev, &table->ctrl_eqs[MLX5_EQ_VEC_PAGES]);
	if (err)
		mlx5_core_err(dev, "failed to destroy pages eq, err(%d)\n",
			      err);

	err = mlx5_destroy_unmap_eq(dev, &table->ctrl_eqs[MLX5_EQ_VEC_ASYNC]);
	if (err)
		mlx5_core_err(dev, "failed to destroy async eq, err(%d)\n",
			      err);
	mlx5_cmd_use_polling(dev);

	err = mlx5_destroy_unmap_eq(dev, &table->ctrl_eqs[MLX5_EQ_VEC_CMD]);
	if (err)
		mlx5_core_err(dev, "failed to destroy command eq, err(%d)\n",
			      err);
}

int mlx5_core_eq_query(struct mlx5_core_dev *dev, struct mlx5_eq *eq,
		       u32 *out, int outlen)
{
	u32 in[MLX5_ST_SZ_DW(query_eq_in)] = {0};

	MLX5_SET(query_eq_in, in, opcode, MLX5_CMD_OP_QUERY_EQ);
	MLX5_SET(query_eq_in, in, eq_number, eq->eqn);
	return mlx5_cmd_exec(dev, in, sizeof(in), out, outlen);
}

/* This function should only be called after mlx5_cmd_force_teardown_hca */
void mlx5_core_eq_free_irqs(struct mlx5_core_dev *dev)
{
	struct mlx5_eq_table *table = &dev->priv.eq_table;
	struct mlx5_irq_info *irq_info;
	struct mlx5_eq *eq;
	int i;

#ifdef CONFIG_RFS_ACCEL
	if (dev->rmap) {
		free_irq_cpu_rmap(dev->rmap);
		dev->rmap = NULL;
	}
#endif
	list_for_each_entry(eq, &table->comp_eqs_list, list)
		free_irq(eq->irqn, eq);

	irq_info = &dev->priv.irq_info[MLX5_EQ_VEC_SHARED_CTRL];

	if (!irq_info->is_shared) {
		for (i = 0; i < MLX5_EQ_MAX_ASYNC_EQS - 1; i++)
			free_irq(table->ctrl_eqs[i].irqn, &table->ctrl_eqs[i]);
	} else {
		free_irq(table->ctrl_eqs[MLX5_EQ_VEC_CMD].irqn, &table->ctrl_eqs[MLX5_EQ_VEC_CMD]);
	}
#ifdef CONFIG_INFINIBAND_ON_DEMAND_PAGING
	if (MLX5_CAP_GEN(dev, pg))
		free_irq(table->ctrl_eqs[MLX5_EQ_VEC_PFAULT].irqn, &table->ctrl_eqs[MLX5_EQ_VEC_PFAULT]);
#endif

#ifdef HAVE_PCI_IRQ_API
	pci_free_irq_vectors(dev->pdev);
#endif
}

void mlx5_core_eq_disable_irqs(struct mlx5_core_dev *dev)
{
	struct mlx5_eq_table *table = &dev->priv.eq_table;
	struct mlx5_irq_info *irq_info;
	struct mlx5_eq *eq;
	int i;

	list_for_each_entry(eq, &table->comp_eqs_list, list)
		disable_irq(eq->irqn);

	irq_info = &dev->priv.irq_info[MLX5_EQ_VEC_SHARED_CTRL];
	if (!irq_info->is_shared) {
		for (i = 0; i < MLX5_EQ_MAX_ASYNC_EQS - 1; i++)
			disable_irq(table->ctrl_eqs[i].irqn);
	} else {
		disable_irq(table->ctrl_eqs[MLX5_EQ_VEC_CMD].irqn);
	}

#ifdef CONFIG_INFINIBAND_ON_DEMAND_PAGING
	if (MLX5_CAP_GEN(dev, pg))
		disable_irq(table->ctrl_eqs[MLX5_EQ_VEC_PFAULT].irqn);
#endif
}
