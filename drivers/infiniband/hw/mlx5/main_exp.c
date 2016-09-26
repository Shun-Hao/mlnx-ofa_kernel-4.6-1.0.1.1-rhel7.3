/*
 * Copyright (c) 2016, Mellanox Technologies. All rights reserved.
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

#include <linux/highmem.h>
#include "mlx5_ib.h"

int mlx5_ib_exp_query_device(struct ib_device *ibdev,
			     struct ib_exp_device_attr *props,
			     struct ib_udata *uhw)
{
	struct mlx5_ib_dev *dev = to_mdev(ibdev);
	int ret;

	ret = mlx5_ib_query_device(ibdev, &props->base, uhw);
	if (ret)
		return ret;

	props->exp_comp_mask = 0;
	props->device_cap_flags2 = 0;
	props->exp_comp_mask |= IB_EXP_DEVICE_ATTR_CAP_FLAGS2;

	props->exp_comp_mask |= IB_EXP_DEVICE_ATTR_WITH_TIMESTAMP_MASK |
		IB_EXP_DEVICE_ATTR_WITH_HCA_CORE_CLOCK;

	props->exp_comp_mask |= IB_EXP_DEVICE_ATTR_DC_REQ_RD;
	props->exp_comp_mask |= IB_EXP_DEVICE_ATTR_DC_RES_RD;
	props->exp_comp_mask |= IB_EXP_DEVICE_ATTR_MAX_DCT;
	if (MLX5_CAP_GEN(dev->mdev, dct)) {
		props->device_cap_flags2 |= IB_EXP_DEVICE_DC_TRANSPORT;
		props->dc_rd_req = 1 << MLX5_CAP_GEN(dev->mdev, log_max_ra_req_dc);
		props->dc_rd_res = 1 << MLX5_CAP_GEN(dev->mdev, log_max_ra_res_dc);
		props->max_dct = props->base.max_qp;
	} else {
		props->dc_rd_req = 0;
		props->dc_rd_res = 0;
		props->max_dct = 0;
	}
	props->exp_comp_mask |= IB_EXP_DEVICE_ATTR_INLINE_RECV_SZ;
	if (MLX5_CAP_GEN(dev->mdev, sctr_data_cqe))
		props->inline_recv_sz = MLX5_MAX_INLINE_RECEIVE_SIZE;
	else
		props->inline_recv_sz = 0;

	return 0;
}

void mlx5_ib_enable_dc_tracer(struct mlx5_ib_dev *dev)
{
	struct device *device = dev->ib_dev.dma_device;
	struct mlx5_dc_tracer *dct = &dev->dctr;
	int order;
	void *tmp;
	int size;
	int err;

	size = MLX5_CAP_GEN(dev->mdev, num_ports) * 4096;
	if (size <= PAGE_SIZE)
		order = 0;
	else
		order = 1;

	dct->pg = alloc_pages(GFP_KERNEL, order);
	if (!dct->pg) {
		mlx5_ib_err(dev, "failed to allocate %d pages\n", order);
		return;
	}

	tmp = kmap(dct->pg);
	if (!tmp) {
		mlx5_ib_err(dev, "failed to kmap one page\n");
		err = -ENOMEM;
		goto map_err;
	}

	memset(tmp, 0xff, size);
	kunmap(dct->pg);

	dct->size = size;
	dct->order = order;
	dct->dma = dma_map_page(device, dct->pg, 0, size, DMA_FROM_DEVICE);
	if (dma_mapping_error(device, dct->dma)) {
		mlx5_ib_err(dev, "dma mapping error\n");
		goto map_err;
	}

	err = mlx5_core_set_dc_cnak_trace(dev->mdev, 1, dct->dma);
	if (err) {
		mlx5_ib_warn(dev, "failed to enable DC tracer\n");
		goto cmd_err;
	}

	return;

cmd_err:
	dma_unmap_page(device, dct->dma, size, DMA_FROM_DEVICE);
map_err:
	__free_pages(dct->pg, dct->order);
	dct->pg = NULL;
}

void mlx5_ib_disable_dc_tracer(struct mlx5_ib_dev *dev)
{
	struct device *device = dev->ib_dev.dma_device;
	struct mlx5_dc_tracer *dct = &dev->dctr;
	int err;

	if (!dct->pg)
		return;

	err = mlx5_core_set_dc_cnak_trace(dev->mdev, 0, dct->dma);
	if (err) {
		mlx5_ib_warn(dev, "failed to disable DC tracer\n");
		return;
	}

	dma_unmap_page(device, dct->dma, dct->size, DMA_FROM_DEVICE);
	__free_pages(dct->pg, dct->order);
	dct->pg = NULL;
}

int mlx5_ib_mmap_dc_info_page(struct mlx5_ib_dev *dev,
			      struct vm_area_struct *vma)
{
	struct mlx5_dc_tracer *dct;
	phys_addr_t pfn;
	int err;

	if ((MLX5_CAP_GEN(dev->mdev, port_type) !=
	     MLX5_CAP_PORT_TYPE_IB) ||
	    (!mlx5_core_is_pf(dev->mdev)) ||
	    (!MLX5_CAP_GEN(dev->mdev, dc_cnak_trace)))
		return -ENOTSUPP;

	dct = &dev->dctr;
	if (!dct->pg) {
		mlx5_ib_err(dev, "mlx5_ib_mmap DC no page\n");
		return -ENOMEM;
	}

	pfn = page_to_pfn(dct->pg);
	err = remap_pfn_range(vma, vma->vm_start, pfn, dct->size, vma->vm_page_prot);
	if (err) {
		mlx5_ib_err(dev, "mlx5_ib_mmap DC remap_pfn_range failed\n");
		return err;
	}
	return 0;
}
