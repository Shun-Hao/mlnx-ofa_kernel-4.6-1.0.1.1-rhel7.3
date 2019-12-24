/*
 * Copyright (c) 2015, Mellanox Technologies. All rights reserved.
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

#include <linux/list.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/tcp.h>
#include <linux/mlx5/fs.h>
#include <net/vxlan.h>
#include "en.h"
#include "lib/mpfs.h"
#include "fs_core.h"
#include "net/ip_tunnels.h"

static int mlx5e_add_l2_flow_rule(struct mlx5e_priv *priv,
				  struct mlx5e_l2_rule *ai, int type);
static void mlx5e_del_l2_flow_rule(struct mlx5e_priv *priv,
				   struct mlx5e_l2_rule *ai);

enum {
	MLX5E_FULLMATCH = 0,
	MLX5E_ALLMULTI  = 1,
	MLX5E_PROMISC   = 2,
};

enum {
	MLX5E_UC        = 0,
	MLX5E_MC_IPV4   = 1,
	MLX5E_MC_IPV6   = 2,
	MLX5E_MC_OTHER  = 3,
};

enum {
	MLX5E_ACTION_NONE = 0,
	MLX5E_ACTION_ADD  = 1,
	MLX5E_ACTION_DEL  = 2,
};

struct mlx5e_l2_hash_node {
	struct hlist_node          hlist;
	u8                         action;
	struct mlx5e_l2_rule ai;
	bool   mpfs;
};

static inline int mlx5e_hash_l2(u8 *addr)
{
	return addr[5];
}

static void mlx5e_add_l2_to_hash(struct hlist_head *hash, u8 *addr)
{
	struct mlx5e_l2_hash_node *hn;
	int ix = mlx5e_hash_l2(addr);
	int found = 0;
	COMPAT_HL_NODE

	compat_hlist_for_each_entry(hn, &hash[ix], hlist)
		if (ether_addr_equal_64bits(hn->ai.addr, addr)) {
			found = 1;
			break;
		}

	if (found) {
		hn->action = MLX5E_ACTION_NONE;
		return;
	}

	hn = kzalloc(sizeof(*hn), GFP_ATOMIC);
	if (!hn)
		return;

	ether_addr_copy(hn->ai.addr, addr);
	hn->action = MLX5E_ACTION_ADD;

	hlist_add_head(&hn->hlist, &hash[ix]);
}

static void mlx5e_del_l2_from_hash(struct mlx5e_l2_hash_node *hn)
{
	hlist_del(&hn->hlist);
	kfree(hn);
}

static int mlx5e_vport_context_update_vlans(struct mlx5e_priv *priv)
{
	struct net_device *ndev = priv->netdev;
	int max_list_size;
	int list_size;
	u16 *vlans;
	int vlan;
	int err;
	int i;

	list_size = 0;
	for_each_set_bit(vlan, priv->fs.vlan.active_cvlans, VLAN_N_VID)
		list_size++;

	max_list_size = 1 << MLX5_CAP_GEN(priv->mdev, log_max_vlan_list);

	if (list_size > max_list_size) {
		netdev_warn(ndev,
			    "netdev vlans list size (%d) > (%d) max vport list size, some vlans will be dropped\n",
			    list_size, max_list_size);
		list_size = max_list_size;
	}

	vlans = kcalloc(list_size, sizeof(*vlans), GFP_KERNEL);
	if (!vlans)
		return -ENOMEM;

	i = 0;
	for_each_set_bit(vlan, priv->fs.vlan.active_cvlans, VLAN_N_VID) {
		if (i >= list_size)
			break;
		vlans[i++] = vlan;
	}

	err = mlx5_modify_nic_vport_vlans(priv->mdev, vlans, list_size);
	if (err)
		netdev_err(ndev, "Failed to modify vport vlans list err(%d)\n",
			   err);

	kfree(vlans);
	return err;
}

enum mlx5e_vlan_rule_type {
	MLX5E_VLAN_RULE_TYPE_UNTAGGED,
	MLX5E_VLAN_RULE_TYPE_ANY_CTAG_VID,
	MLX5E_VLAN_RULE_TYPE_ANY_STAG_VID,
	MLX5E_VLAN_RULE_TYPE_MATCH_CTAG_VID,
#ifdef HAVE_NETIF_F_HW_VLAN_STAG_RX
	MLX5E_VLAN_RULE_TYPE_MATCH_STAG_VID,
#endif
};

static int __mlx5e_add_vlan_rule(struct mlx5e_priv *priv,
				 enum mlx5e_vlan_rule_type rule_type,
				 u16 vid, struct mlx5_flow_spec *spec)
{
	struct mlx5_flow_table *ft = priv->fs.vlan.ft.t;
	struct mlx5_flow_destination dest = {};
	struct mlx5_flow_handle **rule_p;
	MLX5_DECLARE_FLOW_ACT(flow_act);
	int err = 0;

	dest.type = MLX5_FLOW_DESTINATION_TYPE_FLOW_TABLE;
	dest.ft = priv->fs.l2.ft.t;

	spec->match_criteria_enable = MLX5_MATCH_OUTER_HEADERS;

	switch (rule_type) {
	case MLX5E_VLAN_RULE_TYPE_UNTAGGED:
		/* cvlan_tag enabled in match criteria and
		 * disabled in match value means both S & C tags
		 * don't exist (untagged of both)
		 */
		rule_p = &priv->fs.vlan.untagged_rule;
		MLX5_SET_TO_ONES(fte_match_param, spec->match_criteria,
				 outer_headers.cvlan_tag);
		break;
	case MLX5E_VLAN_RULE_TYPE_ANY_CTAG_VID:
		rule_p = &priv->fs.vlan.any_cvlan_rule;
		MLX5_SET_TO_ONES(fte_match_param, spec->match_criteria,
				 outer_headers.cvlan_tag);
		MLX5_SET(fte_match_param, spec->match_value, outer_headers.cvlan_tag, 1);
		break;
	case MLX5E_VLAN_RULE_TYPE_ANY_STAG_VID:
		rule_p = &priv->fs.vlan.any_svlan_rule;
		MLX5_SET_TO_ONES(fte_match_param, spec->match_criteria,
				 outer_headers.svlan_tag);
		MLX5_SET(fte_match_param, spec->match_value, outer_headers.svlan_tag, 1);
		break;
#ifdef HAVE_NETIF_F_HW_VLAN_STAG_RX
	case MLX5E_VLAN_RULE_TYPE_MATCH_STAG_VID:
		rule_p = &priv->fs.vlan.active_svlans_rule[vid];
		MLX5_SET_TO_ONES(fte_match_param, spec->match_criteria,
				 outer_headers.svlan_tag);
		MLX5_SET(fte_match_param, spec->match_value, outer_headers.svlan_tag, 1);
		MLX5_SET_TO_ONES(fte_match_param, spec->match_criteria,
				 outer_headers.first_vid);
		MLX5_SET(fte_match_param, spec->match_value, outer_headers.first_vid,
			 vid);
		break;
#endif
	default: /* MLX5E_VLAN_RULE_TYPE_MATCH_CTAG_VID */
		rule_p = &priv->fs.vlan.active_cvlans_rule[vid];
		MLX5_SET_TO_ONES(fte_match_param, spec->match_criteria,
				 outer_headers.cvlan_tag);
		MLX5_SET(fte_match_param, spec->match_value, outer_headers.cvlan_tag, 1);
		MLX5_SET_TO_ONES(fte_match_param, spec->match_criteria,
				 outer_headers.first_vid);
		MLX5_SET(fte_match_param, spec->match_value, outer_headers.first_vid,
			 vid);
		break;
	}

	*rule_p = mlx5_add_flow_rules(ft, spec, &flow_act, &dest, 1);

	if (IS_ERR(*rule_p)) {
		err = PTR_ERR(*rule_p);
		*rule_p = NULL;
		netdev_err(priv->netdev, "%s: add rule failed\n", __func__);
	}

	return err;
}

static int mlx5e_add_vlan_rule(struct mlx5e_priv *priv,
			       enum mlx5e_vlan_rule_type rule_type, u16 vid)
{
	struct mlx5_flow_spec *spec;
	int err = 0;

	spec = kvzalloc(sizeof(*spec), GFP_KERNEL);
	if (!spec)
		return -ENOMEM;

	if (rule_type == MLX5E_VLAN_RULE_TYPE_MATCH_CTAG_VID)
		mlx5e_vport_context_update_vlans(priv);

	err = __mlx5e_add_vlan_rule(priv, rule_type, vid, spec);

	kvfree(spec);

	return err;
}

static void mlx5e_del_vlan_rule(struct mlx5e_priv *priv,
				enum mlx5e_vlan_rule_type rule_type, u16 vid)
{
	switch (rule_type) {
	case MLX5E_VLAN_RULE_TYPE_UNTAGGED:
		if (priv->fs.vlan.untagged_rule) {
			mlx5_del_flow_rules(priv->fs.vlan.untagged_rule);
			priv->fs.vlan.untagged_rule = NULL;
		}
		break;
	case MLX5E_VLAN_RULE_TYPE_ANY_CTAG_VID:
		if (priv->fs.vlan.any_cvlan_rule) {
			mlx5_del_flow_rules(priv->fs.vlan.any_cvlan_rule);
			priv->fs.vlan.any_cvlan_rule = NULL;
		}
		break;
	case MLX5E_VLAN_RULE_TYPE_ANY_STAG_VID:
		if (priv->fs.vlan.any_svlan_rule) {
			mlx5_del_flow_rules(priv->fs.vlan.any_svlan_rule);
			priv->fs.vlan.any_svlan_rule = NULL;
		}
		break;
#ifdef HAVE_NETIF_F_HW_VLAN_STAG_RX
	case MLX5E_VLAN_RULE_TYPE_MATCH_STAG_VID:
		if (priv->fs.vlan.active_svlans_rule[vid]) {
			mlx5_del_flow_rules(priv->fs.vlan.active_svlans_rule[vid]);
			priv->fs.vlan.active_svlans_rule[vid] = NULL;
		}
		break;
#endif
	case MLX5E_VLAN_RULE_TYPE_MATCH_CTAG_VID:
		if (priv->fs.vlan.active_cvlans_rule[vid]) {
			mlx5_del_flow_rules(priv->fs.vlan.active_cvlans_rule[vid]);
			priv->fs.vlan.active_cvlans_rule[vid] = NULL;
		}
		mlx5e_vport_context_update_vlans(priv);
		break;
	}
}

static void mlx5e_del_any_vid_rules(struct mlx5e_priv *priv)
{
	mlx5e_del_vlan_rule(priv, MLX5E_VLAN_RULE_TYPE_ANY_CTAG_VID, 0);
	mlx5e_del_vlan_rule(priv, MLX5E_VLAN_RULE_TYPE_ANY_STAG_VID, 0);
}

static int mlx5e_add_any_vid_rules(struct mlx5e_priv *priv)
{
	int err;

	err = mlx5e_add_vlan_rule(priv, MLX5E_VLAN_RULE_TYPE_ANY_CTAG_VID, 0);
	if (err)
		return err;

	return mlx5e_add_vlan_rule(priv, MLX5E_VLAN_RULE_TYPE_ANY_STAG_VID, 0);
}

void mlx5e_enable_cvlan_filter(struct mlx5e_priv *priv)
{
	if (!priv->fs.vlan.cvlan_filter_disabled)
		return;

	priv->fs.vlan.cvlan_filter_disabled = false;
	if (priv->netdev->flags & IFF_PROMISC)
		return;
	mlx5e_del_vlan_rule(priv, MLX5E_VLAN_RULE_TYPE_ANY_CTAG_VID, 0);
}

void mlx5e_disable_cvlan_filter(struct mlx5e_priv *priv)
{
	if (priv->fs.vlan.cvlan_filter_disabled)
		return;

	priv->fs.vlan.cvlan_filter_disabled = true;
	if (priv->netdev->flags & IFF_PROMISC)
		return;
	mlx5e_add_vlan_rule(priv, MLX5E_VLAN_RULE_TYPE_ANY_CTAG_VID, 0);
}

