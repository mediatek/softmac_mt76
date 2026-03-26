/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (C) 2026 MediaTek Inc.
 */

#ifndef __MT7999_MAC_H
#define __MT7999_MAC_H

#include "../mt76_connac5_mac.h"

struct mt7999_dfs_pulse {
	u32 max_width;		/* us */
	int max_pwr;		/* dbm */
	int min_pwr;		/* dbm */
	u32 min_stgr_pri;	/* us */
	u32 max_stgr_pri;	/* us */
	u32 min_cr_pri;		/* us */
	u32 max_cr_pri;		/* us */
};

struct mt7999_dfs_pattern {
	u8 enable;
	u8 stgr;
	u8 min_crpn;
	u8 max_crpn;
	u8 min_crpr;
	u8 min_pw;
	u32 min_pri;
	u32 max_pri;
	u8 max_pw;
	u8 min_crbn;
	u8 max_crbn;
	u8 min_stgpn;
	u8 max_stgpn;
	u8 min_stgpr;
	u8 rsv[2];
	u32 min_stgpr_diff;
} __packed;

#define MT7999_SDO_EVENT_COUNT			GENMASK(26, 20)
#define MT7999_SDO_EVENT_DW_LEN			GENMASK(31, 27)
#define MT7999_SDO_EVENT_ID			GENMASK(26, 21)

#define MT7999_SDO_EVENT_BA_TRIG_WLAN_IDX	GENMASK(13, 0)
#define MT7999_SDO_EVENT_BA_TRIG_TID		GENMASK(16, 14)
enum {
	MT7999_SDO_EVENT_BA_TRIGGER,
};

#endif
