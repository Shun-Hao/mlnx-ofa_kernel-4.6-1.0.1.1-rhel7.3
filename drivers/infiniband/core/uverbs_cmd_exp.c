/*
 * Copyright (c) 2015 Mellanox Technologies.  All rights reserved.
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

#include <linux/file.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/moduleparam.h>
#include <linux/printk.h>
#include <rdma/ib_addr.h>
#include <rdma/ib_umem_odp.h>
#include <linux/sched.h>

#include <asm/uaccess.h>
#include <linux/sched.h>

#include "uverbs.h"
#include "core_priv.h"

int ib_uverbs_exp_create_qp(struct ib_uverbs_file *file, struct ib_device *ib_dev,
			    struct ib_udata *ucore, struct ib_udata *uhw)
{
	struct ib_uqp_object           *obj;
	struct ib_device	       *device;
	struct ib_pd                   *pd = NULL;
	struct ib_xrcd		       *xrcd = NULL;
	struct ib_uobject	       *xrcd_uobj = ERR_PTR(-ENOENT);
	struct ib_cq                   *scq = NULL, *rcq = NULL;
	struct ib_srq                  *srq = NULL;
	struct ib_qp                   *qp;
	struct ib_qp                   *parentqp = NULL;
	struct ib_exp_qp_init_attr     *attr;
	struct ib_uverbs_exp_create_qp *cmd_exp;
	struct ib_uverbs_exp_create_qp_resp resp_exp;
	int                             ret;

	cmd_exp = kzalloc(sizeof(*cmd_exp), GFP_KERNEL);
	attr = kzalloc(sizeof(*attr), GFP_KERNEL);
	if (!cmd_exp || !attr) {
		ret = -ENOMEM;
		goto err_cmd_attr;
	}

	ret = ib_copy_from_udata(cmd_exp, ucore, sizeof(*cmd_exp));
	if (ret)
		goto err_cmd_attr;

	obj  = (struct ib_uqp_object *)uobj_alloc(UVERBS_OBJECT_QP,
						  file, &ib_dev);
	if (IS_ERR(obj))
		return PTR_ERR(obj);
	obj->uxrcd = NULL;
	obj->uevent.uobject.user_handle = cmd_exp->user_handle;


	if (cmd_exp->qp_type == IB_QPT_XRC_TGT) {
		xrcd_uobj = uobj_get_read(UVERBS_OBJECT_XRCD, cmd_exp->pd_handle,
					  file);

		if (IS_ERR(xrcd_uobj)) {
			ret = -EINVAL;
			goto err_put;
		}

		xrcd = (struct ib_xrcd *)xrcd_uobj->object;
		if (!xrcd) {
			ret = -EINVAL;
			goto err_put;
		}
		device = xrcd->device;
	} else {
		if (cmd_exp->qp_type == IB_QPT_XRC_INI) {
			cmd_exp->max_recv_wr = 0;
			cmd_exp->max_recv_sge = 0;
		} else {
			if (cmd_exp->is_srq) {
				srq = uobj_get_obj_read(srq, UVERBS_OBJECT_SRQ,
							cmd_exp->srq_handle,
							file);
				if (!srq || srq->srq_type != IB_SRQT_BASIC) {
					ret = -EINVAL;
					goto err_put;
				}
			}

			if (cmd_exp->recv_cq_handle != cmd_exp->send_cq_handle) {
				rcq = uobj_get_obj_read(cq, UVERBS_OBJECT_CQ,
							cmd_exp->recv_cq_handle,
							file);
				if (!rcq) {
					ret = -EINVAL;
					goto err_put;
				}
			}
		}

		scq = uobj_get_obj_read(cq, UVERBS_OBJECT_CQ,
					cmd_exp->send_cq_handle,
					file);
		rcq = rcq ?: scq;
		pd  = uobj_get_obj_read(pd, UVERBS_OBJECT_PD,
					cmd_exp->pd_handle, file);
		if (!pd || !scq) {
			ret = -EINVAL;
			goto err_put;
		}

		device = pd->device;
	}

	attr->event_handler = ib_uverbs_qp_event_handler;
	attr->qp_context    = file;
	attr->send_cq       = scq;
	attr->recv_cq       = rcq;
	attr->srq           = srq;
	attr->xrcd	   = xrcd;
	attr->sq_sig_type   = cmd_exp->sq_sig_all ? IB_SIGNAL_ALL_WR : IB_SIGNAL_REQ_WR;
	attr->qp_type       = cmd_exp->qp_type;
	attr->create_flags  = 0;

	attr->cap.max_send_wr     = cmd_exp->max_send_wr;
	attr->cap.max_recv_wr     = cmd_exp->max_recv_wr;
	attr->cap.max_send_sge    = cmd_exp->max_send_sge;
	attr->cap.max_recv_sge    = cmd_exp->max_recv_sge;
	attr->cap.max_inline_data = cmd_exp->max_inline_data;

	if (cmd_exp->comp_mask & IB_UVERBS_EXP_CREATE_QP_CAP_FLAGS) {
		if (cmd_exp->qp_cap_flags & ~IBV_UVERBS_EXP_CREATE_QP_FLAGS) {
			ret = -EINVAL;
			goto err_put;
		}

		attr->create_flags |= cmd_exp->qp_cap_flags;
		if (attr->create_flags & IB_QP_EXP_USER_CREATE_ATOMIC_BE_REPLY) {
			/* convert user requset to kernel matching creation flag */
			attr->create_flags &= ~IB_QP_EXP_USER_CREATE_ATOMIC_BE_REPLY;
			attr->create_flags |= IB_QP_EXP_CREATE_ATOMIC_BE_REPLY;
		}
	}

	if (cmd_exp->comp_mask & IB_UVERBS_EXP_CREATE_QP_QPG) {
		struct ib_uverbs_qpg *qpg;

		if (cmd_exp->qp_type != IB_QPT_RAW_PACKET &&
		    cmd_exp->qp_type != IB_QPT_UD) {
			ret = -EINVAL;
			goto err_put;
		}
		qpg = &cmd_exp->qpg;
		switch (qpg->qpg_type) {
		case IB_QPG_PARENT:
			attr->parent_attrib.rss_child_count =
				qpg->parent_attrib.rss_child_count;
			attr->parent_attrib.tss_child_count =
				qpg->parent_attrib.tss_child_count;
			break;
		case IB_QPG_CHILD_RX:
		case IB_QPG_CHILD_TX:
			parentqp = uobj_get_obj_read(qp, UVERBS_OBJECT_QP, qpg->parent_handle,
						     file);
			if (!parentqp) {
				ret = -EINVAL;
				goto err_put;
			}
			attr->qpg_parent = parentqp;
			break;
		default:
			ret = -EINVAL;
			goto err_put;
		}
		attr->qpg_type = qpg->qpg_type;
	}

	if (cmd_exp->comp_mask & IB_UVERBS_EXP_CREATE_QP_INL_RECV)
		attr->max_inl_recv = cmd_exp->max_inl_recv;

	obj->uevent.events_reported     = 0;
	INIT_LIST_HEAD(&obj->uevent.event_list);
	INIT_LIST_HEAD(&obj->mcast_list);

	if (cmd_exp->qp_type == IB_QPT_XRC_TGT)
		qp = ib_create_qp(pd, (struct ib_qp_init_attr *)attr);
	else
		qp = device->exp_create_qp(pd, attr, uhw);

	if (IS_ERR(qp)) {
		ret = PTR_ERR(qp);
		goto err_put;
	}

	if (cmd_exp->qp_type != IB_QPT_XRC_TGT) {
		ret = ib_create_qp_security(qp, device);
		if (ret)
			goto err_copy;

		qp->real_qp	  = qp;
		qp->device	  = device;
		qp->pd		  = pd;
		qp->send_cq	  = attr->send_cq;
		qp->recv_cq	  = attr->recv_cq;
		qp->srq		  = attr->srq;
		qp->event_handler = attr->event_handler;
		qp->qp_context	  = attr->qp_context;
		qp->qp_type	  = attr->qp_type;
		atomic_set(&qp->usecnt, 0);
		atomic_inc(&pd->usecnt);
		atomic_inc(&attr->send_cq->usecnt);
		if (attr->recv_cq)
			atomic_inc(&attr->recv_cq->usecnt);
		if (attr->srq)
			atomic_inc(&attr->srq->usecnt);
	}
	qp->uobject = &obj->uevent.uobject;

	obj->uevent.uobject.object = qp;

	memset(&resp_exp, 0, sizeof(resp_exp));
	resp_exp.qpn             = qp->qp_num;
	resp_exp.qp_handle       = obj->uevent.uobject.id;
	resp_exp.max_recv_sge    = attr->cap.max_recv_sge;
	resp_exp.max_send_sge    = attr->cap.max_send_sge;
	resp_exp.max_recv_wr     = attr->cap.max_recv_wr;
	resp_exp.max_send_wr     = attr->cap.max_send_wr;

	if (cmd_exp->comp_mask & IB_UVERBS_EXP_CREATE_QP_INL_RECV) {
		resp_exp.comp_mask |= IB_UVERBS_EXP_CREATE_QP_RESP_INL_RECV;
		resp_exp.max_inl_recv = attr->max_inl_recv;
	}

	ret = ib_copy_to_udata(ucore, &resp_exp, sizeof(resp_exp));
	if (ret)
		goto err_copy;

	if (xrcd) {
		obj->uxrcd = container_of(xrcd_uobj, struct ib_uxrcd_object, uobject);
		atomic_inc(&obj->uxrcd->refcnt);
		uobj_put_read(xrcd_uobj);
	}

	if (pd)
		uobj_put_obj_read(pd);
	if (scq)
		uobj_put_obj_read(scq);
	if (rcq && rcq != scq)
		uobj_put_obj_read(rcq);
	if (srq)
		uobj_put_obj_read(srq);
	if (parentqp)
		uobj_put_obj_read(parentqp);

	kfree(attr);
	kfree(cmd_exp);

	return uobj_alloc_commit(&obj->uevent.uobject, 0);