static int mlx5e_vlan_rx_add_cvid(struct mlx5e_priv *priv, u16 vid)
{
	int err;

#if (1) /* MLX5E TRUE backport*/

	/* This is a WA for old kernels (<3.10) that don't delete vlan id 0
	 * when the interface goes down.
	 */
	if (test_bit(vid, priv->fs.vlan.active_cvlans))
		return 0;
#endif

	set_bit(vid, priv->fs.vlan.active_cvlans);

	err = mlx5e_add_vlan_rule(priv, MLX5E_VLAN_RULE_TYPE_MATCH_CTAG_VID, vid);
	if (err)
		clear_bit(vid, priv->fs.vlan.active_cvlans);

	return err;
}

#ifdef HAVE_NETIF_F_HW_VLAN_STAG_RX
static int mlx5e_vlan_rx_add_svid(struct mlx5e_priv *priv, u16 vid)
{
	struct net_device *netdev = priv->netdev;
	int err;

	set_bit(vid, priv->fs.vlan.active_svlans);

	err = mlx5e_add_vlan_rule(priv, MLX5E_VLAN_RULE_TYPE_MATCH_STAG_VID, vid);
	if (err) {
		clear_bit(vid, priv->fs.vlan.active_svlans);
		return err;
	}

	/* Need to fix some features.. */
	netdev_update_features(netdev);
	return err;
}
#endif

#if defined(HAVE_NDO_RX_ADD_VID_HAS_3_PARAMS)
int mlx5e_vlan_rx_add_vid(struct net_device *dev, __always_unused __be16 proto,
			  u16 vid)
#elif defined(HAVE_NDO_RX_ADD_VID_HAS_2_PARAMS_RET_INT)
int mlx5e_vlan_rx_add_vid(struct net_device *dev, u16 vid)
#else
void mlx5e_vlan_rx_add_vid(struct net_device *dev, u16 vid)
#endif
{
	struct mlx5e_priv *priv = netdev_priv(dev);
#ifndef HAVE_NETIF_F_HW_VLAN_STAG_RX
	__be16 proto = cpu_to_be16(ETH_P_8021Q);
#endif

	if (be16_to_cpu(proto) == ETH_P_8021Q)
#if (defined(HAVE_NDO_RX_ADD_VID_HAS_3_PARAMS) || \
     defined(HAVE_NDO_RX_ADD_VID_HAS_2_PARAMS_RET_INT))
		return mlx5e_vlan_rx_add_cvid(priv, vid);
#else
		mlx5e_vlan_rx_add_cvid(priv, vid);
#endif
#ifdef HAVE_NETIF_F_HW_VLAN_STAG_RX
	else if (be16_to_cpu(proto) == ETH_P_8021AD)
#if (defined(HAVE_NDO_RX_ADD_VID_HAS_3_PARAMS) || \
     defined(HAVE_NDO_RX_ADD_VID_HAS_2_PARAMS_RET_INT))
		return mlx5e_vlan_rx_add_svid(priv, vid);
#else
		mlx5e_vlan_rx_add_svid(priv, vid);
#endif
#endif

#if (defined(HAVE_NDO_RX_ADD_VID_HAS_3_PARAMS) || \
     defined(HAVE_NDO_RX_ADD_VID_HAS_2_PARAMS_RET_INT))
	return -EOPNOTSUPP;
#endif
}

#if defined(HAVE_NDO_RX_ADD_VID_HAS_3_PARAMS)
int mlx5e_vlan_rx_kill_vid(struct net_device *dev, __always_unused __be16 proto,
			   u16 vid)
#elif defined(HAVE_NDO_RX_ADD_VID_HAS_2_PARAMS_RET_INT)
int mlx5e_vlan_rx_kill_vid(struct net_device *dev, u16 vid)
#else
void mlx5e_vlan_rx_kill_vid(struct net_device *dev, u16 vid)
#endif
{
	struct mlx5e_priv *priv = netdev_priv(dev);
#ifndef HAVE_NETIF_F_HW_VLAN_STAG_RX
	__be16 proto = cpu_to_be16(ETH_P_8021Q);
#endif

	if (be16_to_cpu(proto) == ETH_P_8021Q) {
		clear_bit(vid, priv->fs.vlan.active_cvlans);
		mlx5e_del_vlan_rule(priv, MLX5E_VLAN_RULE_TYPE_MATCH_CTAG_VID, vid);
#ifdef HAVE_NETIF_F_HW_VLAN_STAG_RX
	} else if (be16_to_cpu(proto) == ETH_P_8021AD) {
		clear_bit(vid, priv->fs.vlan.active_svlans);
		mlx5e_del_vlan_rule(priv, MLX5E_VLAN_RULE_TYPE_MATCH_STAG_VID, vid);
		netdev_update_features(dev);
#endif
	}

#if (defined(HAVE_NDO_RX_ADD_VID_HAS_3_PARAMS) || \
     defined(HAVE_NDO_RX_ADD_VID_HAS_2_PARAMS_RET_INT))
	return 0;
#endif
}

static void mlx5e_add_vlan_rules(struct mlx5e_priv *priv)
{
	int i;

	mlx5e_add_vlan_rule(priv, MLX5E_VLAN_RULE_TYPE_UNTAGGED, 0);

	for_each_set_bit(i, priv->fs.vlan.active_cvlans, VLAN_N_VID) {
		mlx5e_add_vlan_rule(priv, MLX5E_VLAN_RULE_TYPE_MATCH_CTAG_VID, i);
	}

#ifdef HAVE_NETIF_F_HW_VLAN_STAG_RX
	for_each_set_bit(i, priv->fs.vlan.active_svlans, VLAN_N_VID)
		mlx5e_add_vlan_rule(priv, MLX5E_VLAN_RULE_TYPE_MATCH_STAG_VID, i);
#endif

	if (priv->fs.vlan.cvlan_filter_disabled &&
	    !(priv->netdev->flags & IFF_PROMISC))
		mlx5e_add_any_vid_rules(priv);
}

static void mlx5e_del_vlan_rules(struct mlx5e_priv *priv)
{
	int i;

	mlx5e_del_vlan_rule(priv, MLX5E_VLAN_RULE_TYPE_UNTAGGED, 0);

	for_each_set_bit(i, priv->fs.vlan.active_cvlans, VLAN_N_VID) {
		mlx5e_del_vlan_rule(priv, MLX5E_VLAN_RULE_TYPE_MATCH_CTAG_VID, i);
	}

#ifdef HAVE_NETIF_F_HW_VLAN_STAG_RX
	for_each_set_bit(i, priv->fs.vlan.active_svlans, VLAN_N_VID)
		mlx5e_del_vlan_rule(priv, MLX5E_VLAN_RULE_TYPE_MATCH_STAG_VID, i);
#endif

	if (priv->fs.vlan.cvlan_filter_disabled &&
	    !(priv->netdev->flags & IFF_PROMISC))
		mlx5e_del_any_vid_rules(priv);
}

#define mlx5e_for_each_hash_node(hn, tmp, hash, i) \
	for (i = 0; i < MLX5E_L2_ADDR_HASH_SIZE; i++) \
		compat_hlist_for_each_entry_safe(hn, tmp, &hash[i], hlist)

static void mlx5e_execute_l2_action(struct mlx5e_priv *priv,
				    struct mlx5e_l2_hash_node *hn)
{
	u8 action = hn->action;
	u8 mac_addr[ETH_ALEN];
	int l2_err = 0;

	ether_addr_copy(mac_addr, hn->ai.addr);

	switch (action) {
	case MLX5E_ACTION_ADD:
		mlx5e_add_l2_flow_rule(priv, &hn->ai, MLX5E_FULLMATCH);
		if (!is_multicast_ether_addr(mac_addr)) {
			l2_err = mlx5_mpfs_add_mac(priv->mdev, mac_addr);
			hn->mpfs = !l2_err;
		}
		hn->action = MLX5E_ACTION_NONE;
		break;

	case MLX5E_ACTION_DEL:
		if (!is_multicast_ether_addr(mac_addr) && hn->mpfs)
			l2_err = mlx5_mpfs_del_mac(priv->mdev, mac_addr);
		mlx5e_del_l2_flow_rule(priv, &hn->ai);
		mlx5e_del_l2_from_hash(hn);
		break;
	}

	if (l2_err)
		netdev_warn(priv->netdev, "MPFS, failed to %s mac %pM, err(%d)\n",
			    action == MLX5E_ACTION_ADD ? "add" : "del", mac_addr, l2_err);
}

static void mlx5e_sync_netdev_addr(struct mlx5e_priv *priv)
{
	struct net_device *netdev = priv->netdev;
	struct netdev_hw_addr *ha;
#ifndef HAVE_NETDEV_FOR_EACH_MC_ADDR
	struct dev_mc_list *mclist;
#endif

	netif_addr_lock_bh(netdev);

	mlx5e_add_l2_to_hash(priv->fs.l2.netdev_uc,
			     priv->netdev->dev_addr);

	netdev_for_each_uc_addr(ha, netdev)
		mlx5e_add_l2_to_hash(priv->fs.l2.netdev_uc, ha->addr);

#ifdef HAVE_NETDEV_FOR_EACH_MC_ADDR
	netdev_for_each_mc_addr(ha, netdev)
		mlx5e_add_l2_to_hash(priv->fs.l2.netdev_mc, ha->addr);
#else
	for (mclist = netdev->mc_list; mclist; mclist = mclist->next)
		mlx5e_add_l2_to_hash(priv->fs.l2.netdev_mc,
				     mclist->dmi_addr);
#endif

	netif_addr_unlock_bh(netdev);
}

static void mlx5e_fill_addr_array(struct mlx5e_priv *priv, int list_type,
				  u8 addr_array[][ETH_ALEN], int size)
{
	bool is_uc = (list_type == MLX5_NVPRT_LIST_TYPE_UC);
	struct net_device *ndev = priv->netdev;
	struct mlx5e_l2_hash_node *hn;
	struct hlist_head *addr_list;
	struct hlist_node *tmp;
	int i = 0;
	int hi;
	COMPAT_HL_NODE

	addr_list = is_uc ? priv->fs.l2.netdev_uc : priv->fs.l2.netdev_mc;

	if (is_uc) /* Make sure our own address is pushed first */
		ether_addr_copy(addr_array[i++], ndev->dev_addr);
	else if (priv->fs.l2.broadcast_enabled)
		ether_addr_copy(addr_array[i++], ndev->broadcast);

	mlx5e_for_each_hash_node(hn, tmp, addr_list, hi) {
		if (ether_addr_equal(ndev->dev_addr, hn->ai.addr))
			continue;
		if (i >= size)
			break;
		ether_addr_copy(addr_array[i++], hn->ai.addr);
	}
}

static void mlx5e_vport_context_update_addr_list(struct mlx5e_priv *priv,
						 int list_type)
{
	bool is_uc = (list_type == MLX5_NVPRT_LIST_TYPE_UC);
	struct mlx5e_l2_hash_node *hn;
	u8 (*addr_array)[ETH_ALEN] = NULL;
	struct hlist_head *addr_list;
	struct hlist_node *tmp;
	int max_size;
	int size;
	int err;
	int hi;
	COMPAT_HL_NODE

	size = is_uc ? 0 : (priv->fs.l2.broadcast_enabled ? 1 : 0);
	max_size = is_uc ?
		1 << MLX5_CAP_GEN(priv->mdev, log_max_current_uc_list) :
		1 << MLX5_CAP_GEN(priv->mdev, log_max_current_mc_list);

	addr_list = is_uc ? priv->fs.l2.netdev_uc : priv->fs.l2.netdev_mc;
	mlx5e_for_each_hash_node(hn, tmp, addr_list, hi)
		size++;

	if (size > max_size) {
		netdev_warn(priv->netdev,
			    "netdev %s list size (%d) > (%d) max vport list size, some addresses will be dropped\n",
			    is_uc ? "UC" : "MC", size, max_size);
		size = max_size;
	}

	if (size) {
		addr_array = kcalloc(size, ETH_ALEN, GFP_KERNEL);
		if (!addr_array) {
			err = -ENOMEM;
			goto out;
		}
		mlx5e_fill_addr_array(priv, list_type, addr_array, size);
	}

	err = mlx5_modify_nic_vport_mac_list(priv->mdev, list_type, addr_array, size);
out:
	if (err)
		netdev_err(priv->netdev,
			   "Failed to modify vport %s list err(%d)\n",
			   is_uc ? "UC" : "MC", err);
	kfree(addr_array);
}

