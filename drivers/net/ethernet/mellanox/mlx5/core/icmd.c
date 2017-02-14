/*
 * Copyright (c) 2013-2017, Mellanox Technologies. All rights reserved.
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

#include <linux/module.h>
#include <linux/errno.h>
#include <linux/pci.h>
#include <linux/jiffies.h>
#include <linux/mlx5/driver.h>
#include "mlx5_core.h"
#include "icmd.h"

enum {
	MLX5_VENDOR_SPECIFIC_PCIE_CAP_ID	= 0x9,
};

#define CAP_ADDR(__off) (icmd->cap_addr + (__off))

enum {
	STAT_SPACE	= 4,
	COUNTER		= 8,
	SEMAPHORE	= 0xc,
	GW_ADDR		= 0x10,
	GW_DATA		= 0x14,
};

enum {
	ABORT_TIME_MILI		= 2000,
	CMD_POLL_TIME_MILI	= 10000,
};

enum {
	DOMAIN_ICMD		= 1,
	DOMAIN_CRSPACE		= 2,
	DOMAIN_ALL_ICMD		= 3,
	DOMAIN_NODNIC		= 4,
	DOMAIN_EXPROM		= 5,
	DOMAIN_SEMAPHORES	= 0xa,
};

enum {
	SEM_ICMD_ADDR		= 0,
};

enum {
	ICMD_ADDR_CTRL		= 0,
	ICMD_ADDR_MBOX_SZ	= 0x1000,
	ICMD_ADDR_TOOLS_FLAGS	= 0x1004,
	ICMD_ADDR_SYNDROME	= 0x1008,
	ICMD_ADDR_MBOX		= 0x100000,
};

static void release_icmd_sem(struct mlx5_icmd *icmd);

/* Gateway ownership */
static int acquire_vsec_sem(struct mlx5_icmd *icmd)
{
	struct mlx5_core_dev *dev = container_of(icmd, struct mlx5_core_dev, icmd);
	unsigned long start = jiffies;
	u32 reread;
	u32 val;
	int err;

	while (true) {
		/* Step 1 */
		err = pci_read_config_dword(dev->pdev, CAP_ADDR(SEMAPHORE), &val);
		if (err)
			return err;

		if (val) {
			if (time_after(jiffies, start + msecs_to_jiffies(ABORT_TIME_MILI)))
				return -ETIMEDOUT;
			msleep(10);
			continue;
		}

		/* Step 2 */
		err = pci_read_config_dword(dev->pdev, CAP_ADDR(COUNTER), &val);
		if (err)
			return err;

		err = pci_write_config_dword(dev->pdev, CAP_ADDR(SEMAPHORE), val);
		if (err)
			return err;

		/* Step 3 */
		err = pci_read_config_dword(dev->pdev, CAP_ADDR(SEMAPHORE), &reread);
		if (err)
			return err;

		if (reread == val)
			return 0;
	}
}

static void release_vsec_sem(struct mlx5_icmd *icmd)
{
	struct mlx5_core_dev *dev = container_of(icmd, struct mlx5_core_dev, icmd);

	if (pci_write_config_dword(dev->pdev, CAP_ADDR(SEMAPHORE), 0))
		mlx5_core_warn(dev, "failed to release semaphore\n");
}

static int gw_write(struct mlx5_icmd *icmd, u32 addr, u32 value)
{
	struct mlx5_core_dev *dev = container_of(icmd, struct mlx5_core_dev, icmd);
	unsigned long end = jiffies + msecs_to_jiffies(ABORT_TIME_MILI);
	int err;

	err = pci_write_config_dword(dev->pdev, CAP_ADDR(GW_DATA), value);
	if (err)
		return err;

	err = pci_write_config_dword(dev->pdev, CAP_ADDR(GW_ADDR), addr | 0x80000000);
	if (err)
		return err;

	while (!time_after(jiffies, end)) {
		err = pci_read_config_dword(dev->pdev, CAP_ADDR(GW_ADDR), &value);
		if (err)
			return err;

		if (!(value & 0x80000000))
			return 0;
	}
	return -ETIMEDOUT;
}

static int gw_read(struct mlx5_icmd *icmd, u32 addr, u32 *value)
{
	struct mlx5_core_dev *dev = container_of(icmd, struct mlx5_core_dev, icmd);
	unsigned long end = jiffies + msecs_to_jiffies(ABORT_TIME_MILI);
	int err;

	err = pci_write_config_dword(dev->pdev, CAP_ADDR(GW_ADDR), addr);
	if (err)
		return err;

	while (!time_after(jiffies, end)) {
		err = pci_read_config_dword(dev->pdev, CAP_ADDR(GW_ADDR), value);
		if (err)
			return err;

		if (*value & 0x80000000)
			return pci_read_config_dword(dev->pdev, CAP_ADDR(GW_DATA), value);

		msleep(20);
	}
	return -ETIMEDOUT;
}

