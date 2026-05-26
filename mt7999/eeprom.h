/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (C) 2026 MediaTek Inc.
 */

#ifndef __MT7999_EEPROM_H
#define __MT7999_EEPROM_H

#include "mt7999.h"

enum mt7999_eeprom_field {
	MT_EE_CHIP_ID =			0x000,
	MT_EE_BASE_VERSION =		0x002,
	MT_EE_MAC_WAN_ADDR =		0x004,
	MT_EE_MAC_LAN_ADDR =		0x00a,
	MT_EE_WIFI_CONF_BASE =		0x170,
	MT_EE_MAC_ADDR_BASE =		0x17a,
	MT_EE_DO_RX_GAIN_CAL =		0x1a1,
	MT_EE_DO_PRE_CAL =		0x1a5,
	MT_EE_TESTMODE_EN =		0x1af,
	MT_EE_VERSION =			0x1300,
	MT_EE_TX0_POWER_2G =		0x2000,
	MT_EE_RATE_DELTA_2G =		0x2010,
	MT_EE_TX0_POWER_5G_B0 =		0x2400,	/* for band 0 5g variant */
	MT_EE_RATE_DELTA_5G_B0 =	0x2420,
	MT_EE_TX0_POWER_5G =		0x2c00,
	MT_EE_RATE_DELTA_5G =		0x2c20,
	MT_EE_TX0_POWER_6G =		0x4000,
	MT_EE_RATE_DELTA_6G =		0x4030,
	MT_EE_RX_GAIN_CAL =		0x5b00,

	__MT_EE_MAX =			0x63ff,
	/* offsets after 0x6410 are used to save cal data */
	MT_EE_PRECAL =			0x6410,
};

#define MT_EE_WIFI_CONF(_band)			(MT_EE_WIFI_CONF_BASE + (_band) * 0x10)
#define MT_EE_MAC_ADDR(_band)			(MT_EE_MAC_ADDR_BASE + (_band) * 0x10)
#define MT_EE_WIFI_CONF_BAND_SEL		GENMASK(2, 0)
#define MT_EE_WIFI_CONF_PA_LNA			GENMASK(4, 3)
#define MT_EE_WIFI_CONF_TX_PATH			GENMASK(2, 0)
#define MT_EE_WIFI_CONF_RX_PATH			GENMASK(5, 3)
#define MT_EE_WIFI_CONF_STREAM_NUM		GENMASK(2, 0)

enum mt7999_eeprom_band {
	MT_EE_BAND_SEL_DEFAULT,
	MT_EE_BAND_SEL_2GHZ,
	MT_EE_BAND_SEL_5GHZ,
	MT_EE_BAND_SEL_6GHZ,
};

#define MT_EE_RATE_DELTA_MASK			GENMASK(5, 0)
#define MT_EE_RATE_DELTA_SIGN			BIT(6)
#define MT_EE_RATE_DELTA_EN			BIT(7)

#define MT_EE_WIFI_CAL_RX_GAIN			GENMASK(5, 3)

#define MT_EE_WIFI_CAL_GROUP_2G			BIT(0)
#define MT_EE_WIFI_CAL_GROUP_5G			BIT(1)
#define MT_EE_WIFI_CAL_GROUP_6G			BIT(2)
#define MT_EE_WIFI_CAL_GROUP			GENMASK(2, 0)
#define MT_EE_WIFI_CAL_DPD_2G			BIT(3)
#define MT_EE_WIFI_CAL_DPD_5G			BIT(4)
#define MT_EE_WIFI_CAL_DPD_6G			BIT(5)
#define MT_EE_WIFI_CAL_DPD			GENMASK(5, 3)

#define MT_EE_CAL_UNIT				1024

enum mt7999_prek_rev {
	GROUP_SIZE_2G,
	GROUP_SIZE_5G,
	GROUP_SIZE_6G,
	GROUP_SIZE_2G_V2,
	GROUP_SIZE_ABAND_V2,
	ADCDCOC_SIZE_2G,
	ADCDCOC_SIZE_5G,
	ADCDCOC_SIZE_6G,
	DPD_LEGACY_SIZE,
	DPD_MEM_SIZE,
	DPD_OTFG0_SIZE,
};

static const u32 mt7999_prek_rev[] = {
	[GROUP_SIZE_2G] =			4 * MT_EE_CAL_UNIT,
	[GROUP_SIZE_5G] =			45 * MT_EE_CAL_UNIT,
	[GROUP_SIZE_6G] =			125 * MT_EE_CAL_UNIT,
	[ADCDCOC_SIZE_2G] =			4 * 4,
	[ADCDCOC_SIZE_5G] =			4 * 4,
	[ADCDCOC_SIZE_6G] =			4 * 5,
	[DPD_LEGACY_SIZE] =			4 * MT_EE_CAL_UNIT,
	[DPD_MEM_SIZE] =			13 * MT_EE_CAL_UNIT,
	[DPD_OTFG0_SIZE] =			2 * MT_EE_CAL_UNIT,
};

static const u32 mt7999_prek_rev_233[] = {
	[GROUP_SIZE_2G] =			4 * MT_EE_CAL_UNIT,
	[GROUP_SIZE_5G] =			44 * MT_EE_CAL_UNIT,
	[GROUP_SIZE_6G] =			125 * MT_EE_CAL_UNIT,
	[ADCDCOC_SIZE_2G] =			4 * 4,
	[ADCDCOC_SIZE_5G] =			4 * 4,
	[ADCDCOC_SIZE_6G] =			4 * 5,
	[DPD_LEGACY_SIZE] =			4 * MT_EE_CAL_UNIT,
	[DPD_MEM_SIZE] =			13 * MT_EE_CAL_UNIT,
	[DPD_OTFG0_SIZE] =			2 * MT_EE_CAL_UNIT,
};