static void mlx5e_vport_context_update(struct mlx5e_priv *priv)
{
	struct mlx5e_l2_table *ea = &priv->fs.l2;

	mlx5e_vport_context_update_addr_list(priv, MLX5_NVPRT_LIST_TYPE_UC);
	mlx5e_vport_context_update_addr_list(priv, MLX5_NVPRT_LIST_TYPE_MC);
	mlx5_modify_nic_vport_promisc(priv->mdev, 0,
				      ea->allmulti_enabled,
				      ea->promisc_enabled);
}

static void mlx5e_apply_netdev_addr(struct mlx5e_priv *priv)
{
	struct mlx5e_l2_hash_node *hn;
	struct hlist_node *tmp;
	int i;
	COMPAT_HL_NODE

	mlx5e_for_each_hash_node(hn, tmp, priv->fs.l2.netdev_uc, i)
		mlx5e_execute_l2_action(priv, hn);

	mlx5e_for_each_hash_node(hn, tmp, priv->fs.l2.netdev_mc, i)
		mlx5e_execute_l2_action(priv, hn);
}

static void mlx5e_handle_netdev_addr(struct mlx5e_priv *priv)
{
	struct mlx5e_l2_hash_node *hn;
	struct hlist_node *tmp;
	int i;
	COMPAT_HL_NODE

	mlx5e_for_each_hash_node(hn, tmp, priv->fs.l2.netdev_uc, i)
		hn->action = MLX5E_ACTION_DEL;
	mlx5e_for_each_hash_node(hn, tmp, priv->fs.l2.netdev_mc, i)
		hn->action = MLX5E_ACTION_DEL;

	if (!test_bit(MLX5E_STATE_DESTROYING, &priv->state))
		mlx5e_sync_netdev_addr(priv);

	mlx5e_apply_netdev_addr(priv);
}

void mlx5e_set_rx_mode_work(struct work_struct *work)
{
	struct mlx5e_priv *priv = container_of(work, struct mlx5e_priv,
					       set_rx_mode_work);

	struct mlx5e_l2_table *ea = &priv->fs.l2;
	struct net_device *ndev = priv->netdev;

	bool rx_mode_enable   = !test_bit(MLX5E_STATE_DESTROYING, &priv->state);
	bool promisc_enabled   = rx_mode_enable && (ndev->flags & IFF_PROMISC);
	bool allmulti_enabled  = rx_mode_enable && (ndev->flags & IFF_ALLMULTI);
	bool broadcast_enabled = rx_mode_enable;

	bool enable_promisc    = !ea->promisc_enabled   &&  promisc_enabled;
	bool disable_promisc   =  ea->promisc_enabled   && !promisc_enabled;
	bool enable_allmulti   = !ea->allmulti_enabled  &&  allmulti_enabled;
	bool disable_allmulti  =  ea->allmulti_enabled  && !allmulti_enabled;
	bool enable_broadcast  = !ea->broadcast_enabled &&  broadcast_enabled;
	bool disable_broadcast =  ea->broadcast_enabled && !broadcast_enabled;

	if (enable_promisc) {
#ifdef HAVE_NETIF_F_HW_VLAN_STAG_RX
		if (!priv->channels.params.vlan_strip_disable)
			netdev_warn_once(ndev,
					 "S-tagged traffic will be dropped while C-tag vlan stripping is enabled\n");
#endif
		mlx5e_add_l2_flow_rule(priv, &ea->promisc, MLX5E_PROMISC);
		if (!priv->fs.vlan.cvlan_filter_disabled)
			mlx5e_add_any_vid_rules(priv);
	}
	if (enable_allmulti)
		mlx5e_add_l2_flow_rule(priv, &ea->allmulti, MLX5E_ALLMULTI);
	if (enable_broadcast)
		mlx5e_add_l2_flow_rule(priv, &ea->broadcast, MLX5E_FULLMATCH);

	mlx5e_handle_netdev_addr(priv);

	if (disable_broadcast)
		mlx5e_del_l2_flow_rule(priv, &ea->broadcast);
	if (disable_allmulti)
		mlx5e_del_l2_flow_rule(priv, &ea->allmulti);
	if (disable_promisc) {
		if (!priv->fs.vlan.cvlan_filter_disabled)
			mlx5e_del_any_vid_rules(priv);
		mlx5e_del_l2_flow_rule(priv, &ea->promisc);
	}

	ea->promisc_enabled   = promisc_enabled;
	ea->allmulti_enabled  = allmulti_enabled;
	ea->broadcast_enabled = broadcast_enabled;

	mlx5e_vport_context_update(priv);
}

static void mlx5e_destroy_groups(struct mlx5e_flow_table *ft)
{
	int i;

	for (i = ft->num_groups - 1; i >= 0; i--) {
		if (!IS_ERR_OR_NULL(ft->g[i]))
			mlx5_destroy_flow_group(ft->g[i]);
		ft->g[i] = NULL;
	}
	ft->num_groups = 0;
}

void mlx5e_init_l2_addr(struct mlx5e_priv *priv)
{
	ether_addr_copy(priv->fs.l2.broadcast.addr, priv->netdev->broadcast);
}

void mlx5e_destroy_flow_table(struct mlx5e_flow_table *ft)
{
	mlx5e_destroy_groups(ft);
	kfree(ft->g);
	mlx5_destroy_flow_table(ft->t);
	ft->t = NULL;
}

static void mlx5e_cleanup_ttc_rules(struct mlx5e_ttc_table *ttc)
{
	int i;

	for (i = 0; i < MLX5E_NUM_TT; i++) {
		if (!IS_ERR_OR_NULL(ttc->rules[i])) {
			mlx5_del_flow_rules(ttc->rules[i]);
			ttc->rules[i] = NULL;
		}
	}

	for (i = 0; i < MLX5E_NUM_TUNNEL_TT; i++) {
		if (!IS_ERR_OR_NULL(ttc->tunnel_rules[i])) {
			mlx5_del_flow_rules(ttc->tunnel_rules[i]);
			ttc->tunnel_rules[i] = NULL;
		}
	}
}

struct mlx5e_etype_proto {
	u16 etype;
	u8 proto;
};

static struct mlx5e_etype_proto ttc_rules[] = {
	[MLX5E_TT_IPV4_TCP] = {
		.etype = ETH_P_IP,
		.proto = IPPROTO_TCP,
	},
	[MLX5E_TT_IPV6_TCP] = {
		.etype = ETH_P_IPV6,
		.proto = IPPROTO_TCP,
	},
	[MLX5E_TT_IPV4_UDP] = {
		.etype = ETH_P_IP,
		.proto = IPPROTO_UDP,
	},
	[MLX5E_TT_IPV6_UDP] = {
		.etype = ETH_P_IPV6,
		.proto = IPPROTO_UDP,
	},
	[MLX5E_TT_IPV4_IPSEC_AH] = {
		.etype = ETH_P_IP,
		.proto = IPPROTO_AH,
	},
	[MLX5E_TT_IPV6_IPSEC_AH] = {
		.etype = ETH_P_IPV6,
		.proto = IPPROTO_AH,
	},
	[MLX5E_TT_IPV4_IPSEC_ESP] = {
		.etype = ETH_P_IP,
		.proto = IPPROTO_ESP,
	},
	[MLX5E_TT_IPV6_IPSEC_ESP] = {
		.etype = ETH_P_IPV6,
		.proto = IPPROTO_ESP,
	},
	[MLX5E_TT_IPV4] = {
		.etype = ETH_P_IP,
		.proto = 0,
	},
	[MLX5E_TT_IPV6] = {
		.etype = ETH_P_IPV6,
		.proto = 0,
	},
	[MLX5E_TT_ANY] = {
		.etype = 0,
		.proto = 0,
	},
};

static struct mlx5e_etype_proto ttc_tunnel_rules[] = {
	[MLX5E_TT_IPV4_GRE] = {
		.etype = ETH_P_IP,
		.proto = IPPROTO_GRE,
	},
	[MLX5E_TT_IPV6_GRE] = {
		.etype = ETH_P_IPV6,
		.proto = IPPROTO_GRE,
	},
};

static u8 mlx5e_etype_to_ipv(u16 ethertype)
{
	if (ethertype == ETH_P_IP)
		return 4;

	if (ethertype == ETH_P_IPV6)
		return 6;

	return 0;
}

static struct mlx5_flow_handle *
mlx5e_generate_ttc_rule(struct mlx5e_priv *priv,
			struct mlx5_flow_table *ft,
			struct mlx5_flow_destination *dest,
			u16 etype,
			u8 proto)
{
	int match_ipv_outer = MLX5_CAP_FLOWTABLE_NIC_RX(priv->mdev, ft_field_support.outer_ip_version);
	MLX5_DECLARE_FLOW_ACT(flow_act);
	struct mlx5_flow_handle *rule;
	struct mlx5_flow_spec *spec;
	int err = 0;
	u8 ipv;

	spec = kvzalloc(sizeof(*spec), GFP_KERNEL);
	if (!spec)
		return ERR_PTR(-ENOMEM);

	if (proto) {
		spec->match_criteria_enable = MLX5_MATCH_OUTER_HEADERS;
		MLX5_SET_TO_ONES(fte_match_param, spec->match_criteria, outer_headers.ip_protocol);
		MLX5_SET(fte_match_param, spec->match_value, outer_headers.ip_protocol, proto);
	}

	ipv = mlx5e_etype_to_ipv(etype);
	if (match_ipv_outer && ipv) {
		spec->match_criteria_enable = MLX5_MATCH_OUTER_HEADERS;
		MLX5_SET_TO_ONES(fte_match_param, spec->match_criteria, outer_headers.ip_version);
		MLX5_SET(fte_match_param, spec->match_value, outer_headers.ip_version, ipv);
	} else if (etype) {
		spec->match_criteria_enable = MLX5_MATCH_OUTER_HEADERS;
		MLX5_SET_TO_ONES(fte_match_param, spec->match_criteria, outer_headers.ethertype);
		MLX5_SET(fte_match_param, spec->match_value, outer_headers.ethertype, etype);
	}

	rule = mlx5_add_flow_rules(ft, spec, &flow_act, dest, 1);
	if (IS_ERR(rule)) {
		err = PTR_ERR(rule);
		netdev_err(priv->netdev, "%s: add rule failed\n", __func__);
	}

	kvfree(spec);
	return err ? ERR_PTR(err) : rule;
}

static int mlx5e_generate_ttc_table_rules(struct mlx5e_priv *priv,
					  struct ttc_params *params,
					  struct mlx5e_ttc_table *ttc)
{
	struct mlx5_flow_destination dest = {};
	struct mlx5_flow_handle **rules;
	struct mlx5_flow_table *ft;
	int tt;
	int err;

	ft = ttc->ft.t;
	rules = ttc->rules;

	dest.type = MLX5_FLOW_DESTINATION_TYPE_TIR;
	for (tt = 0; tt < MLX5E_NUM_TT; tt++) {
		if (tt == MLX5E_TT_ANY)
			dest.tir_num = params->any_tt_tirn;
		else
			dest.tir_num = params->indir_tirn[tt];
		rules[tt] = mlx5e_generate_ttc_rule(priv, ft, &dest,
						    ttc_rules[tt].etype,
						    ttc_rules[tt].proto);
		if (IS_ERR(rules[tt]))
			goto del_rules;
	}

	if (!params->inner_ttc || !mlx5e_tunnel_inner_ft_supported(priv->mdev))
		return 0;

	rules     = ttc->tunnel_rules;
	dest.type = MLX5_FLOW_DESTINATION_TYPE_FLOW_TABLE;
	dest.ft   = params->inner_ttc->ft.t;
	for (tt = 0; tt < MLX5E_NUM_TUNNEL_TT; tt++) {
		rules[tt] = mlx5e_generate_ttc_rule(priv, ft, &dest,
						    ttc_tunnel_rules[tt].etype,
						    ttc_tunnel_rules[tt].proto);
		if (IS_ERR(rules[tt]))
			goto del_rules;
	}

	return 0;

del_rules:
	err = PTR_ERR(rules[tt]);
	rules[tt] = NULL;
	mlx5e_cleanup_ttc_rules(ttc);
	return err;
}

#define MLX5E_TTC_NUM_GROUPS	3
#define MLX5E_TTC_GROUP1_SIZE	(BIT(3) + MLX5E_NUM_TUNNEL_TT)
#define MLX5E_TTC_GROUP2_SIZE	 BIT(1)
#define MLX5E_TTC_GROUP3_SIZE	 BIT(0)
#define MLX5E_TTC_TABLE_SIZE	(MLX5E_TTC_GROUP1_SIZE +\
				 MLX5E_TTC_GROUP2_SIZE +\
				 MLX5E_TTC_GROUP3_SIZE)