static int select_icmd_space(struct mlx5_icmd *icmd)
{
	struct mlx5_core_dev *dev = container_of(icmd, struct mlx5_core_dev, icmd);

	return pci_write_config_dword(dev->pdev, CAP_ADDR(STAT_SPACE), DOMAIN_ICMD);
}

static int select_sem_space(struct mlx5_icmd *icmd)
{
	struct mlx5_core_dev *dev = container_of(icmd, struct mlx5_core_dev, icmd);

	return pci_write_config_dword(dev->pdev, CAP_ADDR(STAT_SPACE), DOMAIN_SEMAPHORES);
}

static int get_icmd_syndrome(struct mlx5_icmd *icmd, u32 *synd)
{
	return gw_read(icmd, ICMD_ADDR_SYNDROME, synd);
}

int poll_cmd(struct mlx5_icmd *icmd)
{
	struct mlx5_core_dev *dev = container_of(icmd, struct mlx5_core_dev, icmd);
	unsigned long end = jiffies + msecs_to_jiffies(5000);
	u8 status;
	u32 synd;
	u32 val;
	int err;

	while (1) {
		err = select_icmd_space(icmd);
		if (err)
			goto error;

		err = gw_read(icmd, ICMD_ADDR_CTRL, &val);
		if (err)
			goto error;

		if (!(val & 1)) {
			status = (val >> 8) & 0xff;
			if (status) {
				mlx5_core_warn(dev, "icmd failed with status 0x%x\n", status);
				err = get_icmd_syndrome(icmd, &synd);
				if (err)
					mlx5_core_warn(dev, "failed to get icmd syndrom\n");
				else
					mlx5_core_warn(dev, "icmd syndrome 0x%08x\n", synd);
			}

			return status;
		}

		release_vsec_sem(icmd);

		if (time_after(jiffies, end))
			return -ETIMEDOUT;

		msleep(20);

		err = acquire_vsec_sem(icmd);
		if (err)
			return err;
	}

error:
	release_icmd_sem(icmd);
	release_vsec_sem(icmd);
	return err;
}

static int verify_space(struct mlx5_icmd *icmd, u16 space)
{
	struct mlx5_core_dev *dev = container_of(icmd, struct mlx5_core_dev, icmd);
	u32 val;
	int err;

	err = pci_write_config_dword(dev->pdev, CAP_ADDR(STAT_SPACE), space);
	if (err)
		return err;

	err = pci_read_config_dword(dev->pdev, CAP_ADDR(STAT_SPACE), &val);
	if (err)
		return err;

	if ((val >> 29) != 1)
		return -EINVAL;

	return 0;
}

static int verify_icmd_space(struct mlx5_icmd *icmd)
{
	return verify_space(icmd, DOMAIN_ICMD);
}

/* Must hold vsec semaphore */
static int acquire_icmd_sem_no_lock_vsec(struct mlx5_icmd *icmd)
{
	struct mlx5_core_dev *dev = container_of(icmd, struct mlx5_core_dev, icmd);
	unsigned long end = jiffies + msecs_to_jiffies(ABORT_TIME_MILI);
	pid_t pid = task_tgid_vnr(current);
	u32 value;
	int err;

	while (1) {
		err = select_sem_space(icmd);
		if (err) {
			mlx5_core_warn(dev, "failed to acquire vsec sem\n");
			goto error;
		}

		err = gw_write(icmd, SEM_ICMD_ADDR, pid);
		if (err) {
			mlx5_core_warn(dev, "failed to write pid\n");
			goto error;
		}

		err = gw_read(icmd, SEM_ICMD_ADDR, &value);
		if (err) {
			mlx5_core_warn(dev, "failed to read pid\n");
			goto error;
		}

		if (value == pid)
			return 0;

		release_vsec_sem(icmd);

		if (time_after(jiffies, end))
			return -EBUSY;

		msleep(20);

		err = acquire_vsec_sem(icmd);
		if (err) {
			mlx5_core_warn(dev, "failed to acquire vsec sem\n");
			return err;
		}
	}

error:
	release_vsec_sem(icmd);
	return -EBUSY;
}

/* grab both vsec and icmd sem lock. */
static int acquire_icmd_sem(struct mlx5_icmd *icmd)
{
	struct mlx5_core_dev *dev = container_of(icmd, struct mlx5_core_dev, icmd);
	int err;

	err = acquire_vsec_sem(icmd);
	if (err) {
		mlx5_core_warn(dev, "failed to acquire vsec sem\n");
		return err;
	}

	return acquire_icmd_sem_no_lock_vsec(icmd);
}

