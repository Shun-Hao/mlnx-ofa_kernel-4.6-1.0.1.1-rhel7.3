/*
 * Copyright (c) 2017, Mellanox Technologies, Ltd.  All rights reserved.
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

#ifndef __MLX5_FPGA_H__
#define __MLX5_FPGA_H__

#include <linux/in6.h>
#include <linux/mlx5/driver.h>

enum mlx5_fpga_id {
	MLX5_FPGA_NEWTON = 0,
	MLX5_FPGA_EDISON = 1,
	MLX5_FPGA_MORSE = 2,
	MLX5_FPGA_MORSEQ = 3,
};

enum mlx5_fpga_image {
	MLX5_FPGA_IMAGE_USER = 0,
	MLX5_FPGA_IMAGE_FACTORY = 1,
	MLX5_FPGA_IMAGE_MAX = MLX5_FPGA_IMAGE_FACTORY,
	MLX5_FPGA_IMAGE_FACTORY_FAILOVER = 2,
};

enum mlx5_fpga_status {
	MLX5_FPGA_STATUS_SUCCESS = 0,
	MLX5_FPGA_STATUS_FAILURE = 1,
	MLX5_FPGA_STATUS_IN_PROGRESS = 2,
	MLX5_FPGA_STATUS_DISCONNECTED = 3,
};

enum mlx5_fpga_tee {
	MLX5_FPGA_TEE_DISABLE = 0,
	MLX5_FPGA_TEE_GENERATE_EVENT = 1,
	MLX5_FPGA_TEE_GENERATE_SINGLE_EVENT = 2,
};

struct mlx5_fpga_query {
	enum mlx5_fpga_image admin_image;
	enum mlx5_fpga_image oper_image;
	enum mlx5_fpga_status image_status;
};

enum mlx5_fpga_qpc_field_select {
	MLX5_FPGA_QPC_STATE = BIT(0),
};

struct mlx5_fpga_qp_counters {
	u64 rx_ack_packets;
	u64 rx_send_packets;
	u64 tx_ack_packets;
	u64 tx_send_packets;
	u64 rx_total_drop;
};

struct mlx5_fpga_shell_counters {
	u64 ddr_read_requests;
	u64 ddr_write_requests;
	u64 ddr_read_bytes;
	u64 ddr_write_bytes;
};

#define MLX5_FPGA_INTERNAL_SENSORS_LOW 63
#define MLX5_FPGA_INTERNAL_SENSORS_HIGH 63

struct mlx5_fpga_temperature {
	u32 temperature;
	u32 index;
	u32 tee;
	u32 max_temperature;
	u32 temperature_threshold_hi;
	u32 temperature_threshold_lo;
	u32 mte;
	u32 mtr;
	char sensor_name[16];
};

int mlx5_fpga_caps(struct mlx5_core_dev *dev);
int mlx5_fpga_query(struct mlx5_core_dev *dev, struct mlx5_fpga_query *query);
int mlx5_fpga_query_mtmp(struct mlx5_core_dev *dev,
			 struct mlx5_fpga_temperature *temp);
int mlx5_fpga_ctrl_op(struct mlx5_core_dev *dev, u8 op);
int mlx5_fpga_access_reg(struct mlx5_core_dev *dev, u8 size, u64 addr,
			 void *buf, bool write);
int mlx5_fpga_sbu_caps(struct mlx5_core_dev *dev, void *caps, int size);
int mlx5_fpga_load(struct mlx5_core_dev *dev, enum mlx5_fpga_image image);
int mlx5_fpga_image_select(struct mlx5_core_dev *dev,
			   enum mlx5_fpga_image image);
int mlx5_fpga_shell_counters(struct mlx5_core_dev *dev, bool clear,
			     struct mlx5_fpga_shell_counters *data);

int mlx5_fpga_create_qp(struct mlx5_core_dev *dev, void *fpga_qpc,
			u32 *fpga_qpn);
int mlx5_fpga_modify_qp(struct mlx5_core_dev *dev, u32 fpga_qpn,
			enum mlx5_fpga_qpc_field_select fields, void *fpga_qpc);
int mlx5_fpga_query_qp(struct mlx5_core_dev *dev, u32 fpga_qpn, void *fpga_qpc);
int mlx5_fpga_query_qp_counters(struct mlx5_core_dev *dev, u32 fpga_qpn,
				bool clear, struct mlx5_fpga_qp_counters *data);
int mlx5_fpga_destroy_qp(struct mlx5_core_dev *dev, u32 fpga_qpn);

#endif /* __MLX5_FPGA_H__ */