#define MLX5E_INNER_TTC_NUM_GROUPS	3
#define MLX5E_INNER_TTC_GROUP1_SIZE	BIT(3)
#define MLX5E_INNER_TTC_GROUP2_SIZE	BIT(1)
#define MLX5E_INNER_TTC_GROUP3_SIZE	BIT(0)
#define MLX5E_INNER_TTC_TABLE_SIZE	(MLX5E_INNER_TTC_GROUP1_SIZE +\
					 MLX5E_INNER_TTC_GROUP2_SIZE +\
					 MLX5E_INNER_TTC_GROUP3_SIZE)

static int mlx5e_create_ttc_table_groups(struct mlx5e_ttc_table *ttc,
					 bool use_ipv)
{
	int inlen = MLX5_ST_SZ_BYTES(create_flow_group_in);
	struct mlx5e_flow_table *ft = &ttc->ft;
	int ix = 0;
	u32 *in;
	int err;
	u8 *mc;

	ft->g = kcalloc(MLX5E_TTC_NUM_GROUPS,
			sizeof(*ft->g), GFP_KERNEL);
	if (!ft->g)
		return -ENOMEM;
	in = kvzalloc(inlen, GFP_KERNEL);
	if (!in) {
		kfree(ft->g);
		return -ENOMEM;
	}

	/* L4 Group */
	mc = MLX5_ADDR_OF(create_flow_group_in, in, match_criteria);
	MLX5_SET_TO_ONES(fte_match_param, mc, outer_headers.ip_protocol);
	if (use_ipv)
		MLX5_SET_TO_ONES(fte_match_param, mc, outer_headers.ip_version);
	else
		MLX5_SET_TO_ONES(fte_match_param, mc, outer_headers.ethertype);
	MLX5_SET_CFG(in, match_criteria_enable, MLX5_MATCH_OUTER_HEADERS);
	MLX5_SET_CFG(in, start_flow_index, ix);
	ix += MLX5E_TTC_GROUP1_SIZE;
	MLX5_SET_CFG(in, end_flow_index, ix - 1);
	ft->g[ft->num_groups] = mlx5_create_flow_group(ft->t, in);
	if (IS_ERR(ft->g[ft->num_groups]))
		goto err;
	ft->num_groups++;

	/* L3 Group */
	MLX5_SET(fte_match_param, mc, outer_headers.ip_protocol, 0);
	MLX5_SET_CFG(in, start_flow_index, ix);
	ix += MLX5E_TTC_GROUP2_SIZE;
	MLX5_SET_CFG(in, end_flow_index, ix - 1);
	ft->g[ft->num_groups] = mlx5_create_flow_group(ft->t, in);
	if (IS_ERR(ft->g[ft->num_groups]))
		goto err;
	ft->num_groups++;

	/* Any Group */
	memset(in, 0, inlen);
	MLX5_SET_CFG(in, start_flow_index, ix);
	ix += MLX5E_TTC_GROUP3_SIZE;
	MLX5_SET_CFG(in, end_flow_index, ix - 1);
	ft->g[ft->num_groups] = mlx5_create_flow_group(ft->t, in);
	if (IS_ERR(ft->g[ft->num_groups]))
		goto err;
	ft->num_groups++;

	kvfree(in);
	return 0;

err:
	err = PTR_ERR(ft->g[ft->num_groups]);
	ft->g[ft->num_groups] = NULL;
	kvfree(in);

	return err;
}

static struct mlx5_flow_handle *
mlx5e_generate_inner_ttc_rule(struct mlx5e_priv *priv,
			      struct mlx5_flow_table *ft,
			      struct mlx5_flow_destination *dest,
			      u16 etype, u8 proto)
{
	MLX5_DECLARE_FLOW_ACT(flow_act);
	struct mlx5_flow_handle *rule;
	struct mlx5_flow_spec *spec;
	int err = 0;
	u8 ipv;

	spec = kvzalloc(sizeof(*spec), GFP_KERNEL);
	if (!spec)
		return ERR_PTR(-ENOMEM);

	ipv = mlx5e_etype_to_ipv(etype);
	if (etype && ipv) {
		spec->match_criteria_enable = MLX5_MATCH_INNER_HEADERS;
		MLX5_SET_TO_ONES(fte_match_param, spec->match_criteria, inner_headers.ip_version);
		MLX5_SET(fte_match_param, spec->match_value, inner_headers.ip_version, ipv);
	}

	if (proto) {
		spec->match_criteria_enable = MLX5_MATCH_INNER_HEADERS;
		MLX5_SET_TO_ONES(fte_match_param, spec->match_criteria, inner_headers.ip_protocol);
		MLX5_SET(fte_match_param, spec->match_value, inner_headers.ip_protocol, proto);
	}

	rule = mlx5_add_flow_rules(ft, spec, &flow_act, dest, 1);
	if (IS_ERR(rule)) {
		err = PTR_ERR(rule);
		netdev_err(priv->netdev, "%s: add rule failed\n", __func__);
	}

	kvfree(spec);
	return err ? ERR_PTR(err) : rule;
}

static int mlx5e_generate_inner_ttc_table_rules(struct mlx5e_priv *priv,
						struct ttc_params *params,
						struct mlx5e_ttc_table *ttc)
{
	struct mlx5_flow_destination dest = {};
	struct mlx5_flow_handle **rules;
	struct mlx5_flow_table *ft;
	int err;
	int tt;

	ft = ttc->ft.t;
	rules = ttc->rules;

	dest.type = MLX5_FLOW_DESTINATION_TYPE_TIR;
	for (tt = 0; tt < MLX5E_NUM_TT; tt++) {
		if (tt == MLX5E_TT_ANY)
			dest.tir_num = params->any_tt_tirn;
		else
			dest.tir_num = params->indir_tirn[tt];

		rules[tt] = mlx5e_generate_inner_ttc_rule(priv, ft, &dest,
							  ttc_rules[tt].etype,
							  ttc_rules[tt].proto);
		if (IS_ERR(rules[tt]))
			goto del_rules;
	}

	return 0;

del_rules:
	err = PTR_ERR(rules[tt]);
	rules[tt] = NULL;
	mlx5e_cleanup_ttc_rules(ttc);
	return err;
}

static int mlx5e_create_inner_ttc_table_groups(struct mlx5e_ttc_table *ttc)
{
	int inlen = MLX5_ST_SZ_BYTES(create_flow_group_in);
	struct mlx5e_flow_table *ft = &ttc->ft;
	int ix = 0;
	u32 *in;
	int err;
	u8 *mc;

	ft->g = kcalloc(MLX5E_INNER_TTC_NUM_GROUPS, sizeof(*ft->g), GFP_KERNEL);
	if (!ft->g)
		return -ENOMEM;
	in = kvzalloc(inlen, GFP_KERNEL);
	if (!in) {
		kfree(ft->g);
		return -ENOMEM;
	}

	/* L4 Group */
	mc = MLX5_ADDR_OF(create_flow_group_in, in, match_criteria);
	MLX5_SET_TO_ONES(fte_match_param, mc, inner_headers.ip_protocol);
	MLX5_SET_TO_ONES(fte_match_param, mc, inner_headers.ip_version);
	MLX5_SET_CFG(in, match_criteria_enable, MLX5_MATCH_INNER_HEADERS);
	MLX5_SET_CFG(in, start_flow_index, ix);
	ix += MLX5E_INNER_TTC_GROUP1_SIZE;
	MLX5_SET_CFG(in, end_flow_index, ix - 1);
	ft->g[ft->num_groups] = mlx5_create_flow_group(ft->t, in);
	if (IS_ERR(ft->g[ft->num_groups]))
		goto err;
	ft->num_groups++;

	/* L3 Group */
	MLX5_SET(fte_match_param, mc, inner_headers.ip_protocol, 0);
	MLX5_SET_CFG(in, start_flow_index, ix);
	ix += MLX5E_INNER_TTC_GROUP2_SIZE;
	MLX5_SET_CFG(in, end_flow_index, ix - 1);
	ft->g[ft->num_groups] = mlx5_create_flow_group(ft->t, in);
	if (IS_ERR(ft->g[ft->num_groups]))
		goto err;
	ft->num_groups++;

	/* Any Group */
	memset(in, 0, inlen);
	MLX5_SET_CFG(in, start_flow_index, ix);
	ix += MLX5E_INNER_TTC_GROUP3_SIZE;
	MLX5_SET_CFG(in, end_flow_index, ix - 1);
	ft->g[ft->num_groups] = mlx5_create_flow_group(ft->t, in);
	if (IS_ERR(ft->g[ft->num_groups]))
		goto err;
	ft->num_groups++;

	kvfree(in);
	return 0;

err:
	err = PTR_ERR(ft->g[ft->num_groups]);
	ft->g[ft->num_groups] = NULL;
	kvfree(in);

	return err;
}

void mlx5e_set_ttc_basic_params(struct mlx5e_priv *priv,
				struct ttc_params *ttc_params)
{
	ttc_params->any_tt_tirn = priv->direct_tir[0].tirn;
	ttc_params->inner_ttc = &priv->fs.inner_ttc;
}

void mlx5e_set_inner_ttc_ft_params(struct ttc_params *ttc_params)
{
	struct mlx5_flow_table_attr *ft_attr = &ttc_params->ft_attr;

	ft_attr->max_fte = MLX5E_INNER_TTC_TABLE_SIZE;
	ft_attr->level = MLX5E_INNER_TTC_FT_LEVEL;
	ft_attr->prio = MLX5E_NIC_PRIO;
}

void mlx5e_set_ttc_ft_params(struct ttc_params *ttc_params)

{
	struct mlx5_flow_table_attr *ft_attr = &ttc_params->ft_attr;

	ft_attr->max_fte = MLX5E_TTC_TABLE_SIZE;
	ft_attr->level = MLX5E_TTC_FT_LEVEL;
	ft_attr->prio = MLX5E_NIC_PRIO;
}

int mlx5e_create_inner_ttc_table(struct mlx5e_priv *priv, struct ttc_params *params,
				 struct mlx5e_ttc_table *ttc)
{
	struct mlx5e_flow_table *ft = &ttc->ft;
	int err;

	if (!mlx5e_tunnel_inner_ft_supported(priv->mdev))
		return 0;

	ft->t = mlx5_create_flow_table(priv->fs.ns, &params->ft_attr);
	if (IS_ERR(ft->t)) {
		err = PTR_ERR(ft->t);
		ft->t = NULL;
		return err;
	}

	err = mlx5e_create_inner_ttc_table_groups(ttc);
	if (err)
		goto err;

	err = mlx5e_generate_inner_ttc_table_rules(priv, params, ttc);
	if (err)
		goto err;

	return 0;

err:
	mlx5e_destroy_flow_table(ft);
	return err;
}

void mlx5e_destroy_inner_ttc_table(struct mlx5e_priv *priv,
				   struct mlx5e_ttc_table *ttc)
{
	if (!mlx5e_tunnel_inner_ft_supported(priv->mdev))
		return;

	mlx5e_cleanup_ttc_rules(ttc);
	mlx5e_destroy_flow_table(&ttc->ft);
}

void mlx5e_destroy_ttc_table(struct mlx5e_priv *priv,
			     struct mlx5e_ttc_table *ttc)
{
	mlx5e_cleanup_ttc_rules(ttc);
	mlx5e_destroy_flow_table(&ttc->ft);
}

int mlx5e_create_ttc_table(struct mlx5e_priv *priv, struct ttc_params *params,
			   struct mlx5e_ttc_table *ttc)
{
	bool match_ipv_outer = MLX5_CAP_FLOWTABLE_NIC_RX(priv->mdev, ft_field_support.outer_ip_version);
	struct mlx5e_flow_table *ft = &ttc->ft;
	int err;

	ft->t = mlx5_create_flow_table(priv->fs.ns, &params->ft_attr);
	if (IS_ERR(ft->t)) {
		err = PTR_ERR(ft->t);
		ft->t = NULL;
		return err;
	}

	err = mlx5e_create_ttc_table_groups(ttc, match_ipv_outer);
	if (err)
		goto err;

	err = mlx5e_generate_ttc_table_rules(priv, params, ttc);
	if (err)
		goto err;

