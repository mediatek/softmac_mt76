/* SPDX-License-Identifier: ISC */
/*
 * Copyright (C) 2026 MediaTek Inc.
 */
#ifndef __MT76_MULDF_H_
#define __MT76_MULDF_H_

#ifdef CONFIG_MULDF_HELPER
#include <linux/soc/mediatek/muldf.h>
#include <linux/soc/mediatek/muldf_monitor.h>

#define MULDF_MODULE_NAME	"mt76"

struct muldf_query_data {
	struct poll_info *info;
	u32 cur_idx;
};

struct muldf {
	u8 *data; /* for relay */

	/* for fwlog */
	bool fwlog_en;
	bool fwlog_bin_en;
	bool fwlog_txt_en;

	/* for consys monitor */
	bool monitor_en;
	int (*query)(struct mt76_dev *dev, struct muldf_query_data *data);
};

enum cm_queue_type {
	CM_PLE_AC_Q = 0,
	CM_PLE_TXCMD_Q,
	CM_PLE_ALTX_Q,
	CM_PLE_BMC_Q,
	CM_PSE_HIF_Q,
	CM_PSE_MDP_RX_Q,
	CM_PSE_SEC_RX_Q,
	CM_ARB_WMM_AC_Q,
	CM_ARB_ALTX_Q,
	CM_ARB_BCN_Q,
	CM_ARB_BMC_Q,
	CM_MONITOR_QUEUE_TYPE_MAX
};

struct muldf_tlv {
	enum consys_info_tag tag;
	u32 len;
	u8 data[];
} __packed;

struct canary_monitor_event {
	u8 rsv[4];
	u8 buf[];
} __packed;

struct cm_common_info {
	__le16 tag;
	__le16 len;
	__le32 cur_systime;
	__le32 mod;
	__le32 sub_mod;
	__le32 err_code;
	__le32 utc_sec;
	__le32 utc_usec;
	__le32 seq_num;
	u8 tlv_present;
	u8 rsv[11];
} __packed;

struct cm_mac_queue_info {
	u8 q_type;
	u8 q_idx;
	u8 band;
	u8 bss_idx;
	__le16 head_fid;
	__le16 txd_ac_head_fid;
} __packed;

struct cm_mac_pause_info {
	__le16 wcid;
	__le16 reason;
	u8 ac_num;
	u8 rsv[3];
} __packed;

int mt76_muldf_init(struct mt76_dev *dev, struct dentry *debugfs_dir);
void mt76_muldf_exit(struct mt76_dev *dev);
struct muldf_tlv *
mt76_muldf_query_add_tlv(struct muldf_query_data *data, enum consys_info_tag tag, u32 len);
int mt76_muldf_monitor_mac_queue_check(struct mt76_dev *dev,
				       struct cm_mac_queue_info *info);

static inline int
mt76_muldf_drv_notify(struct mt76_dev *dev, u8 type, bool err)
{
	return muldf_monitor_drv_notify(dev, type, err);
}
#else
static inline int mt76_muldf_init(struct mt76_dev *dev, struct dentry *debugfs_dir)
{
	return 0;
}

static inline void mt76_muldf_exit(struct mt76_dev *dev)
{}

static inline int
mt76_muldf_drv_notify(struct mt76_dev *dev, u8 type, bool err)
{
	return 0;
}
#endif /* CONFIG_MULDF_HELPER */

#endif /* __MT76_MULDF_H_ */