err_copy:
	ib_destroy_qp(qp);

err_put:
	if (!IS_ERR(xrcd_uobj))
		uobj_put_read(xrcd_uobj);
	if (pd)
		uobj_put_obj_read(pd);
	if (scq)
		uobj_put_obj_read(scq);
	if (rcq && rcq != scq)
		uobj_put_obj_read(rcq);
	if (srq)
		uobj_put_obj_read(srq);
	if (parentqp)
		uobj_put_obj_read(parentqp);

	uobj_alloc_abort(&obj->uevent.uobject);

err_cmd_attr:
	kfree(attr);
	kfree(cmd_exp);
	return ret;
}

int ib_uverbs_exp_modify_cq(struct ib_uverbs_file *file, struct ib_device *ib_dev,
			    struct ib_udata *ucore, struct ib_udata *uhw)
{
	struct ib_uverbs_exp_modify_cq cmd;
	struct ib_cq               *cq;
	struct ib_cq_attr           attr;
	int                         ret;

	memset(&cmd, 0, sizeof(cmd));
	ret = ib_copy_from_udata(&cmd, ucore, sizeof(cmd));
	if (ret)
		return ret;

	if (cmd.comp_mask >= IB_UVERBS_EXP_CQ_ATTR_RESERVED)
		return -ENOSYS;