	return 0;
err:
	mlx5e_destroy_flow_table(ft);
	return err;
}

static void mlx5e_del_l2_flow_rule(struct mlx5e_priv *priv,
				   struct mlx5e_l2_rule *ai)
{
	if (!IS_ERR_OR_NULL(ai->rule)) {
		mlx5_del_flow_rules(ai->rule);
		ai->rule = NULL;
	}
}

static int mlx5e_add_l2_flow_rule(struct mlx5e_priv *priv,
				  struct mlx5e_l2_rule *ai, int type)
{
	struct mlx5_flow_table *ft = priv->fs.l2.ft.t;
	struct mlx5_flow_destination dest = {};
	MLX5_DECLARE_FLOW_ACT(flow_act);
	struct mlx5_flow_spec *spec;
	int err = 0;
	u8 *mc_dmac;
	u8 *mv_dmac;

	spec = kvzalloc(sizeof(*spec), GFP_KERNEL);
	if (!spec)
		return -ENOMEM;

	mc_dmac = MLX5_ADDR_OF(fte_match_param, spec->match_criteria,
			       outer_headers.dmac_47_16);
	mv_dmac = MLX5_ADDR_OF(fte_match_param, spec->match_value,
			       outer_headers.dmac_47_16);

	dest.type = MLX5_FLOW_DESTINATION_TYPE_FLOW_TABLE;
	dest.ft = priv->fs.decap.ft.t;

	switch (type) {
	case MLX5E_FULLMATCH:
		spec->match_criteria_enable = MLX5_MATCH_OUTER_HEADERS;
		eth_broadcast_addr(mc_dmac);
		ether_addr_copy(mv_dmac, ai->addr);
		break;

	case MLX5E_ALLMULTI:
		spec->match_criteria_enable = MLX5_MATCH_OUTER_HEADERS;
		mc_dmac[0] = 0x01;
		mv_dmac[0] = 0x01;
		break;

	case MLX5E_PROMISC:
		break;
	}

	ai->rule = mlx5_add_flow_rules(ft, spec, &flow_act, &dest, 1);
	if (IS_ERR(ai->rule)) {
		netdev_err(priv->netdev, "%s: add l2 rule(mac:%pM) failed\n",
			   __func__, mv_dmac);
		err = PTR_ERR(ai->rule);
		ai->rule = NULL;
	}

	kvfree(spec);

	return err;
}

#define MLX5E_NUM_L2_GROUPS	   3
#define MLX5E_L2_GROUP1_SIZE	   BIT(0)
#define MLX5E_L2_GROUP2_SIZE	   BIT(15)
#define MLX5E_L2_GROUP3_SIZE	   BIT(0)
#define MLX5E_L2_TABLE_SIZE	   (MLX5E_L2_GROUP1_SIZE +\
				    MLX5E_L2_GROUP2_SIZE +\
				    MLX5E_L2_GROUP3_SIZE)
static int mlx5e_create_l2_table_groups(struct mlx5e_l2_table *l2_table)
{
	int inlen = MLX5_ST_SZ_BYTES(create_flow_group_in);
	struct mlx5e_flow_table *ft = &l2_table->ft;
	int ix = 0;
	u8 *mc_dmac;
	u32 *in;
	int err;
	u8 *mc;

	ft->g = kcalloc(MLX5E_NUM_L2_GROUPS, sizeof(*ft->g), GFP_KERNEL);
	if (!ft->g)
		return -ENOMEM;
	in = kvzalloc(inlen, GFP_KERNEL);
	if (!in) {
		kfree(ft->g);
		return -ENOMEM;
	}

	mc = MLX5_ADDR_OF(create_flow_group_in, in, match_criteria);
	mc_dmac = MLX5_ADDR_OF(fte_match_param, mc,
			       outer_headers.dmac_47_16);
	/* Flow Group for promiscuous */
	MLX5_SET_CFG(in, start_flow_index, ix);
	ix += MLX5E_L2_GROUP1_SIZE;
	MLX5_SET_CFG(in, end_flow_index, ix - 1);
	ft->g[ft->num_groups] = mlx5_create_flow_group(ft->t, in);
	if (IS_ERR(ft->g[ft->num_groups]))
		goto err_destroy_groups;
	ft->num_groups++;

	/* Flow Group for full match */
	eth_broadcast_addr(mc_dmac);
	MLX5_SET_CFG(in, match_criteria_enable, MLX5_MATCH_OUTER_HEADERS);
	MLX5_SET_CFG(in, start_flow_index, ix);
	ix += MLX5E_L2_GROUP2_SIZE;
	MLX5_SET_CFG(in, end_flow_index, ix - 1);
	ft->g[ft->num_groups] = mlx5_create_flow_group(ft->t, in);
	if (IS_ERR(ft->g[ft->num_groups]))
		goto err_destroy_groups;
	ft->num_groups++;

	/* Flow Group for allmulti */
	eth_zero_addr(mc_dmac);
	mc_dmac[0] = 0x01;
	MLX5_SET_CFG(in, start_flow_index, ix);
	ix += MLX5E_L2_GROUP3_SIZE;
	MLX5_SET_CFG(in, end_flow_index, ix - 1);
	ft->g[ft->num_groups] = mlx5_create_flow_group(ft->t, in);
	if (IS_ERR(ft->g[ft->num_groups]))
		goto err_destroy_groups;
	ft->num_groups++;

	kvfree(in);
	return 0;

err_destroy_groups:
	err = PTR_ERR(ft->g[ft->num_groups]);
	ft->g[ft->num_groups] = NULL;
	mlx5e_destroy_groups(ft);
	kvfree(in);

	return err;
}

static void mlx5e_destroy_l2_table(struct mlx5e_priv *priv)
{
	mlx5e_destroy_flow_table(&priv->fs.l2.ft);
}

static int mlx5e_create_l2_table(struct mlx5e_priv *priv)
{
	struct mlx5e_l2_table *l2_table = &priv->fs.l2;
	struct mlx5e_flow_table *ft = &l2_table->ft;
	struct mlx5_flow_table_attr ft_attr = {};
	int err;

	ft->num_groups = 0;

	ft_attr.max_fte = MLX5E_L2_TABLE_SIZE;
	ft_attr.level = MLX5E_L2_FT_LEVEL;
	ft_attr.prio = MLX5E_NIC_PRIO;

	ft->t = mlx5_create_flow_table(priv->fs.ns, &ft_attr);
	if (IS_ERR(ft->t)) {
		err = PTR_ERR(ft->t);
		ft->t = NULL;
		return err;
	}

	err = mlx5e_create_l2_table_groups(l2_table);
	if (err)
		goto err_destroy_flow_table;

	return 0;

err_destroy_flow_table:
	mlx5_destroy_flow_table(ft->t);
	ft->t = NULL;

	return err;
}

#ifdef HAVE_NETIF_F_HW_VLAN_STAG_RX
#define MLX5E_NUM_VLAN_GROUPS	4
#define MLX5E_VLAN_GROUP0_SIZE	BIT(12)
#define MLX5E_VLAN_GROUP1_SIZE	BIT(12)
#define MLX5E_VLAN_GROUP2_SIZE	BIT(1)
#define MLX5E_VLAN_GROUP3_SIZE	BIT(0)
#define MLX5E_VLAN_TABLE_SIZE	(MLX5E_VLAN_GROUP0_SIZE +\
				 MLX5E_VLAN_GROUP1_SIZE +\
				 MLX5E_VLAN_GROUP2_SIZE +\
				 MLX5E_VLAN_GROUP3_SIZE)
#else
#define MLX5E_NUM_VLAN_GROUPS  3
#define MLX5E_VLAN_GROUP0_SIZE BIT(12)
#define MLX5E_VLAN_GROUP1_SIZE BIT(1)
#define MLX5E_VLAN_GROUP2_SIZE BIT(0)
#define MLX5E_VLAN_TABLE_SIZE  (MLX5E_VLAN_GROUP0_SIZE +\
				MLX5E_VLAN_GROUP1_SIZE +\
				MLX5E_VLAN_GROUP2_SIZE)
#endif

static int __mlx5e_create_vlan_table_groups(struct mlx5e_flow_table *ft, u32 *in,
					    int inlen)
{
	int err;
	int ix = 0;
	u8 *mc = MLX5_ADDR_OF(create_flow_group_in, in, match_criteria);

	memset(in, 0, inlen);
	MLX5_SET_CFG(in, match_criteria_enable, MLX5_MATCH_OUTER_HEADERS);
	MLX5_SET_TO_ONES(fte_match_param, mc, outer_headers.cvlan_tag);
	MLX5_SET_TO_ONES(fte_match_param, mc, outer_headers.first_vid);
	MLX5_SET_CFG(in, start_flow_index, ix);
	ix += MLX5E_VLAN_GROUP0_SIZE;
	MLX5_SET_CFG(in, end_flow_index, ix - 1);
	ft->g[ft->num_groups] = mlx5_create_flow_group(ft->t, in);
	if (IS_ERR(ft->g[ft->num_groups]))
		goto err_destroy_groups;
	ft->num_groups++;

	memset(in, 0, inlen);
	MLX5_SET_CFG(in, match_criteria_enable, MLX5_MATCH_OUTER_HEADERS);
#ifdef HAVE_NETIF_F_HW_VLAN_STAG_RX
	MLX5_SET_TO_ONES(fte_match_param, mc, outer_headers.svlan_tag);
	MLX5_SET_TO_ONES(fte_match_param, mc, outer_headers.first_vid);
#else
	MLX5_SET_TO_ONES(fte_match_param, mc, outer_headers.cvlan_tag);
#endif
	MLX5_SET_CFG(in, start_flow_index, ix);
	ix += MLX5E_VLAN_GROUP1_SIZE;
	MLX5_SET_CFG(in, end_flow_index, ix - 1);
	ft->g[ft->num_groups] = mlx5_create_flow_group(ft->t, in);
	if (IS_ERR(ft->g[ft->num_groups]))
		goto err_destroy_groups;
	ft->num_groups++;

	memset(in, 0, inlen);
	MLX5_SET_CFG(in, match_criteria_enable, MLX5_MATCH_OUTER_HEADERS);
#ifdef HAVE_NETIF_F_HW_VLAN_STAG_RX
	MLX5_SET_TO_ONES(fte_match_param, mc, outer_headers.cvlan_tag);
#else
	MLX5_SET_TO_ONES(fte_match_param, mc, outer_headers.svlan_tag);
#endif
	MLX5_SET_CFG(in, start_flow_index, ix);
	ix += MLX5E_VLAN_GROUP2_SIZE;
	MLX5_SET_CFG(in, end_flow_index, ix - 1);
	ft->g[ft->num_groups] = mlx5_create_flow_group(ft->t, in);
	if (IS_ERR(ft->g[ft->num_groups]))
		goto err_destroy_groups;
	ft->num_groups++;

#ifdef HAVE_NETIF_F_HW_VLAN_STAG_RX
	memset(in, 0, inlen);
	MLX5_SET_CFG(in, match_criteria_enable, MLX5_MATCH_OUTER_HEADERS);
	MLX5_SET_TO_ONES(fte_match_param, mc, outer_headers.svlan_tag);
	MLX5_SET_CFG(in, start_flow_index, ix);
	ix += MLX5E_VLAN_GROUP3_SIZE;
	MLX5_SET_CFG(in, end_flow_index, ix - 1);
	ft->g[ft->num_groups] = mlx5_create_flow_group(ft->t, in);
	if (IS_ERR(ft->g[ft->num_groups]))
		goto err_destroy_groups;
	ft->num_groups++;
#endif

	return 0;

err_destroy_groups:
	err = PTR_ERR(ft->g[ft->num_groups]);
	ft->g[ft->num_groups] = NULL;
	mlx5e_destroy_groups(ft);

	return err;
}

static int mlx5e_create_vlan_table_groups(struct mlx5e_flow_table *ft)
{
	u32 *in;
	int inlen = MLX5_ST_SZ_BYTES(create_flow_group_in);
	int err;

	in = kvzalloc(inlen, GFP_KERNEL);
	if (!in)
		return -ENOMEM;

	err = __mlx5e_create_vlan_table_groups(ft, in, inlen);

	kvfree(in);
	return err;
}