/* Only release icmd sem. Must hold vsec semaphore */
static void release_icmd_sem(struct mlx5_icmd *icmd)
{
	struct mlx5_core_dev *dev = container_of(icmd, struct mlx5_core_dev, icmd);
	int err;

	err = select_sem_space(icmd);
	if (err)
		return;

	err = gw_write(icmd, SEM_ICMD_ADDR, 0);
	if (err)
		mlx5_core_warn(dev, "failed to release icmd semaphore\n");
}

static void clear_mbox(struct mlx5_icmd *icmd, int offset)
{
	struct mlx5_core_dev *dev = container_of(icmd, struct mlx5_core_dev, icmd);
	int err = 0;
	int i;

	for (i = offset; i < icmd->mbox_size; i += 4)
		err |= gw_write(icmd, ICMD_ADDR_MBOX + i, 0);

	if (err)
		mlx5_core_warn(dev, "failed to clear mailbox\n");
}

int mlx5_icmd_exec(struct mlx5_icmd *icmd, u16 opcode, void *inbox,
		   int in_dw_sz, void *outbox, int out_dw_sz)
{
	struct mlx5_core_dev *dev = container_of(icmd, struct mlx5_core_dev, icmd);
	u32 *data;
	u32 addr;
	int err;
	u32 opw;
	int i;

	if (!icmd->initialized)
		return -EINVAL;

	/* Note that acquire_icmd_sem may lose vsec semaphore */
	err = acquire_icmd_sem(icmd);
	if (err) {
		mlx5_core_warn(dev, "failed to acquire icmd sem\n");
		return err;
	}

	err = select_icmd_space(icmd);
	if (err) {
		mlx5_core_warn(dev, "failed to select icmd space\n");
		goto out;
	}

	data = inbox;
	for (i = 0; i < in_dw_sz; i++) {
		addr = ICMD_ADDR_MBOX + i * 4;
		err = gw_write(icmd, addr, data[i]);
		if (err) {
			mlx5_core_warn(dev, "failed to write 0x%x to address 0x%x\n", data[i], addr);
			goto out;
		}
	}
	clear_mbox(icmd, in_dw_sz * 4);

	opw = (opcode << 16) | 1;
	err = gw_write(icmd, ICMD_ADDR_CTRL, opw);
	if (err) {
		mlx5_core_warn(dev, "failed to write opcode word 0x%x\n", opw);
		goto out;
	}

	/* Note that poll_cmd may lose vsec semaphore */
	err = poll_cmd(icmd);
	if (err) {
		mlx5_core_warn(dev, "poll command failed\n");
		return err;
	}

	data = outbox;
	for (i = 0; i < out_dw_sz; i++) {
		addr = ICMD_ADDR_MBOX + i * 4;
		err = gw_read(icmd, addr, &data[i]);
		if (err) {
			mlx5_core_warn(dev, "read from address 0x%x failed\n", addr);
			goto out;
		}
	}

out:
	release_icmd_sem(icmd);
	release_vsec_sem(icmd);
	return err;
}

static int get_cap_addr(struct mlx5_icmd *icmd)
{
	struct mlx5_core_dev *dev = container_of(icmd, struct mlx5_core_dev, icmd);

	icmd->cap_addr = pci_find_capability(dev->pdev, MLX5_VENDOR_SPECIFIC_PCIE_CAP_ID);
	if (!icmd->cap_addr)
		return -ENODEV;

	return 0;
}

static int verify_sem_space(struct mlx5_icmd *icmd)
{
	return verify_space(icmd, DOMAIN_SEMAPHORES);
}

static int update_mbox_size(struct mlx5_icmd *icmd)
{
	int err;

	err = select_icmd_space(icmd);
	if (err)
		return err;

	return gw_read(icmd, ICMD_ADDR_MBOX_SZ, &icmd->mbox_size);
}

int mlx5_icmd_init(struct mlx5_core_dev *dev)
{
	struct mlx5_icmd *icmd;
	int err;

	icmd = &dev->icmd;
	err = get_cap_addr(icmd);
	if (err)
		return err;

	err = acquire_vsec_sem(icmd);
	if (err)
		return err;

	/* verify icmd space is supported */
	err = verify_icmd_space(icmd);
	if (err)
		goto out;

	/* verify semaphore space is supported */
	err = verify_sem_space(icmd);
	if (err)
		goto out;

	/* vsec semaphore is released if error */
	err = acquire_icmd_sem_no_lock_vsec(icmd);
	if (err)
		return err;

	err = update_mbox_size(icmd);
	if (err) {
		mlx5_core_warn(dev, "failed to get mailbox size\n");
		goto out_icmd;
	}

	icmd->initialized = true;

out_icmd:
	release_icmd_sem(icmd);

out:
	release_vsec_sem(icmd);
	return err;
}

void mlx5_icmd_cleanup(struct mlx5_core_dev *dev)
{
	/* Nothing for now */
}