	cq = uobj_get_obj_read(cq, UVERBS_OBJECT_CQ, cmd.cq_handle, file);
	if (!cq)
		return -EINVAL;

	attr.moderation.cq_count  = cmd.cq_count;
	attr.moderation.cq_period = cmd.cq_period;
	attr.cq_cap_flags         = cmd.cq_cap_flags;

	ret = ib_exp_modify_cq(cq, &attr, cmd.attr_mask);

	uobj_put_obj_read(cq);

	return ret;
}

int ib_uverbs_exp_query_device(struct ib_uverbs_file *file,
			       struct ib_device *ib_dev,
			       struct ib_udata *ucore, struct ib_udata *uhw)
{
	struct ib_uverbs_exp_query_device_resp *resp;
	struct ib_uverbs_exp_query_device cmd;
	struct ib_exp_device_attr              *exp_attr;
	int                                    ret;

	ret = ib_copy_from_udata(&cmd, ucore, sizeof(cmd));
	if (ret)
		return ret;

	resp = kzalloc(sizeof(*resp), GFP_KERNEL);
	exp_attr = kzalloc(sizeof(*exp_attr), GFP_KERNEL);
	if (!exp_attr || !resp) {
		ret = -ENOMEM;
		goto out;
	}

	ret = ib_exp_query_device(file->device->ib_dev, exp_attr, uhw);
	if (ret)
		goto out;

	memset(resp, 0, sizeof(*resp));
	copy_query_dev_fields(file->ucontext, &resp->base, &exp_attr->base);

	resp->comp_mask = 0;
	resp->device_cap_flags2 = 0;

