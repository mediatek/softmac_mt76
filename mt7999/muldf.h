/* SPDX-License-Identifier: ISC */
/*
 * Copyright (C) 2026 MediaTek Inc.
 */
#ifndef __MT7999_MULDF_H_
#define __MT7999_MULDF_H_

#ifdef CONFIG_MULDF_HELPER

struct mt7999_dev;

enum {
	CM_MOD_COMMON             = 0x1,
	CM_MOD_TX                 = 0x2,
	CM_MOD_RX                 = 0x3,
	CM_MOD_MAC                = 0x4,
	CM_MOD_PHY                = 0x5,
	CM_MOD_HIF                = 0x6,
	CM_MOD_RADIO              = 0x7,
	CM_MOD_LOW_POWER          = 0x8,
	CM_MOD_BSS                = 0x9,
	CM_MOD_CHIP_HEALTH_CHECK  = 0xA,
	CM_MOD_SECURITY           = 0xB,
	CM_MOD_WIFI_SYS           = 0xC,
	CM_MOD_DMA                = 0xD,
};

enum {
	CM_SUBMOD_MAC_QUEUE         = 0x1, /* Queue hang / anomaly */
	CM_SUBMOD_MAC_STATE_MACHINE = 0x2, /* Abnormal FSM state */
	CM_SUBMOD_MAC_PAUSE         = 0x3, /* STA pause anomaly */
};

enum {
	CM_SUBMOD_RA = 0x1, /* Rate Adaptation anomaly */
};

enum {
	CM_SUBMOD_RADIO_PROBE = 0x1,
	CM_SUBMOD_RADIO_SCN   = 0x2,
};

enum {
	CM_SUBMOD_BSS_AP    = 0x1,
	CM_SUBMOD_BSS_INFRA = 0x2,
	CM_SUBMOD_BSS_P2P   = 0x3,
	CM_SUBMOD_BSS_SAP   = 0x4,
	CM_SUBMOD_BSS_NAN   = 0x5,
};

enum {
	CM_SUBMOD_SEC_VIO_MPU                   = 0x1,
	CM_SUBMOD_SEC_VIO_DEVAPC                = 0x2,
	CM_SUBMOD_SEC_VIO_IDLM_PROTECTOR        = 0x3,
	CM_SUBMOD_SEC_VIO_BUS_MONITOR           = 0x4,
	CM_SUBMOD_SEC_VIO_WFDMA_ALLOWLIST       = 0x5,
	CM_SUBMOD_SEC_VIO_UNUSED_FUNC_PTR       = 0x6,
	CM_SUBMOD_SEC_VIO_INVALID_RXBUF         = 0x7,
	CM_SUBMOD_SEC_VIO_STACK_CANARY_CORRUPT  = 0x10,
	CM_SUBMOD_SEC_VIO_MPU_SETTING_CORRUPT   = 0x11,
	CM_SUBMOD_SEC_VIO_CODE_MEMORY_CORRUPT   = 0x12,
	CM_SUBMOD_SEC_VIO_PROTECTED_DATA_CORRUPT = 0x13,
	CM_SUBMOD_SEC_VIO_DRBG_HEALTH_CHECK_FAIL = 0x14,
	CM_SUBMOD_SEC_VIO_PSE_HEALTH_CHECK_FAIL  = 0x15,
	CM_SUBMOD_SEC_VIO_OTHER_HEALTH_CHECK_FAIL = 0x16,
	CM_SUBMOD_SEC_VIO_GENERAL_EXCEPTIO       = 0x17,  /* NOTE: kept as-is from spec */
	CM_SUBMOD_SEC_VIO_HEAP_DOUBLE_FREE        = 0x20,
	CM_SUBMOD_SEC_VIO_HEAP_FREE_INVALID_ADDR  = 0x21,
	CM_SUBMOD_SEC_VIO_HEAP_FREE_UNUSED_BLOCK  = 0x22,
	CM_SUBMOD_SEC_VIO_HEAP_METADATA_VALIDATION = 0x23,
	CM_SUBMOD_SEC_VIO_HEAP_MEM_CHECK_FAIL      = 0x24,
	CM_SUBMOD_SEC_VIO_HEAP_LINK_CHECK_FAIL     = 0x25,
};

enum {
	CM_SUBMOD_WIFI_SYS_MSG_TIME = 0x1,
};

enum {
	CM_SUBMOD_WFDMA = 0x1, /* WFDMA anomaly */
};

struct cm_tlv {
	__le16 tag;
	__le16 len;
	__le16 fail_type;
	__le16 padding;
	u8 data[];
} __packed;

#define CM_MAC_QUEUE_BATCH_MAX_NUM 16
struct cm_mac_queue_info_batch {
	__le16 count;
	__le16 rsv;
	struct cm_mac_queue_info entry[CM_MAC_QUEUE_BATCH_MAX_NUM];
} __packed;

#define CM_MAC_PAUSE_BATCH_MAX_NUM 176 /* 44 STA * 4 AC */
struct cm_mac_pause_info_batch {
	__le16 count;
	__le16 rsv;
	struct cm_mac_pause_info entry[CM_MAC_PAUSE_BATCH_MAX_NUM];
} __packed;

struct cm_mac_sm_info {
	u8 cr_idx;
	u8 bss_idx;
	u8 detected_module;
	u8 rsv;
	__le32 val;
} __packed;

struct cm_phy_ra_info {
	__le16 wcid;
	__le16 fail_reason;
} __packed;

struct cm_dma_wpdma_info {
	__le32 rsv;
} __packed;

void mt7999_muldf_canary_monitor_event(struct mt7999_dev *dev,
				       struct sk_buff *skb);
#endif
#endif
