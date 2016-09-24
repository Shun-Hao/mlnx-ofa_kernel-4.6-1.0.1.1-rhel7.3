/*
 * Copyright (c) 2016 Mellanox Technologies. All rights reserved.
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

#include "mlx5_ib.h"
#include "user_exp.h"
#include <linux/mlx5/qp.h>
#include <linux/mlx5/qp_exp.h>

int mlx5_ib_exp_max_inl_recv(struct ib_qp_init_attr *init_attr)
{
	return ((struct ib_exp_qp_init_attr *)init_attr)->max_inl_recv;
}

struct ib_qp *mlx5_ib_exp_create_qp(struct ib_pd *pd,
				    struct ib_exp_qp_init_attr *init_attr,
				    struct ib_udata *udata)
{
	int use_inlr;

	use_inlr = (init_attr->qp_type == IB_QPT_RC ||
		    init_attr->qp_type == IB_QPT_UC) &&
		init_attr->max_inl_recv && pd;

	if (use_inlr) {
		int rcqe_sz;
		int scqe_sz;

		rcqe_sz = mlx5_ib_get_cqe_size(init_attr->recv_cq);
		scqe_sz = mlx5_ib_get_cqe_size(init_attr->send_cq);

		if (rcqe_sz == 128)
			init_attr->max_inl_recv = 64;
		else
			init_attr->max_inl_recv = 32;
	} else {
		init_attr->max_inl_recv = 0;
	}

	return _mlx5_ib_create_qp(pd, (struct ib_qp_init_attr *)init_attr,
				  udata, 1);
}

static u32 atomic_mode_dct(struct mlx5_ib_dev *dev)
{
	unsigned long mask;
	unsigned long tmp;

	mask = MLX5_CAP_ATOMIC(dev->mdev, atomic_size_qp) &
	       MLX5_CAP_ATOMIC(dev->mdev, atomic_size_dc);

	tmp = find_last_bit(&mask, BITS_PER_LONG);
	if (tmp < 2)
		return MLX5_ATOMIC_MODE_DCT_NONE;

	if (tmp == 2)
		return MLX5_ATOMIC_MODE_DCT_CX;

	return tmp << MLX5_ATOMIC_MODE_DCT_OFF;
}

static u32 ib_to_dct_access(struct mlx5_ib_dev *dev, u32 ib_flags)
{
	u32 flags = 0;

	if (ib_flags & IB_ACCESS_REMOTE_READ)
		flags |= MLX5_DCT_BIT_RRE;
	if (ib_flags & IB_ACCESS_REMOTE_WRITE)
		flags |= (MLX5_DCT_BIT_RWE | MLX5_DCT_BIT_RRE);
	if (ib_flags & IB_ACCESS_REMOTE_ATOMIC) {
		flags |= (MLX5_DCT_BIT_RAE | MLX5_DCT_BIT_RWE | MLX5_DCT_BIT_RRE);
		flags |= atomic_mode_dct(dev);
	}

	return flags;
}

static void mlx5_ib_dct_event(struct mlx5_core_dct *dct, enum mlx5_event type)
{
	struct ib_dct *ibdct = &to_mibdct(dct)->ibdct;
	struct ib_event event;

	if (ibdct->event_handler) {
		event.device     = ibdct->device;
		event.element.dct = ibdct;
		switch (type) {
		case MLX5_EVENT_TYPE_WQ_INVAL_REQ_ERROR:
			event.event = IB_EXP_EVENT_DCT_REQ_ERR;
			break;
		case MLX5_EVENT_TYPE_WQ_ACCESS_ERROR:
			event.event = IB_EXP_EVENT_DCT_ACCESS_ERR;
			break;
		case MLX5_EVENT_TYPE_DCT_KEY_VIOLATION:
			event.event = IB_EXP_EVENT_DCT_KEY_VIOLATION;
			break;
		default:
			pr_warn("mlx5_ib: Unexpected event type %d on DCT %06x\n",
				type, dct->dctn);
			return;
		}

		ibdct->event_handler(&event, ibdct->dct_context);
	}
}

struct ib_dct *mlx5_ib_create_dct(struct ib_pd *pd,
				  struct ib_dct_init_attr *attr,
				  struct ib_udata *udata)
{
	u32 *in;
	int inlen = MLX5_ST_SZ_BYTES(create_dct_in);
	struct mlx5_ib_create_dct ucmd;
	struct mlx5_ib_dev *dev = to_mdev(pd->device);
	struct mlx5_ib_dct *dct;
	void *dctc;
	int cqe_sz;
	int err;
	u32 flags = 0;
	u32 uidx = 0;
	u32 cqn;

	if (pd && pd->uobject) {
		if (ib_copy_from_udata(&ucmd, udata, sizeof(ucmd))) {
			mlx5_ib_err(dev, "copy failed\n");
			return ERR_PTR(-EFAULT);
		}

		if (udata->inlen)
			uidx = ucmd.uidx;
		else
			uidx = 0xffffff;
	} else {
		uidx = 0xffffff;
	}

	dct = kzalloc(sizeof(*dct), GFP_KERNEL);
	if (!dct)
		return ERR_PTR(-ENOMEM);

	in = kzalloc(inlen, GFP_KERNEL);
	if (!in) {
		err = -ENOMEM;
		goto err_alloc;
	}

	dctc = MLX5_ADDR_OF(create_dct_in, in, dct_context_entry);
	cqn = to_mcq(attr->cq)->mcq.cqn;
	if (cqn & 0xff000000) {
		mlx5_ib_warn(dev, "invalid cqn 0x%x\n", cqn);
		err = -EINVAL;
		goto err_alloc;
	}

	MLX5_SET(dctc, dctc, cqn, cqn);

	flags = ib_to_dct_access(dev, attr->access_flags);
	if (flags & MLX5_DCT_BIT_RRE)
		MLX5_SET(dctc, dctc, rre, 1);
	if (flags & MLX5_DCT_BIT_RWE)
		MLX5_SET(dctc, dctc, rwe, 1);
	if (flags & MLX5_DCT_BIT_RAE)
		MLX5_SET(dctc, dctc, rae, 1);

	if (attr->inline_size) {
		cqe_sz = mlx5_ib_get_cqe_size(dev, attr->cq);
		if (cqe_sz == 128) {
			MLX5_SET(dctc, dctc, cs_res, MLX5_DCT_CS_RES_64);
			attr->inline_size = 64;
		} else {
			attr->inline_size = 0;
		}
	}

	MLX5_SET(dctc, dctc, min_rnr_nak , attr->min_rnr_timer);
	MLX5_SET(dctc, dctc, srqn_xrqn , to_msrq(attr->srq)->msrq.srqn);
	MLX5_SET(dctc, dctc, pd , to_mpd(pd)->pdn);
	MLX5_SET(dctc, dctc, tclass, attr->tclass);
	MLX5_SET(dctc, dctc, flow_label , attr->flow_label);
	MLX5_SET64(dctc, dctc, dc_access_key , attr->dc_key);
	MLX5_SET(dctc, dctc, mtu , attr->mtu);
	MLX5_SET(dctc, dctc, port , attr->port);
	MLX5_SET(dctc, dctc, pkey_index , attr->pkey_index);
	MLX5_SET(dctc, dctc, my_addr_index , attr->gid_index);
	MLX5_SET(dctc, dctc, hop_limit , attr->hop_limit);

	if (MLX5_CAP_GEN(dev->mdev, cqe_version)) {
		/* 0xffffff means we ask to work with cqe version 0 */
		MLX5_SET(dctc, dctc, user_index, uidx);
	}

	err = mlx5_core_create_dct(dev->mdev, &dct->mdct, in);
	if (err)
		goto err_alloc;

	dct->ibdct.dct_num = dct->mdct.dctn;
	dct->mdct.event = mlx5_ib_dct_event;
	kfree(in);
	return &dct->ibdct;

