/*
 * Copyright (c) 2017, Mellanox Technologies. All rights reserved.
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

#include <linux/mlx5/driver.h>
#include <linux/mlx5/srq.h>
#include "srq_exp.h"

int get_nvmf_pas_size(struct mlx5_nvmf_attr *nvmf)
{
	return nvmf->staging_buffer_number_of_pages * sizeof(u64);
}

void set_nvmf_srq_pas(struct mlx5_nvmf_attr *nvmf, __be64 *pas)
{
	int i;

	for (i = 0; i < nvmf->staging_buffer_number_of_pages; i++)
		pas[i] = cpu_to_be64(nvmf->staging_buffer_pas[i]);
}

void set_nvmf_xrq_context(struct mlx5_nvmf_attr *nvmf, void *xrqc)
{
	MLX5_SET(xrqc, xrqc,
		 nvme_offload_context.nvmf_offload_type,
		 nvmf->type);
	MLX5_SET(xrqc, xrqc,
		 nvme_offload_context.log_max_namespace,
		 nvmf->log_max_namespace);
	MLX5_SET(xrqc, xrqc,
		 nvme_offload_context.offloaded_capsules_count,
		 nvmf->offloaded_capsules_count);
	MLX5_SET(xrqc, xrqc,
		 nvme_offload_context.ioccsz,
		 nvmf->ioccsz);
	MLX5_SET(xrqc, xrqc,
		 nvme_offload_context.icdoff,
		 nvmf->icdoff);
	MLX5_SET(xrqc, xrqc,
		 nvme_offload_context.log_max_io_size,
		 nvmf->log_max_io_size);
	MLX5_SET(xrqc, xrqc,
		 nvme_offload_context.nvme_memory_log_page_size,
		 nvmf->nvme_memory_log_page_size);
	MLX5_SET(xrqc, xrqc,
		 nvme_offload_context.staging_buffer_log_page_size,
		 nvmf->staging_buffer_log_page_size);
	MLX5_SET(xrqc, xrqc,
		 nvme_offload_context.staging_buffer_number_of_pages,
		 nvmf->staging_buffer_number_of_pages);
	MLX5_SET(xrqc, xrqc,
		 nvme_offload_context.staging_buffer_page_offset,
		 nvmf->staging_buffer_page_offset);
	MLX5_SET(xrqc, xrqc,
		 nvme_offload_context.nvme_queue_size,
		 nvmf->nvme_queue_size);
}

int set_xrq_dc_params_entry(struct mlx5_core_dev *dev,
			    struct mlx5_core_srq *srq,
			    struct mlx5_dc_offload_params *dc_op)
{
	u32 in[MLX5_ST_SZ_DW(set_xrq_dc_params_entry_in)] = {0};
	u32 out[MLX5_ST_SZ_DW(set_xrq_dc_params_entry_out)] = {0};

	MLX5_SET(set_xrq_dc_params_entry_in, in, pkey_table_index,
		 dc_op->pkey_index);
	MLX5_SET(set_xrq_dc_params_entry_in, in, mtu, dc_op->path_mtu);
	MLX5_SET(set_xrq_dc_params_entry_in, in, sl, dc_op->sl);
	MLX5_SET(set_xrq_dc_params_entry_in, in, reverse_sl, dc_op->sl);
	MLX5_SET(set_xrq_dc_params_entry_in, in, cnak_reverse_sl, dc_op->sl);
	MLX5_SET(set_xrq_dc_params_entry_in, in, ack_timeout, dc_op->timeout);
	MLX5_SET64(set_xrq_dc_params_entry_in, in, dc_access_key,
		   dc_op->dct_key);

	MLX5_SET(set_xrq_dc_params_entry_in, in, xrqn, srq->srqn);
	MLX5_SET(set_xrq_dc_params_entry_in, in, opcode,
		 MLX5_CMD_OP_SET_XRQ_DC_PARAMS_ENTRY);

	return mlx5_cmd_exec(dev, in, sizeof(in), out, sizeof(out));
}

