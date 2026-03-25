// SPDX-License-Identifier: ISC
/*
 * Copyright (C) 2026 MediaTek Inc.
 */

#include "mt76.h"

static void
mt76_muldf_register_debugfs(struct mt76_dev *dev, struct dentry *dir)
{
	debugfs_create_bool("muldf_fwlog_en", 0600, dir, &dev->muldf.fwlog_en);
	debugfs_create_bool("muldf_fwlog_bin_en", 0600, dir, &dev->muldf.fwlog_bin_en);
	debugfs_create_bool("muldf_fwlog_txt_en", 0600, dir, &dev->muldf.fwlog_txt_en);

	debugfs_create_bool("muldf_monitor_en", 0600, dir, &dev->muldf.monitor_en);
}

struct muldf_tlv *
mt76_muldf_query_add_tlv(struct muldf_query_data *data, enum consys_info_tag tag, u32 len)
{
	struct muldf_tlv *tlv = (void *)(data->info->tlv_buf + data->cur_idx);
	u32 next_idx = data->cur_idx + len;

	if (next_idx > data->info->tlv_buf_len)
		return NULL;

	tlv->tag = tag;
	tlv->len = len;
	data->cur_idx = next_idx;

	return tlv;
}
EXPORT_SYMBOL_GPL(mt76_muldf_query_add_tlv);

static int
mt76_muldf_sys_info_tlv(struct muldf_query_data *data, struct mt76_dev *dev)
{
	struct consys_sys_info *sys_info;
	struct muldf_tlv *tlv;
	struct mt76_phy *phy;
	u32 flags = 0;
	int i;

	tlv = mt76_muldf_query_add_tlv(data, CONSYS_SYS_INFO, sizeof(*sys_info));
	if (!tlv)
		return -ENOMEM;

	sys_info = (struct consys_sys_info *)tlv;
	for (i = 0; i < ARRAY_SIZE(dev->phys); i++) {
		phy = dev->phys[i];
		if (!phy)
			continue;

		if (!test_bit(MT76_STATE_RUNNING, &phy->state)) {
			flags |= BIT(CONSYS_FLAG_SYSTEM_SUSPEND) |
				 BIT(CONSYS_FLAG_RADIO_OFF);
		}

		if (mt76_phy_dfs_state(phy) == MT_DFS_STATE_CAC)
			flags |= BIT(CONSYS_FLAG_DFS_SILENCE);

		if (test_bit(MT76_RESET, &phy->state))
			flags |= BIT(CONSYS_FLAG_SER);
	}

	sys_info->sys_flag = flags;

	return 0;
}

static int
mt76_muldf_query(const void *priv, struct poll_info *poll_info)
{
	struct muldf_query_data data = { .info = poll_info };
	struct mt76_dev *dev = (void *)priv;
	int ret;

	if (!dev->muldf.monitor_en)
		return 0;

	ret = mt76_muldf_sys_info_tlv(&data, dev);
	if (ret)
		return ret;

	return dev->muldf.query(dev, &data);
}

/* TODO: sync more driver-reporting events */
static const struct monitor_module_info consys_module = {
	.name = MULDF_MONITOR_MODULE_CONSYS,
	.types = {
		/* TX status monitor types */
		CONSYS_MONITOR_TXSTATUS_PHY,
#ifdef CONFIG_FW_CANARY_MONITOR
		CONSYS_MONITOR_TXSTATUS_TXCMD,	CONSYS_MONITOR_TXSTATUS_TXD,
#endif
		CONSYS_MONITOR_TXSTATUS_WM,	CONSYS_MONITOR_TXSTATUS_SDO,
		CONSYS_MONITOR_TXSTATUS_DMA,	CONSYS_MONITOR_TXSTATUS_HOST,
		/* RX status monitor types */
		CONSYS_MONITOR_RXSTATUS_HOST,	CONSYS_MONITOR_RXSTATUS_WM,
		CONSYS_MONITOR_RXSTATUS_SDO,	CONSYS_MONITOR_RXSTATUS_DMA,
#ifdef CONFIG_FW_CANARY_MONITOR
		CONSYS_MONITOR_RXSTATUS_UMAC,
#endif
		CONSYS_MONITOR_RXSTATUS_PHY,
		/* Other monitor types */
		CONSYS_MONITOR_CMD_TIMEOUT,	CONSYS_MONITOR_NOBCN,
		CONSYS_MONITOR_L1SER,		CONSYS_MONITOR_ICV,
	},
	.fn = mt76_muldf_query,
};

#define X(_cat, _val)	(#_cat),
static const char * const debug_category[] = {
	MT76_DBG_LIST
};
#undef X

int mt76_muldf_init(struct mt76_dev *dev, struct dentry *debugfs_dir)
{
#define MULDF_MAX_HEX_PRINT_LEN 2000
	int ret, i;

	dev->muldf.data = kzalloc(MULDF_MAX_HEX_PRINT_LEN, GFP_KERNEL);
	if (!dev->muldf.data)
		return -ENOMEM;

	muldf_add_module(MULDF_MODULE_NAME, MTK_MSG_INFO);

	if (dev->muldf.query) {
		ret = muldf_monitor_register(dev, mt76_chip(dev), &consys_module);
		if (ret)
			goto error;
	}

	for (i = 0; i < ARRAY_SIZE(debug_category); i++) {
		muldf_add_cat(MULDF_MODULE_NAME, debug_category[i]);
		muldf_set_sub_cat(MULDF_MODULE_NAME, debug_category[i],
				  debug_category[i], MTK_DUMP_NONE, 1);
	}

	mt76_muldf_register_debugfs(dev, debugfs_dir);

	return 0;

error:
	mt76_muldf_exit(dev);
	return ret;
}
EXPORT_SYMBOL_GPL(mt76_muldf_init);

void mt76_muldf_exit(struct mt76_dev *dev)
{
	if (!dev->muldf.data)
		return;

	muldf_monitor_unregister(dev, mt76_chip(dev), &consys_module);
	muldf_del_module(MULDF_MODULE_NAME);

	kfree(dev->muldf.data);
	dev->muldf.data = NULL;
}
EXPORT_SYMBOL_GPL(mt76_muldf_exit);

struct tlv {
	__le16 tag;
	__le16 len;
	u8 data[];
} __packed;

int mt76_muldf_monitor_mac_queue_check(struct mt76_dev *dev,
				       struct cm_mac_queue_info *info)
{
	switch (info->q_type) {
	case CM_PLE_AC_Q:
	case CM_PLE_ALTX_Q:
	case CM_PLE_BMC_Q:
		mt76_muldf_drv_notify(dev, CONSYS_MONITOR_TXSTATUS_TXD, true);
		break;
	case CM_PLE_TXCMD_Q:
		mt76_muldf_drv_notify(dev, CONSYS_MONITOR_TXSTATUS_TXCMD, true);
		break;
	case CM_PSE_HIF_Q:
	case CM_PSE_MDP_RX_Q:
	case CM_PSE_SEC_RX_Q:
		mt76_muldf_drv_notify(dev, CONSYS_MONITOR_RXSTATUS_UMAC, true);
		break;
	case CM_ARB_ALTX_Q:
	case CM_ARB_BCN_Q:
	case CM_ARB_BMC_Q:
	case CM_ARB_WMM_AC_Q:
		/* TBD */
		break;
	default:
		break;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(mt76_muldf_monitor_mac_queue_check);