	if (exp_attr->exp_comp_mask & IB_EXP_DEVICE_ATTR_WITH_TIMESTAMP_MASK) {
		resp->timestamp_mask = exp_attr->base.timestamp_mask;
		resp->comp_mask |= IB_EXP_DEVICE_ATTR_WITH_TIMESTAMP_MASK;
	}

	if (exp_attr->exp_comp_mask & IB_EXP_DEVICE_ATTR_WITH_HCA_CORE_CLOCK) {
		resp->hca_core_clock = exp_attr->base.hca_core_clock;
		resp->comp_mask |= IB_EXP_DEVICE_ATTR_WITH_HCA_CORE_CLOCK;
	}

	/* Handle experimental attr fields */
	if (exp_attr->exp_comp_mask & IB_EXP_DEVICE_ATTR_CAP_FLAGS2 ||
	    exp_attr->base.device_cap_flags & IB_EXP_DEVICE_MASK) {
		resp->device_cap_flags2 = exp_attr->device_cap_flags2;
		resp->comp_mask |= IB_EXP_DEVICE_ATTR_CAP_FLAGS2;
		resp->device_cap_flags2 |= IB_EXP_DEVICE_MASK & exp_attr->base.device_cap_flags;
		resp->base.device_cap_flags &= ~IB_EXP_DEVICE_MASK;
		if (resp->device_cap_flags2 & IB_DEVICE_CROSS_CHANNEL) {
			resp->device_cap_flags2 &= ~IB_DEVICE_CROSS_CHANNEL;
			resp->device_cap_flags2 |= IB_EXP_DEVICE_CROSS_CHANNEL;
		}
	}

	if (exp_attr->exp_comp_mask & IB_EXP_DEVICE_ATTR_RSS_TBL_SZ) {
		resp->max_rss_tbl_sz = exp_attr->max_rss_tbl_sz;
		resp->comp_mask |= IB_EXP_DEVICE_ATTR_RSS_TBL_SZ;
	}

	if (exp_attr->exp_comp_mask & IB_EXP_DEVICE_ATTR_INLINE_RECV_SZ) {
		resp->inline_recv_sz = exp_attr->inline_recv_sz;
		resp->comp_mask |= IB_EXP_DEVICE_ATTR_INLINE_RECV_SZ;
	}

	ret = ib_copy_to_udata(ucore, resp, min_t(size_t, sizeof(*resp), ucore->outlen));
out:
	kfree(exp_attr);
	kfree(resp);
	return ret;
}

enum ib_uverbs_cq_exp_create_flags {
	IB_UVERBS_CQ_EXP_TIMESTAMP	= 1 << 1,
};

static u32 create_cq_exp_flags_to_ex_flags(__u64 create_flags)
{
	switch (create_flags) {
	case IB_UVERBS_CQ_EXP_TIMESTAMP:
		return IB_UVERBS_CQ_FLAGS_TIMESTAMP_COMPLETION;
	default:
		return 0;
	}
}

int ib_uverbs_exp_create_cq(struct ib_uverbs_file *file, struct ib_device *ib_dev,
			    struct ib_udata *ucore, struct ib_udata *uhw)
{
	int out_len = ucore->outlen + uhw->outlen;
	struct ib_uverbs_exp_create_cq cmd_exp;
	struct ib_uverbs_ex_create_cq	cmd_ex;
	struct ib_uverbs_create_cq_resp resp;
	int ret;
	struct ib_ucq_object           *obj;

	if (out_len < sizeof(resp))
		return -ENOSPC;

	ret = ib_copy_from_udata(&cmd_exp, ucore, sizeof(cmd_exp));
	if (ret)
		return ret;

	if (cmd_exp.comp_mask >= IB_UVERBS_EXP_CREATE_CQ_ATTR_RESERVED)
		return -ENOSYS;

	if (cmd_exp.comp_mask & IB_UVERBS_EXP_CREATE_CQ_CAP_FLAGS &&
	   /* Check that there is no bit that is not supported */
	    cmd_exp.create_flags & ~(IB_UVERBS_CQ_EXP_TIMESTAMP))
		return -EINVAL;