static int mlx5e_create_vlan_table(struct mlx5e_priv *priv)
{
	struct mlx5e_flow_table *ft = &priv->fs.vlan.ft;
	struct mlx5_flow_table_attr ft_attr = {};
	int err;

	ft->num_groups = 0;

	ft_attr.max_fte = MLX5E_VLAN_TABLE_SIZE;
	ft_attr.level = MLX5E_VLAN_FT_LEVEL;
	ft_attr.prio = MLX5E_NIC_PRIO;

	ft->t = mlx5_create_flow_table(priv->fs.ns, &ft_attr);

	if (IS_ERR(ft->t)) {
		err = PTR_ERR(ft->t);
		ft->t = NULL;
		return err;
	}
	ft->g = kcalloc(MLX5E_NUM_VLAN_GROUPS, sizeof(*ft->g), GFP_KERNEL);
	if (!ft->g) {
		err = -ENOMEM;
		goto err_destroy_vlan_table;
	}

	err = mlx5e_create_vlan_table_groups(ft);
	if (err)
		goto err_free_g;

	mlx5e_add_vlan_rules(priv);

	return 0;

err_free_g:
	kfree(ft->g);
err_destroy_vlan_table:
	mlx5_destroy_flow_table(ft->t);
	ft->t = NULL;

	return err;
}

static void mlx5e_destroy_vlan_table(struct mlx5e_priv *priv)
{
	mlx5e_del_vlan_rules(priv);
	mlx5e_destroy_flow_table(&priv->fs.vlan.ft);
}

#define MLX5E_MAX_AMOUNT_OF_ACCELERATED_TUNNELS 40
#define MLX5E_DECAP_MATCHES_TABLE_LEN MLX5E_MAX_AMOUNT_OF_ACCELERATED_TUNNELS
#define MLX5E_DECAP_NUM_GROUPS 2
#define MLX5E_DECAP_GROUP2_SIZE 1 //miss group
#define MLX5E_DECAP_TABLE_SIZE (MLX5E_DECAP_MATCHES_TABLE_LEN + MLX5E_DECAP_GROUP2_SIZE)

static int mlx5e_add_decap_rule(struct mlx5e_priv *priv, struct mlx5e_decap_match *decap_match, int match_index)
{
	struct mlx5_flow_table *ft = priv->fs.decap.ft.t;
	struct mlx5_flow_handle *decap_rule;
	struct mlx5_flow_destination dest = {};
	struct mlx5_flow_act flow_act = {
               .action = MLX5_FLOW_CONTEXT_ACTION_FWD_DEST | MLX5_FLOW_CONTEXT_ACTION_DECAP,
	};
	struct mlx5_flow_spec *spec;
	int err = 0;

	spec = kvzalloc(sizeof(*spec), GFP_KERNEL);
	if (!spec)
		return -ENOMEM;

	flow_act.flags |= FLOW_ACT_HAS_TAG;
	flow_act.flow_tag = match_index;

	dest.type = MLX5_FLOW_DESTINATION_TYPE_FLOW_TABLE;
	dest.ft = priv->fs.ttc.ft.t;
	// update spec, flow act, dest here
	spec->match_criteria_enable = MLX5_MATCH_OUTER_HEADERS | MLX5_MATCH_MISC_PARAMETERS;

	MLX5_SET_TO_ONES(fte_match_param, spec->match_criteria, outer_headers.ethertype);
	MLX5_SET(fte_match_param, spec->match_value, outer_headers.ethertype, ETH_P_IP);
	MLX5_SET_TO_ONES(fte_match_param, spec->match_criteria, outer_headers.udp_dport);
	MLX5_SET(fte_match_param, spec->match_value, outer_headers.udp_dport, be16_to_cpu(decap_match->tp_dst));

	MLX5_SET_TO_ONES(fte_match_param, spec->match_criteria, outer_headers.dst_ipv4_dst_ipv6.ipv4_layout.ipv4);
	MLX5_SET(fte_match_param, spec->match_value, outer_headers.dst_ipv4_dst_ipv6.ipv4_layout.ipv4, be32_to_cpu(decap_match->dst));

	MLX5_SET_TO_ONES(fte_match_param, spec->match_criteria, misc_parameters.vxlan_vni);
	MLX5_SET(fte_match_param, spec->match_value, misc_parameters.vxlan_vni, be64_to_cpu(decap_match->tun_id));

	decap_rule = mlx5_add_flow_rules(ft, spec, &flow_act, &dest, 1);
	if (IS_ERR(decap_rule)) {
		err = PTR_ERR(decap_rule);
		netdev_err(priv->netdev, "%s: Failed to add decap rule, err:%d\n", __func__, err);
		goto free_spec;
	}

	decap_match->rule = decap_rule;

free_spec:
	kvfree(spec);
	return err;
}

static int mlx5e_find_decap_match(struct mlx5e_decap_match_table *decap_match_table, __be64 tun_id, __be32 dst, __be16 tp_dst)
{
	int i;

	for (i = 0; i < MLX5E_DECAP_MATCHES_TABLE_LEN; i++) {
		if (decap_match_table->data[i].ref_count > 0) {
			if (decap_match_table->data[i].tun_id == tun_id &&
				decap_match_table->data[i].dst == dst &&
				decap_match_table->data[i].tp_dst == tp_dst)
				return i;
		}
	}

	return -ENODATA;
}

static int mlx5e_find_available_decap_entry_slot(struct mlx5e_decap_match_table *decap_match_table)
{
	int i;

	for (i = 0; i < MLX5E_DECAP_MATCHES_TABLE_LEN; i++) {
		if (decap_match_table->data[i].ref_count == 0)
			return i;
	}

	return -ENODATA;
}

void mlx5e_insert_decap_match(struct net_device *netdev, __be64 tun_id, __be32 src, __be32 dst,
				  __u8 tos, __u8 ttl, __be16 tp_src, __be16 tp_dst, struct net_device *vxlan_device)
{
	struct mlx5e_priv *priv;
	struct mlx5e_decap_match_table *decap_match_table;
	struct mlx5e_decap_match *new_decap_match;
	int new_entry_index;
	int err;

	if (!netdev_is_mlx5e_netdev(netdev))
		return;

	priv = netdev_priv(netdev);
	decap_match_table = priv->decap_match_table;

	mutex_lock(&decap_match_table->lock);

	new_entry_index = mlx5e_find_decap_match(decap_match_table, tun_id, dst, tp_dst);
	if (new_entry_index < 0)
		new_entry_index = mlx5e_find_available_decap_entry_slot(decap_match_table);
	if (new_entry_index < 0)
		goto unlock;

	new_decap_match = &decap_match_table->data[new_entry_index];

	if (new_decap_match->ref_count > 0)
		goto increment_ref_count;

	new_decap_match->tun_id = tun_id;
	new_decap_match->src = src;
	new_decap_match->dst = dst;
	new_decap_match->tos = tos;
	new_decap_match->ttl = ttl;
	new_decap_match->tp_src = tp_src;
	new_decap_match->tp_dst = tp_dst;
	new_decap_match->vxlan_device = vxlan_device;

	err = mlx5e_add_decap_rule(priv, new_decap_match, new_entry_index);
	if (err)
		goto unlock;

increment_ref_count:
	new_decap_match->ref_count++;

unlock:
	mutex_unlock(&decap_match_table->lock);
}
EXPORT_SYMBOL(mlx5e_insert_decap_match);

void mlx5e_remove_decap_match(struct net_device *netdev, __be64 tun_id, __be32 dst, __be16 tp_dst)
{
	struct mlx5e_priv *priv;
	struct mlx5e_decap_match_table *decap_match_table;
	struct mlx5e_decap_match *decap_match;
	int entry_index;

	if (!netdev_is_mlx5e_netdev(netdev))
		return;

	priv = netdev_priv(netdev);
	decap_match_table = priv->decap_match_table;

	mutex_lock(&decap_match_table->lock);

	entry_index = mlx5e_find_decap_match(decap_match_table, tun_id, dst, tp_dst);
	if (entry_index < 0)
		goto unlock;

	decap_match = &decap_match_table->data[entry_index];
	if (decap_match->ref_count <= 0)
		goto unlock;

	decap_match->ref_count--;

	if (decap_match->ref_count <= 0) {
		if (decap_match->rule) {
			mlx5_del_flow_rules(decap_match->rule);
			decap_match->rule = NULL;
		}
	}

unlock:
	mutex_unlock(&decap_match_table->lock);
}
EXPORT_SYMBOL(mlx5e_remove_decap_match);

static int mlx5e_add_decap_table_miss_rule(struct mlx5e_priv *priv)
{
	struct mlx5_flow_table *ft = priv->fs.decap.ft.t;
	struct mlx5_flow_handle *miss_rule;
	struct mlx5_flow_destination dest = {};
	struct mlx5_flow_act flow_act = {};
	struct mlx5_flow_spec *spec;
	int err = 0;

	spec = kvzalloc(sizeof(*spec), GFP_KERNEL);
	if (!spec)
		return -ENOMEM;

	dest.type = MLX5_FLOW_DESTINATION_TYPE_FLOW_TABLE;
	dest.ft = priv->fs.ttc.ft.t;

	flow_act.action = MLX5_FLOW_CONTEXT_ACTION_FWD_DEST;
	flow_act.flags = FLOW_ACT_HAS_TAG;
	flow_act.flow_tag = MLX5E_DECAP_TABLE_MISS_TAG;

	miss_rule = mlx5_add_flow_rules(ft, spec, &flow_act, &dest, 1);
	if (IS_ERR(miss_rule)) {
		err = PTR_ERR(miss_rule);
		netdev_err(priv->netdev, "%s: Failed to add decap table miss rule, err:%d\n", __func__, err);
		goto free_spec;
	}

	priv->fs.decap.miss_rule = miss_rule;

free_spec:
	kvfree(spec);
	return err;
}

static void mlx5e_del_decap_table_miss_rule(struct mlx5e_priv *priv)
{
	if (priv->fs.decap.miss_rule){
		mlx5_del_flow_rules(priv->fs.decap.miss_rule);
		priv->fs.decap.miss_rule = NULL;
	}
}

static int mlx5e_create_decap_table(struct mlx5e_priv *priv)
{
	int inlen = MLX5_ST_SZ_BYTES(create_flow_group_in);
	struct mlx5e_flow_table *ft = &priv->fs.decap.ft;
	struct mlx5_flow_table_attr ft_attr = {};
	int ix=0;
	int err;
	u32 *decap_in;
	void *match_criteria;

	ft_attr.max_fte = MLX5E_DECAP_TABLE_SIZE;
	ft_attr.level = MLX5E_DECAP_FT_LEVEL;
	ft_attr.prio = MLX5E_NIC_PRIO;
	ft_attr.flags = MLX5_FLOW_TABLE_TUNNEL_EN_DECAP; //decap_en bit

	ft->t = mlx5_create_flow_table(priv->fs.ns, &ft_attr);

	if (IS_ERR(ft->t)) {
		err = PTR_ERR(ft->t);
		ft->t = NULL;
		return err;
	}

	// add more groups for more matchers.
	ft->g = kcalloc(MLX5E_DECAP_NUM_GROUPS, sizeof(*ft->g), GFP_KERNEL);
	if (!ft->g) {
		err = -ENOMEM;
		goto err_destroy_decap_table;
	}

	decap_in = kvzalloc(inlen, GFP_KERNEL);
	if (!decap_in) {
		err = -ENOMEM;
		kfree(ft->g);
		goto err_destroy_decap_table;
	}

	memset(decap_in, 0, inlen);
	match_criteria = MLX5_ADDR_OF(create_flow_group_in, decap_in, match_criteria);
	MLX5_SET_TO_ONES(fte_match_param, match_criteria, outer_headers.ethertype);
	MLX5_SET_TO_ONES(fte_match_param, match_criteria, outer_headers.udp_dport);
	MLX5_SET_TO_ONES(fte_match_param, match_criteria, outer_headers.dst_ipv4_dst_ipv6.ipv4_layout.ipv4);
	MLX5_SET_TO_ONES(fte_match_param, match_criteria, misc_parameters.vxlan_vni);
	MLX5_SET_CFG(decap_in, match_criteria_enable, MLX5_MATCH_OUTER_HEADERS | MLX5_MATCH_MISC_PARAMETERS);
	MLX5_SET_CFG(decap_in, start_flow_index, ix);
	ix += MLX5E_DECAP_MATCHES_TABLE_LEN;
	MLX5_SET_CFG(decap_in, end_flow_index, ix - 1);

	ft->g[ft->num_groups] = mlx5_create_flow_group(ft->t, decap_in);
	if (IS_ERR(ft->g[ft->num_groups]))
		goto err_destroy_groups;
	ft->num_groups++;

	memset(decap_in, 0, inlen);
	MLX5_SET_CFG(decap_in, start_flow_index, ix);
	ix += MLX5E_DECAP_GROUP2_SIZE;
	MLX5_SET_CFG(decap_in, end_flow_index, ix - 1);

	ft->g[ft->num_groups] = mlx5_create_flow_group(ft->t, decap_in);
	if (IS_ERR(ft->g[ft->num_groups]))
		goto err_destroy_groups;
	ft->num_groups++;

	mlx5e_add_decap_table_miss_rule(priv);

	kvfree(decap_in);
	return 0;

err_destroy_groups:
	err = PTR_ERR(ft->g[ft->num_groups]);
	ft->g[ft->num_groups] = NULL;
	mlx5e_destroy_groups(ft);
	kfree(ft->g);
	kvfree(decap_in);
err_destroy_decap_table:
	mlx5_destroy_flow_table(ft->t);
	ft->t = NULL;

	return err;
}