extern const struct ieee80211_channel dpd_2g_ch_list_bw20[];
extern const struct ieee80211_channel dpd_5g_skip_ch_list[];
extern const struct ieee80211_channel dpd_5g_ch_list_bw80[];
extern const struct ieee80211_channel dpd_5g_ch_list_bw160[];
extern const struct ieee80211_channel dpd_6g_ch_list_bw80[];
extern const struct ieee80211_channel dpd_6g_ch_list_bw160[];
extern const struct ieee80211_channel dpd_6g_ch_list_bw320[];

#define PREK(id)				(dev->prek.rev[(id)])
#define DPD_CH_NUM(_type)			(dev->prek.dpd_ch_num[DPD_CH_NUM_##_type])
#define MT_EE_CAL_GROUP_SIZE			(PREK(GROUP_SIZE_2G) + PREK(GROUP_SIZE_5G) + \
						 PREK(GROUP_SIZE_6G) + PREK(ADCDCOC_SIZE_2G) + \
						 PREK(ADCDCOC_SIZE_5G) + PREK(ADCDCOC_SIZE_6G))
#define DPD_PER_CH_BW20_SIZE			(PREK(DPD_LEGACY_SIZE) + PREK(DPD_OTFG0_SIZE))
#define DPD_PER_CH_GT_BW20_SIZE			(PREK(DPD_MEM_SIZE) ? PREK(DPD_MEM_SIZE) + \
								      PREK(DPD_OTFG0_SIZE) : \
								      DPD_PER_CH_BW20_SIZE)
#define MT_EE_CAL_DPD_SIZE_2G			(DPD_CH_NUM(BW20_2G) * DPD_PER_CH_BW20_SIZE)
#define MT_EE_CAL_DPD_SIZE_5G			(DPD_CH_NUM(BW20_5G) * DPD_PER_CH_BW20_SIZE + \
						 DPD_CH_NUM(BW80_5G) * DPD_PER_CH_GT_BW20_SIZE + \
						 DPD_CH_NUM(BW160_5G) * DPD_PER_CH_GT_BW20_SIZE)
#define MT_EE_CAL_DPD_SIZE_6G			(DPD_CH_NUM(BW20_6G) * DPD_PER_CH_BW20_SIZE + \
						 DPD_CH_NUM(BW80_6G) * DPD_PER_CH_GT_BW20_SIZE + \
						 DPD_CH_NUM(BW160_6G) * DPD_PER_CH_GT_BW20_SIZE + \
						 DPD_CH_NUM(BW320_6G) * DPD_PER_CH_GT_BW20_SIZE)
#define MT_EE_CAL_DPD_SIZE			(MT_EE_CAL_DPD_SIZE_2G + MT_EE_CAL_DPD_SIZE_5G + \
						 MT_EE_CAL_DPD_SIZE_6G)

#define MT_EE_CAL_RX_GAIN_SIZE			748

enum rf_cal_item {
	RF_RX_GAIN_CAL = BIT(24),
	RF_DPD_FLAT_CAL = BIT(28),
	RF_PRE_CAL = BIT(29),
	RF_DPD_FLAT_5G_CAL = GENMASK(29, 28),
	RF_DPD_FLAT_5G_MEM_CAL = (BIT(30) | BIT(28)),
	RF_DPD_FLAT_6G_CAL = GENMASK(30, 28),
	RF_DPD_FLAT_6G_MEM_CAL = (BIT(31) | BIT(28)),
};

static inline int
mt7999_get_channel_group_5g(int channel)
{
	if (channel <= 64)
		return 0;
	if (channel <= 96)
		return 1;
	if (channel <= 128)
		return 2;
	if (channel <= 144)
		return 3;
	return 4;
}

static inline int
mt7999_get_channel_group_6g(int channel)
{
	if (channel <= 29)
		return 0;

	return DIV_ROUND_UP(channel - 29, 32);
}

enum mt7999_sku_rate_group {
	SKU_CCK,
	SKU_OFDM,

	SKU_HT20,
	SKU_HT40,

	SKU_VHT20,
	SKU_VHT40,
	SKU_VHT80,
	SKU_VHT160,

	SKU_HE26,
	SKU_HE52,
	SKU_HE106,
	SKU_HE242,
	SKU_HE484,
	SKU_HE996,
	SKU_HE2x996,

	SKU_EHT26,
	SKU_EHT52,
	SKU_EHT106,
	SKU_EHT242,
	SKU_EHT484,
	SKU_EHT996,
	SKU_EHT2x996,
	SKU_EHT4x996,
	SKU_EHT26_52,
	SKU_EHT26_106,
	SKU_EHT484_242,
	SKU_EHT996_484,
	SKU_EHT996_484_242,
	SKU_EHT2x996_484,
	SKU_EHT3x996,
	SKU_EHT3x996_484,

	SKU_UHR26,
	SKU_UHR52,
	SKU_UHR106,
	SKU_UHR242,
	SKU_UHR484,
	SKU_UHR996,
	SKU_UHR2x996,
	SKU_UHR4x996,
	SKU_UHR26_52,
	SKU_UHR26_106,
	SKU_UHR484_242,
	SKU_UHR996_484,
	SKU_UHR996_484_242,
	SKU_UHR2x996_484,
	SKU_UHR3x996,
	SKU_UHR3x996_484,

	MAX_SKU_RATE_GROUP_NUM,
};

extern const u8 mt7999_sku_group_len[MAX_SKU_RATE_GROUP_NUM];

static inline int
mt7999_get_sku_rate_num(void)
{
	int i, num = 0;

	for (i = 0; i < MAX_SKU_RATE_GROUP_NUM; i++)
		num += mt7999_sku_group_len[i];

	return num;
}

#endif