	memset(&cmd_ex, 0, sizeof(cmd_ex));
	cmd_ex.user_handle = cmd_exp.user_handle;
	cmd_ex.cqe = cmd_exp.cqe;
	cmd_ex.comp_vector = cmd_exp.comp_vector;
	cmd_ex.comp_channel = cmd_exp.comp_channel;
	cmd_ex.flags = create_cq_exp_flags_to_ex_flags(cmd_exp.create_flags);

	obj = create_cq(file, ucore, uhw, &cmd_ex,
			sizeof(cmd_ex), ib_uverbs_create_cq_cb,
			NULL);

	if (IS_ERR(obj))
		return PTR_ERR(obj);

	return 0;
}

int ib_uverbs_exp_modify_qp(struct ib_uverbs_file *file, struct ib_device *ib_dev,
			    struct ib_udata *ucore, struct ib_udata *uhw)
{
	struct ib_uverbs_exp_modify_qp	cmd;
	int ret;
	struct ib_qp		       *qp;
	struct ib_qp_attr	       *attr;
	u32				exp_mask;

	if (ucore->inlen < offsetof(typeof(cmd), comp_mask) +  sizeof(cmd.comp_mask))
		return -EINVAL;

	ret = ib_copy_from_udata(&cmd, ucore, min(sizeof(cmd), ucore->inlen));
	if (ret)
		return ret;

	if (cmd.comp_mask >= IB_UVERBS_EXP_QP_ATTR_RESERVED)
		return -ENOSYS;

	/* Verify that upper & lower 32 bits from user can fit into qp_attr_mask which is 32 bits
	 * and there is no overflow.
	*/
	if ((cmd.exp_attr_mask << IBV_EXP_ATTR_MASK_SHIFT >= 1ULL << 32) ||
	    (cmd.attr_mask >= IBV_EXP_QP_ATTR_FIRST))
		return -EINVAL;

	attr = kzalloc(sizeof(*attr), GFP_KERNEL);
	if (!attr)
		return -ENOMEM;

	qp = uobj_get_obj_read(qp, UVERBS_OBJECT_QP, cmd.qp_handle,
			       file);
	if (!qp) {
		kfree(attr);
		return -EINVAL;
	}

	attr->qp_state            = cmd.qp_state;
	attr->cur_qp_state        = cmd.cur_qp_state;
	attr->path_mtu            = cmd.path_mtu;
	attr->path_mig_state      = cmd.path_mig_state;
	attr->qkey                = cmd.qkey;
	attr->rq_psn              = cmd.rq_psn & 0xffffff;
	attr->sq_psn              = cmd.sq_psn & 0xffffff;
	attr->dest_qp_num         = cmd.dest_qp_num;
	attr->qp_access_flags     = cmd.qp_access_flags;
	attr->pkey_index          = cmd.pkey_index;
	attr->alt_pkey_index      = cmd.alt_pkey_index;
	attr->en_sqd_async_notify = cmd.en_sqd_async_notify;
	attr->max_rd_atomic       = cmd.max_rd_atomic;
	attr->max_dest_rd_atomic  = cmd.max_dest_rd_atomic;
	attr->min_rnr_timer       = cmd.min_rnr_timer;
	attr->port_num            = cmd.port_num;
	attr->timeout             = cmd.timeout;
	attr->retry_cnt           = cmd.retry_cnt;
	attr->rnr_retry           = cmd.rnr_retry;
	attr->alt_port_num        = cmd.alt_port_num;
	attr->alt_timeout         = cmd.alt_timeout;

	copy_ah_attr_from_uverbs(qp->device, &attr->ah_attr, &cmd.dest);
	copy_ah_attr_from_uverbs(qp->device, &attr->alt_ah_attr,
				 &cmd.alt_dest);

	if (cmd.comp_mask & IB_UVERBS_EXP_QP_ATTR_FLOW_ENTROPY) {
		if (offsetof(typeof(cmd), flow_entropy) + sizeof(cmd.flow_entropy) <= ucore->inlen) {
			attr->flow_entropy = cmd.flow_entropy;
		} else {
			ret = -EINVAL;
			goto out;
		}
	}

	exp_mask = (cmd.exp_attr_mask << IBV_EXP_ATTR_MASK_SHIFT) & IBV_EXP_QP_ATTR_MASK;

	ret = ib_modify_qp_with_udata(qp, attr,
				      modify_qp_mask(qp->qp_type, cmd.attr_mask | exp_mask),
				      uhw);

out:
	uobj_put_obj_read(qp);
	kfree(attr);

	return ret;
}