static void mlx5e_destroy_decap_table(struct mlx5e_priv *priv)
{
	mlx5e_del_decap_table_miss_rule(priv);
	mlx5e_destroy_flow_table(&priv->fs.decap.ft);
}

int mlx5e_init_decap_matches_table(struct mlx5e_priv *priv)
{
	struct mlx5e_decap_match_table *decap_match_table;

	decap_match_table = kzalloc(sizeof(*decap_match_table) + MLX5E_DECAP_MATCHES_TABLE_LEN * sizeof(struct mlx5e_decap_match), GFP_ATOMIC);
	if (!decap_match_table)
		return -ENODATA;

	mutex_init(&decap_match_table->lock);

	priv->decap_match_table = decap_match_table;

	return 0;
}

void mlx5e_destroy_decap_matches_table(struct mlx5e_priv *priv)
{
	struct mlx5e_decap_match_table *decap_match_table = priv->decap_match_table;
	int i;

	for (i = 0; i < MLX5E_DECAP_MATCHES_TABLE_LEN; i++) {
			struct mlx5_flow_handle *current_rule = decap_match_table->data[i].rule;

			if (current_rule)
					mlx5_del_flow_rules(current_rule);
	}

	kfree(decap_match_table);
}

int mlx5e_create_flow_steering(struct mlx5e_priv *priv)
{
	struct ttc_params ttc_params = {};
	int tt, err;

	priv->fs.ns = mlx5_get_flow_namespace(priv->mdev,
					       MLX5_FLOW_NAMESPACE_KERNEL);

	if (!priv->fs.ns)
		return -EOPNOTSUPP;

	err = mlx5e_arfs_create_tables(priv);
	if (err) {
		netdev_err(priv->netdev, "Failed to create arfs tables, err=%d\n",
			   err);
#ifdef HAVE_NETDEV_HW_FEATURES
		priv->netdev->hw_features &= ~NETIF_F_NTUPLE;
#endif
	}

	mlx5e_set_ttc_basic_params(priv, &ttc_params);
	mlx5e_set_inner_ttc_ft_params(&ttc_params);
	for (tt = 0; tt < MLX5E_NUM_INDIR_TIRS; tt++)
		ttc_params.indir_tirn[tt] = priv->inner_indir_tir[tt].tirn;

	err = mlx5e_create_inner_ttc_table(priv, &ttc_params, &priv->fs.inner_ttc);
	if (err) {
		netdev_err(priv->netdev, "Failed to create inner ttc table, err=%d\n",
			   err);
		goto err_destroy_arfs_tables;
	}

	mlx5e_set_ttc_ft_params(&ttc_params);
	for (tt = 0; tt < MLX5E_NUM_INDIR_TIRS; tt++)
		ttc_params.indir_tirn[tt] = priv->indir_tir[tt].tirn;

	err = mlx5e_create_ttc_table(priv, &ttc_params, &priv->fs.ttc);
	if (err) {
		netdev_err(priv->netdev, "Failed to create ttc table, err=%d\n",
			   err);
		goto err_destroy_inner_ttc_table;
	}

	err = mlx5e_create_l2_table(priv);
	if (err) {
		netdev_err(priv->netdev, "Failed to create l2 table, err=%d\n",
			   err);
		goto err_destroy_ttc_table;
	}

	err = mlx5e_create_vlan_table(priv);
	if (err) {
		netdev_err(priv->netdev, "Failed to create vlan table, err=%d\n",
			   err);
		goto err_destroy_l2_table;
	}

	err = mlx5e_create_decap_table(priv);
	if (err) {
		netdev_err(priv->netdev, "Failed to create decap table, err=%d\n",
			   err);
		goto err_destroy_vlan_table;
	}

	mlx5e_ethtool_init_steering(priv);

	return 0;

err_destroy_vlan_table:
	mlx5e_destroy_vlan_table(priv);
err_destroy_l2_table:
	mlx5e_destroy_l2_table(priv);
err_destroy_ttc_table:
	mlx5e_destroy_ttc_table(priv, &priv->fs.ttc);
err_destroy_inner_ttc_table:
	mlx5e_destroy_inner_ttc_table(priv, &priv->fs.inner_ttc);
err_destroy_arfs_tables:
	mlx5e_arfs_destroy_tables(priv);

	return err;
}

void mlx5e_destroy_flow_steering(struct mlx5e_priv *priv)
{
	mlx5e_destroy_decap_table(priv);
	mlx5e_destroy_vlan_table(priv);
	mlx5e_destroy_l2_table(priv);
	mlx5e_destroy_ttc_table(priv, &priv->fs.ttc);
	mlx5e_destroy_inner_ttc_table(priv, &priv->fs.inner_ttc);
	mlx5e_arfs_destroy_tables(priv);
	mlx5e_ethtool_cleanup_steering(priv);
}

#ifndef CONFIG_COMPAT_IP_TUNNELS
static inline __be32 tunnel_id_to_key32(__be64 tun_id)
{
#ifdef __BIG_ENDIAN
   return (__force __be32)tun_id;
#else
   return (__force __be32)((__force u64)tun_id >> 32);
#endif
}
#endif

#define MLX5E_ENCAP_CONTEXT_TABLE_LEN MLX5E_MAX_AMOUNT_OF_ACCELERATED_TUNNELS
#define MLX5E_ENCAP_TABLE_LEN (MLX5E_ENCAP_CONTEXT_TABLE_LEN + 1)
#define MLX5E_DONT_ENCAP_TAG 0x0
#define MLX5E_ENCAP_TAG_STAMP 0x0af2
#define MLX5E_ENCAP_TAG_OFFSET (MLX5E_ENCAP_TAG_STAMP << 16)

static int mlx5e_create_encap_header(struct mlx5e_priv *priv, struct mlx5e_encap_context *encap_context)
{
	int encap_size;
	char *encap_header;
	struct vxlanhdr *vxh;
	struct udphdr *uh;
	struct iphdr *ip;
	struct ethhdr *ehdr;

	/*
+	VxLAN header:
+	ETH:   14(VLAN:18)
+	IP:    20
+	UDP:   8
+	VxLAN: 8
+	*/

	int max_encap_size = MLX5_CAP_FLOWTABLE(priv->mdev, max_encap_header_size);
	encap_size = (encap_context->is_vlan ? VLAN_ETH_HLEN : ETH_HLEN) + sizeof(struct iphdr) + VXLAN_HLEN;
	if (encap_size > max_encap_size) {
		mlx5_core_warn(priv->mdev, "encap size %d too big, max supported is %d\n",
			       encap_size, max_encap_size);
		return -EOPNOTSUPP;
	}

	encap_header = kvzalloc(encap_size, GFP_KERNEL);
	if (!encap_header)
		return -ENOMEM;

	/* add ethernet header */
	ehdr = (struct ethhdr *)encap_header;
	ether_addr_copy(ehdr->h_dest, encap_context->mac_dest);
	ether_addr_copy(ehdr->h_source, encap_context->mac_source);

	/* add ip header */
	if (encap_context->is_vlan) {
		struct vlan_hdr *vlan = (struct vlan_hdr *)
					((char *)ehdr + ETH_HLEN);
		ip = (struct iphdr *)((char *)vlan + VLAN_HLEN);
		ehdr->h_proto = htons(encap_context->vlan_proto);
		vlan->h_vlan_TCI = htons(encap_context->vid);
		vlan->h_vlan_encapsulated_proto = htons(ETH_P_IP);
	}
	else {
		ehdr->h_proto = htons(ETH_P_IP);
		ip = (struct iphdr *)((char *)ehdr + ETH_HLEN);
	}

	ip->version = 4;
	ip->ihl = sizeof(struct iphdr) >> 2;
	ip->protocol = IPPROTO_UDP;
	ip->tos = encap_context->tos;
	ip->ttl = encap_context->ttl;
	ip->daddr = encap_context->dst;
	ip->saddr = encap_context->src;
	ip->frag_off = encap_context->frag_off;

	/* add tunneling protocol header */
	uh = (struct udphdr *)((char*)ip + sizeof(struct iphdr));
	uh->dest = encap_context->tp_dst;
	uh->source = htons((encap_context->tp_dst % 1024) + 1025);
	vxh = (struct vxlanhdr *)((char *)uh + sizeof(struct udphdr));
	vxh->vx_flags = VXLAN_HF_VNI;
	vxh->vx_vni = vxlan_vni_field(tunnel_id_to_key32(encap_context->tun_id));

	encap_context->info.encap_header = encap_header;
	encap_context->info.encap_size = encap_size;

	return 0;
}

static int mlx5e_add_encap_rule(struct mlx5e_priv *priv, struct mlx5e_encap_context *encap_context)
{
	struct mlx5_flow_handle *encap_rule;
	struct mlx5_flow_spec *spec;
	struct mlx5e_encap_table *encap = &priv->tx_steering.encap;
	struct mlx5_flow_table *ft = encap->ft.t;
	struct mlx5_encap_info *encap_info = &encap_context->info;
	struct mlx5_flow_act flow_act = {
			   .action = MLX5_FLOW_CONTEXT_ACTION_ALLOW | MLX5_FLOW_CONTEXT_ACTION_PACKET_REFORMAT,
	};
	int err = 0;

	spec = kvzalloc(sizeof(*spec), GFP_KERNEL);
	if (!spec) {
		return -ENOMEM;
	}

	err = mlx5e_create_encap_header(priv, encap_context);
	if (err) {
		mlx5_core_warn(priv->mdev, "Failed to create encap offload header, %d\n",
				err);
		goto err_free_spec;
	}

	err = mlx5_packet_reformat_alloc(priv->mdev,
					MLX5_REFORMAT_TYPE_L2_TO_L2_TUNNEL,
					encap_info->encap_size,
					encap_info->encap_header,
					MLX5_FLOW_NAMESPACE_EGRESS,
					&encap_info->encap_id);
	if (err) {
		mlx5_core_warn(priv->mdev, "Failed to offload cached encapsulation header, %d\n",
				err);
		goto err_free_encap;
	}

	encap_info->encap_id_valid = 1;
	memset(spec, 0, sizeof(*spec));
	flow_act.reformat_id = encap_info->encap_id;
	spec->match_criteria_enable = MLX5_MATCH_MISC_PARAMETERS_2;
	MLX5_SET_TO_ONES(fte_match_param, spec->match_criteria, misc_parameters_2.metadata_reg_a);
	MLX5_SET(fte_match_param, spec->match_value, misc_parameters_2.metadata_reg_a, htonl(encap_context->flow_tag));
	encap_rule = mlx5_add_flow_rules(ft, spec, &flow_act, NULL, 0);
	if (IS_ERR(encap_rule)) {
		err = PTR_ERR(encap_rule);
		netdev_err(priv->netdev, "%s: add encap rule failed, err:%d\n", __func__, err);
		goto err_free_encap;
	}

	encap_context->rule = encap_rule;

	kvfree(spec);
	return 0;

err_free_encap:
	kvfree(encap_info->encap_header);
	encap_info->encap_header = NULL;
err_free_spec:
	kvfree(spec);
	return err;
}