err_alloc:
	kfree(in);
	kfree(dct);
	return ERR_PTR(err);
}

int mlx5_ib_destroy_dct(struct ib_dct *dct)
{
	struct mlx5_ib_dev *dev = to_mdev(dct->device);
	struct mlx5_ib_dct *mdct = to_mdct(dct);
	int err;

	err = mlx5_core_destroy_dct(dev->mdev, &mdct->mdct);
	if (!err)
		kfree(mdct);

	return err;
}

int dct_to_ib_access(u32 dc_flags)
{
	u32 flags = 0;

	if (dc_flags & MLX5_DCT_BIT_RRE)
		flags |= IB_ACCESS_REMOTE_READ;
	if (dc_flags & MLX5_QP_BIT_RWE)
		flags |= IB_ACCESS_REMOTE_WRITE;
	if ((dc_flags & MLX5_ATOMIC_MODE_CX) == MLX5_ATOMIC_MODE_CX)
		flags |= IB_ACCESS_REMOTE_ATOMIC;

	return flags;
}

int mlx5_ib_query_dct(struct ib_dct *dct, struct ib_dct_attr *attr)
{
	struct mlx5_ib_dev *dev = to_mdev(dct->device);
	struct mlx5_ib_dct *mdct = to_mdct(dct);
	u32 dc_flags = 0;
	u32 *out;
	int outlen = MLX5_ST_SZ_BYTES(query_dct_out);
	void *dctc;
	int err;

	out = kzalloc(outlen, GFP_KERNEL);
	if (!out)
		return -ENOMEM;

	err = mlx5_core_dct_query(dev->mdev, &mdct->mdct, out, outlen);
	if (err)
		goto out;

	dctc = MLX5_ADDR_OF(query_dct_out, out, dct_context_entry);

	if (MLX5_GET(dctc, dctc, rre))
		dc_flags |= MLX5_DCT_BIT_RRE;
	if (MLX5_GET(dctc, dctc, rwe))
		dc_flags |= MLX5_DCT_BIT_RWE;
	if (MLX5_GET(dctc, dctc, rae))
		dc_flags |= MLX5_DCT_BIT_RAE;

	attr->dc_key = MLX5_GET64(dctc, dctc, dc_access_key);
	attr->port = MLX5_GET(dctc, dctc, port);
	attr->access_flags = dct_to_ib_access(dc_flags);
	attr->min_rnr_timer = MLX5_GET(dctc, dctc, min_rnr_nak);
	attr->tclass = MLX5_GET(dctc, dctc, tclass);
	attr->flow_label = MLX5_GET(dctc, dctc, flow_label);
	attr->mtu = MLX5_GET(dctc, dctc, mtu);
	attr->pkey_index = MLX5_GET(dctc, dctc, pkey_index);
	attr->gid_index = MLX5_GET(dctc, dctc, my_addr_index);
	attr->hop_limit = MLX5_GET(dctc, dctc, hop_limit);
	attr->key_violations = MLX5_GET(dctc, dctc,
					dc_access_key_violation_count);
	attr->state = MLX5_GET(dctc, dctc, state);

out:
	kfree(out);
	return err;
}

int mlx5_ib_arm_dct(struct ib_dct *dct, struct ib_udata *udata)
{
	struct mlx5_ib_dev *dev = to_mdev(dct->device);
	struct mlx5_ib_dct *mdct = to_mdct(dct);
	struct mlx5_ib_arm_dct ucmd;
	struct mlx5_ib_arm_dct_resp resp;
	int err;

	err = ib_copy_from_udata(&ucmd, udata, sizeof(ucmd));
	if (err) {
		mlx5_ib_err(dev, "copy failed\n");
		return err;
	}

	if (ucmd.reserved0 || ucmd.reserved1)
		return -EINVAL;

	err = mlx5_core_arm_dct(dev->mdev, &mdct->mdct);
	if (err)
		goto out;

	memset(&resp, 0, sizeof(resp));
	err = ib_copy_to_udata(udata, &resp, sizeof(resp));
	if (err)
		mlx5_ib_err(dev, "copy failed\n");

out:
	return err;
}