static void mlx5e_del_encap_rule(struct mlx5e_priv *priv, struct mlx5e_encap_context *encap_context)
{
	if (encap_context->info.encap_header){
		kvfree(encap_context->info.encap_header);
		encap_context->info.encap_header = NULL;
	}
	if (encap_context->rule){
		mlx5_del_flow_rules(encap_context->rule);
		encap_context->rule = NULL;
	}
	if (encap_context->info.encap_id_valid){
		mlx5_packet_reformat_dealloc(priv->mdev, encap_context->info.encap_id);
		encap_context->info.encap_id_valid = 0;
	}
}

static int mlx5e_find_encap_context(struct mlx5e_encap_context_table *encap_context_table, __be64 tun_id, __be32 src, __be32 dst,
				 __u8 tos, __u8 ttl, __be16 frag_off, __be16 tp_dst, unsigned char *mac_source, unsigned char *mac_dest)
{
	int i;

	for (i = 0; i < MLX5E_ENCAP_CONTEXT_TABLE_LEN; i++) {
		if (encap_context_table->data[i].ref_count > 0) {
			if (encap_context_table->data[i].tun_id == tun_id &&
				encap_context_table->data[i].src == src &&
				encap_context_table->data[i].tos == tos &&
				encap_context_table->data[i].ttl == ttl &&
				encap_context_table->data[i].frag_off == frag_off &&
				encap_context_table->data[i].tp_dst == tp_dst &&
				ether_addr_equal(encap_context_table->data[i].mac_source, mac_source) &&
				ether_addr_equal(encap_context_table->data[i].mac_dest, mac_dest))
				return i;
		}
	}

	return -ENODATA;
}

static int mlx5e_find_available_encap_context_slot(struct mlx5e_encap_context_table *encap_context_table)
{
	int i;

	for (i = 0; i < MLX5E_ENCAP_CONTEXT_TABLE_LEN; i++) {
		if (encap_context_table->data[i].ref_count == 0)
			return i;
	}

	return -ENODATA;
}

int mlx5e_insert_encap_context(struct net_device *netdev, __be64 tun_id, __be32 src, __be32 dst,
				 __u8 tos, __u8 ttl, __be16 frag_off, __be16 tp_dst, unsigned char *mac_source, unsigned char *mac_dest)
{
	struct mlx5e_priv *priv;
	struct mlx5e_encap_context_table *encap_context_table;
	struct mlx5e_encap_context *new_encap_context;
	int new_entry_index;
	int err = 0;

	if (!netdev_is_mlx5e_netdev(netdev))
		return -EOPNOTSUPP;

	priv = netdev_priv(netdev);
	encap_context_table = priv->tx_steering.encap_context_table;

	mutex_lock(&encap_context_table->lock);

	new_entry_index = mlx5e_find_encap_context(encap_context_table, tun_id, src, dst, tos, ttl, frag_off, tp_dst, mac_source, mac_dest);
	if (new_entry_index < 0)
		new_entry_index = mlx5e_find_available_encap_context_slot(encap_context_table);
	if (new_entry_index < 0) {
		err = -ENODATA;
		goto unlock;
	}

	new_encap_context = &encap_context_table->data[new_entry_index];

	if (new_encap_context->ref_count > 0)
		goto increment_ref_count;

	new_encap_context->tun_id = tun_id;
	new_encap_context->src = src;
	new_encap_context->dst = dst;
	new_encap_context->tos = tos;
	new_encap_context->ttl = ttl;
	new_encap_context->frag_off = frag_off;
	new_encap_context->tp_dst = tp_dst;
	ether_addr_copy(new_encap_context->mac_source, mac_source);
	ether_addr_copy(new_encap_context->mac_dest, mac_dest);
	new_encap_context->flow_tag = new_entry_index + MLX5E_ENCAP_TAG_OFFSET;

	err = mlx5e_add_encap_rule(priv, new_encap_context);
	if (err)
		goto unlock;

increment_ref_count:
	new_encap_context->ref_count++;

unlock:
	mutex_unlock(&encap_context_table->lock);

	return (err < 0) ? err : new_encap_context->flow_tag;
}
EXPORT_SYMBOL(mlx5e_insert_encap_context);

void mlx5e_remove_encap_context(struct net_device *netdev, u32 encap_flow_tag)
{
	struct mlx5e_priv *priv;
	struct mlx5e_encap_context_table *encap_context_table;
	struct mlx5e_encap_context *encap_context;
	int context_index;

	if (!netdev_is_mlx5e_netdev(netdev))
		return;

	context_index = encap_flow_tag - MLX5E_ENCAP_TAG_OFFSET;

	if (context_index < 0 || context_index >= MLX5E_ENCAP_CONTEXT_TABLE_LEN)
		return;

	priv = netdev_priv(netdev);
	encap_context_table = priv->tx_steering.encap_context_table;

	mutex_lock(&encap_context_table->lock);

	encap_context = &encap_context_table->data[context_index];
	if (encap_context->ref_count <= 0)
		goto unlock;

	encap_context->ref_count--;

	if (encap_context->ref_count <= 0) {
		if (encap_context->rule) {
			mlx5e_del_encap_rule(priv, encap_context);
			encap_context->rule = NULL;
		}
	}

unlock:
	mutex_unlock(&encap_context_table->lock);
}
EXPORT_SYMBOL(mlx5e_remove_encap_context);

static int mlx5e_add_encap_table_dont_encap_rule(struct mlx5e_priv *priv)
{
	struct mlx5_flow_table *ft = priv->tx_steering.encap.ft.t;
	struct mlx5_flow_handle *dont_encap_rule;
	struct mlx5_flow_act flow_act = {};
	struct mlx5_flow_spec *spec;
	int err = 0;

	spec = kvzalloc(sizeof(*spec), GFP_KERNEL);
	if (!spec)
		return -ENOMEM;

	memset(spec, 0, sizeof(*spec));
	memset(&flow_act, 0, sizeof(struct mlx5_flow_act));
	flow_act.action = MLX5_FLOW_CONTEXT_ACTION_ALLOW;
	spec->match_criteria_enable = MLX5_MATCH_MISC_PARAMETERS_2;
	MLX5_SET_TO_ONES(fte_match_param, spec->match_criteria, misc_parameters_2.metadata_reg_a);
	MLX5_SET(fte_match_param, spec->match_value, misc_parameters_2.metadata_reg_a, MLX5E_DONT_ENCAP_TAG);

	dont_encap_rule = mlx5_add_flow_rules(ft, spec, &flow_act, NULL, 0);
	if (IS_ERR(dont_encap_rule)) {
		err = PTR_ERR(dont_encap_rule);
		netdev_err(priv->netdev, "%s: add Dont encap rule failed, err:%d\n", __func__, err);
		goto err_free_spec;
	}

	priv->tx_steering.encap.dont_encap_rule = dont_encap_rule;

err_free_spec:
	kvfree(spec);
	return err;
}

static void mlx5e_del_encap_table_dont_encap_rule(struct mlx5e_priv *priv)
{
	if (priv->tx_steering.encap.dont_encap_rule){
		mlx5_del_flow_rules(priv->tx_steering.encap.dont_encap_rule);
		priv->tx_steering.encap.dont_encap_rule = NULL;
	}
}

#define MLX5E_ENCAP_TABLE_MAX_NUM_GROUPS 1
static int mlx5e_create_encap_table(struct mlx5e_priv *priv)
{
	struct mlx5_flow_namespace *root_ns;
	int inlen = MLX5_ST_SZ_BYTES(create_flow_group_in);
	struct mlx5e_flow_table *ft = &priv->tx_steering.encap.ft;
	struct mlx5_flow_table_attr ft_attr = {};
	int ix=0;
	void *match_criteria;
	u32 *encap_in;
	int err;

	root_ns = mlx5_get_flow_namespace(priv->mdev, MLX5_FLOW_NAMESPACE_EGRESS);
	if (!root_ns) {
		netdev_warn(priv->netdev, "Failed to get FDB flow namespace\n");
		err = -EOPNOTSUPP;
		return err;
	}

	ft_attr.max_fte = MLX5E_ENCAP_TABLE_LEN;
	ft_attr.level = MLX5E_ENCAP_FT_LEVEL;
	ft_attr.prio = MLX5E_NIC_PRIO;
	ft_attr.flags = MLX5_FLOW_TABLE_TUNNEL_EN_REFORMAT;

	ft->t = mlx5_create_flow_table(root_ns, &ft_attr);

	if (IS_ERR(ft->t)) {
		err = PTR_ERR(ft->t);
		ft->t = NULL;
		netdev_warn(priv->netdev, "Failed to create Encap Table, err=%d (table prio: %d, level: %d, size: %d)\n",
			 err, MLX5E_NIC_PRIO, MLX5E_ENCAP_FT_LEVEL, MLX5E_ENCAP_TABLE_LEN);
		return err;
	}

	ft->g = kcalloc(MLX5E_ENCAP_TABLE_MAX_NUM_GROUPS, sizeof(*ft->g), GFP_KERNEL);
	if (!ft->g) {
		err = -ENOMEM;
		goto err_destroy_encap_table;
	}

	encap_in = kvzalloc(inlen, GFP_KERNEL);
	if (!encap_in) {
		err = -ENOMEM;
		kfree(ft->g);
		goto err_destroy_encap_table;
	}

	memset(encap_in, 0, inlen);
	match_criteria = MLX5_ADDR_OF(create_flow_group_in, encap_in, match_criteria);
	MLX5_SET_TO_ONES(fte_match_param, match_criteria, misc_parameters_2.metadata_reg_a);
	MLX5_SET_CFG(encap_in, match_criteria_enable, MLX5_MATCH_MISC_PARAMETERS_2);
	MLX5_SET_CFG(encap_in, start_flow_index, ix);
	ix += MLX5E_ENCAP_TABLE_LEN;
	MLX5_SET_CFG(encap_in, end_flow_index, ix - 1);

	ft->g[ft->num_groups] = mlx5_create_flow_group(ft->t, encap_in);
	if (IS_ERR(ft->g[ft->num_groups])) {
		err = PTR_ERR(ft->g[ft->num_groups]);
		ft->g[ft->num_groups] = NULL;
		goto err_destroy_groups;
	}
	ft->num_groups++;

	err = mlx5e_add_encap_table_dont_encap_rule(priv);
	if (err)
		goto err_destroy_groups;

	kvfree(encap_in);
	return 0;

err_destroy_groups:
	mlx5e_destroy_groups(ft);
	kfree(ft->g);
	kvfree(encap_in);
err_destroy_encap_table:
	mlx5_destroy_flow_table(ft->t);
	ft->t = NULL;
	return err;
}

static void mlx5e_destroy_encap_table(struct mlx5e_priv *priv)
{
	mlx5e_del_encap_table_dont_encap_rule(priv);
	mlx5e_destroy_flow_table(&priv->tx_steering.encap.ft);
}

int mlx5e_init_encap_context_table(struct mlx5e_priv *priv)
{
	struct mlx5e_encap_context_table *encap_context_table;

	encap_context_table = kzalloc(sizeof(*encap_context_table) + MLX5E_ENCAP_CONTEXT_TABLE_LEN * sizeof(struct mlx5e_encap_context), GFP_ATOMIC);
	if (!encap_context_table)
		return -ENODATA;

	mutex_init(&encap_context_table->lock);

	priv->tx_steering.encap_context_table = encap_context_table;

	return 0;
}

void mlx5e_destroy_encap_context_table(struct mlx5e_priv *priv)
{
	struct mlx5e_encap_context_table *encap_context_table = priv->tx_steering.encap_context_table;
	int i;

	for (i = 0; i < MLX5E_ENCAP_CONTEXT_TABLE_LEN; i++)
			mlx5e_del_encap_rule(priv, &encap_context_table->data[i]);

	kfree(encap_context_table);
}

int mlx5e_create_tx_steering(struct mlx5e_priv *priv)
{
	int err;

	err = mlx5e_init_encap_context_table(priv);
	if (err) {
		netdev_err(priv->netdev, "Failed to init encap context table, err=%d\n",
			   err);
		goto error_out;
	}

	err = mlx5e_create_encap_table(priv);
	if (err) {
		netdev_err(priv->netdev, "Failed to create Encap Table, err=%d\n",
			   err);
		goto destroy_encap_context_table;
	}

	return 0;

destroy_encap_context_table:
	mlx5e_destroy_encap_context_table(priv);
error_out:
	return err;
}

void mlx5e_destroy_tx_steering(struct mlx5e_priv *priv)
{
	mlx5e_destroy_encap_context_table(priv);
	mlx5e_destroy_encap_table(priv);
}