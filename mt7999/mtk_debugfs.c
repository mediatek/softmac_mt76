// SPDX-License-Identifier: ISC
/*
 * Copyright (C) 2026 MediaTek Inc.
 */
#include <linux/vmalloc.h>
#include "mt7999.h"
#include "../mt76.h"
#include "mcu.h"
#include "mac.h"
#include "eeprom.h"
#include "mtk_debug.h"
#include "mtk_mcu.h"
#include "coredump.h"

#ifdef CONFIG_MTK_DEBUG

#ifndef PKG_YEAR
#define PKG_YEAR 0
#endif

#ifndef PKG_MONTH
#define PKG_MONTH 0
#endif

/* AGG INFO */
static int mt7999_agginfo_show(struct seq_file *s, void *data)
{
#define MT7999_BA_WINDOW_CNT	15
	struct mt7999_phy *phy = s->private;
	struct mt7999_dev *dev = phy->dev;
	u64 total_burst, total_ampdu, ampdu_cnt[16];
	u32 value, idx, row_idx, col_idx, start_range, agg_rang_sel[16], burst_cnt[16], band_offset = 0;
	u8 partial_str[16] = {}, full_str[64] = {};
	u8 band_idx = phy->mt76->band_idx;

	switch (band_idx) {
	case 0:
		band_offset = 0;
		break;
	case 1:
		band_offset = BN1_WF_AGG_TOP_BASE - BN0_WF_AGG_TOP_BASE;
		break;
	case 2:
		band_offset = IP1_BN0_WF_AGG_TOP_BASE - BN0_WF_AGG_TOP_BASE;
		break;
	default:
		return 0;
	}

	seq_printf(s, "Band %d AGG Status\n", band_idx);
	seq_printf(s, "===============================\n");
	value = mt76_rr(dev, BN0_WF_AGG_TOP_AALCR0_ADDR + band_offset);
	seq_printf(s, "AC00 Agg limit = %d\t", u32_get_bits(value, BN0_WF_AGG_TOP_AALCR0_AC00_AGG_LIMIT_MASK));
	seq_printf(s, "AC01 Agg limit = %d\t", u32_get_bits(value, BN0_WF_AGG_TOP_AALCR0_AC01_AGG_LIMIT_MASK));
	value = mt76_rr(dev, BN0_WF_AGG_TOP_AALCR1_ADDR + band_offset);
	seq_printf(s, "AC02 Agg limit = %d\t", u32_get_bits(value, BN0_WF_AGG_TOP_AALCR1_AC02_AGG_LIMIT_MASK));
	seq_printf(s, "AC03 Agg limit = %d\n", u32_get_bits(value, BN0_WF_AGG_TOP_AALCR1_AC03_AGG_LIMIT_MASK));
	value = mt76_rr(dev, BN0_WF_AGG_TOP_AALCR2_ADDR + band_offset);
	seq_printf(s, "AC10 Agg limit = %d\t", u32_get_bits(value, BN0_WF_AGG_TOP_AALCR2_AC10_AGG_LIMIT_MASK));
	seq_printf(s, "AC11 Agg limit = %d\t", u32_get_bits(value, BN0_WF_AGG_TOP_AALCR2_AC11_AGG_LIMIT_MASK));
	value = mt76_rr(dev, BN0_WF_AGG_TOP_AALCR3_ADDR + band_offset);
	seq_printf(s, "AC12 Agg limit = %d\t", u32_get_bits(value, BN0_WF_AGG_TOP_AALCR3_AC12_AGG_LIMIT_MASK));
	seq_printf(s, "AC13 Agg limit = %d\n", u32_get_bits(value, BN0_WF_AGG_TOP_AALCR3_AC13_AGG_LIMIT_MASK));
	value = mt76_rr(dev, BN0_WF_AGG_TOP_AALCR4_ADDR + band_offset);
	seq_printf(s, "AC20 Agg limit = %d\t", u32_get_bits(value, BN0_WF_AGG_TOP_AALCR4_AC20_AGG_LIMIT_MASK));
	seq_printf(s, "AC21 Agg limit = %d\t", u32_get_bits(value, BN0_WF_AGG_TOP_AALCR4_AC21_AGG_LIMIT_MASK));
	value = mt76_rr(dev, BN0_WF_AGG_TOP_AALCR5_ADDR + band_offset);
	seq_printf(s, "AC22 Agg limit = %d\t", u32_get_bits(value, BN0_WF_AGG_TOP_AALCR5_AC22_AGG_LIMIT_MASK));
	seq_printf(s, "AC23 Agg limit = %d\n", u32_get_bits(value, BN0_WF_AGG_TOP_AALCR5_AC23_AGG_LIMIT_MASK));
	value = mt76_rr(dev, BN0_WF_AGG_TOP_AALCR6_ADDR + band_offset);
	seq_printf(s, "AC30 Agg limit = %d\t", u32_get_bits(value, BN0_WF_AGG_TOP_AALCR6_AC30_AGG_LIMIT_MASK));
	seq_printf(s, "AC31 Agg limit = %d\t", u32_get_bits(value, BN0_WF_AGG_TOP_AALCR6_AC31_AGG_LIMIT_MASK));
	value = mt76_rr(dev, BN0_WF_AGG_TOP_AALCR7_ADDR + band_offset);
	seq_printf(s, "AC32 Agg limit = %d\t", u32_get_bits(value, BN0_WF_AGG_TOP_AALCR7_AC32_AGG_LIMIT_MASK));
	seq_printf(s, "AC33 Agg limit = %d\n", u32_get_bits(value, BN0_WF_AGG_TOP_AALCR7_AC33_AGG_LIMIT_MASK));

	for (idx = 0; idx < MT7999_BA_WINDOW_CNT; idx++) {
		u32 value0 = 0, value1 = 0;

		mt76_wr(dev, WF_WTBLON_TOP_ITCR_ADDR, (0x81000000 + idx));
		value0 = mt76_rr(dev, WF_WTBLON_TOP_ITDR0_ADDR);
		value1 = mt76_rr(dev, WF_WTBLON_TOP_ITDR1_ADDR);
		seq_printf(s, "Winsize%x limit = %d, %d\n", idx, value0, value1);
	}

	switch (band_idx) {
	case 0:
		band_offset = 0;
		break;
	case 1:
		band_offset = BN1_WF_MIB_TOP_BASE - BN0_WF_MIB_TOP_BASE;
		break;
	case 2:
		band_offset = IP1_BN0_WF_MIB_TOP_BASE - BN0_WF_MIB_TOP_BASE;
		break;
	default:
		return 0;
	}

	seq_printf(s, "===AMPDU Related Counters===\n");

	value = mt76_rr(dev, BN0_WF_MIB_TOP_TRARC0_ADDR + band_offset);
	agg_rang_sel[0] = u32_get_bits(value, BN0_WF_MIB_TOP_TRARC0_AGG_RANG_SEL_0_MASK);
	agg_rang_sel[1] = u32_get_bits(value, BN0_WF_MIB_TOP_TRARC0_AGG_RANG_SEL_1_MASK);
	value = mt76_rr(dev, BN0_WF_MIB_TOP_TRARC1_ADDR + band_offset);
	agg_rang_sel[2] = u32_get_bits(value, BN0_WF_MIB_TOP_TRARC1_AGG_RANG_SEL_2_MASK);
	agg_rang_sel[3] = u32_get_bits(value, BN0_WF_MIB_TOP_TRARC1_AGG_RANG_SEL_3_MASK);
	value = mt76_rr(dev, BN0_WF_MIB_TOP_TRARC2_ADDR + band_offset);
	agg_rang_sel[4] = u32_get_bits(value, BN0_WF_MIB_TOP_TRARC2_AGG_RANG_SEL_4_MASK);
	agg_rang_sel[5] = u32_get_bits(value, BN0_WF_MIB_TOP_TRARC2_AGG_RANG_SEL_5_MASK);
	value = mt76_rr(dev, BN0_WF_MIB_TOP_TRARC3_ADDR + band_offset);
	agg_rang_sel[6] = u32_get_bits(value, BN0_WF_MIB_TOP_TRARC3_AGG_RANG_SEL_6_MASK);
	agg_rang_sel[7] = u32_get_bits(value, BN0_WF_MIB_TOP_TRARC3_AGG_RANG_SEL_7_MASK);
	value = mt76_rr(dev, BN0_WF_MIB_TOP_TRARC4_ADDR + band_offset);
	agg_rang_sel[8] = u32_get_bits(value, BN0_WF_MIB_TOP_TRARC4_AGG_RANG_SEL_8_MASK);
	agg_rang_sel[9] = u32_get_bits(value, BN0_WF_MIB_TOP_TRARC4_AGG_RANG_SEL_9_MASK);
	value = mt76_rr(dev, BN0_WF_MIB_TOP_TRARC5_ADDR + band_offset);
	agg_rang_sel[10] = u32_get_bits(value, BN0_WF_MIB_TOP_TRARC5_AGG_RANG_SEL_10_MASK);
	agg_rang_sel[11] = u32_get_bits(value, BN0_WF_MIB_TOP_TRARC5_AGG_RANG_SEL_11_MASK);
	value = mt76_rr(dev, BN0_WF_MIB_TOP_TRARC6_ADDR + band_offset);
	agg_rang_sel[12] = u32_get_bits(value, BN0_WF_MIB_TOP_TRARC6_AGG_RANG_SEL_12_MASK);
	agg_rang_sel[13] = u32_get_bits(value, BN0_WF_MIB_TOP_TRARC6_AGG_RANG_SEL_13_MASK);
	value = mt76_rr(dev, BN0_WF_MIB_TOP_TRARC7_ADDR + band_offset);
	agg_rang_sel[14] = u32_get_bits(value, BN0_WF_MIB_TOP_TRARC7_AGG_RANG_SEL_14_MASK);

	burst_cnt[0] = mt76_rr(dev, BN0_WF_MIB_TOP_TRDR0_ADDR + band_offset);
	burst_cnt[1] = mt76_rr(dev, BN0_WF_MIB_TOP_TRDR1_ADDR + band_offset);
	burst_cnt[2] = mt76_rr(dev, BN0_WF_MIB_TOP_TRDR2_ADDR + band_offset);
	burst_cnt[3] = mt76_rr(dev, BN0_WF_MIB_TOP_TRDR3_ADDR + band_offset);
	burst_cnt[4] = mt76_rr(dev, BN0_WF_MIB_TOP_TRDR4_ADDR + band_offset);
	burst_cnt[5] = mt76_rr(dev, BN0_WF_MIB_TOP_TRDR5_ADDR + band_offset);
	burst_cnt[6] = mt76_rr(dev, BN0_WF_MIB_TOP_TRDR6_ADDR + band_offset);
	burst_cnt[7] = mt76_rr(dev, BN0_WF_MIB_TOP_TRDR7_ADDR + band_offset);
	burst_cnt[8] = mt76_rr(dev, BN0_WF_MIB_TOP_TRDR8_ADDR + band_offset);
	burst_cnt[9] = mt76_rr(dev, BN0_WF_MIB_TOP_TRDR9_ADDR + band_offset);
	burst_cnt[10] = mt76_rr(dev, BN0_WF_MIB_TOP_TRDR10_ADDR + band_offset);
	burst_cnt[11] = mt76_rr(dev, BN0_WF_MIB_TOP_TRDR11_ADDR + band_offset);
	burst_cnt[12] = mt76_rr(dev, BN0_WF_MIB_TOP_TRDR12_ADDR + band_offset);
	burst_cnt[13] = mt76_rr(dev, BN0_WF_MIB_TOP_TRDR13_ADDR + band_offset);
	burst_cnt[14] = mt76_rr(dev, BN0_WF_MIB_TOP_TRDR14_ADDR + band_offset);
	burst_cnt[15] = mt76_rr(dev, BN0_WF_MIB_TOP_TRDR15_ADDR + band_offset);

	start_range = 1;
	total_burst = 0;
	total_ampdu = 0;
	agg_rang_sel[15] = 1023;

	/* Need to add 1 after read from AGG_RANG_SEL CR */
	for (idx = 0; idx < 16; idx++) {
		agg_rang_sel[idx]++;
		total_burst += burst_cnt[idx];

		if (start_range == agg_rang_sel[idx])
			ampdu_cnt[idx] = (u64) start_range * burst_cnt[idx];
		else
			ampdu_cnt[idx] = (u64) ((start_range + agg_rang_sel[idx]) >> 1) * burst_cnt[idx];

		start_range = agg_rang_sel[idx] + 1;
		total_ampdu += ampdu_cnt[idx];
	}

	start_range = 1;
	sprintf(full_str, "%13s ", "Tx Agg Range:");

	for (row_idx = 0; row_idx < 4; row_idx++) {
		for (col_idx = 0; col_idx < 4; col_idx++, idx++) {
			idx = 4 * row_idx + col_idx;

			if (start_range == agg_rang_sel[idx])
				sprintf(partial_str, "%d", agg_rang_sel[idx]);
			else
				sprintf(partial_str, "%d~%d", start_range, agg_rang_sel[idx]);

			start_range = agg_rang_sel[idx] + 1;
			sprintf(full_str + strlen(full_str), "%-11s ", partial_str);
		}

		idx = 4 * row_idx;

		seq_printf(s, "%s\n", full_str);
		seq_printf(s, "%13s 0x%-9x 0x%-9x 0x%-9x 0x%-9x\n",
			row_idx ? "" : "Burst count:",
			burst_cnt[idx], burst_cnt[idx + 1],
			burst_cnt[idx + 2], burst_cnt[idx + 3]);

		if (total_burst != 0) {
			sprintf(full_str, "%13s ", row_idx ? "": "Burst ratio:");

			for (col_idx = 0; col_idx < 4; col_idx++) {
				u64 count = (u64) burst_cnt[idx + col_idx] * 100;

				sprintf(partial_str, "(%llu%%)",
					div64_u64(count, total_burst));
				sprintf(full_str + strlen(full_str),
					"%-11s ", partial_str);
			}

			seq_printf(s, "%s\n", full_str);

			sprintf(full_str, "%13s ", row_idx ? "" : "MDPU ratio:");

			for (col_idx = 0; col_idx < 4; col_idx++) {
				u64 count = ampdu_cnt[idx + col_idx] * 100;

				sprintf(partial_str, "(%llu%%)",
					div64_u64(count, total_ampdu));
				sprintf(full_str + strlen(full_str),
					"%-11s ", partial_str);
			}

			seq_printf(s, "%s\n", full_str);
		}

		sprintf(full_str, "%13s ", "");
	}

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(mt7999_agginfo);

/* AMSDU INFO */
static int mt7999_amsdu_result_read(struct seq_file *s, void *data)
{
	struct mt7999_dev *dev = dev_get_drvdata(s->private);
	struct mt7999_phy *phy = &dev->phy;
	struct mt76_mib_stats *mib = &phy->mib;
	static u32 tx_amsdu_last[8] = {0};
	static u32 tx_amsdu_cnt_last = 0;
	u32 tx_amsdu, tx_amsdu_cnt, ratio;
	int i;

	mutex_lock(&dev->mt76.mutex);

	mt7999_mac_update_stats(phy);

	tx_amsdu_cnt = mib->tx_amsdu_cnt - tx_amsdu_cnt_last;

	seq_puts(s, "Tx MSDU statistics:\n");
	for (i = 0; i < ARRAY_SIZE(mib->tx_amsdu); i++) {
		tx_amsdu = mib->tx_amsdu[i] - tx_amsdu_last[i];
		ratio = tx_amsdu_cnt ? tx_amsdu * 100 / tx_amsdu_cnt : 0;

		seq_printf(s, "AMSDU pack count of %d MSDU in TXD: %8d (%3d%%)\n",
			   i + 1, tx_amsdu, ratio);

		tx_amsdu_last[i] = mib->tx_amsdu[i];
	}

	tx_amsdu_cnt_last = mib->tx_amsdu_cnt;

	mutex_unlock(&dev->mt76.mutex);

	return 0;
}

/* DBG MODLE */
static int
mt7999_fw_debug_module_set(void *data, u64 module)
{
	struct mt7999_dev *dev = data;

	dev->dbg.fw_dbg_module = module;
	return 0;
}

static int
mt7999_fw_debug_module_get(void *data, u64 *module)
{
	struct mt7999_dev *dev = data;

	*module = dev->dbg.fw_dbg_module;
	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(fops_fw_debug_module, mt7999_fw_debug_module_get,
			 mt7999_fw_debug_module_set, "%lld\n");

static int
mt7999_fw_debug_level_set(void *data, u64 level)
{
	struct mt7999_dev *dev = data;

	dev->dbg.fw_dbg_lv = level;
	mt7999_mcu_fw_dbg_ctrl(dev, dev->dbg.fw_dbg_module, dev->dbg.fw_dbg_lv);
	return 0;
}

static int
mt7999_fw_debug_level_get(void *data, u64 *level)
{
	struct mt7999_dev *dev = data;

	*level = dev->dbg.fw_dbg_lv;
	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(fops_fw_debug_level, mt7999_fw_debug_level_get,
			 mt7999_fw_debug_level_set, "%lld\n");

/* usage: echo 0x[arg3][arg2][arg1] > fw_wa_set */
static int
mt7999_wa_set(void *data, u64 val)
{
	struct mt7999_dev *dev = data;
	u32 arg1, arg2, arg3;

	arg1 = FIELD_GET(GENMASK_ULL(7, 0), val);
	arg2 = FIELD_GET(GENMASK_ULL(15, 8), val);
	arg3 = FIELD_GET(GENMASK_ULL(23, 16), val);

	return mt7999_mcu_sdo_cmd(dev, false, arg1, arg2, arg3);
}

DEFINE_DEBUGFS_ATTRIBUTE(fops_wa_set, NULL, mt7999_wa_set,
			 "0x%llx\n");

/* usage: echo 0x[arg3][arg2][arg1] > fw_wa_query */
static int
mt7999_wa_query(void *data, u64 val)
{
	struct mt7999_dev *dev = data;
	u32 arg1, arg2, arg3;

	arg1 = FIELD_GET(GENMASK_ULL(7, 0), val);
	arg2 = FIELD_GET(GENMASK_ULL(15, 8), val);
	arg3 = FIELD_GET(GENMASK_ULL(23, 16), val);

	return mt7999_mcu_sdo_cmd(dev, true, arg1, arg2, arg3);
}

DEFINE_DEBUGFS_ATTRIBUTE(fops_wa_query, NULL, mt7999_wa_query,
			 "0x%llx\n");

static int mt7999_dump_version(struct seq_file *s, void *data)
{
#define MAX_ADIE_NUM	3
	struct mt7999_dev *dev = dev_get_drvdata(s->private);
	u32 regval;
	u16 adie_chip_id, adie_chip_ver;
	int adie_idx;
	static const char * const fem_type[] = {
		[MT7999_FEM_EXT] = "eFEM",
		[MT7999_FEM_INT] = "iFEM",
	};

	seq_printf(s, "Version: 4.4.%02d.%02d\n", PKG_YEAR, PKG_MONTH);

	if (!test_bit(MT76_STATE_MCU_RUNNING, &dev->mphy.state))
		return 0;

	seq_printf(s, "Firmware Build Time: %.16s, Mode: %s\n",
		   dev->fw_build_date,
		   dev->testmode_enable ? "Testmode" : "Normal mode");
	for (adie_idx = 0; adie_idx < MAX_ADIE_NUM; adie_idx++) {
		mt7999_mcu_rf_regval(dev, MT_ADIE_CHIP_ID(adie_idx), &regval, false);
		adie_chip_id = FIELD_GET(MT_ADIE_CHIP_ID_MASK, regval);
		adie_chip_ver = FIELD_GET(MT_ADIE_VERSION_MASK, regval);
		if (adie_chip_id)
			seq_printf(s, "Adie %d: ID = 0x%04x, Ver = 0x%04x\n",
				   adie_idx, adie_chip_id, adie_chip_ver);
		else
			seq_printf(s, "Adie %d: ID = N/A, Ver = N/A\n", adie_idx);
	}
	seq_printf(s, "FEM type: %s\n", fem_type[dev->var.fem]);

	return 0;
}

/* fw wm call trace info dump */
static void
mt7999_show_lp_history(struct seq_file *s, u32 type)
{
	struct mt7999_dev *dev = dev_get_drvdata(s->private);
	struct mt7999_crash_data *crash_data;
	struct mt7999_coredump *dump;
	u64 now = 0;
	int i = 0;
	u8 fw_type = !!type;

	mutex_lock(&dev->dump_mutex);

	crash_data = mt7999_coredump_new(dev, fw_type);
	if (!crash_data) {
		mutex_unlock(&dev->dump_mutex);
		seq_printf(s, "the coredump is disable!\n");
		return;
	}
	mutex_unlock(&dev->dump_mutex);

	dump = mt7999_coredump_build(dev, fw_type, false);
	if (!dump) {
		seq_printf(s, "no call stack data found!\n");
		return;
	}

	seq_printf(s, "\x1b[32m%s log output\x1b[0m\n", dump->fw_type);
	seq_printf(s, "\x1b[32mfw status: %s\n", dump->fw_state);
	/* PC log */
	now = jiffies;
	for (i = 0; i < 10; i++)
		seq_printf(s, "\tCurrent PC=%x\n", dump->pc_cur[i]);

	seq_printf(s, "PC log contorl=0x%x(T=%llu)(latest PC index = 0x%x)\n",
		dump->pc_dbg_ctrl, now, dump->pc_cur_idx);
	for (i = 0; i < 32; i++)
		seq_printf(s, "\tPC log(%d)=0x%08x\n", i, dump->pc_stack[i]);

	/* LR log */
	now = jiffies;
	seq_printf(s, "\nLR log contorl=0x%x(T=%llu)(latest LR index = 0x%x)\n",
		dump->lr_dbg_ctrl, now, dump->lr_cur_idx);
	for (i = 0; i < 32; i++)
		seq_printf(s, "\tLR log(%d)=0x%08x\n", i, dump->lr_stack[i]);

	vfree(dump);
}

static int mt7999_fw_wm_info_read(struct seq_file *s, void *data)
{
	seq_printf(s, "======[ShowPcLpHistory]======\n");
	mt7999_show_lp_history(s, MT7999_RAM_TYPE_WM);
	seq_printf(s, "======[End ShowPcLpHistory]==\n");

	return 0;
}

/* dma info dump */
static void
dump_dma_tx_ring_info(struct seq_file *s, struct mt7999_dev *dev,
		      char *str1, char *str2, u32 ring_base)
{
	u32 ctrl0, ctrl1, ctrl2, ctrl3;
	u64 base;
	u32 max_cnt, cidx, didx, queue_cnt;

	ctrl0 = mt76_rr(dev, ring_base);
	ctrl1 = mt76_rr(dev, ring_base + 4);
	ctrl2 = mt76_rr(dev, ring_base + 8);
	ctrl3 = mt76_rr(dev, ring_base + 12);

	/* Base address is 36 bits */
	base = (u64)ctrl0;
	base |= (u64)(u32_get_bits(ctrl1, WF_P0_WFDMA_WPDMA_TX_RING16_CTRL1_BASE_PTR_EXT_MASK)) << 32;
	max_cnt = u32_get_bits(ctrl1, WF_P0_WFDMA_WPDMA_TX_RING16_CTRL1_MAX_CNT_MASK);
	cidx = u32_get_bits(ctrl2, WF_P0_WFDMA_WPDMA_TX_RING16_CTRL2_CPU_IDX_MASK);
	didx = u32_get_bits(ctrl3, WF_P0_WFDMA_WPDMA_TX_RING16_CTRL3_DMA_IDX_MASK);
	/* HW supported remaining count */
	queue_cnt = u32_get_bits(ctrl3, WF_P0_WFDMA_WPDMA_TX_RING16_CTRL3_PLD_CNT_MASK);

	seq_printf(s, "%20s %6s %10llx %18x %10x %10x %10x\n",
		   str1, str2, base, max_cnt, cidx, didx, queue_cnt);
}

static void
dump_dma_rx_ring_info(struct seq_file *s, struct mt7999_dev *dev,
		      char *str1, char *str2, u32 ring_base)
{
	u32 ctrl0, ctrl1, ctrl2, ctrl3;
	u64 base;
	u32 max_cnt, cidx, didx, queue_cnt;

	ctrl0 = mt76_rr(dev, ring_base);
	ctrl1 = mt76_rr(dev, ring_base + 4);
	ctrl2 = mt76_rr(dev, ring_base + 8);
	ctrl3 = mt76_rr(dev, ring_base + 12);
	/* Base address is 36 bits */
	base = (u64)ctrl0;
	base |= (u64)(u32_get_bits(ctrl1, WF_P0_WFDMA_WPDMA_RX_RING0_CTRL1_BASE_PTR_EXT_MASK)) << 32;
	max_cnt = u32_get_bits(ctrl1, WF_P0_WFDMA_WPDMA_RX_RING0_CTRL1_MAX_CNT_MASK);
	cidx = u32_get_bits(ctrl2, WF_P0_WFDMA_WPDMA_RX_RING0_CTRL2_CPU_IDX_MASK);
	didx = u32_get_bits(ctrl3, WF_P0_WFDMA_WPDMA_RX_RING0_CTRL3_DMA_IDX_MASK);
	/* HW supported remaining count */
	queue_cnt = u32_get_bits(ctrl3, WF_P0_WFDMA_WPDMA_RX_RING0_CTRL3_PLD_CNT_MASK);

	seq_printf(s, "%20s %6s %10llx %11x(%5x) %10x %10x %10x\n",
		    str1, str2, base, ctrl1, max_cnt, cidx, didx, queue_cnt);
}

static void
mt7999_show_dma_info(struct seq_file *s, struct mt7999_dev *dev)
{
	u32 sys_ctrl[10];

	/* HOST DMA0 information */
	sys_ctrl[0] = mt76_rr(dev, WF_P0_WFDMA_HOST_INT_STA_ADDR);
	sys_ctrl[1] = mt76_rr(dev, WF_P0_WFDMA_HOST_INT_ENA_ADDR);
	sys_ctrl[2] = mt76_rr(dev, WF_P0_WFDMA_WPDMA_GLO_CFG_ADDR);

	seq_printf(s, "HOST_DMA Configuration\n");
	seq_printf(s, "%10s %10s %10s %10s %10s %10s\n",
		"DMA", "IntCSR", "IntMask", "Glocfg", "Tx/RxEn", "Tx/RxBusy");
	seq_printf(s, "%10s %10x %10x %10x %4x/%5x %4x/%5x\n",
		"DMA0", sys_ctrl[0], sys_ctrl[1], sys_ctrl[2],
			u32_get_bits(sys_ctrl[2], WF_P0_WFDMA_WPDMA_GLO_CFG_TX_DMA_EN_MASK),
			u32_get_bits(sys_ctrl[2], WF_P0_WFDMA_WPDMA_GLO_CFG_RX_DMA_EN_MASK),
			u32_get_bits(sys_ctrl[2], WF_P0_WFDMA_WPDMA_GLO_CFG_TX_DMA_BUSY_MASK),
			u32_get_bits(sys_ctrl[2], WF_P0_WFDMA_WPDMA_GLO_CFG_RX_DMA_BUSY_MASK));

	if (dev->hif2) {
		/* HOST DMA1 information */
		sys_ctrl[0] = mt76_rr(dev, WF_P1_WFDMA_HOST_INT_STA_ADDR);
		sys_ctrl[1] = mt76_rr(dev, WF_P1_WFDMA_HOST_INT_ENA_ADDR);
		sys_ctrl[2] = mt76_rr(dev, WF_P1_WFDMA_WPDMA_GLO_CFG_ADDR);

		seq_printf(s, "%10s %10x %10x %10x %4x/%5x %4x/%5x\n",
			"DMA0P1", sys_ctrl[0], sys_ctrl[1], sys_ctrl[2],
			u32_get_bits(sys_ctrl[2], WF_P1_WFDMA_WPDMA_GLO_CFG_TX_DMA_EN_MASK),
			u32_get_bits(sys_ctrl[2], WF_P1_WFDMA_WPDMA_GLO_CFG_RX_DMA_EN_MASK),
			u32_get_bits(sys_ctrl[2], WF_P1_WFDMA_WPDMA_GLO_CFG_TX_DMA_BUSY_MASK),
			u32_get_bits(sys_ctrl[2], WF_P1_WFDMA_WPDMA_GLO_CFG_RX_DMA_BUSY_MASK));
	}

	seq_printf(s, "HOST_DMA0 Ring Configuration\n");
	seq_printf(s, "%20s %6s %10s %15s %13s %10s %10s\n",
		"Name", "Used", "Base", "Ctrl1(MaxCnt)", "CIDX", "DIDX", "QCnt");
	dump_dma_tx_ring_info(s, dev, "T16:FWDL", "Both",
		WF_P0_WFDMA_WPDMA_TX_RING16_CTRL0_ADDR);
	dump_dma_tx_ring_info(s, dev, "T17:Cmd(H2WM)", "Both",
		WF_P0_WFDMA_WPDMA_TX_RING17_CTRL0_ADDR);
	dump_dma_tx_ring_info(s, dev, "T18:TXD0(H2SDO)", "AP",
		WF_P0_WFDMA_WPDMA_TX_RING18_CTRL0_ADDR);
	dump_dma_tx_ring_info(s, dev, "T19:TXD1(H2SDO)", "AP",
		WF_P0_WFDMA_WPDMA_TX_RING19_CTRL0_ADDR);
	dump_dma_tx_ring_info(s, dev, "T20:VIP Cmd(H2WM)", "AP",
		WF_P0_WFDMA_WPDMA_TX_RING20_CTRL0_ADDR);

	dump_dma_rx_ring_info(s, dev, "R0:Event(WM2H)", "Both",
		WF_P0_WFDMA_WPDMA_RX_RING0_CTRL0_ADDR);
	dump_dma_rx_ring_info(s, dev, "R1:Event(SDO2H)", "AP",
		WF_P0_WFDMA_WPDMA_RX_RING1_CTRL0_ADDR);
	dump_dma_rx_ring_info(s, dev, "R4:Data0(MAC2H)", "Both",
		WF_P0_WFDMA_WPDMA_RX_RING4_CTRL0_ADDR);
	dump_dma_rx_ring_info(s, dev, "R5:Data1(MAC2H)", "Both",
		WF_P0_WFDMA_WPDMA_RX_RING5_CTRL0_ADDR);
	dump_dma_rx_ring_info(s, dev, "R6:TxDone0(MAC2H)", "Both",
		WF_P0_WFDMA_WPDMA_RX_RING6_CTRL0_ADDR);
	dump_dma_rx_ring_info(s, dev, "R7:TxDone1(MAC2H)", "Both",
		WF_P0_WFDMA_WPDMA_RX_RING7_CTRL0_ADDR);
	dump_dma_rx_ring_info(s, dev, "R8:RRO Data0(MAC2H)", "Both",
		WF_P0_WFDMA_WPDMA_RX_RING8_CTRL0_ADDR);
	dump_dma_rx_ring_info(s, dev, "R9:RRO Data1(MAC2H)", "Both",
		WF_P0_WFDMA_WPDMA_RX_RING9_CTRL0_ADDR);
	dump_dma_rx_ring_info(s, dev, "R10:MSDU_PG(MAC2H)", "Both",
		WF_P0_WFDMA_WPDMA_RX_RING10_CTRL0_ADDR);
	dump_dma_rx_ring_info(s, dev, "R11:ICS(MAC2H)", "Both",
		WF_P0_WFDMA_WPDMA_RX_RING11_CTRL0_ADDR);
	dump_dma_rx_ring_info(s, dev, "R12", "AP",
		WF_P0_WFDMA_WPDMA_RX_RING12_CTRL0_ADDR);
	dump_dma_rx_ring_info(s, dev, "IND:IND_CMD(MAC2H)", "Both",
		WF_RRO_TOP_IND_CMD_0_CTRL0_ADDR);
	dump_dma_rx_ring_info(s, dev, "RRO:RxDMAD_C(MAC2H)", "Both",
		WF_RRO_TOP_RX_RING_AP_0_CTRL0_ADDR);

	seq_printf(s, "HOST_DMA0 PCIe1 Ring Configuration\n");
	seq_printf(s, "%20s %6s %10s %15s %13s %10s %10s\n",
		   "Name", "Used", "Base", "Ctrl1(MaxCnt)", "CIDX", "DIDX", "QCnt");
	dump_dma_tx_ring_info(s, dev, "T21:TXD2(H2SDO)", "AP",
		WF_P1_WFDMA_WPDMA_TX_RING21_CTRL0_ADDR);
	dump_dma_tx_ring_info(s, dev, "T22:TXD3(H2SDO)", "AP",
		WF_P1_WFDMA_WPDMA_TX_RING22_CTRL0_ADDR);
	dump_dma_rx_ring_info(s, dev, "R5:Data1(MAC2H)", "Both",
		WF_P1_WFDMA_WPDMA_RX_RING5_CTRL0_ADDR);
	dump_dma_rx_ring_info(s, dev, "R7:TxDone1(MAC2H)", "Both",
		WF_P1_WFDMA_WPDMA_RX_RING7_CTRL0_ADDR);
	dump_dma_rx_ring_info(s, dev, "R9:RRO Data1(MAC2H)", "Both",
		WF_P1_WFDMA_WPDMA_RX_RING9_CTRL0_ADDR);
	/* MCU DMA information */
	sys_ctrl[0] = mt76_rr(dev, WF_M0_WFDMA_WPDMA_GLO_CFG_ADDR);
	sys_ctrl[1] = mt76_rr(dev, WF_M0_WFDMA_HOST_INT_STA_ADDR);
	sys_ctrl[2] = mt76_rr(dev, WF_M0_WFDMA_HOST_INT_ENA_ADDR);

	seq_printf(s, "MCU_DMA Configuration\n");
	seq_printf(s, "%10s %10s %10s %10s %10s %10s\n",
		      "DMA", "IntCSR", "IntMask", "Glocfg", "Tx/RxEn", "Tx/RxBusy");
	seq_printf(s, "%10s %10x %10x %10x %4x/%5x %4x/%5x\n",
		      "DMA0", sys_ctrl[1], sys_ctrl[2], sys_ctrl[0],
		      u32_get_bits(sys_ctrl[0], WF_M0_WFDMA_WPDMA_GLO_CFG_TX_DMA_EN_MASK),
		      u32_get_bits(sys_ctrl[0], WF_M0_WFDMA_WPDMA_GLO_CFG_RX_DMA_EN_MASK),
		      u32_get_bits(sys_ctrl[0], WF_M0_WFDMA_WPDMA_GLO_CFG_TX_DMA_BUSY_MASK),
		      u32_get_bits(sys_ctrl[0], WF_M0_WFDMA_WPDMA_GLO_CFG_RX_DMA_BUSY_MASK));

	seq_printf(s, "MCU_DMA0 Ring Configuration\n");
	seq_printf(s, "%20s %6s %10s %15s %13s %10s %10s\n",
		"Name", "Used", "Base", "Cnt", "CIDX", "DIDX", "QCnt");
	dump_dma_tx_ring_info(s, dev, "T0:Event(WM2H)", "Both",
		WF_M0_WFDMA_WPDMA_TX_RING0_CTRL0_ADDR);
	dump_dma_tx_ring_info(s, dev, "T1:Event(SDO2H)", "AP",
		WF_M0_WFDMA_WPDMA_TX_RING1_CTRL0_ADDR);
	dump_dma_tx_ring_info(s, dev, "T4:TXD(WM2MAC)", "Both",
		WF_M0_WFDMA_WPDMA_TX_RING4_CTRL0_ADDR);
	dump_dma_tx_ring_info(s, dev, "T5:TXCMD(WM2MAC)", "Both",
		WF_M0_WFDMA_WPDMA_TX_RING5_CTRL0_ADDR);
	dump_dma_tx_ring_info(s, dev, "T6:TXD(SDO2MAC)", "AP",
		WF_M0_WFDMA_WPDMA_TX_RING6_CTRL0_ADDR);
	dump_dma_rx_ring_info(s, dev, "R0:FWDL", "Both",
		WF_M0_WFDMA_WPDMA_RX_RING0_CTRL0_ADDR);
	dump_dma_rx_ring_info(s, dev, "R1:Cmd(H2WM)", "Both",
		WF_M0_WFDMA_WPDMA_RX_RING1_CTRL0_ADDR);
	dump_dma_rx_ring_info(s, dev, "R2:TXD0(H2SDO)", "AP",
		WF_M0_WFDMA_WPDMA_RX_RING2_CTRL0_ADDR);
	dump_dma_rx_ring_info(s, dev, "R3:TXD1(H2SDO)", "AP",
		WF_M0_WFDMA_WPDMA_RX_RING3_CTRL0_ADDR);
	dump_dma_rx_ring_info(s, dev, "R4:VIP Cmd(H2WM)", "AP",
		WF_M0_WFDMA_WPDMA_RX_RING4_CTRL0_ADDR);
	dump_dma_rx_ring_info(s, dev, "R5:Data0(MAC2WM)", "Both",
		WF_M0_WFDMA_WPDMA_RX_RING5_CTRL0_ADDR);
	dump_dma_rx_ring_info(s, dev, "R6:TxDone(MAC2WM)", "Both",
		WF_M0_WFDMA_WPDMA_RX_RING6_CTRL0_ADDR);
	dump_dma_rx_ring_info(s, dev, "R7:SPL/RPT(MAC2WM)", "Both",
		WF_M0_WFDMA_WPDMA_RX_RING7_CTRL0_ADDR);
	dump_dma_rx_ring_info(s, dev, "R8:Data1(MAC2WM)", "Both",
		WF_M0_WFDMA_WPDMA_RX_RING8_CTRL0_ADDR);
	dump_dma_rx_ring_info(s, dev, "R9:Cosr frame", "Both",
		WF_M0_WFDMA_WPDMA_RX_RING9_CTRL0_ADDR);
}

static int mt7999_trinfo_read(struct seq_file *s, void *data)
{
	struct mt7999_dev *dev = dev_get_drvdata(s->private);
	mt7999_show_dma_info(s, dev);
	return 0;
}

/* MIB INFO */
static u32 mt7999_get_mib_bcn_tx_cnt(struct mt7999_dev *dev, u32 band_offset)
{
	int i;
	u32 band_bcn_tx_cnt = 0;

	/* Beacon Count per bss, calculate all bss bcn count to band bcn count */
	for (i = 0; i < MT7999_MAX_MBSSID; i++) {
		band_bcn_tx_cnt += mt76_rr(dev, BN0_WF_MIB_TOP_TSCR18_ADDR +
						band_offset + i * 4);
	}

	return band_bcn_tx_cnt;
}

static int mt7999_mibinfo_show(struct seq_file *s, void *data)
{
#define BSS_NUM	4
	struct mt7999_phy *phy = s->private;
	struct mt7999_dev *dev = phy->dev;
	u8 band_idx = phy->mt76->band_idx;
	u8 bss_nums = BSS_NUM;
	u32 idx;
	u32 mac_val, band_offset = 0, band_offset_umib = 0;
	u32 msdr6, msdr9, msdr18;
	u32 rvsr0, rscr26, rscr35, mctr5, mctr6, msr0, msr1, msr2;
	u32 tbcr0, tbcr1, tbcr2, tbcr3, tbcr4;
	u32 btscr[7];
	u32 tdrcr[5];
	u32 mbtocr[16], mbtbcr[16], mbrocr[16], mbrbcr[16];
	u32 btcr, btbcr, brocr, brbcr, btdcr, brdcr;
#if 0
	u32 mu_cnt[5];
#endif
	u32 ampdu_cnt[3];
	u64 per = 0;
	u32 band_offset_airtime = 0;
	u32 mpdu_retry_drop_cnt, rts_drop_cnt;

	switch (band_idx) {
	case 0:
		band_offset = 0;
		band_offset_umib = 0;
		band_offset_airtime = 0;
		break;
	case 1:
		band_offset = BN1_WF_MIB_TOP_BASE - BN0_WF_MIB_TOP_BASE;
		band_offset_umib = WF_UMIB_TOP_B1BROCR_ADDR - WF_UMIB_TOP_B0BROCR_ADDR;
		band_offset_airtime = BN1_WF_AIRTIME_TOP_BASE - BN0_WF_AIRTIME_TOP_BASE;
		break;
	case 2:
		band_offset = IP1_BN0_WF_MIB_TOP_BASE - BN0_WF_MIB_TOP_BASE;
		band_offset_umib = WF_UMIB_TOP_B2BROCR_ADDR - WF_UMIB_TOP_B0BROCR_ADDR;
		band_offset_airtime = IP1_BN0_WF_AIRTIME_TOP_BASE - BN0_WF_AIRTIME_TOP_BASE;
		break;
	default:
		return true;
	}

	seq_printf(s, "Band %d MIB Status\n", band_idx);
	seq_printf(s, "===============================\n");
	mac_val = mt76_rr(dev, BN0_WF_MIB_TOP_M0SCR0_ADDR + band_offset);
	seq_printf(s, "MIB Status Control=0x%x\n", mac_val);

	msdr6 = mt76_rr(dev, BN0_WF_AIRTIME_TOP_CHANNEL_IDLE_EIFS_CNT_ADDR + band_offset_airtime);
	rvsr0 = mt76_rr(dev, BN0_WF_MIB_TOP_RVSR0_ADDR + band_offset);
	rscr35 = mt76_rr(dev, BN0_WF_MIB_TOP_RSCR35_ADDR + band_offset);
	msdr9 = mt76_rr(dev, BN0_WF_AIRTIME_TOP_CCA_NAV_TX_TIME_ADDR + band_offset_airtime);
	rscr26 = mt76_rr(dev, BN0_WF_MIB_TOP_RSCR26_ADDR + band_offset);
	mctr5 = mt76_rr(dev, BN0_WF_AIRTIME_TOP_PCCA_TIME_ADDR + band_offset_airtime);
	mctr6 = mt76_rr(dev, BN0_WF_AIRTIME_TOP_SCCA_TIME_ADDR + band_offset_airtime);
	msdr18 = mt76_rr(dev, BN0_WF_AIRTIME_TOP_PRIM_ED_TIME_ADDR + band_offset_airtime);
	msr0 = mt76_rr(dev, BN0_WF_MIB_TOP_MSR0_ADDR + band_offset);
	msr1 = mt76_rr(dev, BN0_WF_MIB_TOP_MSR1_ADDR + band_offset);
	msr2 = mt76_rr(dev, BN0_WF_MIB_TOP_MSR2_ADDR + band_offset);
	ampdu_cnt[0] = mt76_rr(dev, BN0_WF_MIB_TOP_TSCR0_ADDR + band_offset);
	ampdu_cnt[1] = mt76_rr(dev, BN0_WF_MIB_TOP_TSCR3_ADDR + band_offset);
	ampdu_cnt[2] = mt76_rr(dev, BN0_WF_MIB_TOP_TSCR4_ADDR + band_offset);
	ampdu_cnt[1] &= BN0_WF_MIB_TOP_TSCR3_AMPDU_MPDU_COUNT_MASK;
	ampdu_cnt[2] &= BN0_WF_MIB_TOP_TSCR4_AMPDU_ACKED_COUNT_MASK;
	mpdu_retry_drop_cnt = mt76_rr(dev, BN0_WF_MIB_TOP_TSCR15_ADDR + band_offset);
	rts_drop_cnt = mt76_rr(dev, BN0_WF_MIB_TOP_TSCR16_ADDR + band_offset);

	seq_printf(s, "===Phy/Timing Related Counters===\n");
	seq_printf(s, "\tChannelIdleCnt=0x%x\n",
		msdr6 & BN0_WF_AIRTIME_TOP_CHANNEL_IDLE_EIFS_CNT_CHANNEL_IDLE_COUNT_MASK);
	seq_printf(s, "\tCCA_NAV_Tx_Time=0x%x\n",
		msdr9 & BN0_WF_AIRTIME_TOP_CCA_NAV_TX_TIME_CCA_NAV_TX_TIME_MASK);
	seq_printf(s, "\tRx_MDRDY_CNT=0x%x\n",
		rscr26 & BN0_WF_MIB_TOP_RSCR26_RX_MDRDY_COUNT_MASK);
	seq_printf(s, "\tCCK_MDRDY_TIME=0x%x, OFDM_MDRDY_TIME=0x%x",
		msr0 & BN0_WF_MIB_TOP_MSR0_CCK_MDRDY_TIME_MASK,
		msr1 & BN0_WF_MIB_TOP_MSR1_OFDM_LG_MIXED_VHT_MDRDY_TIME_MASK);
	seq_printf(s, ", OFDM_GREEN_MDRDY_TIME=0x%x\n",
		msr2 & BN0_WF_MIB_TOP_MSR2_OFDM_GREEN_MDRDY_TIME_MASK);
	seq_printf(s, "\tPrim CCA Time=0x%x\n",
		mctr5 & BN0_WF_AIRTIME_TOP_PCCA_TIME_PCCA_TIME_MASK);
	seq_printf(s, "\tSec CCA Time=0x%x\n",
		mctr6 & BN0_WF_AIRTIME_TOP_SCCA_TIME_SCCA_TIME_MASK);
	seq_printf(s, "\tPrim ED Time=0x%x\n",
		msdr18 & BN0_WF_AIRTIME_TOP_PRIM_ED_TIME_P_ED_TIME_MASK);

	seq_printf(s, "===Tx Related Counters(Generic)===\n");
	dev->dbg.bcn_total_cnt[band_idx] += mt7999_get_mib_bcn_tx_cnt(dev, band_offset);
	seq_printf(s, "\tBeaconTxCnt=0x%x\n", dev->dbg.bcn_total_cnt[band_idx]);
	dev->dbg.bcn_total_cnt[band_idx] = 0;

	tbcr0 = mt76_rr(dev, BN0_WF_MIB_TOP_TBCR0_ADDR + band_offset);
	seq_printf(s, "\tTx 20MHz Cnt=0x%x\n",
		tbcr0 & BN0_WF_MIB_TOP_TBCR0_TX_20MHZ_CNT_MASK);
	tbcr1 = mt76_rr(dev, BN0_WF_MIB_TOP_TBCR1_ADDR + band_offset);
	seq_printf(s, "\tTx 40MHz Cnt=0x%x\n",
		tbcr1 & BN0_WF_MIB_TOP_TBCR1_TX_40MHZ_CNT_MASK);
	tbcr2 = mt76_rr(dev, BN0_WF_MIB_TOP_TBCR2_ADDR + band_offset);
	seq_printf(s, "\tTx 80MHz Cnt=0x%x\n",
		tbcr2 & BN0_WF_MIB_TOP_TBCR2_TX_80MHZ_CNT_MASK);
	tbcr3 = mt76_rr(dev, BN0_WF_MIB_TOP_TBCR3_ADDR + band_offset);
	seq_printf(s, "\tTx 160MHz Cnt=0x%x\n",
		tbcr3 & BN0_WF_MIB_TOP_TBCR3_TX_160MHZ_CNT_MASK);
	tbcr4 = mt76_rr(dev, BN0_WF_MIB_TOP_TBCR4_ADDR + band_offset);
	seq_printf(s, "\tTx 320MHz Cnt=0x%x\n",
		tbcr4 & BN0_WF_MIB_TOP_TBCR4_TX_320MHZ_CNT_MASK);
	seq_printf(s, "\tAMPDU Cnt=0x%x\n", ampdu_cnt[0]);
	seq_printf(s, "\tAMPDU MPDU Cnt=0x%x\n", ampdu_cnt[1]);
	seq_printf(s, "\tAMPDU MPDU Ack Cnt=0x%x\n", ampdu_cnt[2]);
	if (ampdu_cnt[1])
		per = (u64)1000 * (ampdu_cnt[1] - ampdu_cnt[2]) / ampdu_cnt[1];
	seq_printf(s, "\tAMPDU MPDU PER=%llu.%1llu%%\n", per / 10, per % 10);
	seq_printf(s, "\tMPDU Retry Drop Cnt=0x%x\n", mpdu_retry_drop_cnt);
	seq_printf(s, "\tRTS Drop Cnt=0x%x\n", rts_drop_cnt);

#if 0
	seq_printf(s, "===MU Related Counters===\n");
	mu_cnt[0] = mt76_rr(dev, BN0_WF_MIB_TOP_BSCR2_ADDR + band_offset);
	mu_cnt[1] = mt76_rr(dev, BN0_WF_MIB_TOP_TSCR5_ADDR + band_offset);
	mu_cnt[2] = mt76_rr(dev, BN0_WF_MIB_TOP_TSCR6_ADDR + band_offset);
	mu_cnt[3] = mt76_rr(dev, BN0_WF_MIB_TOP_TSCR8_ADDR + band_offset);
	mu_cnt[4] = mt76_rr(dev, BN0_WF_MIB_TOP_TSCR7_ADDR + band_offset);

	seq_printf(s, "\tMUBF_TX_COUNT=0x%x\n",
		mu_cnt[0] & BN0_WF_MIB_TOP_BSCR2_MUBF_TX_COUNT_MASK);
	seq_printf(s, "\tMU_TX_MPDU_COUNT(Ok+Fail)=0x%x\n", mu_cnt[1]);
	seq_printf(s, "\tMU_TX_OK_MPDU_COUNT=0x%x\n", mu_cnt[2]);
	seq_printf(s, "\tMU_TO_MU_FAIL_PPDU_COUNT=0x%x\n", mu_cnt[3]);
	seq_printf(s, "\tSU_TX_OK_MPDU_COUNT=0x%x\n", mu_cnt[4]);
#endif

	seq_printf(s, "===Rx Related Counters(Generic)===\n");
	seq_printf(s, "\tVector Mismacth Cnt=0x%x\n",
		rvsr0 & BN0_WF_MIB_TOP_RVSR0_VEC_MISS_COUNT_MASK);
	seq_printf(s, "\tDelimiter Fail Cnt=0x%x\n",
		rscr35 & BN0_WF_MIB_TOP_RSCR35_DELIMITER_FAIL_COUNT_MASK);

	mac_val = mt76_rr(dev, BN0_WF_MIB_TOP_RSCR1_ADDR + band_offset);
	seq_printf(s, "\tRxFCSErrCnt=0x%x\n",
		(mac_val & BN0_WF_MIB_TOP_RSCR1_RX_FCS_ERROR_COUNT_MASK));
	mac_val = mt76_rr(dev, BN0_WF_MIB_TOP_RSCR33_ADDR + band_offset);
	seq_printf(s, "\tRxFifoFullCnt=0x%x\n",
		(mac_val & BN0_WF_MIB_TOP_RSCR33_RX_FIFO_FULL_COUNT_MASK));
	mac_val = mt76_rr(dev, BN0_WF_MIB_TOP_RSCR36_ADDR + band_offset);
	seq_printf(s, "\tRxLenMismatch=0x%x\n",
		(mac_val & BN0_WF_MIB_TOP_RSCR36_RX_LEN_MISMATCH_MASK));
	mac_val = mt76_rr(dev, BN0_WF_MIB_TOP_RSCR31_ADDR + band_offset);
	seq_printf(s, "\tRxMPDUCnt=0x%x\n",
		(mac_val & BN0_WF_MIB_TOP_RSCR31_RX_MPDU_COUNT_MASK));
	mac_val = mt76_rr(dev, BN0_WF_MIB_TOP_RSCR27_ADDR + band_offset);
	seq_printf(s, "\tRx AMPDU Cnt=0x%x\n", mac_val);
	mac_val = mt76_rr(dev, BN0_WF_MIB_TOP_RSCR28_ADDR + band_offset);
	seq_printf(s, "\tRx Total ByteCnt=0x%x\n", mac_val);


	/* Per-BSS T/RX Counters */
	seq_printf(s, "===Per-BSS Related Tx/Rx Counters===\n");
	seq_printf(s, "BSS Idx TxCnt/DataCnt TxByteCnt RxOkCnt/DataCnt RxByteCnt\n");
	for (idx = 0; idx < bss_nums; idx++) {
		btcr = mt76_rr(dev, BN0_WF_MIB_TOP_BTCR_ADDR + band_offset + idx * 4);
		btdcr = mt76_rr(dev, BN0_WF_MIB_TOP_BTDCR_ADDR + band_offset + idx * 4);
		btbcr = mt76_rr(dev, BN0_WF_MIB_TOP_BTBCR_ADDR + band_offset + idx * 4);

		brocr = mt76_rr(dev, WF_UMIB_TOP_B0BROCR_ADDR + band_offset_umib + idx * 4);
		brdcr = mt76_rr(dev, WF_UMIB_TOP_B0BRDCR_ADDR + band_offset_umib + idx * 4);
		brbcr = mt76_rr(dev, WF_UMIB_TOP_B0BRBCR_ADDR + band_offset_umib + idx * 4);

		seq_printf(s, "%d\t 0x%x/0x%x\t 0x%x \t 0x%x/0x%x \t 0x%x\n",
			idx, btcr, btdcr, btbcr, brocr, brdcr, brbcr);
	}

	seq_printf(s, "===Per-BSS Related MIB Counters===\n");
	seq_printf(s, "BSS Idx RTSTx/RetryCnt BAMissCnt AckFailCnt FrmRetry1/2/3Cnt\n");

	/* Per-BSS TX Status */
	for (idx = 0; idx < bss_nums; idx++) {
		btscr[0] = mt76_rr(dev, BN0_WF_MIB_TOP_BTSCR5_ADDR + band_offset + idx * 4);
		btscr[1] = mt76_rr(dev, BN0_WF_MIB_TOP_BTSCR6_ADDR + band_offset + idx * 4);
		btscr[2] = mt76_rr(dev, BN0_WF_MIB_TOP_BTSCR0_ADDR + band_offset + idx * 4);
		btscr[3] = mt76_rr(dev, BN0_WF_MIB_TOP_BTSCR1_ADDR + band_offset + idx * 4);
		btscr[4] = mt76_rr(dev, BN0_WF_MIB_TOP_BTSCR2_ADDR + band_offset + idx * 4);
		btscr[5] = mt76_rr(dev, BN0_WF_MIB_TOP_BTSCR3_ADDR + band_offset + idx * 4);
		btscr[6] = mt76_rr(dev, BN0_WF_MIB_TOP_BTSCR4_ADDR + band_offset + idx * 4);

		seq_printf(s, "%d:\t0x%x/0x%x  0x%x \t 0x%x \t  0x%x/0x%x/0x%x\n",
			idx, (btscr[0] & BN0_WF_MIB_TOP_BTSCR5_RTSTXCOUNTn_MASK),
			(btscr[1] & BN0_WF_MIB_TOP_BTSCR6_RTSRETRYCOUNTn_MASK),
			(btscr[2] & BN0_WF_MIB_TOP_BTSCR0_BAMISSCOUNTn_MASK),
			(btscr[3] & BN0_WF_MIB_TOP_BTSCR1_ACKFAILCOUNTn_MASK),
			(btscr[4] & BN0_WF_MIB_TOP_BTSCR2_FRAMERETRYCOUNTn_MASK),
			(btscr[5] & BN0_WF_MIB_TOP_BTSCR3_FRAMERETRY2COUNTn_MASK),
			(btscr[6] & BN0_WF_MIB_TOP_BTSCR4_FRAMERETRY3COUNTn_MASK));
	}

	/* Dummy delimiter insertion result */
	seq_printf(s, "===Dummy delimiter insertion result===\n");
	tdrcr[0] = mt76_rr(dev, BN0_WF_MIB_TOP_TDRCR0_ADDR + band_offset);
	tdrcr[1] = mt76_rr(dev, BN0_WF_MIB_TOP_TDRCR1_ADDR + band_offset);
	tdrcr[2] = mt76_rr(dev, BN0_WF_MIB_TOP_TDRCR2_ADDR + band_offset);
	tdrcr[3] = mt76_rr(dev, BN0_WF_MIB_TOP_TDRCR3_ADDR + band_offset);
	tdrcr[4] = mt76_rr(dev, BN0_WF_MIB_TOP_TDRCR4_ADDR + band_offset);

	seq_printf(s, "Range0 = %d\t Range1 = %d\t Range2 = %d\t Range3 = %d\t Range4 = %d\n",
		tdrcr[0], tdrcr[1], tdrcr[2], tdrcr[3], tdrcr[4]);

	/* Per-MBSS T/RX Counters */
	seq_printf(s, "===Per-MBSS Related Tx/Rx Counters===\n");
	seq_printf(s, "MBSSIdx   TxOkCnt  TxByteCnt  RxOkCnt  RxByteCnt\n");

	for (idx = 0; idx < 16; idx++) {
		mbtocr[idx] = mt76_rr(dev, BN0_WF_MIB_TOP_BTOCR_ADDR + band_offset + (bss_nums + idx) * 4);
		mbtbcr[idx] = mt76_rr(dev, BN0_WF_MIB_TOP_BTBCR_ADDR + band_offset + (bss_nums + idx) * 4);

		mbrocr[idx] = mt76_rr(dev, WF_UMIB_TOP_B0BROCR_ADDR + band_offset_umib + (bss_nums + idx) * 4);
		mbrbcr[idx] = mt76_rr(dev, WF_UMIB_TOP_B0BRBCR_ADDR + band_offset_umib + (bss_nums + idx) * 4);
	}

	for (idx = 0; idx < 16; idx++) {
		seq_printf(s, "%d\t 0x%x\t 0x%x \t 0x%x \t 0x%x\n",
			idx, mbtocr[idx], mbtbcr[idx], mbrocr[idx], mbrbcr[idx]);
	}

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(mt7999_mibinfo);

/* WTBL INFO */
static int
mt7999_wtbl_read_raw(struct mt7999_dev *dev, u16 idx,
		     enum mt7999_wtbl_type type, u16 start_dw,
		     u16 len, void *buf)
{
	u32 *dest_cpy = (u32 *)buf;
	u32 size_dw = len;
	u32 src = 0;

	if (!buf)
		return 0xFF;

	if (!dev->wtbl_query_cr)
		return mt7999_mcu_sta_get_wtbl(dev, idx, type, start_dw, len, buf);

	if (type == WTBL_TYPE_LMAC) {
		mt76_wr(dev, MT_WTBLON_TOP_WDUCR,
			FIELD_PREP(MT_WTBLON_TOP_WDUCR_GROUP, (idx >> 7)));
		src = LWTBL_IDX2BASE(idx, start_dw);
	} else if (type == WTBL_TYPE_UMAC) {
		mt76_wr(dev,  MT_DBG_UWTBL_TOP_WDUCR_ADDR,
			FIELD_PREP(MT_DBG_UWTBL_TOP_WDUCR_GROUP, (idx >> 7)));
		src = UWTBL_IDX2BASE(idx, start_dw);
	} else if (type == WTBL_TYPE_KEY) {
		mt76_wr(dev,  MT_DBG_UWTBL_TOP_KDUCR_ADDR,
			FIELD_PREP(MT_DBG_UWTBL_TOP_KDUCR_GROUP, (idx >> 6)));
		src = KEYTBL_IDX2BASE(idx, start_dw);
	}

	while (size_dw--) {
		*dest_cpy++ = mt76_rr(dev, src);
		src += 4;
	};

	return 0;
}

#if 0
static int
mt7999_wtbl_write_raw(struct mt7999_dev *dev, u16 idx,
			  enum mt7999_wtbl_type type, u16 start_dw,
			  u32 val)
{
	u32 addr = 0;

	if (type == WTBL_TYPE_LMAC) {
		mt76_wr(dev, MT_WTBLON_TOP_WDUCR,
			FIELD_PREP(MT_WTBLON_TOP_WDUCR_GROUP, (idx >> 7)));
		addr = LWTBL_IDX2BASE(idx, start_dw);
	} else if (type == WTBL_TYPE_UMAC) {
		mt76_wr(dev, MT_DBG_UWTBL_TOP_WDUCR_ADDR,
			FIELD_PREP(MT_DBG_UWTBL_TOP_WDUCR_GROUP, (idx >> 7)));
		addr = UWTBL_IDX2BASE(idx, start_dw);
	} else if (type == WTBL_TYPE_KEY) {
		mt76_wr(dev, MT_DBG_UWTBL_TOP_WDUCR_ADDR,
			MT_DBG_UWTBL_TOP_WDUCR_TARGET |
			FIELD_PREP(MT_DBG_UWTBL_TOP_WDUCR_GROUP, (idx >> 7)));
		addr = KEYTBL_IDX2BASE(idx, start_dw);
	}

	mt76_wr(dev, addr, val);

	return 0;
}
#endif

static void dump_key_table(struct seq_file *s, u8 keyidx, u16 keyloc)
{
#define INVALID_KEY_ENTRY 	WF_UWTBL7_KEY_LOC0
#define ONE_KEY_ENTRY_LEN_IN_DW	8
	struct mt7999_dev *dev = dev_get_drvdata(s->private);
	u8 keytbl[ONE_KEY_ENTRY_LEN_IN_DW * 4] = {0};
	u8 x;

	seq_printf(s, "\t\n");
	seq_printf(s, "\t%s%d:%d\n", "keyloc", keyidx, keyloc);
	if (keyloc != INVALID_KEY_ENTRY) {
		/* Don't swap below two lines, halWtblReadRaw will
		* write new value WF_WTBLON_TOP_WDUCR_ADDR
		*/
		mt7999_wtbl_read_raw(dev, keyloc,
			WTBL_TYPE_KEY, 0, ONE_KEY_ENTRY_LEN_IN_DW, keytbl);
		seq_printf(s, "\t\tKEY WTBL Addr: group:0x%x=0x%x addr: 0x%lx\n",
			   MT_DBG_UWTBL_TOP_KDUCR_ADDR,
			   mt76_rr(dev, MT_DBG_UWTBL_TOP_KDUCR_ADDR),
			   KEYTBL_IDX2BASE(keyloc, 0));
		for (x = 0; x < ONE_KEY_ENTRY_LEN_IN_DW; x++) {
			seq_printf(s, "\t\tDW%02d: %02x %02x %02x %02x\n",
				x, keytbl[x * 4 + 3], keytbl[x * 4 + 2],
				keytbl[x * 4 + 1],keytbl[x * 4]);
		}
	}
}

static bool is_wtbl_bigtk_exist(u32 *lwtbl)
{
	if (u32_get_bits(lwtbl[0], WF_LWTBL0_MUAR) == WF_MUAR_OMAC_ADDR_BC_MC &&
	    u32_get_bits(lwtbl[16], WF_LWTBL16_CIPHER_SUIT_BIGTK) != IGTK_CIPHER_SUIT_NONE)
		return true;

	return false;
}

static char *hw_rate_ofdm_str(u16 ofdm_idx)
{
	char *HW_TX_RATE_OFDM_STR[] = {"6M", "9M", "12M", "18M", "24M",
				       "36M", "48M", "54M", "N/A"};
	switch (ofdm_idx) {
	case 11: /* 6M */
		return HW_TX_RATE_OFDM_STR[0];

	case 15: /* 9M */
		return HW_TX_RATE_OFDM_STR[1];

	case 10: /* 12M */
		return HW_TX_RATE_OFDM_STR[2];

	case 14: /* 18M */
		return HW_TX_RATE_OFDM_STR[3];

	case 9: /* 24M */
		return HW_TX_RATE_OFDM_STR[4];

	case 13: /* 36M */
		return HW_TX_RATE_OFDM_STR[5];

	case 8: /* 48M */
		return HW_TX_RATE_OFDM_STR[6];

	case 12: /* 54M */
		return HW_TX_RATE_OFDM_STR[7];

	default:
		return HW_TX_RATE_OFDM_STR[8];
	}
}

static char *hw_rate_str(u8 mode, u16 rate_idx)
{
	char *HW_TX_RATE_CCK_STR[] = {"1M", "2Mlong", "5.5Mlong", "11Mlong",
				      "N/A", "2Mshort", "5.5Mshort",
				      "11Mshort", "N/A"};

	if (mode == 0)
		return rate_idx < 8 ? HW_TX_RATE_CCK_STR[rate_idx] : HW_TX_RATE_CCK_STR[8];
	else if (mode == 1)
		return hw_rate_ofdm_str(rate_idx);
	else
		return "MCS";
}

static void
parse_rate(struct seq_file *s, u8 rate_idx, u16 txrate)
{
#define MAX_TX_MODE 16
	char *HW_TX_MODE_STR[] = {"CCK", "OFDM", "HT-Mix", "HT-GF", "VHT",
				 "N/A", "N/A", "N/A",
				 "HE_SU", "HE_EXT_SU", "HE_TRIG", "HE_MU",
				 "N/A",
				 "EHT_EXT_SU", "EHT_TRIG", "EHT_MU",
				 "N/A"};
	u8 txmode, mcs, nss, stbc, txmode_idx;

	txmode = u16_get_bits(txrate, BNMAC_RATE_ID_TXMODE_MASK);
	mcs = u16_get_bits(txrate, BNMAC_RATE_ID_MCS_MASK);
	nss = u16_get_bits(txrate, BNMAC_RATE_ID_NSTS_MASK);
	stbc = u16_get_bits(txrate, BNMAC_RATE_ID_STBC_MASK);

	txmode_idx = txmode < MAX_TX_MODE ? txmode : MAX_TX_MODE;

	seq_printf(s, "\tRate%d(0x%x):TxMode=%d(%s), TxRate=%d(%s), Nsts=%d, STBC=%d\n",
			rate_idx, txrate, txmode, HW_TX_MODE_STR[txmode_idx],
			mcs, hw_rate_str(txmode, mcs), nss, stbc);
}

					  "20/40/80/160/80+80/320MHz"};
static void mt7999_parse_lwtbl(struct seq_file *s, void *data)
{
	static const char *fcap_name[] = {"20MHz", "20/40MHz", "20/40/80MHz",
					  "20/40/80/160/80+80MHz",
					  "20/40/80/160/80+80/320MHz"};
	u8 *lwtbl = (u8 *)data;
	u32 *lwtbl_dw = (u32 *)data;

	seq_printf(s, "\t\n");
	seq_printf(s, "LinkAddr: %02x:%02x:%02x:%02x:%02x:%02x(D0[B0~15], D1[B0~31])\n",
		lwtbl[4], lwtbl[5], lwtbl[6], lwtbl[7], lwtbl[0], lwtbl[1]);

	seq_printf(s, "\t\n");
	seq_printf(s, "LWTBL DW 0/1\n");
#define _pr(_str, _dw, _field)	\
	seq_printf(s, "\t%s:%u\n", _str, u32_get_bits(lwtbl_dw[_dw], _field))
	_pr("MUAR_IDX", 0, WF_LWTBL0_MUAR);
	_pr("RCA1", 0, WF_LWTBL0_RCA1);
	_pr("KID", 0, WF_LWTBL0_KID);
	_pr("RCID", 0, WF_LWTBL0_RCID);
	_pr("BAND", 0, WF_LWTBL0_BAND);
	_pr("RV", 0, WF_LWTBL0_RV);
	_pr("RCA2", 0, WF_LWTBL0_RCA2);
	_pr("WPI_FLAG", 0, WF_LWTBL0_WPI_FLAG);

	seq_printf(s, "LWTBL DW 2\n");
	_pr("AID", 2, WF_LWTBL2_AID);
	_pr("GID_SU", 2, WF_LWTBL2_GID_SU);
	_pr("HDRT_MODE", 2, WF_LWTBL2_HDRT_MODE);
	_pr("CIPHER_PGTK", 2, WF_LWTBL2_CIPHER_SUIT_PGTK);
	_pr("FROM_DS", 2, WF_LWTBL2_FD);
	_pr("TO_DS", 2, WF_LWTBL2_TD);
	_pr("UL", 2, WF_LWTBL2_UL);
	_pr("TX_POWER_SAVE", 2, WF_LWTBL2_TX_PS);
	_pr("QOS", 2, WF_LWTBL2_QOS);
	_pr("HT", 2, WF_LWTBL2_HT);
	_pr("VHT", 2, WF_LWTBL2_VHT);
	_pr("HE", 2, WF_LWTBL2_HE);
	_pr("EHT", 2, WF_LWTBL2_EHT);
	_pr("UHR", 2, WF_LWTBL2_UHR);
	_pr("MESH", 2, WF_LWTBL2_MESH);

	seq_printf(s, "LWTBL DW 3\n");
	_pr("EMLSR_CAP", 3, WF_LWTBL3_EMLSR_CAP);
	_pr("LLI", 3, WF_LWTBL3_LLI);
	_pr("C_TDMA", 3, WF_LWTBL3_C_TDMA);
	_pr("DUAL_PTEC_EN", 3, WF_LWTBL3_DUAL_PTEC_EN);
	_pr("DUAL_CTS_CAP", 3, WF_LWTBL3_DUAL_CTS_CAP);
	_pr("EBSR", 3, WF_LWTBL3_EBSR);
	_pr("DUO", 3, WF_LWTBL3_DUO);
	_pr("DPS", 3, WF_LWTBL3_DPS);
	_pr("PFMU_IDX", 3, WF_LWTBL3_PFMU_IDX0);
	_pr("SWITCH_BACK_CAP", 3, WF_LWTBL3_SWITCH_BACK_CAP);
	_pr("DSO", 3, WF_LWTBL3_DSO);
	_pr("NPCA", 3, WF_LWTBL3_NPCA);
	_pr("TBF_HT", 3, WF_LWTBL3_TBF_HT);
	_pr("TBF_VHT", 3, WF_LWTBL3_TBF_VHT);
	_pr("TBF_HE", 3, WF_LWTBL3_TBF_HE);
	_pr("TBF_EHT", 3, WF_LWTBL3_TBF_EHT);
	_pr("TBF_UHR", 3, WF_LWTBL3_TBF_UHR);

	seq_printf(s, "LWTBL DW 4\n");
	_pr("NEGOTIATED_WINSIZE0", 4, WF_LWTBL4_NEGOTIATED_WINSIZE0);
	_pr("NEGOTIATED_WINSIZE1", 4, WF_LWTBL4_NEGOTIATED_WINSIZE1);
	_pr("NEGOTIATED_WINSIZE2", 4, WF_LWTBL4_NEGOTIATED_WINSIZE2);
	_pr("NEGOTIATED_WINSIZE3", 4, WF_LWTBL4_NEGOTIATED_WINSIZE3);
	_pr("NEGOTIATED_WINSIZE4", 4, WF_LWTBL4_NEGOTIATED_WINSIZE4);
	_pr("NEGOTIATED_WINSIZE5", 4, WF_LWTBL4_NEGOTIATED_WINSIZE5);
	_pr("NEGOTIATED_WINSIZE6", 4, WF_LWTBL4_NEGOTIATED_WINSIZE6);
	_pr("NEGOTIATED_WINSIZE7", 4, WF_LWTBL4_NEGOTIATED_WINSIZE7);
	_pr("PE", 4, WF_LWTBL4_PE);
	_pr("LDPC_2X", 4, WF_LWTBL4_LDPC_2X);
	_pr("LDPC_HT", 4, WF_LWTBL4_LDPC_HT);
	_pr("LDPC_VHT", 4, WF_LWTBL4_LDPC_VHT);
	_pr("LDPC_HE", 4, WF_LWTBL4_LDPC_HE);
	_pr("LDPC_EHT", 4, WF_LWTBL4_LDPC_EHT);
	_pr("LDPC_UHR", 4, WF_LWTBL4_LDPC_UHR);

	seq_printf(s, "LWTBL DW 5\n");
	_pr("AF", 5, WF_LWTBL5_AF);
	_pr("BSA_EN", 5, WF_LWTBL5_BSA_EN);
	_pr("RDG", 5, WF_LWTBL5_RDG);
	_pr("SMPS", 5, WF_LWTBL5_SMPS);
	_pr("DYN_BW", 5, WF_LWTBL5_DYN_BW);
	_pr("MMSS", 5, WF_LWTBL5_MMSS);
	_pr("USR", 5, WF_LWTBL5_USR);
	_pr("SR_RATE", 5, WF_LWTBL5_SR_RATE);
	_pr("SR_ABORT", 5, WF_LWTBL5_SR_ABORT);
	_pr("SPE_IDX", 5, WF_LWTBL5_SPE_IDX);
	_pr("NPCA_SWITCH_DELAY", 5, WF_LWTBL5_NPCA_SWITCH_DELAY);
	_pr("TXOP_PS_CAP", 5, WF_LWTBL5_TXOP_PS_CAP);
	_pr("DONOT_UPDATE_I_PSM", 5, WF_LWTBL5_DU_I_PSM);
	_pr("I_PSM", 5, WF_LWTBL5_I_PSM);
	_pr("PSM", 5, WF_LWTBL5_PSM);
	_pr("SKIP_TX", 5, WF_LWTBL5_SKIP_TX);

	seq_printf(s, "LWTBL DW 6\n");
	_pr("TF_PAD", 6, WF_LWTBL6_TF_PAD);
	_pr("PTF_OPT", 6, WF_LWTBL6_PTF_OPT);
	_pr("RDGBA", 6, WF_LWTBL6_RDGBA);
	_pr("DOPPL", 6, WF_LWTBL6_DOPPL);
	_pr("CBRN", 6, WF_LWTBL6_CBRN);
	_pr("DBNSS_EN", 6, WF_LWTBL6_DBNSS_EN);
	_pr("TX_POWER_OFFSET", 6, WF_LWTBL6_TX_POWER_OFFSET);
	_pr("BAF_EN", 6, WF_LWTBL6_BAF_EN);
	_pr("EHDD", 6, WF_LWTBL6_EHDD);
	_pr("UHR_SIG_MCS", 6, WF_LWTBL6_UHR_SIG_MCS);
	_pr("UHR_LTF_SYM_NUM_OPT", 6, WF_LWTBL6_UHR_LTF_SYM_NUM_OPT);
	_pr("EHT_SIG_MCS", 6, WF_LWTBL6_EHT_SIG_MCS);
	_pr("EHT_LTF_SYM_NUM_OPT", 6, WF_LWTBL6_EHT_LTF_SYM_NUM_OPT);

	seq_printf(s, "LWTBL DW 7\n");
	_pr("MRU2", 7, WF_LWTBL7_MRU2);
	_pr("IFCS", 7, WF_LWTBL7_IFCS);
	_pr("BYPASS_TXSMM", 7, WF_LWTBL7_BYPASS_TXSMM);
	_pr("LTF_UHR", 7, WF_LWTBL7_LTF_UHR);
	_pr("GI_UHR", 7, WF_LWTBL7_GI_UHR);
	_pr("LTF_EHT", 7, WF_LWTBL7_LTF_EHT);
	_pr("GI_EHT", 7, WF_LWTBL7_GI_EHT);
	_pr("G2", 7, WF_LWTBL7_G2);
	_pr("G4", 7, WF_LWTBL7_G4);
	_pr("G8", 7, WF_LWTBL7_G8);
	_pr("G16", 7, WF_LWTBL7_G16);
	_pr("G2_LTF", 7, WF_LWTBL7_G2_LTF);
	_pr("G4_LTF", 7, WF_LWTBL7_G4_LTF);
	_pr("G8_LTF", 7, WF_LWTBL7_G8_LTF);
	_pr("G16_LTF", 7, WF_LWTBL7_G16_LTF);
	_pr("G2_HE", 7, WF_LWTBL7_G2_HE);
	_pr("G4_HE", 7, WF_LWTBL7_G4_HE);
	_pr("G8_HE", 7, WF_LWTBL7_G8_HE);
	_pr("G16_HE", 7, WF_LWTBL7_G16_HE);

	seq_printf(s, "LWTBL DW 8\n");
	_pr("BA_WIN_SIZE0", 8, WF_LWTBL8_BA_WIN_SIZE0);
	_pr("BA_WIN_SIZE1", 8, WF_LWTBL8_BA_WIN_SIZE1);
	_pr("BA_WIN_SIZE2", 8, WF_LWTBL8_BA_WIN_SIZE2);
	_pr("BA_WIN_SIZE3", 8, WF_LWTBL8_BA_WIN_SIZE3);
	_pr("BA_WIN_SIZE4", 8, WF_LWTBL8_BA_WIN_SIZE4);
	_pr("BA_WIN_SIZE5", 8, WF_LWTBL8_BA_WIN_SIZE5);
	_pr("BA_WIN_SIZE6", 8, WF_LWTBL8_BA_WIN_SIZE6);
	_pr("BA_WIN_SIZE7", 8, WF_LWTBL8_BA_WIN_SIZE7);

	seq_printf(s, "LWTBL DW 9\n");
	_pr("ANT_ID0", 9, WF_LWTBL9_ANT_ID0);
	_pr("ANT_ID1", 9, WF_LWTBL9_ANT_ID1);
	_pr("ANT_ID2", 9, WF_LWTBL9_ANT_ID2);
	_pr("ANT_ID3", 9, WF_LWTBL9_ANT_ID3);
	_pr("CTS_SA_APPLY", 9, WF_LWTBL9_CTS_SA_APPLY);
	_pr("ACK_SA_APPLY", 9, WF_LWTBL9_ACK_SA_APPLY);

	seq_printf(s, "LWTBL DW 10\n");
	_pr("RTS_FAIL_CNT_AC0", 10, WF_LWTBL10_AC0_RTS_FAIL_CNT);
	_pr("RTS_FAIL_CNT_AC1", 10, WF_LWTBL10_AC1_RTS_FAIL_CNT);
	_pr("RTS_FAIL_CNT_AC2", 10, WF_LWTBL10_AC2_RTS_FAIL_CNT);
	_pr("RTS_FAIL_CNT_AC3", 10, WF_LWTBL10_AC3_RTS_FAIL_CNT);
	_pr("PARTIAL_AID", 10, WF_LWTBL10_PARTIAL_AID);
	_pr("CHK_PER", 10, WF_LWTBL10_CHK_PER);

	seq_printf(s, "LWTBL DW 11\n");
	_pr("RX_AVG_MPDU_SIZE", 11, WF_LWTBL11_RX_AVG_MPDU_SIZE);
	_pr("PRITX_SW_MODE", 11, WF_LWTBL11_PRITX_SW_MODE);
	_pr("PRITX_LR", 11, WF_LWTBL11_PRITX_LR);
	_pr("FCAP", 11, WF_LWTBL11_FCAP);
	_pr("MPDU_FAIL_CNT", 11, WF_LWTBL11_MPDU_FAIL_CNT);
	_pr("MPDU_OK_CNT", 11, WF_LWTBL11_MPDU_OK_CNT);
	_pr("RATE_IDX", 11, WF_LWTBL11_RATE_IDX);

	seq_printf(s, "\t\nFCAP:%s\n", fcap_name[u32_get_bits(lwtbl_dw[11], WF_LWTBL11_FCAP)]);

	seq_printf(s, "LWTBL DW 12\n");
	parse_rate(s, 1, u32_get_bits(lwtbl_dw[12], WF_LWTBL12_RATE1));
	parse_rate(s, 2, u32_get_bits(lwtbl_dw[12], WF_LWTBL12_RATE2));

	seq_printf(s, "LWTBL DW 13\n");
	parse_rate(s, 3, u32_get_bits(lwtbl_dw[13], WF_LWTBL13_RATE3));
	parse_rate(s, 4, u32_get_bits(lwtbl_dw[13], WF_LWTBL13_RATE4));

	seq_printf(s, "LWTBL DW 14\n");
	parse_rate(s, 5, u32_get_bits(lwtbl_dw[14], WF_LWTBL14_RATE5));
	parse_rate(s, 6, u32_get_bits(lwtbl_dw[14], WF_LWTBL14_RATE6));

	seq_printf(s, "LWTBL DW 15\n");
	parse_rate(s, 7, u32_get_bits(lwtbl_dw[15], WF_LWTBL15_RATE7));
	parse_rate(s, 8, u32_get_bits(lwtbl_dw[15], WF_LWTBL15_RATE8));

	seq_printf(s, "LWTBL DW 16\n");
	if (u32_get_bits(lwtbl_dw[0], WF_LWTBL0_MUAR) == WF_MUAR_OMAC_ADDR_BC_MC) {
		_pr("CIPHER_SUIT_IGTK", 16, WF_LWTBL16_CIPHER_SUIT_IGTK);
		_pr("CIPHER_SUIT_BIGTK", 16, WF_LWTBL16_CIPHER_SUIT_BIGTK);
	} else {
		_pr("RATE1_TX_CNT", 16, WF_LWTBL16_RATE1_TX_CNT);
		_pr("RATE1_FAIL_CNT", 16, WF_LWTBL16_RATE1_FAIL_CNT);
	}

	seq_printf(s, "LWTBL DW 30\n");
	_pr("DISPATCH_POLICY0", 30, WF_LWTBL30_DISPATCH_POLICY0);
	_pr("DISPATCH_POLICY1", 30, WF_LWTBL30_DISPATCH_POLICY1);
	_pr("DISPATCH_POLICY2", 30, WF_LWTBL30_DISPATCH_POLICY2);
	_pr("DISPATCH_POLICY3", 30, WF_LWTBL30_DISPATCH_POLICY3);
	_pr("DISPATCH_POLICY4", 30, WF_LWTBL30_DISPATCH_POLICY4);
	_pr("DISPATCH_POLICY5", 30, WF_LWTBL30_DISPATCH_POLICY5);
	_pr("DISPATCH_POLICY6", 30, WF_LWTBL30_DISPATCH_POLICY6);
	_pr("DISPATCH_POLICY7", 30, WF_LWTBL30_DISPATCH_POLICY7);
	_pr("OWN_MLD_ID", 30, WF_LWTBL30_OWN_MLD_ID);
	_pr("EMLSR0", 30, WF_LWTBL30_EMLSR0);
	_pr("EMLMR0", 30, WF_LWTBL30_EMLMR0);
	_pr("EMLSR1", 30, WF_LWTBL30_EMLSR1);
	_pr("EMLMR1", 30, WF_LWTBL30_EMLMR1);
	_pr("EMLSR2", 30, WF_LWTBL30_EMLSR2);
	_pr("EMLMR2", 30, WF_LWTBL30_EMLMR2);
	_pr("STR_BITMAP", 30, WF_LWTBL30_STR_BITMAP);

	seq_printf(s, "LWTBL DW 31\n");
	_pr("DISPATCH_ORDER", 31, WF_LWTBL31_DISPATCH_ORDER);
	_pr("DISPATCH_RATIO", 31, WF_LWTBL31_DISPATCH_RATIO);
	_pr("LINK_MGF", 31, WF_LWTBL31_LINK_MGF);

	seq_printf(s, "LWTBL DW 37\n");
	_pr("RELATED_IDX0", 37, WF_LWTBL37_RELATED_IDX0);
	_pr("RELATED_BAND0", 37, WF_LWTBL37_RELATED_BAND0);
	_pr("PRI_MLD_BAND", 37, WF_LWTBL37_PRIMARY_MLD_BAND);
	_pr("RELATED_IDX1", 37, WF_LWTBL37_RELATED_IDX1);
	_pr("RELATED_BAND1", 37, WF_LWTBL37_RELATED_BAND1);
	_pr("SEC_MLD_BAND", 37, WF_LWTBL37_SECONDARY_MLD_BAND);

	seq_printf(s, "LWTBL DW 32\n");
	_pr("RXD_DUP_MODE", 32, WF_LWTBL32_RXD_DUP_MODE);
	_pr("PSM_INCLUDE_ALL_CTRL", 32, WF_LWTBL32_PSM_INCLUDE_ALL_CTRL);
	_pr("DROP", 32, WF_LWTBL32_DROP);
	_pr("DIS_RHTR", 32, WF_LWTBL32_DIS_RHTR);
	_pr("RIBF", 32, WF_LWTBL32_RIBF);
	_pr("IGN_FBK", 32, WF_LWTBL32_IGN_FBK);
	_pr("BA_MIN_SSN", 32, WF_LWTBL32_BA_MIN_SSN);
	_pr("SW", 32, WF_LWTBL32_SW);
	_pr("BA_MODE", 32, WF_LWTBL32_BA_MODE);
	_pr("BFTX_TB", 32, WF_LWTBL32_BFTX_TB);
	_pr("CASCAD", 32, WF_LWTBL32_CASCAD);
	_pr("ALL_ACK", 32, WF_LWTBL32_ALL_ACK);
	_pr("MPDU_SIZE", 32, WF_LWTBL32_MPDU_SIZE);
	_pr("ACK_EN", 32, WF_LWTBL32_ACK_EN);

	seq_printf(s, "LWTBL DW 33\n");
	_pr("OM_INFO", 33, WF_LWTBL33_OM_INFO);
	_pr("OM_INFO_EHT", 33, WF_LWTBL33_OM_INFO_EHT);
	_pr("RXD_DUP_FOR_OM_CHG", 33, WF_LWTBL33_RXD_DUP_FOR_OM_CHG);
	_pr("RXD_DUP_WHITE_LIST", 33, WF_LWTBL33_RXD_DUP_WHITE_LIST);

	seq_printf(s, "LWTBL DW 34\n");
	_pr("USER_RSSI", 34, WF_LWTBL34_USER_RSSI);
	_pr("USER_SNR", 34, WF_LWTBL34_USER_SNR);
	_pr("RAPID_REACTION_RATE", 34, WF_LWTBL34_RAPID_REACTION_RATE);
	_pr("HT_AMSDU(Read Only)", 34, WF_LWTBL34_HT_AMSDU);
	_pr("AMSDU_CROSS_LG(Read Only)", 34, WF_LWTBL34_AMSDU_CROSS_LG);

	seq_printf(s, "LWTBL DW 35\n");
	_pr("RESP_RCPI0", 35, WF_LWTBL35_RESP_RCPI0);
	_pr("RESP_RCPI1", 35, WF_LWTBL35_RESP_RCPI1);
	_pr("RESP_RCPI2", 35, WF_LWTBL35_RESP_RCPI2);
	_pr("RESP_RCPI3", 35, WF_LWTBL35_RESP_RCPI3);

	seq_printf(s, "LWTBL DW 36\n");
	_pr("SNR_RX0", 36, WF_LWTBL36_SNR_RX0);
	_pr("SNR_RX1", 36, WF_LWTBL36_SNR_RX1);
	_pr("SNR_RX2", 36, WF_LWTBL36_SNR_RX2);
	_pr("SNR_RX3", 36, WF_LWTBL36_SNR_RX3);

	seq_printf(s, "LWTBL DW 38\n");
	_pr("RESP_RCPI4", 38, WF_LWTBL38_RESP_RCPI4);
	_pr("SNR_RX4", 38, WF_LWTBL38_SNR_RX4);
#undef _pr
}

static void
mt7999_parse_uwtbl(struct seq_file *s, void *data_uwtbl, void *data_lwtbl)
{
	u8 amsdu_len;
	u8 *uwtbl = (u8 *)data_uwtbl;
	u32 *uwtbl_dw = (u32 *)data_uwtbl;
	u32 *lwtbl_dw = (u32 *)data_lwtbl;
	int mesh_type, mesh_endian, mesh_deamsdu;

#define _pr(_str, _dw, _field)	\
	seq_printf(s, "\t%s:%u\n", _str, u32_get_bits(uwtbl_dw[_dw], _field))

	seq_printf(s, "\t\n");
	seq_printf(s, "MldAddr: %02x:%02x:%02x:%02x:%02x:%02x(D0[B0~15], D1[B0~31])\n",
		   uwtbl[4], uwtbl[5], uwtbl[6], uwtbl[7], uwtbl[0], uwtbl[1]);

	seq_printf(s, "\t\n");
	seq_printf(s, "UWTBL MLO info\n");
	_pr("OMLD_ID", 0, WF_UWTBL0_OWN_MLD_ID);

	seq_printf(s, "\t\n");
	seq_printf(s, "UWTBL related idx\n");
	_pr("RELATED_IDX0", 9, WF_UWTBL9_RELATED_IDX0);
	_pr("RELATED_BAND0", 9, WF_UWTBL9_RELATED_BAND0);
	_pr("PRIMARY_MLD_BAND", 9, WF_UWTBL9_PRIMARY_MLD_BAND);
	_pr("RELATED_IDX1", 9, WF_UWTBL9_RELATED_IDX1);
	_pr("RELATED_BAND1", 9, WF_UWTBL9_RELATED_BAND1);
	_pr("SECONDARY_MLD_BAND", 9, WF_UWTBL9_SECONDARY_MLD_BAND);

	seq_printf(s, "\t\n");
	seq_printf(s, "UWTBL SEC info\n");
	_pr("SEC_ADDR_MODE", 7, WF_UWTBL7_SEC_ADDR_MODE);
	_pr("AAD_OM", 7, WF_UWTBL7_AAD_OM);
	_pr("WPI_EVEN", 7, WF_UWTBL7_WPI_EVEN);
	_pr("SPP_EN", 7, WF_UWTBL7_SPP_EN);

	seq_printf(s, "\t\n");
	seq_printf(s, "UWTBL PN\n");
	_pr("PN[31:0]", 2, WF_UWTBL2_PN_31_0);
	_pr("PN[47:32]", 3, WF_UWTBL3_PN_15_0);
	_pr("COM_SN", 3, WF_UWTBL3_COM_SN);

	if (is_wtbl_bigtk_exist(lwtbl_dw)) {
		_pr("BIPN[31:0]", 4, WF_UWTBL4_RX_BIPN_31_0);
		_pr("BIPN[47:32]", 5, WF_UWTBL5_RX_BIPN_47_32);
	} else {
		seq_printf(s, "\t\n");
		seq_printf(s, "UWTBL SN\n");
		_pr("TID0_AC0_SN", 4, WF_UWTBL4_TID0_SN);
		_pr("TID1_AC1_SN", 4, WF_UWTBL4_TID1_SN);
		seq_printf(s, "\tTID2_AC2_SN:%u\n",
			   u32_get_bits(uwtbl_dw[5], WF_UWTBL5_TID2_SN_11_8) << 8 |
			   u32_get_bits(uwtbl_dw[4], WF_UWTBL4_TID2_SN_7_0));
		_pr("TID3_AC3_SN", 5, WF_UWTBL5_TID3_SN);
		_pr("TID4_SN", 5, WF_UWTBL5_TID4_SN);
		seq_printf(s, "\tTID5_SN:%u\n",
			   u32_get_bits(uwtbl_dw[6], WF_UWTBL6_TID5_SN_11_4) << 4 |
			   u32_get_bits(uwtbl_dw[5], WF_UWTBL5_TID5_SN_3_0));
		_pr("TID6_SN", 6, WF_UWTBL6_TID6_SN);
		_pr("TID7_SN", 6, WF_UWTBL6_TID7_SN);
		_pr("COM_SN", 3, WF_UWTBL3_COM_SN);
	}

	seq_printf(s, "\t\n");
	seq_printf(s, "UWTBL key info\n");
	_pr("Key Loc 0:", 7, WF_UWTBL7_KEY_LOC0);
	_pr("Key Loc 1:", 7, WF_UWTBL7_KEY_LOC1);
	if (is_wtbl_bigtk_exist(lwtbl_dw))
		_pr("Key Loc 2:", 6, WF_UWTBL6_KEY_LOC2);

	dump_key_table(s, 0, u32_get_bits(uwtbl_dw[7], WF_UWTBL7_KEY_LOC0));
	dump_key_table(s, 1, u32_get_bits(uwtbl_dw[7], WF_UWTBL7_KEY_LOC1));
	if (is_wtbl_bigtk_exist(lwtbl_dw))
		dump_key_table(s, 2, u32_get_bits(uwtbl_dw[6], WF_UWTBL6_KEY_LOC2));

	_pr("UWTBL_WMM_Q", 0, WF_UWTBL0_WMM_Q);
	_pr("UWTBL_QOS", 8, WF_UWTBL8_QOS);
	_pr("UWTBL_HT_VHT_HE", 8, WF_UWTBL8_HT);
	_pr("UWTBL_HDRT_MODE", 8, WF_UWTBL8_HDRT_MODE);
	_pr("HW AMSDU Enable", 8, WF_UWTBL8_AMSDU_CFG_EN);
	amsdu_len = u32_get_bits(uwtbl_dw[8], WF_UWTBL8_AMSDU_CFG_LEN);
	if (!amsdu_len)
		seq_printf(s, "\tHW AMSDU Len:invalid (WTBL value=0x%x)\n", amsdu_len);
	else if (amsdu_len == 1)
		seq_printf(s, "\tHW AMSDU Len:1~255 (WTBL value=0x%x)\n", amsdu_len);
	else
		seq_printf(s, "\tHW AMSDU Len:%d~%d (WTBL value=0x%x)\n",
			   256 * (amsdu_len - 1), 256 * (amsdu_len) - 1, amsdu_len);

	seq_printf(s, "\tHW AMSDU Num:%u (WTBL value=0x%x)\n",
		   u32_get_bits(uwtbl_dw[8], WF_UWTBL8_AMSDU_CFG_NUM) + 1,
		   u32_get_bits(uwtbl_dw[8], WF_UWTBL8_AMSDU_CFG_NUM));
#undef _pr

	mesh_type = u32_get_bits(uwtbl_dw[8], WF_UWTBL8_MESH_AMSDU_SUBFRM_LEN_TYPE);
	mesh_endian = u32_get_bits(uwtbl_dw[8], WF_UWTBL8_MESH_AMSDU_SUBFRM_LEN_ENDIAN);
	mesh_deamsdu = u32_get_bits(uwtbl_dw[8], WF_UWTBL8_MESH_RX_HW_DE_AMSDU);

	seq_printf(s, "\t%s: %d\n", "Mesh RX HW De-AMSDU enabled", mesh_deamsdu);
	seq_printf(s, "\t%s: %d (%s)\n", "Mesh A-MSDU subframe length type",
			mesh_type, mesh_type ==
			UWTBL_MESH_AMSDU_SUBFRM_LEN_EXCL_MESH_CTRL ?
			"exclude mesh control field length" :
			"include mesh control field length");
	seq_printf(s, "\t%s: %d (%s)\n", "Mesh A-MSDU subframe length endian",
			mesh_endian, mesh_endian ==
			UWTBL_MESH_AMSDU_SUBFRM_LEN_BIG_ENDIAN ?
			"big endian" : "little endian");
}
static int mt7999_wtbl_read(struct seq_file *s, void *data)
{
	struct mt7999_dev *dev = dev_get_drvdata(s->private);
	u8 lwtbl[LWTBL_LEN_IN_DW * 4] = {0};
	u8 uwtbl[UWTBL_LEN_IN_DW * 4] = {0};
	int x;

	mt7999_wtbl_read_raw(dev, dev->wlan_idx, WTBL_TYPE_LMAC, 0,
				 LWTBL_LEN_IN_DW, lwtbl);
	mt7999_wtbl_read_raw(dev, dev->wlan_idx, WTBL_TYPE_UMAC, 0,
				 UWTBL_LEN_IN_DW, uwtbl);

	seq_printf(s, "Dump WTBL info of WLAN_IDX:%d\n", dev->wlan_idx);
	seq_printf(s, "LMAC WTBL Addr: group:0x%x=0x%x addr: 0x%lx\n",
		   MT_WTBLON_TOP_WDUCR,
		   mt76_rr(dev, MT_WTBLON_TOP_WDUCR),
		   LWTBL_IDX2BASE(dev->wlan_idx, 0));
	for (x = 0; x < LWTBL_LEN_IN_DW; x++) {
		seq_printf(s, "DW%02d: %02x %02x %02x %02x\n",
			   x, lwtbl[x * 4 + 3], lwtbl[x * 4 + 2],
			   lwtbl[x * 4 + 1],lwtbl[x * 4]);
	}
	seq_printf(s, "UMAC WTBL Addr: group:0x%x=0x%x addr: 0x%lx\n",
		   MT_DBG_UWTBL_TOP_WDUCR_ADDR,
		   mt76_rr(dev, MT_DBG_UWTBL_TOP_WDUCR_ADDR),
		   UWTBL_IDX2BASE(dev->wlan_idx, 0));
	for (x = 0; x < UWTBL_LEN_IN_DW; x++) {
		seq_printf(s, "DW%02d: %02x %02x %02x %02x\n",
			   x, uwtbl[x * 4 + 3], uwtbl[x * 4 + 2],
			   uwtbl[x * 4 + 1],uwtbl[x * 4]);
	}

	mt7999_parse_lwtbl(s, lwtbl);
	mt7999_parse_uwtbl(s, uwtbl, lwtbl);

	return 0;
}

static int mt7999_sta_info(struct seq_file *s, void *data)
{
	struct mt7999_dev *dev = dev_get_drvdata(s->private);
	struct mt76_wcid *wcid;
	u8 lwtbl[LWTBL_LEN_IN_DW * 4] = {0};
	u16 i;

	for (i = 1; i < mt7999_wtbl_size(dev); i++) {
		mt7999_wtbl_read_raw(dev, i, WTBL_TYPE_LMAC, 0,
				     LWTBL_LEN_IN_DW, lwtbl);

		if (lwtbl[4] || lwtbl[5] || lwtbl[6] || lwtbl[7] || lwtbl[0] || lwtbl[1]) {
			u32 *addr, dw_value;

			seq_printf(s, "wcid:%d\tAddr: %02x:%02x:%02x:%02x:%02x:%02x",
				   i, lwtbl[4], lwtbl[5], lwtbl[6], lwtbl[7], lwtbl[0], lwtbl[1]);

			addr = (u32 *)&(lwtbl[2 * 4]);
			dw_value = *addr;
			seq_printf(s, "\tAID:%u", u32_get_bits(dw_value, WF_LWTBL2_AID));

			addr = (u32 *)&(lwtbl[5 * 4]);
			dw_value = *addr;
			seq_printf(s, "\tPSM:%u", u32_get_bits(dw_value, WF_LWTBL5_PSM));

			rcu_read_lock();
			wcid = rcu_dereference(dev->mt76.wcid[i]);
			if (wcid && wcid->token_flush_count)
				seq_printf(s, "\ttoken_flush_count:%u", wcid->token_flush_count);
			rcu_read_unlock();

			memset(lwtbl, 0, sizeof(lwtbl));
			seq_printf(s, "\n");
		}
	}

	return 0;
}

static int mt7999_token_read(struct seq_file *s, void *data)
{
	struct mt7999_dev *dev = dev_get_drvdata(s->private);
	int msdu_id, i;
	struct mt76_txwi_cache *txwi;

	seq_printf(s, "Token from host:\n");
	spin_lock_bh(&dev->mt76.token_lock);
	idr_for_each_entry(&dev->mt76.token, txwi, msdu_id) {
		seq_printf(s, "%4d (wcid = %4d, pending time %u ms)\n",
			   msdu_id, txwi->wcid,
			   jiffies_to_msecs(jiffies - txwi->jiffies));
	}
	seq_printf(s, "\n");
	for (i = 0; i < __MT_MAX_BAND; i++) {
		struct mt76_phy *phy = mt76_dev_phy(&dev->mt76, i);

		if (!mt7999_band_valid(dev, i))
			continue;

		seq_printf(s, "Band%u consume: %d, free: %d total: %d\n",
			   i, phy->tokens, dev->mt76.token_threshold - phy->tokens,
			   dev->mt76.token_threshold);
	}
	spin_unlock_bh(&dev->mt76.token_lock);

	return 0;
}

static int
mt7999_scs_enable_set(void *data, u64 val)
{
	struct mt7999_phy *phy = data;

	return mt7999_mcu_set_scs(phy, (u8) val);
}
DEFINE_DEBUGFS_ATTRIBUTE(fops_scs_enable, NULL,
			 mt7999_scs_enable_set, "%lld\n");

static int
mt7999_txpower_level_set(void *data, u64 val)
{
	struct mt7999_phy *phy = data;
	int ret;

	if (val > 100)
		return -EINVAL;

	ret = mt7999_mcu_set_tx_power_ctrl(phy, UNI_TXPOWER_PERCENTAGE_CTRL, !!val);
	if (ret)
		return ret;

	return mt7999_mcu_set_tx_power_ctrl(phy, UNI_TXPOWER_PERCENTAGE_DROP_CTRL, val);
}

DEFINE_DEBUGFS_ATTRIBUTE(fops_txpower_level, NULL,
			 mt7999_txpower_level_set, "%lld\n");

static ssize_t
mt7999_get_txpower_info(struct file *file, char __user *user_buf,
			size_t count, loff_t *ppos)
{
	struct mt7999_phy *phy = file->private_data;
	struct mt7999_mcu_txpower_event *event;
	struct txpower_basic_info *basic_info;
	struct device_node *np;
	static const size_t size = 2048;
	int len = 0;
	ssize_t ret;
	char *buf;

	buf = kzalloc(size, GFP_KERNEL);
	event = kzalloc(sizeof(*event) + sizeof(struct txpower_basic_info), GFP_KERNEL);
	if (!buf || !event) {
		ret = -ENOMEM;
		goto out;
	}

	ret = mt7999_mcu_get_tx_power_info(phy, BASIC_INFO, event);
	if (ret || le16_to_cpu(event->tag) != UNI_EVENT_TXPOWER_BASIC_INFO)
		goto out;

	basic_info = (struct txpower_basic_info *)&event->data;

	len += scnprintf(buf + len, size - len,
			 "======================== BASIC INFO ========================\n");
	len += scnprintf(buf + len, size - len, "    Band Index: %d, Channel Band: %d\n",
			 basic_info->band_idx, basic_info->band);
	len += scnprintf(buf + len, size - len, "    PA Type: %s\n",
			 basic_info->is_epa ? "ePA" : "iPA");
	len += scnprintf(buf + len, size - len, "    LNA Type: %s\n",
			 basic_info->is_elna ? "eLNA" : "iLNA");

	len += scnprintf(buf + len, size - len,
			 "------------------------------------------------------------\n");
	len += scnprintf(buf + len, size - len, "    SKU: %s\n",
			 basic_info->sku_enable ? "enable" : "disable");
	len += scnprintf(buf + len, size - len, "    Percentage Control: %s\n",
			 basic_info->percentage_ctrl_enable ? "enable" : "disable");
	len += scnprintf(buf + len, size - len, "    Power Drop: %d [dBm]\n",
			 basic_info->power_drop_level >> 1);
	len += scnprintf(buf + len, size - len, "    Backoff: %s\n",
			 basic_info->bf_backoff_enable ? "enable" : "disable");
	len += scnprintf(buf + len, size - len, "    TX Front-end Loss:  %d, %d, %d, %d\n",
			 basic_info->front_end_loss_tx[0], basic_info->front_end_loss_tx[1],
			 basic_info->front_end_loss_tx[2], basic_info->front_end_loss_tx[3]);
	len += scnprintf(buf + len, size - len, "    RX Front-end Loss:  %d, %d, %d, %d\n",
			 basic_info->front_end_loss_rx[0], basic_info->front_end_loss_rx[1],
			 basic_info->front_end_loss_rx[2], basic_info->front_end_loss_rx[3]);
	len += scnprintf(buf + len, size - len,
			 "    MU TX Power Mode:  %s\n",
			 basic_info->mu_tx_power_manual_enable ? "manual" : "auto");
	len += scnprintf(buf + len, size - len,
			 "    MU TX Power (Auto / Manual): %d / %d [0.5 dBm]\n",
			 basic_info->mu_tx_power_auto, basic_info->mu_tx_power_manual);
	len += scnprintf(buf + len, size - len,
			 "    Thermal Compensation:  %s\n",
			 basic_info->thermal_compensate_enable ? "enable" : "disable");
	len += scnprintf(buf + len, size - len,
			 "    Theraml Compensation Value: %d\n",
			 basic_info->thermal_compensate_value);
	np = mt76_find_power_limits_node(phy->mt76);
	len += scnprintf(buf + len, size - len,
			 "    RegDB:  %s\n",
			 !np ? "enable" : "disable");
	len += scnprintf(buf + len, size - len,
			 "    sku_index:  %d\n", phy->mt76->sku_idx);
	len += scnprintf(buf + len, size - len,
			 "    lpi:  %s\n",
			 phy->mt76->dev->lpi_mode ? "enable" : "disable");
	ret = simple_read_from_buffer(user_buf, count, ppos, buf, len);

out:
	kfree(buf);
	kfree(event);
	return ret;
}

static const struct file_operations mt7999_txpower_info_fops = {
	.read = mt7999_get_txpower_info,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

#define MT7999_SKU_FMT_LEN	21

#define mt7999_txpower_puts(rate, bw)							\
({											\
	len += scnprintf(buf + len, size - len, "%-*s:", MT7999_SKU_FMT_LEN,		\
			 bw ? "BW" #bw "/" #rate " (TMAC)" :				\
			 #rate " (TMAC)");						\
	for (i = 0; i < mt7999_sku_group_len[SKU_##rate]; i++, offs++)			\
		len += scnprintf(buf + len, size - len, " %6d",				\
				 rate_info->frame_power[offs]);				\
	len += scnprintf(buf + len, size - len, "\n");					\
})

static ssize_t
mt7999_get_txpower_sku(struct file *file, char __user *user_buf,
		       size_t count, loff_t *ppos)
{
	struct mt7999_phy *phy = file->private_data;
	struct mt7999_dev *dev = phy->dev;
	struct mt7999_mcu_txpower_event *event;
	struct txpower_phy_rate_info *rate_info;
	struct ieee80211_channel *chan = phy->mt76->chandef.chan;
	struct ieee80211_supported_band sband;
	u8 band_idx = phy->mt76->band_idx;
	static const size_t size = 8192;
	int i, offs = 0, len = 0;
	u32 target_power = 0;
	int n_chains = hweight16(phy->mt76->chainmask);
	int path_delta = mt76_tx_power_path_delta(n_chains);
	int pwr_delta;
	ssize_t ret;
	char *buf;

	buf = kzalloc(size, GFP_KERNEL);
	event = kzalloc(sizeof(*event) + sizeof(*rate_info) + mt7999_get_sku_rate_num(),
			GFP_KERNEL);
	if (!buf || !event) {
		ret = -ENOMEM;
		goto out;
	}

	ret = mt7999_mcu_get_tx_power_info(phy, PHY_RATE_INFO, event);
	if (ret || le16_to_cpu(event->tag) != UNI_EVENT_TXPOWER_PHY_RATE_INFO)
		goto out;

	rate_info = (struct txpower_phy_rate_info *)&event->data;

	len += scnprintf(buf + len, size - len,
			 "\nPhy %d TX Power Table (Channel %d)\n",
			 band_idx, phy->mt76->chandef.chan->hw_value);
	len += scnprintf(buf + len, size - len,
			 "%-*s  %6s %6s %6s %6s\n", MT7999_SKU_FMT_LEN,
			 " ", "1m", "2m", "5m", "11m");
	mt7999_txpower_puts(CCK, 0);

	len += scnprintf(buf + len, size - len,
			 "%-*s  %6s %6s %6s %6s %6s %6s %6s %6s\n",
			 MT7999_SKU_FMT_LEN,
			 " ", "6m", "9m", "12m", "18m", "24m", "36m", "48m",
			 "54m");
	mt7999_txpower_puts(OFDM, 0);

	len += scnprintf(buf + len, size - len,
			 "%-*s  %6s %6s %6s %6s %6s %6s %6s %6s\n",
			 MT7999_SKU_FMT_LEN,
			 " ", "mcs0", "mcs1", "mcs2", "mcs3", "mcs4",
			 "mcs5", "mcs6", "mcs7");
	mt7999_txpower_puts(HT20, 0);

	len += scnprintf(buf + len, size - len,
			 "%-*s  %6s %6s %6s %6s %6s %6s %6s %6s %6s\n",
			 MT7999_SKU_FMT_LEN,
			 " ", "mcs0", "mcs1", "mcs2", "mcs3", "mcs4", "mcs5",
			 "mcs6", "mcs7", "mcs32");
	mt7999_txpower_puts(HT40, 0);

	len += scnprintf(buf + len, size - len,
			 "%-*s  %6s %6s %6s %6s %6s %6s %6s %6s %6s %6s %6s %6s\n",
			 MT7999_SKU_FMT_LEN,
			 " ", "mcs0", "mcs1", "mcs2", "mcs3", "mcs4", "mcs5",
			 "mcs6", "mcs7", "mcs8", "mcs9", "mcs10", "mcs11");
	mt7999_txpower_puts(VHT20, 0);
	mt7999_txpower_puts(VHT40, 0);
	mt7999_txpower_puts(VHT80, 0);
	mt7999_txpower_puts(VHT160, 0);
	mt7999_txpower_puts(HE26, 0);
	mt7999_txpower_puts(HE52, 0);
	mt7999_txpower_puts(HE106, 0);
	mt7999_txpower_puts(HE242, 20);
	mt7999_txpower_puts(HE484, 40);
	mt7999_txpower_puts(HE996, 80);
	mt7999_txpower_puts(HE2x996, 160);

	len += scnprintf(buf + len, size - len,
			 "%-*s  %6s %6s %6s %6s %6s %6s %6s %6s ",
			 MT7999_SKU_FMT_LEN,
			 " ", "mcs0", "mcs1", "mcs2", "mcs3", "mcs4", "mcs5", "mcs6", "mcs7");
	len += scnprintf(buf + len, size - len,
			 "%6s %6s %6s %6s %6s %6s %6s %6s\n",
			 "mcs8", "mcs9", "mcs10", "mcs11", "mcs12", "mcs13", "mcs14", "mcs15");
	mt7999_txpower_puts(EHT26, 0);
	mt7999_txpower_puts(EHT52, 0);
	mt7999_txpower_puts(EHT106, 0);
	mt7999_txpower_puts(EHT242, 20);
	mt7999_txpower_puts(EHT484, 40);
	mt7999_txpower_puts(EHT996, 80);
	mt7999_txpower_puts(EHT2x996, 160);
	mt7999_txpower_puts(EHT4x996, 320);
	mt7999_txpower_puts(EHT26_52, 0);
	mt7999_txpower_puts(EHT26_106, 0);
	mt7999_txpower_puts(EHT484_242, 0);
	mt7999_txpower_puts(EHT996_484, 0);
	mt7999_txpower_puts(EHT996_484_242, 0);
	mt7999_txpower_puts(EHT2x996_484, 0);
	mt7999_txpower_puts(EHT3x996, 0);
	mt7999_txpower_puts(EHT3x996_484, 0);

	len += scnprintf(buf + len, size - len,
			 "%-*s  %6s %6s %6s %6s %6s %6s %6s %6s %6s %6s ",
			 MT7999_SKU_FMT_LEN,
			 " ", "mcs0", "mcs1", "mcs2", "mcs3", "mcs4", "mcs5",
			 "mcs6", "mcs7", "mcs8", "mcs9");
	len += scnprintf(buf + len, size - len,
			 "%6s %6s %6s %6s %6s %6s %6s %6s %6s %6s\n",
			 "mcs10", "mcs11", "mcs12", "mcs13", "mcs14", "mcs15",
			 "mcs17", "mcs19", "mcs20", "mcs23");
	mt7999_txpower_puts(UHR26, 0);
	mt7999_txpower_puts(UHR52, 0);
	mt7999_txpower_puts(UHR106, 0);
	mt7999_txpower_puts(UHR242, 20);
	mt7999_txpower_puts(UHR484, 40);
	mt7999_txpower_puts(UHR996, 80);
	mt7999_txpower_puts(UHR2x996, 160);
	mt7999_txpower_puts(UHR4x996, 320);
	mt7999_txpower_puts(UHR26_52, 0);
	mt7999_txpower_puts(UHR26_106, 0);
	mt7999_txpower_puts(UHR484_242, 0);
	mt7999_txpower_puts(UHR996_484, 0);
	mt7999_txpower_puts(UHR996_484_242, 0);
	mt7999_txpower_puts(UHR2x996_484, 0);
	mt7999_txpower_puts(UHR3x996, 0);
	mt7999_txpower_puts(UHR3x996_484, 0);

	len += scnprintf(buf + len, size - len, "\nePA Gain: %d\n",
			 rate_info->epa_gain);

	len += scnprintf(buf + len, size - len,
			 "\nBBP TX Power (target power from TMAC)  : %6hhd [0.5 dBm]\n",
			 (s8)mt76_get_field(dev, MT_WF_PHYDFE_TSSI_TXCTRL01(band_idx),
			 		    MT_WF_PHYDFE_TSSI_TXCTRL_POWER_TMAC));
	len += scnprintf(buf + len, size - len,
			 "RegDB maximum power:\t%d [dBm]\n",
			 chan->max_reg_power);

	if (chan->band == NL80211_BAND_2GHZ)
		sband = phy->mt76->sband_2g.sband;
	else if (chan->band == NL80211_BAND_5GHZ)
		sband = phy->mt76->sband_5g.sband;
	else if (chan->band == NL80211_BAND_6GHZ)
		sband = phy->mt76->sband_6g.sband;

	pwr_delta = mt7999_eeprom_get_power_delta(dev, sband.band);

	target_power = max_t(u32, target_power, mt7999_eeprom_get_target_power(dev, chan));
	target_power += pwr_delta + path_delta;
	target_power = DIV_ROUND_UP(target_power, 2);
	len += scnprintf(buf + len, size - len,
			 "eeprom maximum power:\t%d [dBm]\n",
			 target_power);

	len += scnprintf(buf + len, size - len,
			 "path_delta:\t%d [0.5 dBm]\n",
			 path_delta);

	ret = simple_read_from_buffer(user_buf, count, ppos, buf, len);

out:
	kfree(buf);
	kfree(event);
	return ret;
}

static const struct file_operations mt7999_txpower_sku_fops = {
	.read = mt7999_get_txpower_sku,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

#define mt7999_txpower_path_puts(rate, arr_length)					\
({											\
	len += scnprintf(buf + len, size - len, "%23s:", #rate " (TMAC)");		\
	if (strncmp(#rate, "BF-OFDM", 7) == 0)						\
		len += scnprintf(buf + len, size - len, "     ");			\
	for (i = 0; i < arr_length; i++, offs++)					\
		len += scnprintf(buf + len, size - len, " %4d",				\
				 backoff_info->frame_power[offs]);			\
	len += scnprintf(buf + len, size - len, "\n");					\
})

static ssize_t
mt7999_get_txpower_path(struct file *file, char __user *user_buf,
		       size_t count, loff_t *ppos)
{
	struct mt7999_phy *phy = file->private_data;
	struct mt7999_mcu_txpower_event *event;
	struct txpower_backoff_table_info *backoff_info;
	static const size_t size = 5120;
	int i, offs = 0, len = 0;
	ssize_t ret;
	char *buf;

	buf = kzalloc(size, GFP_KERNEL);
	event = kzalloc(sizeof(*event) + sizeof(*backoff_info) + MT7999_SKU_PATH_NUM,
			GFP_KERNEL);
	if (!buf || !event) {
		ret = -ENOMEM;
		goto out;
	}

	ret = mt7999_mcu_get_tx_power_info(phy, BACKOFF_TABLE_INFO, event);
	if (ret || le16_to_cpu(event->tag) != UNI_EVENT_TXPOWER_BACKOFF_TABLE_INFO)
		goto out;

	backoff_info = (struct txpower_backoff_table_info *)&event->data;

	len += scnprintf(buf + len, size - len, "\n%*c", 25, ' ');
	len += scnprintf(buf + len, size - len, "1T1S/2T1S/3T1S/4T1S/5T1S/2T2S/3T2S/4T2S/5T2S/"
			 "3T3S/4T3S/5T3S/4T4S/5T4S/5T5S\n");

	mt7999_txpower_path_puts(CCK, 5);
	mt7999_txpower_path_puts(OFDM, 5);
	mt7999_txpower_path_puts(BF-OFDM, 4);

	mt7999_txpower_path_puts(RU26, 15);
	mt7999_txpower_path_puts(BF-RU26, 15);
	mt7999_txpower_path_puts(RU52, 15);
	mt7999_txpower_path_puts(BF-RU52, 15);
	mt7999_txpower_path_puts(RU26_52, 15);
	mt7999_txpower_path_puts(BF-RU26_52, 15);
	mt7999_txpower_path_puts(RU106, 15);
	mt7999_txpower_path_puts(BF-RU106, 15);
	mt7999_txpower_path_puts(RU106_52, 15);
	mt7999_txpower_path_puts(BF-RU106_52, 15);

	mt7999_txpower_path_puts(BW20/RU242, 15);
	mt7999_txpower_path_puts(BF-BW20/RU242, 15);
	mt7999_txpower_path_puts(BW40/RU484, 15);
	mt7999_txpower_path_puts(BF-BW40/RU484, 15);
	mt7999_txpower_path_puts(RU242_484, 15);
	mt7999_txpower_path_puts(BF-RU242_484, 15);
	mt7999_txpower_path_puts(BW80/RU996, 15);
	mt7999_txpower_path_puts(BF-BW80/RU996, 15);
	mt7999_txpower_path_puts(RU484_996, 15);
	mt7999_txpower_path_puts(BF-RU484_996, 15);
	mt7999_txpower_path_puts(RU242_484_996, 15);
	mt7999_txpower_path_puts(BF-RU242_484_996, 15);
	mt7999_txpower_path_puts(BW160/RU996x2, 15);
	mt7999_txpower_path_puts(BF-BW160/RU996x2, 15);
	mt7999_txpower_path_puts(RU484_996x2, 15);
	mt7999_txpower_path_puts(BF-RU484_996x2, 15);
	mt7999_txpower_path_puts(RU996x3, 15);
	mt7999_txpower_path_puts(BF-RU996x3, 15);
	mt7999_txpower_path_puts(RU484_996x3, 15);
	mt7999_txpower_path_puts(BF-RU484_996x3, 15);
	mt7999_txpower_path_puts(BW320/RU996x4, 15);
	mt7999_txpower_path_puts(BF-BW320/RU996x4, 15);

	len += scnprintf(buf + len, size - len, "\nBackoff table: %s\n",
			 backoff_info->backoff_en ? "enable" : "disable");

	ret = simple_read_from_buffer(user_buf, count, ppos, buf, len);

out:
	kfree(buf);
	kfree(event);
	return ret;
}

static const struct file_operations mt7999_txpower_path_fops = {
	.read = mt7999_get_txpower_path,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

static int mt7999_show_eeprom_mode(struct seq_file *s, void *data)
{
	struct mt7999_dev *dev = dev_get_drvdata(s->private);
#ifdef CONFIG_NL80211_TESTMODE
	struct mt76_dev *mdev = &dev->mt76;
	const char *mtd_name = mdev->test_mtd.name;
	u32 mtd_offset = mdev->test_mtd.offset;
#else
	const char *mtd_name = NULL;
	u32 mtd_offset;
#endif

	seq_puts(s, "Current eeprom mode:\n");

	switch (dev->eeprom_mode) {
	case EEPROM_MODE_DEFAULT_BIN:
		seq_printf(s, "\tdefault bin mode\n\tfilename = %s\n", mt7999_eeprom_name(dev));
		break;
	case EEPROM_MODE_EFUSE:
		seq_puts(s, "\tefuse mode (default bin + efuse calibrated data)\n");
		break;
	case EEPROM_MODE_FLASH:
		seq_puts(s, "\tflash mode\n");
		if (mtd_name)
			seq_printf(s, "\tmtd name = %s\n\tflash offset = 0x%x\n",
				   mtd_name, mtd_offset);
		seq_printf(s, "\tcal data applied = %s\n",
			   dev->apply_cal ? "yes" : "no");
		break;
	case EEPROM_MODE_BIN:
		seq_puts(s, "\tbin file mode\n");
		seq_printf(s, "\tfilename = %s\n", dev->mt76.bin_file_name);
		seq_printf(s, "\tcal data applied = %s\n",
			   dev->apply_cal ? "yes" : "no");
		break;
	case EEPROM_MODE_EXT:
		seq_puts(s, "\texternal eeprom mode\n");
		break;
	default:
		break;
	}

	return 0;
}

static int
mt7999_sr_enable_get(void *data, u64 *val)
{
	struct mt7999_phy *phy = data;

	*val = phy->sr_enable;

	return 0;
}

static int
mt7999_sr_enable_set(void *data, u64 val)
{
	struct mt7999_phy *phy = data;
	int ret;

	if (!!val == phy->sr_enable)
		return 0;

	ret = mt7999_mcu_set_sr_enable(phy, UNI_SR_CFG_SR_ENABLE, val, true);
	if (ret)
		return ret;

	return mt7999_mcu_set_sr_enable(phy, UNI_SR_CFG_SR_ENABLE, 0, false);
}
DEFINE_DEBUGFS_ATTRIBUTE(fops_sr_enable, mt7999_sr_enable_get,
			 mt7999_sr_enable_set, "%lld\n");
static int
mt7999_sr_enhanced_enable_get(void *data, u64 *val)
{
	struct mt7999_phy *phy = data;

	*val = phy->enhanced_sr_enable;

	return 0;
}

static int
mt7999_sr_enhanced_enable_set(void *data, u64 val)
{
	struct mt7999_phy *phy = data;
	int ret;

	if (!!val == phy->enhanced_sr_enable)
		return 0;

	ret = mt7999_mcu_set_sr_enable(phy, UNI_SR_HW_ENHANCE_SR_ENABLE, val, true);
	if (ret)
		return ret;

	return mt7999_mcu_set_sr_enable(phy, UNI_SR_HW_ENHANCE_SR_ENABLE, 0, false);
}
DEFINE_DEBUGFS_ATTRIBUTE(fops_sr_enhanced_enable, mt7999_sr_enhanced_enable_get,
			 mt7999_sr_enhanced_enable_set, "%lld\n");

static int
mt7999_sr_stats_show(struct seq_file *file, void *data)
{
	struct mt7999_phy *phy = file->private;

	mt7999_mcu_set_sr_enable(phy, UNI_SR_HW_IND, 0, false);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(mt7999_sr_stats);

static int
mt7999_sr_scene_cond_show(struct seq_file *file, void *data)
{
	struct mt7999_phy *phy = file->private;

	mt7999_mcu_set_sr_enable(phy, UNI_SR_SW_SD, 0, false);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(mt7999_sr_scene_cond);

static int
mt7999_starec_bf_read_set(void *data, u64 wlan_idx)
{
	struct mt7999_phy *phy = data;

	return mt7999_mcu_bf_starec_read(phy, (u16)wlan_idx);
}
DEFINE_DEBUGFS_ATTRIBUTE(fops_starec_bf_read, NULL,
			 mt7999_starec_bf_read_set, "%lld\n");

static ssize_t
mt7999_bf_txsnd_info_set(struct file *file,
			 const char __user *user_buf,
			 size_t count, loff_t *ppos)
{
	struct mt7999_phy *phy = file->private_data;
	char buf[40];
	int ret;

	if (count >= sizeof(buf))
		return -EINVAL;

	if (copy_from_user(buf, user_buf, count))
		return -EFAULT;

	if (count && buf[count - 1] == '\n')
		buf[count - 1] = '\0';
	else
		buf[count] = '\0';

	ret = mt7999_mcu_set_txbf_snd_info(phy->dev, buf);

	if (ret) return -EFAULT;

	return count;
}

static const struct file_operations fops_bf_txsnd_info = {
	.write = mt7999_bf_txsnd_info_set,
	.read = NULL,
	.open = simple_open,
	.llseek = default_llseek,
};

static int
mt7999_bf_fbk_rpt_set(void *data, u64 action)
{
	struct mt7999_phy *phy = data;

	return mt7999_mcu_bf_fbk_rpt_read(phy, (u8)action);
}
DEFINE_DEBUGFS_ATTRIBUTE(fops_bf_fbk_rpt, NULL,
			 mt7999_bf_fbk_rpt_set, "%lld\n");

static int
mt7999_bf_pfmu_tag_read_set(void *data, u64 pfmu_id)
{
	struct mt7999_phy *phy = data;

	return mt7999_mcu_bf_pfmu_tag_read(phy, (u8)pfmu_id, true);
}
DEFINE_DEBUGFS_ATTRIBUTE(fops_bf_pfmu_tag_read, NULL,
			 mt7999_bf_pfmu_tag_read_set, "%lld\n");

static int
mt7999_muru_fixed_rate_set(void *data, u64 val)
{
	struct mt7999_dev *dev = data;

	return mt7999_mcu_set_muru_fixed_rate_enable(dev, UNI_MURU_FIXED_RATE_CTRL,
						     val);
}
DEFINE_DEBUGFS_ATTRIBUTE(fops_muru_fixed_rate_enable, NULL,
			 mt7999_muru_fixed_rate_set, "%lld\n");

static ssize_t
mt7999_muru_fixed_rate_parameter_set(struct file *file,
				     const char __user *user_buf,
				     size_t count, loff_t *ppos)
{
	struct mt7999_dev *dev = file->private_data;
	char buf[40];
	int ret;

	if (count >= sizeof(buf))
		return -EINVAL;

	if (copy_from_user(buf, user_buf, count))
		return -EFAULT;

	if (count && buf[count - 1] == '\n')
		buf[count - 1] = '\0';
	else
		buf[count] = '\0';


	ret = mt7999_mcu_set_muru_fixed_rate_parameter(dev, UNI_MURU_FIXED_GROUP_RATE_CTRL,
						       buf);
	if (ret)
		return -EFAULT;

	return count;
}

static const struct file_operations fops_muru_fixed_group_rate = {
	.write = mt7999_muru_fixed_rate_parameter_set,
	.read = NULL,
	.open = simple_open,
	.llseek = default_llseek,
};

static int mt7999_muru_prot_thr_set(void *data, u64 val)
{
	struct mt7999_dev *dev = data;

	return mt7999_mcu_muru_set_prot_frame_thr(dev, (u32)val);
}

DEFINE_DEBUGFS_ATTRIBUTE(fops_muru_prot_thr, NULL,
			 mt7999_muru_prot_thr_set, "%lld\n");

static int
mt7999_red_config_get(void *data, u64 *val)
{
	struct mt7999_dev *dev = data;

	*val = dev->red_enable;

	return 0;
}

static int
mt7999_red_config_set(void *data, u64 val)
{
	struct mt7999_dev *dev = data;

	return mt7999_mcu_red_config(dev, !!val);
}

DEFINE_DEBUGFS_ATTRIBUTE(fops_red_config, mt7999_red_config_get,
			 mt7999_red_config_set, "%lld\n");

static int
mt7999_vow_drr_dbg(void *data, u64 val)
{
	struct mt7999_dev *dev = data;

	return mt7999_mcu_set_vow_drr_dbg(dev, (u32)val);
}
DEFINE_DEBUGFS_ATTRIBUTE(fops_vow_drr_dbg, NULL,
			 mt7999_vow_drr_dbg, "%lld\n");

static int
mt7999_rro_session_read(struct seq_file *s, void *data)
{
	struct mt7999_dev *dev = dev_get_drvdata(s->private);
	struct mt7999_rro_ba_session *tbl;
	u32 value[2];

	mt76_wr(dev, MT_RRO_DBG_RD_CTRL, MT_RRO_DBG_RD_EXEC +
		(dev->dbg.sid >> 1) + 0x200);

	if (dev->dbg.sid & 0x1) {
		value[0] = mt76_rr(dev, MT_RRO_DBG_RDAT_DW(2));
		value[1] = mt76_rr(dev, MT_RRO_DBG_RDAT_DW(3));
	} else {
		value[0] = mt76_rr(dev, MT_RRO_DBG_RDAT_DW(0));
		value[1] = mt76_rr(dev, MT_RRO_DBG_RDAT_DW(1));
	}

	tbl = (struct mt7999_rro_ba_session *)&value[0];

	seq_printf(s, " seid %d:\nba session table DW0:%08x DW2:%08x\n",
		   dev->dbg.sid, value[0], value[1]);

	seq_printf(s, "ack_sn = 0x%x, last_in_sn = 0x%x, sat/bn/bc/bd/cn = %d/%d/%d/%d/%d\n",
		   tbl->ack_sn, tbl->last_in_sn, tbl->sat, tbl->bn, tbl->bc, tbl->bd, tbl->cn);

	seq_printf(s, "within_cnt = %d, to_sel = %d, last_in_rxtime = %d\n",
		   tbl->within_cnt, tbl->to_sel, tbl->last_in_rxtime);

	return 0;
}

static int
mt7999_show_rro_mib(struct seq_file *s, void *data)
{
	struct mt7999_dev *dev = dev_get_drvdata(s->private);
	u32 reg[12];

	seq_printf(s, "RRO mib Info:\n");

	reg[0] = mt76_rr(dev, WF_RRO_TOP_STATISTIC(0));
	reg[1] = mt76_rr(dev, WF_RRO_TOP_STATISTIC(1));
	reg[2] = mt76_rr(dev, WF_RRO_TOP_STATISTIC(2));
	reg[3] = mt76_rr(dev, WF_RRO_TOP_STATISTIC(3));
	reg[4] = mt76_rr(dev, WF_RRO_TOP_STATISTIC(4));
	reg[5] = mt76_rr(dev, WF_RRO_TOP_STATISTIC(5));
	reg[6] = mt76_rr(dev, WF_RRO_TOP_STATISTIC(6));
	reg[7] = mt76_rr(dev, WF_RRO_TOP_STATISTIC(7));
	reg[8] = mt76_rr(dev, WF_RRO_TOP_STATISTIC(8));
	reg[9] = mt76_rr(dev, WF_RRO_TOP_STATISTIC(9));
	reg[10] = mt76_rr(dev, WF_RRO_TOP_STATISTIC(10));
	reg[11] = mt76_rr(dev, WF_RRO_TOP_STATISTIC(11));

	seq_printf(s, "STEP_ONE/WITHIN/SURPASS = %x/%x/%x\n", reg[0], reg[3], reg[4]);
	seq_printf(s, "REPEAT/OLDPKT/BAR = %x/%x/%x\n", reg[1], reg[2], reg[5]);
	seq_printf(s, "SURPASS with big gap = %x\n", reg[6]);
	seq_printf(s, "DISCONNECT/INVALID = %x/%x\n", reg[7], reg[8]);
	seq_printf(s, "TO(Step one)/TO(flush all) = %x/%x\n", reg[9], reg[10]);
	seq_printf(s, "buf ran out = %x\n", reg[11]);

	return 0;
}

static int
mt7999_thermal_enable_get(void *data, u64 *enable)
{
	struct mt7999_phy *phy = data;

	*enable = phy->thermal_protection_enable;

	return 0;
}

static int
mt7999_thermal_enable_set(void *data, u64 action)
{
	struct mt7999_phy *phy = data;
	int ret;
	u8 throttling;

	if (action > 1)
		return -EINVAL;

	if (!!action == phy->thermal_protection_enable)
		return 0;

	ret = mt7999_mcu_set_thermal_protect(phy, !!action);
	if (ret)
		return ret;

	if (!!!action)
		goto out;

	throttling = MT7999_THERMAL_THROTTLE_MAX - phy->cdev_state;
	ret = mt7999_mcu_set_thermal_throttling(phy, throttling);
	if (ret)
		return ret;

out:
	phy->thermal_protection_enable = !!action;

	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(fops_thermal_enable, mt7999_thermal_enable_get,
			 mt7999_thermal_enable_set, "%lld\n");

static int
mt7999_thermal_recal_set(void *data, u64 val)
{
#define UNI_THERMAL_DEBUG_MODE_RECAL 1
	struct mt7999_phy *phy = data;
	enum thermal_action {
		THERMAL_ACTION_DISABLE,
		THERMAL_ACTION_ENABLE,
		THERMAL_ACTION_TRIGGER,
	};

	if (val > THERMAL_ACTION_TRIGGER)
		return -EINVAL;

	return mt7999_mcu_thermal_debug(phy, UNI_THERMAL_DEBUG_MODE_RECAL, val);
}

DEFINE_DEBUGFS_ATTRIBUTE(fops_thermal_recal, NULL,
			 mt7999_thermal_recal_set, "%llu\n");

static int
mt7999_reset_counter(void *data, u64 val)
{
	struct mt7999_dev *dev = data;
	struct mt76_dev *mdev = &dev->mt76;
	struct mt76_wcid *wcid;
	int ret;

	/* Reset read-clear counters in FW and WTBL. */
	ret = mt7999_mcu_get_all_sta_info(mdev, UNI_ALL_STA_TXRX_ADM_STAT);
	if (ret)
		return ret;

	ret = mt7999_mcu_get_all_sta_info(mdev, UNI_ALL_STA_TXRX_MSDU_COUNT);
	if (ret)
		return ret;

	ret = mt7999_mcu_get_all_sta_info(mdev, UNI_ALL_STA_TXRX_AIR_TIME);
	if (ret)
		return ret;

	ret = mt7999_mcu_get_all_sta_info(mdev, UNI_ALL_STA_RX_MPDU_COUNT);
	if (ret)
		return ret;

	ret = mt7999_mcu_get_all_sta_info(mdev, UNI_ALL_STA_TX_STAT);
	if (ret)
		return ret;

	ret = mt7999_mcu_get_all_sta_info(mdev, UNI_ALL_STA_DATA_TX_RETRY_COUNT);
	if (ret)
		return ret;

	ret = mt7999_mac_sta_poll(mdev);
	if (ret)
		return ret;

	/* Reset counters in MT76. */
	rcu_read_lock();
	wcid = mt76_wcid_ptr(dev, dev->wlan_idx);
	if (wcid)
		memset(&wcid->stats, 0, sizeof(struct mt76_sta_stats));
	else
		ret = -EINVAL;
	rcu_read_unlock();

	return ret;
}
DEFINE_DEBUGFS_ATTRIBUTE(fops_reset_counter, NULL, mt7999_reset_counter, "%llu\n");

void mt7999_packet_log_to_host(struct mt7999_dev *dev, const void *data, int len, int type, int des_len)
{
	struct bin_debug_hdr *hdr;
	char *buf;

	if (!(dev->fw_debug_bin & BIT(type)))
		return;

	if (len > 1500 - sizeof(*hdr))
	len = 1500 - sizeof(*hdr);

	buf = kzalloc(sizeof(*hdr) + len, GFP_KERNEL);
	if (!buf)
		return;

	hdr = (struct bin_debug_hdr *)buf;
	hdr->magic_num = cpu_to_le32(PKT_BIN_DEBUG_MAGIC);
	hdr->serial_id = cpu_to_le16(dev->fw_debug_seq++);
	hdr->msg_type = cpu_to_le16(type - PKT_BIN_DEBUG_FIRST);
	hdr->len = cpu_to_le16(len);
	hdr->des_len = cpu_to_le16(des_len);

	memcpy(buf + sizeof(*hdr), data, len);

	mt7999_debugfs_rx_log(dev, buf, sizeof(*hdr) + len);
	kfree(buf);
}

static int mt7999_rx_token_read(struct seq_file *s, void *data)
{
	struct mt7999_dev *dev = dev_get_drvdata(s->private);
	int id, count = 0;
	struct mt76_txwi_cache *t;

	seq_printf(s, "Rx cut through token:\n");
	spin_lock_bh(&dev->mt76.rx_token_lock);
	idr_for_each_entry(&dev->mt76.rx_token, t, id) {
		count++;
	}
	seq_printf(s, "\ttotal:%8d used:%8d\n",
		   dev->mt76.rx_token_size, count);
	spin_unlock_bh(&dev->mt76.rx_token_lock);

	return 0;
}

/* AMSDU SETTING */
#define AMSDU_ALL_STATIONS 65535
static ssize_t mt7999_amsdu_algo_write(struct file *file,
				       const char __user *user_buf,
				       size_t count,
				       loff_t *ppos)
{
	struct mt7999_dev *dev = file->private_data;
	char buf[100];
	u16 wlan_idx;
	u8 algo_en;
	int ret;

	if (count >= sizeof(buf))
		return -EINVAL;

	if (copy_from_user(buf, user_buf, count))
		return -EFAULT;

	if (count && buf[count - 1] == '\n')
		buf[count - 1] = '\0';
	else
		buf[count] = '\0';

	if (sscanf(buf, "%hu %hhu", &wlan_idx, &algo_en) != 2)
		goto err;

	if (wlan_idx >= mt7999_wtbl_size(dev) && wlan_idx != AMSDU_ALL_STATIONS)
		goto err;

	ret = mt7999_mcu_set_amsdu_algo(dev, wlan_idx, algo_en);
	if (ret)
		return ret;

	return count;
err:
	dev_warn(dev->mt76.dev, "format: wcid algo_en\n");

	return -EINVAL;
}
static const struct file_operations fops_amsdu_algo = {
	.write = mt7999_amsdu_algo_write,
	.read = NULL,
	.open = simple_open,
	.llseek = default_llseek,
};

static ssize_t mt7999_amsdu_para_write(struct file *file,
				       const char __user *user_buf,
				       size_t count,
				       loff_t *ppos)
{
	struct mt7999_dev *dev = file->private_data;
	u16 wlan_idx, length;
	u8  amsdu_en, num;
	char buf[100];
	int ret;

	if (count >= sizeof(buf))
		return -EINVAL;

	if (copy_from_user(buf, user_buf, count))
		return -EFAULT;

	if (count && buf[count - 1] == '\n')
		buf[count - 1] = '\0';
	else
		buf[count] = '\0';

	if (sscanf(buf, "%hu %hhu %hhu %hu", &wlan_idx, &amsdu_en, &num, &length) != 4)
		goto err;

	if (wlan_idx >= mt7999_wtbl_size(dev) && wlan_idx != AMSDU_ALL_STATIONS)
		goto err;

	ret = mt7999_mcu_set_amsdu_para(dev, wlan_idx, amsdu_en, num, length);
	if (ret)
		return ret;

	return count;
err:
	dev_warn(dev->mt76.dev, "format: wcid amsdu_en num length(unit: 256 bytes)\n");

	return -EINVAL;
}
static const struct file_operations fops_amsdu_para = {
	.write = mt7999_amsdu_para_write,
	.read = NULL,
	.open = simple_open,
	.llseek = default_llseek,
};

static ssize_t
mt7999_mesh_hw_amsdu_write(struct file *file, const char __user *user_buf,
			   size_t count, loff_t *ppos)
{
	struct mt7999_dev *dev = file->private_data;
	char buf[32];
	int de_amsdu, type, endian;

	if (count >= sizeof(buf))
		return -EINVAL;

	if (copy_from_user(buf, user_buf, count))
		return -EFAULT;

	if (count && buf[count - 1] == '\n')
		buf[count - 1] = '\0';
	else
		buf[count] = '\0';

	if (sscanf(buf, "%d %d %d", &de_amsdu, &type, &endian) != 3)
		return -EINVAL;

	dev->mesh_hw_amsdu_ctrl.de_amsdu = de_amsdu == 0 ?
				UWTBL_MESH_RX_HW_DE_AMSDU_DISABLE :
				UWTBL_MESH_RX_HW_DE_AMSDU_ENABLE;
	dev->mesh_hw_amsdu_ctrl.subfrm_len_type = type == 0 ?
				UWTBL_MESH_AMSDU_SUBFRM_LEN_EXCL_MESH_CTRL :
				UWTBL_MESH_AMSDU_SUBFRM_LEN_INCL_MESH_CTRL;
	dev->mesh_hw_amsdu_ctrl.subfrm_len_endian = endian == 0 ?
				UWTBL_MESH_AMSDU_SUBFRM_LEN_BIG_ENDIAN :
				UWTBL_MESH_AMSDU_SUBFRM_LEN_LITTLE_ENDIAN;

	return count;
}


static ssize_t
mt7999_mesh_hw_amsdu_read(struct file *file, char __user *user_buf,
			  size_t count, loff_t *ppos)
{
	struct mt7999_dev *dev = file->private_data;
	static const size_t size = 1024;
	int len = 0, ret;
	char *buf;

	buf = kzalloc(size, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	len += scnprintf(buf + len, size - len, "Mesh RX HW De-AMSDU=%u\n",
			 dev->mesh_hw_amsdu_ctrl.de_amsdu);
	len += scnprintf(buf + len, size - len,
			 "Mesh AMSDU Subframe length includes Mesh control=%u\n",
			 dev->mesh_hw_amsdu_ctrl.subfrm_len_type);
	len += scnprintf(buf + len, size - len,
			"Mesh AMSDU Subframe length endianness=%u "
			"(0: big endian, 1: little endian)\n",
			 dev->mesh_hw_amsdu_ctrl.subfrm_len_endian);

	ret = simple_read_from_buffer(user_buf, count, ppos, buf, len);

	kfree(buf);
	return ret;
}

static const struct file_operations fops_mesh_hw_amsdu_ctrl = {
	.write = mt7999_mesh_hw_amsdu_write,
	.read = mt7999_mesh_hw_amsdu_read,
	.open = simple_open,
	.llseek = default_llseek,
};

/* PSE INFO */
static struct bmac_queue_info_t pse_queue_empty_info[] = {
	{"CPU Q0", ENUM_UMAC_CPU_PORT_1, ENUM_UMAC_CTX_Q_0},
	{"CPU Q1", ENUM_UMAC_CPU_PORT_1, ENUM_UMAC_CTX_Q_1},
	{"CPU Q2", ENUM_UMAC_CPU_PORT_1, ENUM_UMAC_CTX_Q_2},
	{"CPU Q3", ENUM_UMAC_CPU_PORT_1, ENUM_UMAC_CTX_Q_3},
	{NULL, 0, 0}, {NULL, 0, 0}, {NULL, 0, 0}, {NULL, 0, 0},
	{NULL, 0, 0}, {NULL, 0, 0}, {NULL, 0, 0}, {NULL, 0, 0},
	{NULL, 0, 0}, {NULL, 0, 0}, {NULL, 0, 0}, {NULL, 0, 0}, /* 4~15 not defined */
	{"LMAC Q", ENUM_UMAC_LMAC_PORT_2, 0},
	{"MDP TX Q0", ENUM_UMAC_LMAC_PORT_2, 1},
	{"MDP RX Q", ENUM_UMAC_LMAC_PORT_2, 2},
	{"SEC TX Q0", ENUM_UMAC_LMAC_PORT_2, 3},
	{"SEC RX Q", ENUM_UMAC_LMAC_PORT_2, 4},
	{"SFD_PARK Q", ENUM_UMAC_LMAC_PORT_2, 5},
	{"MDP_TXIOC Q0", ENUM_UMAC_LMAC_PORT_2, 6},
	{"MDP_RXIOC Q0", ENUM_UMAC_LMAC_PORT_2, 7},
	{"MDP TX Q1", ENUM_UMAC_LMAC_PORT_2, 0x11},
	{"SEC TX Q1", ENUM_UMAC_LMAC_PORT_2, 0x13},
	{"MDP_TXIOC Q1", ENUM_UMAC_LMAC_PORT_2, 0x16},
	{"MDP_RXIOC Q1", ENUM_UMAC_LMAC_PORT_2, 0x17},
	{"CPU Q4", ENUM_UMAC_CPU_PORT_1, ENUM_UMAC_CTX_Q_4},
	{NULL, 0, 0}, {NULL, 0, 0},
	{"RLS Q", ENUM_PLE_CTRL_PSE_PORT_3, ENUM_UMAC_PLE_CTRL_P3_Q_0X1F}
};

static struct bmac_queue_info_t pse_queue_empty2_info[] = {
	{"MDP_TDPIOC Q0", ENUM_UMAC_LMAC_PORT_2, 0x8},
	{"MDP_RDPIOC Q0", ENUM_UMAC_LMAC_PORT_2, 0x9},
	{"MDP_TDPIOC Q1", ENUM_UMAC_LMAC_PORT_2, 0x18},
	{"MDP_RDPIOC Q1", ENUM_UMAC_LMAC_PORT_2, 0x19},
	{"MDP_TDPIOC Q2", ENUM_UMAC_LMAC_PORT_2, 0x28},
	{"MDP_RDPIOC Q2", ENUM_UMAC_LMAC_PORT_2, 0x29},
	{NULL, 0, 0},
	{"MDP_RDPIOC Q3", ENUM_UMAC_LMAC_PORT_2, 0x39},
	{"MDP TX Q2", ENUM_UMAC_LMAC_PORT_2, 0x21},
	{"SEC TX Q2", ENUM_UMAC_LMAC_PORT_2, 0x23},
	{"MDP_TXIOC Q2", ENUM_UMAC_LMAC_PORT_2, 0x26},
	{"MDP_RXIOC Q2", ENUM_UMAC_LMAC_PORT_2, 0x27},
	{"HIF Q16", ENUM_UMAC_HIF_PORT_0, 16},
	{NULL, 0, 0}, {NULL, 0, 0},
	{"MDP_RXIOC Q3", ENUM_UMAC_LMAC_PORT_2, 0x37},
	{"HIF Q0", ENUM_UMAC_HIF_PORT_0, 0},
	{"HIF Q1", ENUM_UMAC_HIF_PORT_0, 1},
	{"HIF Q2", ENUM_UMAC_HIF_PORT_0, 2},
	{"HIF Q3", ENUM_UMAC_HIF_PORT_0, 3},
	{"HIF Q4", ENUM_UMAC_HIF_PORT_0, 4},
	{"HIF Q5", ENUM_UMAC_HIF_PORT_0, 5},
	{"HIF Q6", ENUM_UMAC_HIF_PORT_0, 6},
	{"HIF Q7", ENUM_UMAC_HIF_PORT_0, 7},
	{"HIF Q8", ENUM_UMAC_HIF_PORT_0, 8},
	{"HIF Q9", ENUM_UMAC_HIF_PORT_0, 9},
	{"HIF Q10", ENUM_UMAC_HIF_PORT_0, 10},
	{"HIF Q11", ENUM_UMAC_HIF_PORT_0, 11},
	{"HIF Q12", ENUM_UMAC_HIF_PORT_0, 12},
	{"HIF Q13", ENUM_UMAC_HIF_PORT_0, 13},
	{"HIF Q14", ENUM_UMAC_HIF_PORT_0, 14},
	{"HIF Q15", ENUM_UMAC_HIF_PORT_0, 15},
};

static int
mt7999_pseinfo_read(struct seq_file *s, void *data)
{
	struct mt7999_dev *dev = dev_get_drvdata(s->private);
	u32 pse_buf_ctrl, pg_sz, pg_num;
	u32 pse_stat[2], pg_flow_ctrl[28] = {0};
	u32 fpg_cnt, ffa_cnt, fpg_head, fpg_tail;
	u32 max_q, min_q, rsv_pg, used_pg;
	int i;

	pse_buf_ctrl = mt76_rr(dev, WF_PSE_TOP_PBUF_CTRL_ADDR);
	pse_stat[0] = mt76_rr(dev, WF_PSE_TOP_QUEUE_EMPTY_ADDR);
	pse_stat[1] = mt76_rr(dev, WF_PSE_TOP_QUEUE_EMPTY_1_ADDR);
	pg_flow_ctrl[0] = mt76_rr(dev, WF_PSE_TOP_FREEPG_CNT_ADDR);
	pg_flow_ctrl[1] = mt76_rr(dev, WF_PSE_TOP_FREEPG_HEAD_TAIL_ADDR);
	pg_flow_ctrl[2] = mt76_rr(dev, WF_PSE_TOP_PG_HIF0_GROUP_ADDR);
	pg_flow_ctrl[3] = mt76_rr(dev, WF_PSE_TOP_HIF0_PG_INFO_ADDR);
	pg_flow_ctrl[4] = mt76_rr(dev, WF_PSE_TOP_PG_HIF1_GROUP_ADDR);
	pg_flow_ctrl[5] = mt76_rr(dev, WF_PSE_TOP_HIF1_PG_INFO_ADDR);
	pg_flow_ctrl[6] = mt76_rr(dev, WF_PSE_TOP_PG_CPU_GROUP_ADDR);
	pg_flow_ctrl[7] = mt76_rr(dev, WF_PSE_TOP_CPU_PG_INFO_ADDR);
	pg_flow_ctrl[8] = mt76_rr(dev, WF_PSE_TOP_PG_LMAC0_GROUP_ADDR);
	pg_flow_ctrl[9] = mt76_rr(dev, WF_PSE_TOP_LMAC0_PG_INFO_ADDR);
	pg_flow_ctrl[10] = mt76_rr(dev, WF_PSE_TOP_PG_LMAC1_GROUP_ADDR);
	pg_flow_ctrl[11] = mt76_rr(dev, WF_PSE_TOP_LMAC1_PG_INFO_ADDR);
	pg_flow_ctrl[12] = mt76_rr(dev, WF_PSE_TOP_PG_LMAC2_GROUP_ADDR);
	pg_flow_ctrl[13] = mt76_rr(dev, WF_PSE_TOP_LMAC2_PG_INFO_ADDR);
	pg_flow_ctrl[14] = mt76_rr(dev, WF_PSE_TOP_PG_PLE_GROUP_ADDR);
	pg_flow_ctrl[15] = mt76_rr(dev, WF_PSE_TOP_PLE_PG_INFO_ADDR);
	pg_flow_ctrl[16] = mt76_rr(dev, WF_PSE_TOP_PG_LMAC3_GROUP_ADDR);
	pg_flow_ctrl[17] = mt76_rr(dev, WF_PSE_TOP_LMAC3_PG_INFO_ADDR);
	pg_flow_ctrl[18] = mt76_rr(dev, WF_PSE_TOP_PG_MDP_GROUP_ADDR);
	pg_flow_ctrl[19] = mt76_rr(dev, WF_PSE_TOP_MDP_PG_INFO_ADDR);
	pg_flow_ctrl[20] = mt76_rr(dev, WF_PSE_TOP_PG_PLE1_GROUP_ADDR);
	pg_flow_ctrl[21] = mt76_rr(dev, WF_PSE_TOP_PLE1_PG_INFO_ADDR);
	pg_flow_ctrl[22] = mt76_rr(dev, WF_PSE_TOP_PG_MDP2_GROUP_ADDR);
	pg_flow_ctrl[23] = mt76_rr(dev, WF_PSE_TOP_MDP2_PG_INFO_ADDR);
	pg_flow_ctrl[24] = mt76_rr(dev, WF_PSE_TOP_PG_MDP3_GROUP_ADDR);
	pg_flow_ctrl[25] = mt76_rr(dev, WF_PSE_TOP_MDP3_PG_INFO_ADDR);
	pg_flow_ctrl[26] = mt76_rr(dev, WF_PSE_TOP_PG_HIF2_GROUP_ADDR);
	pg_flow_ctrl[27] = mt76_rr(dev, WF_PSE_TOP_HIF2_PG_INFO_ADDR);
	/* Configuration Info */
	seq_printf(s, "PSE Configuration Info:\n");
	seq_printf(s, "\tPacket Buffer Control: 0x%08x\n", pse_buf_ctrl);
	pg_sz = u32_get_bits(pse_buf_ctrl, WF_PSE_TOP_PBUF_CTRL_PAGE_SIZE_CFG_MASK);
	seq_printf(s, "\t\tPage Size=%d(%d bytes per page)\n", pg_sz, pg_sz * 128);
	seq_printf(s, "\t\tPage Offset=%d(in unit of 64KB)\n",
			 u32_get_bits(pse_buf_ctrl, WF_PSE_TOP_PBUF_CTRL_PBUF_OFFSET_MASK));
	pg_num = u32_get_bits(pse_buf_ctrl, WF_PSE_TOP_PBUF_CTRL_TOTAL_PAGE_NUM_MASK);
	seq_printf(s, "\t\tTotal page numbers=%d pages\n", pg_num);
	/* Page Flow Control */
	seq_printf(s, "PSE Page Flow Control:\n");
	seq_printf(s, "\tFree page counter: 0x%08x\n", pg_flow_ctrl[0]);
	fpg_cnt = u32_get_bits(pg_flow_ctrl[0], WF_PSE_TOP_FREEPG_CNT_FREEPG_CNT_MASK);
	seq_printf(s, "\t\tThe toal page number of free=0x%03x\n", fpg_cnt);
	ffa_cnt = u32_get_bits(pg_flow_ctrl[0], WF_PSE_TOP_FREEPG_CNT_FFA_CNT_MASK);
	seq_printf(s, "\t\tThe free page numbers of free for all=0x%03x\n", ffa_cnt);
	seq_printf(s, "\tFree page head and tail: 0x%08x\n", pg_flow_ctrl[1]);
	fpg_head = u32_get_bits(pg_flow_ctrl[1], WF_PSE_TOP_FREEPG_HEAD_TAIL_FREEPG_HEAD_MASK);
	fpg_tail = u32_get_bits(pg_flow_ctrl[1], WF_PSE_TOP_FREEPG_HEAD_TAIL_FREEPG_TAIL_MASK);
	seq_printf(s, "\t\tThe tail/head page of free page list=0x%04x/0x%04x\n", fpg_tail, fpg_head);
	seq_printf(s, "\tReserved page counter of HIF0 group: 0x%08x\n", pg_flow_ctrl[2]);
	seq_printf(s, "\tHIF0 group page status: 0x%08x\n", pg_flow_ctrl[3]);
	min_q = u32_get_bits(pg_flow_ctrl[2], WF_PSE_TOP_PG_HIF0_GROUP_HIF0_MIN_QUOTA_MASK);
	max_q = u32_get_bits(pg_flow_ctrl[2], WF_PSE_TOP_PG_HIF0_GROUP_HIF0_MAX_QUOTA_MASK);
	seq_printf(s, "\t\tThe max/min quota pages of HIF0 group=0x%03x/0x%03x\n", max_q, min_q);
	rsv_pg = u32_get_bits(pg_flow_ctrl[3], WF_PSE_TOP_HIF0_PG_INFO_HIF0_RSV_CNT_MASK);
	used_pg = u32_get_bits(pg_flow_ctrl[3], WF_PSE_TOP_HIF0_PG_INFO_HIF0_SRC_CNT_MASK);
	seq_printf(s, "\t\tThe used/reserved pages of HIF0 group=0x%03x/0x%03x\n", used_pg, rsv_pg);
	seq_printf(s, "\tReserved page counter of HIF1 group: 0x%08x\n", pg_flow_ctrl[4]);
	seq_printf(s, "\tHIF1 group page status: 0x%08x\n", pg_flow_ctrl[5]);
	min_q = u32_get_bits(pg_flow_ctrl[4], WF_PSE_TOP_PG_HIF1_GROUP_HIF1_MIN_QUOTA_MASK);
	max_q = u32_get_bits(pg_flow_ctrl[4], WF_PSE_TOP_PG_HIF1_GROUP_HIF1_MAX_QUOTA_MASK);
	seq_printf(s, "\t\tThe max/min quota pages of HIF1 group=0x%03x/0x%03x\n", max_q, min_q);
	rsv_pg = u32_get_bits(pg_flow_ctrl[5], WF_PSE_TOP_HIF1_PG_INFO_HIF1_RSV_CNT_MASK);
	used_pg = u32_get_bits(pg_flow_ctrl[5], WF_PSE_TOP_HIF1_PG_INFO_HIF1_SRC_CNT_MASK);
	seq_printf(s, "\t\tThe used/reserved pages of HIF1 group=0x%03x/0x%03x\n", used_pg, rsv_pg);
	seq_printf(s, "\tReserved page counter of HIF2 group: 0x%08x\n", pg_flow_ctrl[26]);
	seq_printf(s, "\tHIF2 group page status: 0x%08x\n", pg_flow_ctrl[27]);
	min_q = u32_get_bits(pg_flow_ctrl[26], WF_PSE_TOP_PG_HIF2_GROUP_HIF2_MIN_QUOTA_MASK);
	max_q = u32_get_bits(pg_flow_ctrl[26], WF_PSE_TOP_PG_HIF2_GROUP_HIF2_MAX_QUOTA_MASK);
	seq_printf(s, "\t\tThe max/min quota pages of HIF2 group=0x%03x/0x%03x\n", max_q, min_q);
	rsv_pg = u32_get_bits(pg_flow_ctrl[27], WF_PSE_TOP_HIF2_PG_INFO_HIF2_RSV_CNT_MASK);
	used_pg = u32_get_bits(pg_flow_ctrl[27], WF_PSE_TOP_HIF2_PG_INFO_HIF2_SRC_CNT_MASK);
	seq_printf(s, "\t\tThe used/reserved pages of HIF2 group=0x%03x/0x%03x\n", used_pg, rsv_pg);
	seq_printf(s, "\tReserved page counter of CPU group: 0x%08x\n", pg_flow_ctrl[6]);
	seq_printf(s, "\tCPU group page status: 0x%08x\n", pg_flow_ctrl[7]);
	min_q = u32_get_bits(pg_flow_ctrl[6], WF_PSE_TOP_PG_CPU_GROUP_CPU_MIN_QUOTA_MASK);
	max_q = u32_get_bits(pg_flow_ctrl[6], WF_PSE_TOP_PG_CPU_GROUP_CPU_MAX_QUOTA_MASK);
	seq_printf(s, "\t\tThe max/min quota pages of CPU group=0x%03x/0x%03x\n", max_q, min_q);
	rsv_pg = u32_get_bits(pg_flow_ctrl[7], WF_PSE_TOP_CPU_PG_INFO_CPU_RSV_CNT_MASK);
	used_pg = u32_get_bits(pg_flow_ctrl[7], WF_PSE_TOP_CPU_PG_INFO_CPU_SRC_CNT_MASK);
	seq_printf(s, "\t\tThe used/reserved pages of CPU group=0x%03x/0x%03x\n", used_pg, rsv_pg);
	seq_printf(s, "\tReserved page counter of LMAC0 group: 0x%08x\n", pg_flow_ctrl[8]);
	seq_printf(s, "\tLMAC0 group page status: 0x%08x\n", pg_flow_ctrl[9]);
	min_q = u32_get_bits(pg_flow_ctrl[8], WF_PSE_TOP_PG_LMAC0_GROUP_LMAC0_MIN_QUOTA_MASK);
	max_q = u32_get_bits(pg_flow_ctrl[8], WF_PSE_TOP_PG_LMAC0_GROUP_LMAC0_MAX_QUOTA_MASK);
	seq_printf(s, "\t\tThe max/min quota pages of LMAC0 group=0x%03x/0x%03x\n", max_q, min_q);
	rsv_pg = u32_get_bits(pg_flow_ctrl[9], WF_PSE_TOP_LMAC0_PG_INFO_LMAC0_RSV_CNT_MASK);
	used_pg = u32_get_bits(pg_flow_ctrl[9], WF_PSE_TOP_LMAC0_PG_INFO_LMAC0_SRC_CNT_MASK);
	seq_printf(s, "\t\tThe used/reserved pages of LMAC0 group=0x%03x/0x%03x\n", used_pg, rsv_pg);
	seq_printf(s, "\tReserved page counter of LMAC1 group: 0x%08x\n", pg_flow_ctrl[10]);
	seq_printf(s, "\tLMAC1 group page status: 0x%08x\n", pg_flow_ctrl[11]);
	min_q = u32_get_bits(pg_flow_ctrl[10], WF_PSE_TOP_PG_LMAC1_GROUP_LMAC1_MIN_QUOTA_MASK);
	max_q = u32_get_bits(pg_flow_ctrl[10], WF_PSE_TOP_PG_LMAC1_GROUP_LMAC1_MAX_QUOTA_MASK);
	seq_printf(s, "\t\tThe max/min quota pages of LMAC1 group=0x%03x/0x%03x\n", max_q, min_q);
	rsv_pg = u32_get_bits(pg_flow_ctrl[11], WF_PSE_TOP_LMAC1_PG_INFO_LMAC1_RSV_CNT_MASK);
	used_pg = u32_get_bits(pg_flow_ctrl[11], WF_PSE_TOP_LMAC1_PG_INFO_LMAC1_SRC_CNT_MASK);
	seq_printf(s, "\t\tThe used/reserved pages of LMAC1 group=0x%03x/0x%03x\n", used_pg, rsv_pg);
	seq_printf(s, "\tReserved page counter of LMAC2 group: 0x%08x\n", pg_flow_ctrl[11]);
	seq_printf(s, "\tLMAC2 group page status: 0x%08x\n", pg_flow_ctrl[12]);
	min_q = u32_get_bits(pg_flow_ctrl[12], WF_PSE_TOP_PG_LMAC2_GROUP_LMAC2_MIN_QUOTA_MASK);
	max_q = u32_get_bits(pg_flow_ctrl[12], WF_PSE_TOP_PG_LMAC2_GROUP_LMAC2_MAX_QUOTA_MASK);
	seq_printf(s, "\t\tThe max/min quota pages of LMAC2 group=0x%03x/0x%03x\n", max_q, min_q);
	rsv_pg = u32_get_bits(pg_flow_ctrl[13], WF_PSE_TOP_LMAC2_PG_INFO_LMAC2_RSV_CNT_MASK);
	used_pg = u32_get_bits(pg_flow_ctrl[13], WF_PSE_TOP_LMAC2_PG_INFO_LMAC2_SRC_CNT_MASK);
	seq_printf(s, "\t\tThe used/reserved pages of LMAC2 group=0x%03x/0x%03x\n", used_pg, rsv_pg);

	seq_printf(s, "\tReserved page counter of LMAC3 group: 0x%08x\n", pg_flow_ctrl[16]);
	seq_printf(s, "\tLMAC3 group page status: 0x%08x\n", pg_flow_ctrl[17]);
	min_q = u32_get_bits(pg_flow_ctrl[16], WF_PSE_TOP_PG_LMAC3_GROUP_LMAC3_MIN_QUOTA_MASK);
	max_q = u32_get_bits(pg_flow_ctrl[16], WF_PSE_TOP_PG_LMAC3_GROUP_LMAC3_MAX_QUOTA_MASK);
	seq_printf(s, "\t\tThe max/min quota pages of LMAC3 group=0x%03x/0x%03x\n", max_q, min_q);
	rsv_pg = u32_get_bits(pg_flow_ctrl[17], WF_PSE_TOP_LMAC3_PG_INFO_LMAC3_RSV_CNT_MASK);
	used_pg = u32_get_bits(pg_flow_ctrl[17], WF_PSE_TOP_LMAC3_PG_INFO_LMAC3_SRC_CNT_MASK);
	seq_printf(s, "\t\tThe used/reserved pages of LMAC3 group=0x%03x/0x%03x\n", used_pg, rsv_pg);

	seq_printf(s, "\tReserved page counter of PLE group: 0x%08x\n", pg_flow_ctrl[14]);
	seq_printf(s, "\tPLE group page status: 0x%08x\n", pg_flow_ctrl[15]);
	min_q = u32_get_bits(pg_flow_ctrl[14], WF_PSE_TOP_PG_PLE_GROUP_PLE_MIN_QUOTA_MASK);
	max_q = u32_get_bits(pg_flow_ctrl[14], WF_PSE_TOP_PG_PLE_GROUP_PLE_MAX_QUOTA_MASK);
	seq_printf(s, "\t\tThe max/min quota pages of PLE group=0x%03x/0x%03x\n", max_q, min_q);
	rsv_pg = u32_get_bits(pg_flow_ctrl[15], WF_PSE_TOP_PLE_PG_INFO_PLE_RSV_CNT_MASK);
	used_pg = u32_get_bits(pg_flow_ctrl[15], WF_PSE_TOP_PLE_PG_INFO_PLE_SRC_CNT_MASK);
	seq_printf(s, "\t\tThe used/reserved pages of PLE group=0x%03x/0x%03x\n", used_pg, rsv_pg);

	seq_printf(s, "\tReserved page counter of PLE1 group: 0x%08x\n", pg_flow_ctrl[14]);
	seq_printf(s, "\tPLE1 group page status: 0x%08x\n", pg_flow_ctrl[15]);
	min_q = u32_get_bits(pg_flow_ctrl[20], WF_PSE_TOP_PG_PLE_GROUP_PLE_MIN_QUOTA_MASK);
	max_q = u32_get_bits(pg_flow_ctrl[20], WF_PSE_TOP_PG_PLE_GROUP_PLE_MAX_QUOTA_MASK);
	seq_printf(s, "\t\tThe max/min quota pages of PLE1 group=0x%03x/0x%03x\n", max_q, min_q);
	rsv_pg = u32_get_bits(pg_flow_ctrl[21], WF_PSE_TOP_PLE_PG_INFO_PLE_RSV_CNT_MASK);
	used_pg = u32_get_bits(pg_flow_ctrl[21], WF_PSE_TOP_PLE_PG_INFO_PLE_SRC_CNT_MASK);
	seq_printf(s, "\t\tThe used/reserved pages of PLE1 group=0x%03x/0x%03x\n", used_pg, rsv_pg);

	seq_printf(s, "\tReserved page counter of MDP group: 0x%08x\n", pg_flow_ctrl[18]);
	seq_printf(s, "\tMDP group page status: 0x%08x\n", pg_flow_ctrl[19]);
	min_q = u32_get_bits(pg_flow_ctrl[18], WF_PSE_TOP_PG_MDP_GROUP_MDP_MIN_QUOTA_MASK);
	max_q = u32_get_bits(pg_flow_ctrl[18], WF_PSE_TOP_PG_MDP_GROUP_MDP_MAX_QUOTA_MASK);
	seq_printf(s, "\t\tThe max/min quota pages of MDP group=0x%03x/0x%03x\n", max_q, min_q);
	rsv_pg = u32_get_bits(pg_flow_ctrl[19], WF_PSE_TOP_MDP_PG_INFO_MDP_RSV_CNT_MASK);
	used_pg = u32_get_bits(pg_flow_ctrl[19], WF_PSE_TOP_MDP_PG_INFO_MDP_SRC_CNT_MASK);
	seq_printf(s, "\t\tThe used/reserved pages of MDP group=0x%03x/0x%03x\n", used_pg, rsv_pg);
	seq_printf(s, "\tReserved page counter of MDP2 group: 0x%08x\n", pg_flow_ctrl[22]);
	seq_printf(s, "\tMDP2 group page status: 0x%08x\n", pg_flow_ctrl[23]);
	min_q = u32_get_bits(pg_flow_ctrl[22], WF_PSE_TOP_PG_MDP2_GROUP_MDP2_MIN_QUOTA_MASK);
	max_q = u32_get_bits(pg_flow_ctrl[22], WF_PSE_TOP_PG_MDP2_GROUP_MDP2_MAX_QUOTA_MASK);
	seq_printf(s, "\t\tThe max/min quota pages of MDP2 group=0x%03x/0x%03x\n", max_q, min_q);
	rsv_pg = u32_get_bits(pg_flow_ctrl[23], WF_PSE_TOP_MDP2_PG_INFO_MDP2_RSV_CNT_MASK);
	used_pg = u32_get_bits(pg_flow_ctrl[23], WF_PSE_TOP_MDP2_PG_INFO_MDP2_SRC_CNT_MASK);
	seq_printf(s, "\t\tThe used/reserved pages of MDP2 group=0x%03x/0x%03x\n", used_pg, rsv_pg);
	seq_printf(s, "\tReserved page counter of MDP3 group: 0x%08x\n", pg_flow_ctrl[24]);
	seq_printf(s, "\tMDP3 group page status: 0x%08x\n", pg_flow_ctrl[25]);
	min_q = u32_get_bits(pg_flow_ctrl[24], WF_PSE_TOP_PG_MDP3_GROUP_MDP3_MIN_QUOTA_MASK);
	max_q = u32_get_bits(pg_flow_ctrl[24], WF_PSE_TOP_PG_MDP3_GROUP_MDP3_MAX_QUOTA_MASK);
	seq_printf(s, "\t\tThe max/min quota pages of MDP3 group=0x%03x/0x%03x\n", max_q, min_q);
	rsv_pg = u32_get_bits(pg_flow_ctrl[25], WF_PSE_TOP_MDP3_PG_INFO_MDP3_RSV_CNT_MASK);
	used_pg = u32_get_bits(pg_flow_ctrl[25], WF_PSE_TOP_MDP3_PG_INFO_MDP3_SRC_CNT_MASK);
	seq_printf(s, "\t\tThe used/reserved pages of MDP3 group=0x%03x/0x%03x\n", used_pg, rsv_pg);
	/* Queue Empty Status */
	seq_printf(s, "PSE Queue Empty Status:\n");
	seq_printf(s, "\tQUEUE_EMPTY: 0x%08x, QUEUE_EMPTY2: 0x%08x\n", pse_stat[0], pse_stat[1]);
	seq_printf(s, "\t\tCPU Q0/1/2/3/4 empty=%d/%d/%d/%d/%d\n",
			  u32_get_bits(pse_stat[0], WF_PSE_TOP_QUEUE_EMPTY_CPU_Q0_EMPTY_MASK),
			  (u32_get_bits(pse_stat[0], WF_PSE_TOP_QUEUE_EMPTY_CPU_Q1_EMPTY_MASK)),
			  (u32_get_bits(pse_stat[0], WF_PSE_TOP_QUEUE_EMPTY_CPU_Q2_EMPTY_MASK)),
			  (u32_get_bits(pse_stat[0], WF_PSE_TOP_QUEUE_EMPTY_CPU_Q3_EMPTY_MASK)),
			  (u32_get_bits(pse_stat[0], WF_PSE_TOP_QUEUE_EMPTY_CPU_Q4_EMPTY_MASK)));
	seq_printf(s, "\t\tHIF Q0/1/2/3/4/5/6/7/8 empty=%d/%d/%d/%d/%d/%d/%d/%d/%d\n",
			  (u32_get_bits(pse_stat[1], WF_PSE_TOP_QUEUE_EMPTY_1_HIF_0_EMPTY_MASK)),
			  (u32_get_bits(pse_stat[1], WF_PSE_TOP_QUEUE_EMPTY_1_HIF_1_EMPTY_MASK)),
			  (u32_get_bits(pse_stat[1], WF_PSE_TOP_QUEUE_EMPTY_1_HIF_2_EMPTY_MASK)),
			  (u32_get_bits(pse_stat[1], WF_PSE_TOP_QUEUE_EMPTY_1_HIF_3_EMPTY_MASK)),
			  (u32_get_bits(pse_stat[1], WF_PSE_TOP_QUEUE_EMPTY_1_HIF_4_EMPTY_MASK)),
			  (u32_get_bits(pse_stat[1], WF_PSE_TOP_QUEUE_EMPTY_1_HIF_5_EMPTY_MASK)),
			  (u32_get_bits(pse_stat[1], WF_PSE_TOP_QUEUE_EMPTY_1_HIF_6_EMPTY_MASK)),
			  (u32_get_bits(pse_stat[1], WF_PSE_TOP_QUEUE_EMPTY_1_HIF_7_EMPTY_MASK)),
			  (u32_get_bits(pse_stat[1], WF_PSE_TOP_QUEUE_EMPTY_1_HIF_8_EMPTY_MASK)));
	seq_printf(s, "\t\tHIF Q9/10/11/12/13/14/15/16 empty=%d/%d/%d/%d/%d/%d/%d/%d\n",
			  (u32_get_bits(pse_stat[1], WF_PSE_TOP_QUEUE_EMPTY_1_HIF_9_EMPTY_MASK)),
			  (u32_get_bits(pse_stat[1], WF_PSE_TOP_QUEUE_EMPTY_1_HIF_10_EMPTY_MASK)),
			  (u32_get_bits(pse_stat[1], WF_PSE_TOP_QUEUE_EMPTY_1_HIF_11_EMPTY_MASK)),
			  (u32_get_bits(pse_stat[1], WF_PSE_TOP_QUEUE_EMPTY_1_HIF_12_EMPTY_MASK)),
			  (u32_get_bits(pse_stat[1], WF_PSE_TOP_QUEUE_EMPTY_1_HIF_13_EMPTY_MASK)),
			  (u32_get_bits(pse_stat[1], WF_PSE_TOP_QUEUE_EMPTY_1_HIF_14_EMPTY_MASK)),
			  (u32_get_bits(pse_stat[1], WF_PSE_TOP_QUEUE_EMPTY_1_HIF_15_EMPTY_MASK)),
			  (u32_get_bits(pse_stat[1], WF_PSE_TOP_QUEUE_EMPTY_1_HIF_16_EMPTY_MASK)));
	seq_printf(s, "\t\tLMAC TX Q empty=%d\n",
			  (u32_get_bits(pse_stat[0], WF_PSE_TOP_QUEUE_EMPTY_LMAC_TX_QUEUE_EMPTY_MASK)));
	seq_printf(s, "\t\tMDP TX Q0/Q1/Q2/RX Q empty=%d/%d/%d/%d\n",
			  (u32_get_bits(pse_stat[0], WF_PSE_TOP_QUEUE_EMPTY_MDP_TX_QUEUE_EMPTY_MASK)),
			  (u32_get_bits(pse_stat[0], WF_PSE_TOP_QUEUE_EMPTY_MDP_TX1_QUEUE_EMPTY_MASK)),
			  (u32_get_bits(pse_stat[1], WF_PSE_TOP_QUEUE_EMPTY_1_MDP_TX2_QUEUE_EMPTY_MASK)),
			  (u32_get_bits(pse_stat[0], WF_PSE_TOP_QUEUE_EMPTY_MDP_RX_QUEUE_EMPTY_MASK)));
	seq_printf(s, "\t\tSEC TX Q0/Q1/Q2/RX Q empty=%d/%d/%d/%d\n",
			  (u32_get_bits(pse_stat[0], WF_PSE_TOP_QUEUE_EMPTY_SEC_TX_QUEUE_EMPTY_MASK)),
			  (u32_get_bits(pse_stat[0], WF_PSE_TOP_QUEUE_EMPTY_SEC_TX1_QUEUE_EMPTY_MASK)),
			  (u32_get_bits(pse_stat[1], WF_PSE_TOP_QUEUE_EMPTY_1_SEC_TX2_QUEUE_EMPTY_MASK)),
			  (u32_get_bits(pse_stat[0], WF_PSE_TOP_QUEUE_EMPTY_SEC_RX_QUEUE_EMPTY_MASK)));
	seq_printf(s, "\t\tSFD PARK Q empty=%d\n",
			  (u32_get_bits(pse_stat[0], WF_PSE_TOP_QUEUE_EMPTY_SFD_PARK_QUEUE_EMPTY_MASK)));
	seq_printf(s, "\t\tMDP TXIOC Q0/Q1/Q2 empty=%d/%d/%d\n",
			  (u32_get_bits(pse_stat[0], WF_PSE_TOP_QUEUE_EMPTY_MDP_TXIOC_QUEUE_EMPTY_MASK)),
			  (u32_get_bits(pse_stat[0], WF_PSE_TOP_QUEUE_EMPTY_MDP_TXIOC1_QUEUE_EMPTY_MASK)),
			  (u32_get_bits(pse_stat[1], WF_PSE_TOP_QUEUE_EMPTY_1_MDP_TXIOC2_QUEUE_EMPTY_MASK)));
	seq_printf(s, "\t\tMDP RXIOC Q0/Q1/Q2/Q3 empty=%d/%d/%d/%d\n",
			  (u32_get_bits(pse_stat[0], WF_PSE_TOP_QUEUE_EMPTY_MDP_RXIOC_QUEUE_EMPTY_MASK)),
			  (u32_get_bits(pse_stat[0], WF_PSE_TOP_QUEUE_EMPTY_MDP_RXIOC1_QUEUE_EMPTY_MASK)),
			  (u32_get_bits(pse_stat[1], WF_PSE_TOP_QUEUE_EMPTY_1_MDP_RXIOC2_QUEUE_EMPTY_MASK)),
			  (u32_get_bits(pse_stat[1], WF_PSE_TOP_QUEUE_EMPTY_1_MDP_RXIOC3_QUEUE_EMPTY_MASK)));
	seq_printf(s, "\t\tRLS Q empty=%d\n",
			  (u32_get_bits(pse_stat[0], WF_PSE_TOP_QUEUE_EMPTY_RLS_Q_EMTPY_MASK)));
	seq_printf(s, "Nonempty Q info:\n");

	for (i = 0; i < 31; i++) {
		if (((pse_stat[0] & (0x1 << i)) >> i) == 0) {
			u32 hfid, tfid, pktcnt, fl_que_ctrl[3] = {0};

			if (pse_queue_empty_info[i].QueueName != NULL) {
				seq_printf(s, "\t%s: ", pse_queue_empty_info[i].QueueName);
				fl_que_ctrl[0] |= WF_PSE_TOP_FL_QUE_CTRL_0_EXECUTE_MASK;
				fl_que_ctrl[0] |= (pse_queue_empty_info[i].Portid << WF_PSE_TOP_FL_QUE_CTRL_0_Q_BUF_PID_SHFT);
				fl_que_ctrl[0] |= (pse_queue_empty_info[i].Queueid << WF_PSE_TOP_FL_QUE_CTRL_0_Q_BUF_QID_SHFT);
			} else
				continue;

			fl_que_ctrl[0] |= (0x1 << 31);
			mt76_wr(dev, WF_PSE_TOP_FL_QUE_CTRL_0_ADDR, fl_que_ctrl[0]);
			fl_que_ctrl[1] = mt76_rr(dev, WF_PSE_TOP_FL_QUE_CTRL_2_ADDR);
			fl_que_ctrl[2] = mt76_rr(dev, WF_PSE_TOP_FL_QUE_CTRL_3_ADDR);
			hfid = u32_get_bits(fl_que_ctrl[1], WF_PSE_TOP_FL_QUE_CTRL_2_QUEUE_HEAD_FID_MASK);
			tfid = u32_get_bits(fl_que_ctrl[1], WF_PSE_TOP_FL_QUE_CTRL_2_QUEUE_TAIL_FID_MASK);
			pktcnt = u32_get_bits(fl_que_ctrl[2], WF_PSE_TOP_FL_QUE_CTRL_3_QUEUE_PKT_NUM_MASK);
			seq_printf(s, "tail/head fid = 0x%04x/0x%04x, pkt cnt = 0x%04x\n",
					  tfid, hfid, pktcnt);
		}
	}

	for (i = 0; i < 31; i++) {
		if (((pse_stat[1] & (0x1 << i)) >> i) == 0) {
			u32 hfid, tfid, pktcnt, fl_que_ctrl[3] = {0};

			if (pse_queue_empty2_info[i].QueueName != NULL) {
				seq_printf(s, "\t%s: ", pse_queue_empty2_info[i].QueueName);
				fl_que_ctrl[0] |= WF_PSE_TOP_FL_QUE_CTRL_0_EXECUTE_MASK;
				fl_que_ctrl[0] |= (pse_queue_empty2_info[i].Portid << WF_PSE_TOP_FL_QUE_CTRL_0_Q_BUF_PID_SHFT);
				fl_que_ctrl[0] |= (pse_queue_empty2_info[i].Queueid << WF_PSE_TOP_FL_QUE_CTRL_0_Q_BUF_QID_SHFT);
			} else
				continue;

			fl_que_ctrl[0] |= (0x1 << 31);
			mt76_wr(dev, WF_PSE_TOP_FL_QUE_CTRL_0_ADDR, fl_que_ctrl[0]);
			fl_que_ctrl[1] = mt76_rr(dev, WF_PSE_TOP_FL_QUE_CTRL_2_ADDR);
			fl_que_ctrl[2] = mt76_rr(dev, WF_PSE_TOP_FL_QUE_CTRL_3_ADDR);
			hfid = u32_get_bits(fl_que_ctrl[1], WF_PSE_TOP_FL_QUE_CTRL_2_QUEUE_HEAD_FID_MASK);
			tfid = u32_get_bits(fl_que_ctrl[1], WF_PSE_TOP_FL_QUE_CTRL_2_QUEUE_TAIL_FID_MASK);
			pktcnt = u32_get_bits(fl_que_ctrl[2], WF_PSE_TOP_FL_QUE_CTRL_3_QUEUE_PKT_NUM_MASK);
			seq_printf(s, "tail/head fid = 0x%04x/0x%04x, pkt cnt = 0x%04x\n",
					  tfid, hfid, pktcnt);
		}
	}

	return 0;
}

/* PLE INFO */
static char *sta_ctrl_reg[] = {"ENABLE", "DISABLE", "PAUSE", "TWT_PAUSE"};
static struct bmac_queue_info ple_queue_empty_info[] = {
	{"CPU Q0",  ENUM_UMAC_CPU_PORT_1,     ENUM_UMAC_CTX_Q_0, 0},
	{"CPU Q1",  ENUM_UMAC_CPU_PORT_1,     ENUM_UMAC_CTX_Q_1, 0},
	{"CPU Q2",  ENUM_UMAC_CPU_PORT_1,     ENUM_UMAC_CTX_Q_2, 0},
	{"CPU Q3",  ENUM_UMAC_CPU_PORT_1,     ENUM_UMAC_CTX_Q_3, 0},
	{"CPU Q4",  ENUM_UMAC_CPU_PORT_1,     ENUM_UMAC_CTX_Q_4, 0},
	{"CPU Q5",  ENUM_UMAC_CPU_PORT_1,     ENUM_UMAC_CTX_Q_5, 0},
	{"CPU Q6",  ENUM_UMAC_CPU_PORT_1,     ENUM_UMAC_CTX_Q_6, 0},
	{"CPU Q7",  ENUM_UMAC_CPU_PORT_1,     ENUM_UMAC_CTX_Q_7, 0},
	{NULL, 0, 0, 0},
	{NULL, 0, 0, 0},
	{NULL, 0, 0, 0},
	{NULL, 0, 0, 0},
	{NULL, 0, 0, 0},
	{NULL, 0, 0, 0},
	{NULL, 0, 0, 0},
	{NULL, 0, 0, 0}, /* BIT 8 - 15 not defined */
	{NULL, 0, 0, 0},
	{NULL, 0, 0, 0},
	{NULL, 0, 0, 0},
	{NULL, 0, 0, 0}, /* 18, 19 not defined */
	{"FIXFID Q", ENUM_UMAC_LMAC_PORT_2, 0x1a, 0},
	{NULL, 0, 0, 0}, /* AMSDU_PNSN_EMPTY */
	{NULL, 0, 0, 0},
	{NULL, 0, 0, 0},
	{NULL, 0, 0, 0}, /* ALL_AC_EMPTY */
	{NULL, 0, 0, 0},
	{NULL, 0, 0, 0},
	{NULL, 0, 0, 0},
	{"RLS4 Q", ENUM_PLE_CTRL_PSE_PORT_3, 0x7c, 0},
	{"RLS3 Q", ENUM_PLE_CTRL_PSE_PORT_3, 0x7d, 0},
	{"RLS2 Q", ENUM_PLE_CTRL_PSE_PORT_3, 0x7e, 0},
	{"RLS Q", ENUM_PLE_CTRL_PSE_PORT_3, 0x7f, 0}
};

static struct bmac_queue_info ple_func_queue_empty_info[] = {
	{"ALTX Q0",	ENUM_UMAC_LMAC_PORT_2, 0x10, 0},
	{"BMC Q0",	ENUM_UMAC_LMAC_PORT_2, 0x11, 0},
	{"BCN Q0",	ENUM_UMAC_LMAC_PORT_2, 0x12, 0},
	{"PSMP Q0",	ENUM_UMAC_LMAC_PORT_2, 0x13, 0},
	{"NAF Q",	ENUM_UMAC_LMAC_PORT_2, 0x18, 0},
	{"NBCN Q",	ENUM_UMAC_LMAC_PORT_2, 0x19, 0},
	{"ALTX Q1",	ENUM_UMAC_LMAC_PORT_2, 0x10, 1},
	{"BMC Q1",	ENUM_UMAC_LMAC_PORT_2, 0x11, 1},
	{"BCN Q1",	ENUM_UMAC_LMAC_PORT_2, 0x12, 1},
	{"PSMP Q1",	ENUM_UMAC_LMAC_PORT_2, 0x13, 1},
	{"NAF Q",	ENUM_UMAC_LMAC_PORT_2, 0x18, 1},
	{"NBCN Q",	ENUM_UMAC_LMAC_PORT_2, 0x19, 1},
	{"ALTX Q2",	ENUM_UMAC_LMAC_PORT_2, 0x10, 2},
	{"BMC Q2",	ENUM_UMAC_LMAC_PORT_2, 0x11, 2},
	{"BCN Q2",	ENUM_UMAC_LMAC_PORT_2, 0x12, 2},
	{"PSMP Q2",	ENUM_UMAC_LMAC_PORT_2, 0x13, 2},
	{"NAF Q",	ENUM_UMAC_LMAC_PORT_2, 0x18, 2},
	{"NBCN Q",	ENUM_UMAC_LMAC_PORT_2, 0x19, 2},
};

static struct bmac_queue_info_t ple_txcmd_queue_empty_info[__MT_MAX_BAND][32] = {
	{{"AC00Q", ENUM_UMAC_LMAC_PORT_2, 0x40},
	{"AC01Q", ENUM_UMAC_LMAC_PORT_2, 0x41},
	{"AC02Q", ENUM_UMAC_LMAC_PORT_2, 0x42},
	{"AC03Q", ENUM_UMAC_LMAC_PORT_2, 0x43},
	{"AC10Q", ENUM_UMAC_LMAC_PORT_2, 0x44},
	{"AC11Q", ENUM_UMAC_LMAC_PORT_2, 0x45},
	{"AC12Q", ENUM_UMAC_LMAC_PORT_2, 0x46},
	{"AC13Q", ENUM_UMAC_LMAC_PORT_2, 0x47},
	{"AC20Q", ENUM_UMAC_LMAC_PORT_2, 0x48},
	{"AC21Q", ENUM_UMAC_LMAC_PORT_2, 0x49},
	{"AC22Q", ENUM_UMAC_LMAC_PORT_2, 0x4a},
	{"AC23Q", ENUM_UMAC_LMAC_PORT_2, 0x4b},
	{"AC30Q", ENUM_UMAC_LMAC_PORT_2, 0x4c},
	{"AC31Q", ENUM_UMAC_LMAC_PORT_2, 0x4d},
	{"AC32Q", ENUM_UMAC_LMAC_PORT_2, 0x4e},
	{"AC33Q", ENUM_UMAC_LMAC_PORT_2, 0x4f},
	{"ALTX Q0", ENUM_UMAC_LMAC_PORT_2, 0x70},
	{"TF Q0", ENUM_UMAC_LMAC_PORT_2, 0x71},
	{"TWT TSF-TF Q0", ENUM_UMAC_LMAC_PORT_2, 0x72},
	{"TWT DL Q0", ENUM_UMAC_LMAC_PORT_2, 0x73},
	{"TWT UL Q0", ENUM_UMAC_LMAC_PORT_2, 0x74},
	{NULL, 0, 0}, {NULL, 0, 0}, {NULL, 0, 0}, {NULL, 0, 0},
	{NULL, 0, 0}, {NULL, 0, 0}, {NULL, 0, 0}, {NULL, 0, 0},
	{NULL, 0, 0}, {NULL, 0, 0}, {NULL, 0, 0},},

	{{"AC00Q", ENUM_UMAC_LMAC_PORT_2, 0x50},
	{"AC01Q", ENUM_UMAC_LMAC_PORT_2, 0x51},
	{"AC02Q", ENUM_UMAC_LMAC_PORT_2, 0x52},
	{"AC03Q", ENUM_UMAC_LMAC_PORT_2, 0x53},
	{"AC10Q", ENUM_UMAC_LMAC_PORT_2, 0x54},
	{"AC11Q", ENUM_UMAC_LMAC_PORT_2, 0x55},
	{"AC12Q", ENUM_UMAC_LMAC_PORT_2, 0x56},
	{"AC13Q", ENUM_UMAC_LMAC_PORT_2, 0x57},
	{"AC20Q", ENUM_UMAC_LMAC_PORT_2, 0x58},
	{"AC21Q", ENUM_UMAC_LMAC_PORT_2, 0x59},
	{"AC22Q", ENUM_UMAC_LMAC_PORT_2, 0x5a},
	{"AC23Q", ENUM_UMAC_LMAC_PORT_2, 0x5b},
	{"AC30Q", ENUM_UMAC_LMAC_PORT_2, 0x5c},
	{"AC31Q", ENUM_UMAC_LMAC_PORT_2, 0x5d},
	{"AC32Q", ENUM_UMAC_LMAC_PORT_2, 0x5e},
	{"AC33Q", ENUM_UMAC_LMAC_PORT_2, 0x5f},
	{"ALTX Q0", ENUM_UMAC_LMAC_PORT_2, 0x75},
	{"TF Q0", ENUM_UMAC_LMAC_PORT_2, 0x76},
	{"TWT TSF-TF Q0", ENUM_UMAC_LMAC_PORT_2, 0x77},
	{"TWT DL Q0", ENUM_UMAC_LMAC_PORT_2, 0x78},
	{"TWT UL Q0", ENUM_UMAC_LMAC_PORT_2, 0x79},
	{NULL, 0, 0}, {NULL, 0, 0}, {NULL, 0, 0}, {NULL, 0, 0},
	{NULL, 0, 0}, {NULL, 0, 0}, {NULL, 0, 0}, {NULL, 0, 0},
	{NULL, 0, 0}, {NULL, 0, 0}, {NULL, 0, 0},},

	{{"AC00Q", ENUM_UMAC_LMAC_PORT_2, 0x60},
	{"AC01Q", ENUM_UMAC_LMAC_PORT_2, 0x61},
	{"AC02Q", ENUM_UMAC_LMAC_PORT_2, 0x62},
	{"AC03Q", ENUM_UMAC_LMAC_PORT_2, 0x63},
	{"AC10Q", ENUM_UMAC_LMAC_PORT_2, 0x64},
	{"AC11Q", ENUM_UMAC_LMAC_PORT_2, 0x65},
	{"AC12Q", ENUM_UMAC_LMAC_PORT_2, 0x66},
	{"AC13Q", ENUM_UMAC_LMAC_PORT_2, 0x67},
	{"AC20Q", ENUM_UMAC_LMAC_PORT_2, 0x68},
	{"AC21Q", ENUM_UMAC_LMAC_PORT_2, 0x69},
	{"AC22Q", ENUM_UMAC_LMAC_PORT_2, 0x6a},
	{"AC23Q", ENUM_UMAC_LMAC_PORT_2, 0x6b},
	{"AC30Q", ENUM_UMAC_LMAC_PORT_2, 0x6c},
	{"AC31Q", ENUM_UMAC_LMAC_PORT_2, 0x6d},
	{"AC32Q", ENUM_UMAC_LMAC_PORT_2, 0x6e},
	{"AC33Q", ENUM_UMAC_LMAC_PORT_2, 0x6f},
	{"ALTX Q0", ENUM_UMAC_LMAC_PORT_2, 0x7a},
	{"TF Q0", ENUM_UMAC_LMAC_PORT_2, 0x7b},
	{"TWT TSF-TF Q0", ENUM_UMAC_LMAC_PORT_2, 0x7c},
	{"TWT DL Q0", ENUM_UMAC_LMAC_PORT_2, 0x7d},
	{"TWT UL Q0", ENUM_UMAC_LMAC_PORT_2, 0x7e},
	{NULL, 0, 0}, {NULL, 0, 0}, {NULL, 0, 0}, {NULL, 0, 0},
	{NULL, 0, 0}, {NULL, 0, 0}, {NULL, 0, 0}, {NULL, 0, 0},
	{NULL, 0, 0}, {NULL, 0, 0}, {NULL, 0, 0},},
};

static size_t
ple_cr_num_of_ac(struct mt76_dev *dev)
{
	return CR_NUM_OF_AC_MT7999;
}

static size_t
ple_cr_num_of_twt(struct mt76_dev *dev)
{
	switch (mt76_chip(dev)) {
	case MT7999_DEVICE_ID:
	default:
		return CR_NUM_OF_TWT_MT7999;
	}
}

static void
mt7999_show_ple_pg_info(struct mt7999_dev *dev, struct seq_file *s)
{
	u32 ple_buf_ctrl = mt76_rr(dev, WF_PLE_TOP_PBUF_CTRL_ADDR);
	u32 pg_flow_ctrl[10] = {0};
	u32 pg_sz, pg_num;
	u32 fpg_cnt, ffa_cnt, fpg_head, fpg_tail, hif_max_q, hif_min_q;
	u32 rpg_hif, upg_hif, cpu_max_q, cpu_min_q, rpg_cpu, upg_cpu;

	pg_flow_ctrl[0] = mt76_rr(dev, WF_PLE_TOP_FREEPG_CNT_ADDR);
	pg_flow_ctrl[1] = mt76_rr(dev, WF_PLE_TOP_FREEPG_HEAD_TAIL_ADDR);
	pg_flow_ctrl[2] = mt76_rr(dev, WF_PLE_TOP_PG_HIF_GROUP_ADDR);
	pg_flow_ctrl[3] = mt76_rr(dev, WF_PLE_TOP_HIF_PG_INFO_ADDR);
	pg_flow_ctrl[4] = mt76_rr(dev, WF_PLE_TOP_PG_CPU_GROUP_ADDR);
	pg_flow_ctrl[5] = mt76_rr(dev, WF_PLE_TOP_CPU_PG_INFO_ADDR);
	pg_flow_ctrl[6] = mt76_rr(dev, WF_PLE_TOP_PG_HIF_TXCMD_GROUP_ADDR);
	pg_flow_ctrl[7] = mt76_rr(dev, WF_PLE_TOP_HIF_TXCMD_PG_INFO_ADDR);
	pg_flow_ctrl[8] = mt76_rr(dev, WF_PLE_TOP_PG_HIF_WMTXD_GROUP_ADDR);
	pg_flow_ctrl[9] = mt76_rr(dev, WF_PLE_TOP_HIF_WMTXD_PG_INFO_ADDR);

	/* Configuration Info */
	seq_printf(s, "PLE Configuration Info:\n");
	seq_printf(s, "\tPacket Buffer Control(0x82060014): 0x%08x\n", ple_buf_ctrl);
	pg_sz = u32_get_bits(ple_buf_ctrl, WF_PLE_TOP_PBUF_CTRL_PAGE_SIZE_CFG_MASK);
	seq_printf(s, "\t\tPage Size=%d(%d bytes per page)\n", pg_sz, (pg_sz == 1 ? 128 : 64));
	seq_printf(s, "\t\tPage Offset=%d(in unit of 2KB)\n",
			 u32_get_bits(ple_buf_ctrl, WF_PLE_TOP_PBUF_CTRL_PBUF_OFFSET_MASK));
	pg_num = u32_get_bits(ple_buf_ctrl, WF_PLE_TOP_PBUF_CTRL_TOTAL_PAGE_NUM_MASK);
	seq_printf(s, "\t\tTotal Page=%d pages\n", pg_num);
	/* Page Flow Control */
	seq_printf(s, "PLE Page Flow Control:\n");
	seq_printf(s, "\tFree page counter: 0x%08x\n", pg_flow_ctrl[0]);
	fpg_cnt = u32_get_bits(pg_flow_ctrl[0], WF_PLE_TOP_FREEPG_CNT_FREEPG_CNT_MASK);
	seq_printf(s, "\t\tThe toal page number of free=0x%03x\n", fpg_cnt);
	ffa_cnt = u32_get_bits(pg_flow_ctrl[0], WF_PLE_TOP_FREEPG_CNT_FFA_CNT_MASK);
	seq_printf(s, "\t\tThe free page numbers of free for all=0x%03x\n", ffa_cnt);
	seq_printf(s, "\tFree page head and tail: 0x%08x\n", pg_flow_ctrl[1]);
	fpg_head = u32_get_bits(pg_flow_ctrl[1], WF_PLE_TOP_FREEPG_HEAD_TAIL_FREEPG_HEAD_MASK);
	fpg_tail = u32_get_bits(pg_flow_ctrl[1], WF_PLE_TOP_FREEPG_HEAD_TAIL_FREEPG_TAIL_MASK);
	seq_printf(s, "\t\tThe tail/head page of free page list=0x%03x/0x%03x\n", fpg_tail, fpg_head);
	seq_printf(s, "\tReserved page counter of HIF group: 0x%08x\n", pg_flow_ctrl[2]);
	seq_printf(s, "\tHIF group page status: 0x%08x\n", pg_flow_ctrl[3]);
	hif_min_q = u32_get_bits(pg_flow_ctrl[2], WF_PLE_TOP_PG_HIF_GROUP_HIF_MIN_QUOTA_MASK);
	hif_max_q = u32_get_bits(pg_flow_ctrl[2], WF_PLE_TOP_PG_HIF_GROUP_HIF_MAX_QUOTA_MASK);
	seq_printf(s, "\t\tThe max/min quota pages of HIF group=0x%03x/0x%03x\n", hif_max_q, hif_min_q);
	rpg_hif = u32_get_bits(pg_flow_ctrl[3], WF_PLE_TOP_HIF_PG_INFO_HIF_RSV_CNT_MASK);
	upg_hif = u32_get_bits(pg_flow_ctrl[3], WF_PLE_TOP_HIF_PG_INFO_HIF_SRC_CNT_MASK);
	seq_printf(s, "\t\tThe used/reserved pages of HIF group=0x%03x/0x%03x\n", upg_hif, rpg_hif);

	seq_printf(s, "\tReserved page counter of WMTXD group: 0x%08x\n", pg_flow_ctrl[8]);
	seq_printf(s, "\tWMTXD group page status: 0x%08x\n", pg_flow_ctrl[9]);
	cpu_min_q = u32_get_bits(pg_flow_ctrl[8], WF_PLE_TOP_PG_HIF_WMTXD_GROUP_HIF_WMTXD_MIN_QUOTA_MASK);
	cpu_max_q = u32_get_bits(pg_flow_ctrl[8], WF_PLE_TOP_PG_HIF_WMTXD_GROUP_HIF_WMTXD_MAX_QUOTA_MASK);
	seq_printf(s, "\t\tThe max/min quota pages of WMTXD group=0x%03x/0x%03x\n", cpu_max_q, cpu_min_q);
	rpg_cpu = u32_get_bits(pg_flow_ctrl[9], WF_PLE_TOP_HIF_WMTXD_PG_INFO_HIF_WMTXD_RSV_CNT_MASK);
	upg_cpu = u32_get_bits(pg_flow_ctrl[9], WF_PLE_TOP_HIF_WMTXD_PG_INFO_HIF_WMTXD_SRC_CNT_MASK);
	seq_printf(s, "\t\tThe used/reserved pages of WMTXD group=0x%03x/0x%03x\n", upg_cpu, rpg_cpu);

	seq_printf(s, "\tReserved page counter of HIF_TXCMD group: 0x%08x\n", pg_flow_ctrl[6]);
	seq_printf(s, "\tHIF_TXCMD group page status: 0x%08x\n", pg_flow_ctrl[7]);
	cpu_min_q = u32_get_bits(pg_flow_ctrl[6], WF_PLE_TOP_PG_HIF_TXCMD_GROUP_HIF_TXCMD_MIN_QUOTA_MASK);
	cpu_max_q = u32_get_bits(pg_flow_ctrl[6], WF_PLE_TOP_PG_HIF_TXCMD_GROUP_HIF_TXCMD_MAX_QUOTA_MASK);
	seq_printf(s, "\t\tThe max/min quota pages of HIF_TXCMD group=0x%03x/0x%03x\n", cpu_max_q, cpu_min_q);
	rpg_cpu = u32_get_bits(pg_flow_ctrl[7], WF_PLE_TOP_HIF_TXCMD_PG_INFO_HIF_TXCMD_RSV_CNT_MASK);
	upg_cpu = u32_get_bits(pg_flow_ctrl[7], WF_PLE_TOP_HIF_TXCMD_PG_INFO_HIF_TXCMD_SRC_CNT_MASK);
	seq_printf(s, "\t\tThe used/reserved pages of HIF_TXCMD group=0x%03x/0x%03x\n", upg_cpu, rpg_cpu);

	seq_printf(s, "\tReserved page counter of CPU group(0x820c0150): 0x%08x\n", pg_flow_ctrl[4]);
	seq_printf(s, "\tCPU group page status(0x820c0154): 0x%08x\n", pg_flow_ctrl[5]);
	cpu_min_q = u32_get_bits(pg_flow_ctrl[4], WF_PLE_TOP_PG_CPU_GROUP_CPU_MIN_QUOTA_MASK);
	cpu_max_q = u32_get_bits(pg_flow_ctrl[4], WF_PLE_TOP_PG_CPU_GROUP_CPU_MAX_QUOTA_MASK);
	seq_printf(s, "\t\tThe max/min quota pages of CPU group=0x%03x/0x%03x\n", cpu_max_q, cpu_min_q);
	rpg_cpu = u32_get_bits(pg_flow_ctrl[5], WF_PLE_TOP_CPU_PG_INFO_CPU_RSV_CNT_MASK);
	upg_cpu = u32_get_bits(pg_flow_ctrl[5], WF_PLE_TOP_CPU_PG_INFO_CPU_SRC_CNT_MASK);
	seq_printf(s, "\t\tThe used/reserved pages of CPU group=0x%03x/0x%03x\n", upg_cpu, rpg_cpu);
}

static void
mt7999_get_ple_acq_stat(struct mt7999_dev *dev, unsigned long *ple_stat)
{
	u32 i, addr;
	size_t cr_num_of_ac = ple_cr_num_of_ac(&dev->mt76);

	ple_stat[0] = mt76_rr(dev, WF_PLE_TOP_QUEUE_EMPTY_ADDR);

	/* Legacy */
	addr = WF_PLE_TOP_AC0_QUEUE_EMPTY0_ADDR;
	for (i = 1; i <= cr_num_of_ac; i++, addr += 4)
		ple_stat[i] = mt76_rr(dev, addr);

	addr = WF_PLE_TOP_AC1_QUEUE_EMPTY0_ADDR;
	for (; i <= cr_num_of_ac * 2; i++, addr += 4)
		ple_stat[i] = mt76_rr(dev, addr);

	addr = WF_PLE_TOP_AC2_QUEUE_EMPTY0_ADDR;
	for (; i <= cr_num_of_ac * 3; i++, addr += 4)
		ple_stat[i] = mt76_rr(dev, addr);

	addr = WF_PLE_TOP_AC3_QUEUE_EMPTY0_ADDR;
	for (; i <= cr_num_of_ac * 4; i++, addr += 4)
		ple_stat[i] = mt76_rr(dev, addr);
}

static void
mt7999_get_sta_pause(struct mt7999_dev *dev, u8 band, u32 *sta_pause, u32 *twt_pause)
{
	size_t cr_num_of_ac = ple_cr_num_of_ac(&dev->mt76);
	size_t cr_num_of_all_ac = cr_num_of_ac * IEEE80211_NUM_ACS;
	size_t cr_num_of_twt = ple_cr_num_of_twt(&dev->mt76);
	u32 cmd = WF_DRR_TOP_DRR_TABLE_CTRL_EXECUTE |
		  u32_encode_bits(BIT(band), WF_DRR_TOP_DRR_TABLE_CTRL_BAND);
	int ac, group, i = 0, total_group = cr_num_of_ac >> 2;

	if (!dev->ple_query_cr) {
		mt7999_mcu_get_sta_pause(dev, band, sta_pause, twt_pause,
					 cr_num_of_all_ac, cr_num_of_twt);
		return;
	}

	for (ac = 0; ac < IEEE80211_NUM_ACS; ac++) {
		for (group = 0; group < total_group && (i < cr_num_of_all_ac);
		     group++, i += 4) {
			mt76_wr(dev, WF_DRR_TOP_DRR_TABLE_CTRL,
				u32_encode_bits(ac, WF_DRR_TOP_DRR_TABLE_CTRL_AC) |
				u32_encode_bits(group, WF_DRR_TOP_DRR_TABLE_CTRL_GROUP) |
				WF_DRR_STA_PAUSE_STATUS | cmd);

			sta_pause[i] = mt76_rr(dev, WF_DRR_TOP_DRR_TABLE0);
			sta_pause[i + 1] = mt76_rr(dev, WF_DRR_TOP_DRR_TABLE1);
			sta_pause[i + 2] = mt76_rr(dev, WF_DRR_TOP_DRR_TABLE2);
			sta_pause[i + 3] = mt76_rr(dev, WF_DRR_TOP_DRR_TABLE3);

			if (i >= cr_num_of_twt)
				continue;

			mt76_wr(dev, WF_DRR_TOP_DRR_TABLE_CTRL,
				u32_encode_bits(group, WF_DRR_TOP_DRR_TABLE_CTRL_GROUP) |
				WF_DRR_TWT_PAUSE_STATUS | cmd);

			twt_pause[i] = mt76_rr(dev, WF_DRR_TOP_DRR_TABLE0);
			twt_pause[i + 1] = mt76_rr(dev, WF_DRR_TOP_DRR_TABLE1);
			twt_pause[i + 2] = mt76_rr(dev, WF_DRR_TOP_DRR_TABLE2);
			twt_pause[i + 3] = mt76_rr(dev, WF_DRR_TOP_DRR_TABLE3);
		}
	}
}

static void
mt7999_get_ple_queue_info(struct mt7999_dev *dev, u32 pid, u32 qid, u32 tgid,
			  u16 wlan_idx, u16 *hfid, u16 *tfid, u16 *pktcnt)
{
	u32 val = WF_PLE_TOP_FL_QUE_CTRL_0_EXECUTE_MASK |
		  u32_encode_bits(pid, WF_PLE_TOP_FL_QUE_CTRL_0_Q_BUF_PID_MASK) |
		  u32_encode_bits(tgid, WF_PLE_TOP_FL_QUE_CTRL_0_Q_BUF_TGID_MASK) |
		  u32_encode_bits(qid, WF_PLE_TOP_FL_QUE_CTRL_0_Q_BUF_QID_MASK) |
		  u32_encode_bits(wlan_idx, WF_PLE_TOP_FL_QUE_CTRL_0_Q_BUF_WLANID_MASK);
	mt76_wr(dev, WF_PLE_TOP_FL_QUE_CTRL_0_ADDR, val);

	val = mt76_rr(dev, WF_PLE_TOP_FL_QUE_CTRL_2_ADDR);
	*hfid = u32_get_bits(val, WF_PLE_TOP_FL_QUE_CTRL_2_QUEUE_HEAD_FID_MASK);
	*tfid = u32_get_bits(val, WF_PLE_TOP_FL_QUE_CTRL_2_QUEUE_TAIL_FID_MASK);

	val = mt76_rr(dev, WF_PLE_TOP_FL_QUE_CTRL_3_ADDR);
	*pktcnt = u32_get_bits(val, WF_PLE_TOP_FL_QUE_CTRL_3_QUEUE_PKT_NUM_MASK);
}

static void mt7999_show_txd(struct seq_file *s, u32 fid)
{
#define TXD_SIZE	64
#define TXD_SIZE_DDW	16
	struct mt7999_dev *dev = dev_get_drvdata(s->private);
	u32 regidx[TXD_SIZE_DDW], regval[TXD_SIZE_DDW] = {0};
	u32 addr = MT_FID_HEADER | u32_encode_bits(fid, MT_FID_MASK);
	int i, ret;

	if (fid > 0x3fff)
		return;

	if (dev->ple_query_cr) {
		for (i = 0; i < TXD_SIZE_DDW; i++)
			regval[i] = mt76_rr(dev, addr + i * 4);
	} else {
		for (i = 0; i < TXD_SIZE_DDW; i++)
			regidx[i] = addr + i * 4;

		ret = mt7999_mcu_regval(dev, regidx, regval, TXD_SIZE_DDW, false);
		if (ret) {
			dev_warn(dev->mt76.dev, "Read reg val failed(%d)\n", ret);
			return;
		}
	}

	for (i = 0; i < TXD_SIZE_DDW; i++)
		seq_printf(s, "DW%02d: 0x%08x\n", i, regval[i]);
}

static void
mt7999_show_sta_acq_info(struct seq_file *s, unsigned long *ple_stat,
			 u32 **sta_pause, u32 **twt_sta_pause)
{
	struct mt7999_dev *dev = dev_get_drvdata(s->private);
	size_t cr_num_of_ac = ple_cr_num_of_ac(&dev->mt76);
	size_t cr_num_of_all_ac = cr_num_of_ac * IEEE80211_NUM_ACS;
	size_t cr_num_of_twt = ple_cr_num_of_twt(&dev->mt76);
	int i, j;

	for (j = 0; j < cr_num_of_all_ac; j++) { /* show AC Q info */
		for (i = 0; i < 32; i++) {
			if (!test_bit(i, &ple_stat[j + 1])) {
				u16 hfid, tfid, pktcnt, wlan_idx = i + (j % cr_num_of_ac) * 32;
				u8 wmmidx, ctrl = 0, acq_idx = j / cr_num_of_ac;
				struct mt7999_sta_link *msta_link;
				struct mt76_wcid *wcid;
				size_t idx;

				if (wlan_idx >= mt7999_wtbl_size(dev))
					break;

				seq_printf(s, "\tSTA%hu AC%hhu: ", wlan_idx, acq_idx);
				mt7999_get_ple_queue_info(dev, ENUM_UMAC_LMAC_PORT_2, acq_idx,
							  0, wlan_idx, &hfid, &tfid, &pktcnt);
				seq_printf(s, "tail/head fid = 0x%04x/0x%04x, pkt cnt = 0x%04x",
					   tfid, hfid, pktcnt);

				rcu_read_lock();
				wcid = mt76_wcid_ptr(dev, wlan_idx);
				if (!wcid) {
					seq_printf(s, ", Error: STA %hu does not exist.\n", wlan_idx);
					rcu_read_unlock();
					continue;
				}
				msta_link = container_of(wcid, struct mt7999_sta_link, wcid);
				wmmidx = msta_link->sta->vif->deflink.mt76.wmm_idx;

				if (sta_pause[wcid->phy_idx][j] & BIT(i))
					ctrl = 2;

				idx = j % cr_num_of_twt;
				if (twt_sta_pause[wcid->phy_idx][idx] & BIT(i))
					ctrl = 3;

				seq_printf(s, ", ctrl = %s (wmmidx=%hhu, band=%hhu)\n",
					   sta_ctrl_reg[ctrl], wmmidx, wcid->phy_idx);
				rcu_read_unlock();

				if (pktcnt > 0 && dev->dbg.dump_ple_txd)
					mt7999_show_txd(s, hfid);
			}
		}
	}
}

static void mt7999_show_txcmd(struct seq_file *s, u32 fid)
{
#define MT_TXCMD_BYTE_CNTS		GENMASK(15, 0)
	struct mt7999_dev *dev = dev_get_drvdata(s->private);
	u32 *regidx = NULL, *regval = NULL, val;
	u32 addr = MT_FID_HEADER | u32_encode_bits(fid, MT_FID_MASK);
	int i, ret;
	u16 txcmd_bytes, num;

	if (fid > 0x3fff)
		return;

	if (dev->ple_query_cr) {
		val = mt76_rr(dev, addr);
	} else {
		ret = mt7999_mcu_regval(dev, &addr, &val, 1, false);
		if (ret) {
			dev_warn(dev->mt76.dev, "Read reg val failed(%d)\n", ret);
			return;
		}
	}

	txcmd_bytes = u32_get_bits(val, MT_TXCMD_BYTE_CNTS);
	num = (txcmd_bytes >> 2) + ((txcmd_bytes % 4) ? 1 : 0);

	regval = kzalloc(sizeof(u32) * num, GFP_KERNEL);
	if (dev->ple_query_cr) {
		for (i = 0; i < num; i++)
			regval[i] = mt76_rr(dev, addr + 4 * i);

	} else {
		regidx = kzalloc(sizeof(u32) * num, GFP_KERNEL);
		for (i = 0; i < num; i++)
			regidx[i] = addr + 4 * i;

		ret = mt7999_mcu_regval(dev, regidx, regval, num, false);
		if (ret) {
			dev_warn(dev->mt76.dev, "Read reg val failed(%d)\n", ret);
			goto out;
		}
	}

	for (i = 0; i < num; i++)
		seq_printf(s, "DW%02d: 0x%08x\n", i, regval[i]);

out:
	kfree(regval);
	kfree(regidx);
}

static void
mt7999_show_txcmdq_info(struct seq_file *s)
{
	const u32 txcmd_queue_empty_addr[__MT_MAX_BAND][2] = {
		[MT_BAND0] = {WF_PLE_TOP_TXCMD_QUEUE_EMPTY_ADDR,
			      WF_PLE_TOP_NATIVE_TXCMD_QUEUE_EMPTY_ADDR},
		[MT_BAND1] = {WF_PLE_TOP_BN1_TXCMD_QUEUE_EMPTY_ADDR,
			      WF_PLE_TOP_BN1_NATIVE_TXCMD_QUEUE_EMPTY_ADDR},
		[MT_BAND2] = {WF_PLE_TOP_BN2_TXCMD_QUEUE_EMPTY_ADDR,
			      WF_PLE_TOP_BN2_NATIVE_TXCMD_QUEUE_EMPTY_ADDR}
	};
	struct mt7999_dev *dev = dev_get_drvdata(s->private);
	u8 band;

	for (band = MT_BAND0; band < __MT_MAX_BAND; ++band) {
		unsigned long txcmdq_stat, native_txcmdq_stat;
		int i;

		if (!dev->mt76.phys[band])
			continue;

		txcmdq_stat = mt76_rr(dev, txcmd_queue_empty_addr[band][0]);
		native_txcmdq_stat = mt76_rr(dev, txcmd_queue_empty_addr[band][1]);

		seq_printf(s, "BN%d non-native/native txcmd queue empty = %lx/%lx\n",
			   band, txcmdq_stat, native_txcmdq_stat);

		for (i = 0; i < 32 ; i++) {
			if (!test_bit(i, &native_txcmdq_stat)) {
				struct bmac_queue_info_t *queue = &ple_txcmd_queue_empty_info[band][i];
				u16 hfid, tfid, pktcnt;

				if (!queue->QueueName)
					continue;

				seq_printf(s, "\t%s: ", queue->QueueName);
				mt7999_get_ple_queue_info(dev, queue->Portid, queue->Queueid,
							  0, 0, &hfid, &tfid, &pktcnt);
				seq_printf(s, "tail/head fid = 0x%04x/0x%04x, pkt cnt = 0x%04x\n",
					   tfid, hfid, pktcnt);
				if (pktcnt > 0 && dev->dbg.dump_ple_txd)
					mt7999_show_txcmd(s, hfid);
			}
		}
	}
}

static void
mt7999_show_funcq_info(struct seq_file *s, u32 *ple_funcq_stat)
{
	struct mt7999_dev *dev = dev_get_drvdata(s->private);
	int i, j;

	for (i = 0; i < 3; i++) {
		for (j = WF_PLE_TOP_TXD_QUEUE_EMPTY_ALTX_EMPTY_SHFT; j <= WF_PLE_TOP_TXD_QUEUE_EMPTY_NBCN_EMPTY_SHFT; j++) {
			if ((ple_funcq_stat[i] & (1 << j)) == 0) {
				u32 hfid, tfid, pktcnt, fl_que_ctrl[3] = {0};
				u32 info_index = i*6 + (j-WF_PLE_TOP_TXD_QUEUE_EMPTY_ALTX_EMPTY_SHFT);

				if (ple_func_queue_empty_info[info_index].QueueName != NULL) {
					seq_printf(s, "\t%s: ", ple_func_queue_empty_info[info_index].QueueName);
					fl_que_ctrl[0] |= WF_PLE_TOP_FL_QUE_CTRL_0_EXECUTE_MASK;
					fl_que_ctrl[0] |= (ple_func_queue_empty_info[info_index].Portid << WF_PLE_TOP_FL_QUE_CTRL_0_Q_BUF_PID_SHFT);
					fl_que_ctrl[0] |= (ple_func_queue_empty_info[info_index].tgid << WF_PLE_TOP_FL_QUE_CTRL_0_Q_BUF_TGID_SHFT);
					fl_que_ctrl[0] |= (ple_func_queue_empty_info[info_index].Queueid << WF_PLE_TOP_FL_QUE_CTRL_0_Q_BUF_QID_SHFT);
				} else
					continue;

				mt76_wr(dev, WF_PLE_TOP_FL_QUE_CTRL_0_ADDR, fl_que_ctrl[0]);
				fl_que_ctrl[1] = mt76_rr(dev, WF_PLE_TOP_FL_QUE_CTRL_2_ADDR);
				fl_que_ctrl[2] = mt76_rr(dev, WF_PLE_TOP_FL_QUE_CTRL_3_ADDR);
				hfid = u32_get_bits(fl_que_ctrl[1], WF_PLE_TOP_FL_QUE_CTRL_2_QUEUE_HEAD_FID_MASK);
				tfid = u32_get_bits(fl_que_ctrl[1], WF_PLE_TOP_FL_QUE_CTRL_2_QUEUE_TAIL_FID_MASK);
				pktcnt = u32_get_bits(fl_que_ctrl[2], WF_PLE_TOP_FL_QUE_CTRL_3_QUEUE_PKT_NUM_MASK);
				seq_printf(s, "tail/head fid = 0x%04x/0x%04x, pkt cnt = 0x%04x\n",
						  tfid, hfid, pktcnt);

				if (pktcnt > 0 && dev->dbg.dump_ple_txd)
					mt7999_show_txd(s, hfid);
			}
		}
	}
}

static int
mt7999_pleinfo_read(struct seq_file *s, void *data)
{
	struct mt7999_dev *dev = dev_get_drvdata(s->private);
	size_t cr_num_of_ac = ple_cr_num_of_ac(&dev->mt76);
	size_t cr_num_of_all_ac = cr_num_of_ac * IEEE80211_NUM_ACS;
	size_t cr_num_of_twt = ple_cr_num_of_twt(&dev->mt76);
	u32 *sta_pause[__MT_MAX_BAND] = {0}, *twt_sta_pause[__MT_MAX_BAND] = {0};
	u32 ple_funcq_stat[3];
	unsigned long *ple_stat;
	int i, j, ret = 0;

	ple_stat = kzalloc((cr_num_of_all_ac + 1) * sizeof(unsigned long), GFP_KERNEL);
	if (!ple_stat)
		return -ENOMEM;

	mt7999_show_ple_pg_info(dev, s);
	ple_funcq_stat[0] = mt76_rr(dev, WF_PLE_TOP_TXD_QUEUE_EMPTY_ADDR);
	ple_funcq_stat[1] = mt76_rr(dev, WF_PLE_TOP_BN1_TXD_QUEUE_EMPTY_ADDR);
	ple_funcq_stat[2] = mt76_rr(dev, WF_PLE_TOP_BN2_TXD_QUEUE_EMPTY_ADDR);
	mt7999_get_ple_acq_stat(dev, ple_stat);

	for (i = MT_BAND0; i < __MT_MAX_BAND; i++) {
		sta_pause[i] = kzalloc(cr_num_of_all_ac * sizeof(u32), GFP_KERNEL);
		twt_sta_pause[i] = kzalloc(cr_num_of_twt * sizeof(u32), GFP_KERNEL);
		if (!sta_pause[i] || !twt_sta_pause[i]) {
			ret = -ENOMEM;
			goto out;
		}
		if (dev->mt76.phys[i])
			mt7999_get_sta_pause(dev, i, sta_pause[i], twt_sta_pause[i]);
	}

	if ((ple_stat[0] & WF_PLE_TOP_QUEUE_EMPTY_ALL_AC_EMPTY_MASK) == 0) {
		for (j = 0; j < cr_num_of_all_ac; j++) {
			if (j % cr_num_of_ac == 0)
				seq_printf(s, "\n\tNonempty AC%ld Q of STA#: ", j / cr_num_of_ac);

			for (i = 0; i < 32; i++) {
				u32 sta_num = i + (j % cr_num_of_ac) * 32;

				if (sta_num >= mt7999_wtbl_size(dev))
					break;

				if (!test_bit(i, &ple_stat[j + 1]))
					seq_printf(s, "%lu ", i + (j % cr_num_of_ac) * 32);
			}
		}
		seq_printf(s, "\n");
	}

	seq_printf(s, "Nonempty Q info:\n");

	for (i = 0; i < 32; i++) {
		if (!test_bit(i, &ple_stat[0])) {
			struct bmac_queue_info *queue = &ple_queue_empty_info[i];
			u16 hfid, tfid, pktcnt;

			if (!queue->QueueName)
				continue;

			seq_printf(s, "\t%s: ", queue->QueueName);
			mt7999_get_ple_queue_info(dev, queue->Portid, queue->Queueid,
						  queue->tgid, 0, &hfid, &tfid, &pktcnt);
			seq_printf(s, "tail/head fid = 0x%04x/0x%04x, pkt cnt = 0x%04x\n",
				   tfid, hfid, pktcnt);

			if (pktcnt > 0 && dev->dbg.dump_ple_txd)
				mt7999_show_txd(s, hfid);
		}
	}

	mt7999_show_funcq_info(s, ple_funcq_stat);
	mt7999_show_sta_acq_info(s, ple_stat, sta_pause, twt_sta_pause);
	mt7999_show_txcmdq_info(s);

out:
	for (i = MT_BAND0; i < __MT_MAX_BAND; i++) {
		kfree(sta_pause[i]);
		kfree(twt_sta_pause[i]);
	}
	kfree(ple_stat);
	return ret;
}

static int
mt7999_tx_drop_show(struct seq_file *s, void *data)
{
	struct mt7999_dev *dev = s->private;
	struct mt76_dev *mdev = &dev->mt76;
	struct mt76_tx_debug *phy_stats[__MT_MAX_BAND];
	int i = 0;

	for (i = 0; i < __MT_MAX_BAND; i++) {
		seq_printf(s, "       Band%d", i);
		if (mdev->phys[i]) {
			phy_stats[i] = &mdev->phys[i]->tx_dbg_stats;
		} else {
			phy_stats[i] = kzalloc(sizeof(struct mt76_tx_debug),
					       GFP_KERNEL);
			if (!phy_stats[i])
				goto out;
		}

	}
	seq_printf(s, "       total\n");

	seq_printf(s, "%-30s%12d%12d%12d%12d\n", "Receive from mac80211",
		       phy_stats[0]->data[MT_TX_FROM_MAC80211],
		       phy_stats[1]->data[MT_TX_FROM_MAC80211],
		       phy_stats[2]->data[MT_TX_FROM_MAC80211],
		       phy_stats[0]->data[MT_TX_FROM_MAC80211] +
		       phy_stats[1]->data[MT_TX_FROM_MAC80211] +
		       phy_stats[2]->data[MT_TX_FROM_MAC80211]);
	seq_printf(s, "%-30s%12d%12d%12d%12d\n\n", "Send to hw",
		       phy_stats[0]->data[MT_TX_TO_HW],
		       phy_stats[1]->data[MT_TX_TO_HW],
		       phy_stats[2]->data[MT_TX_TO_HW],
		       phy_stats[0]->data[MT_TX_TO_HW] +
		       phy_stats[1]->data[MT_TX_TO_HW] +
		       phy_stats[2]->data[MT_TX_TO_HW]);
#define __pr(t) seq_printf(s, "Drop due to %-18s%12d%12d%12d%12d\n",\
			   #t, phy_stats[0]->data[MT_TX_DROP_##t],	\
			   phy_stats[1]->data[MT_TX_DROP_##t],	\
			   phy_stats[2]->data[MT_TX_DROP_##t],	\
			   phy_stats[0]->data[MT_TX_DROP_##t] + 	\
			   phy_stats[1]->data[MT_TX_DROP_##t] +	\
			   phy_stats[2]->data[MT_TX_DROP_##t])

	__pr(IN_TESTMODE);
	__pr(WCID_NOT_INIT);
	__pr(STOPPED_QUEUE);
	__pr(RESET_STATE);
	__pr(GET_TXWI_FAIL);
	__pr(DMA_FAIL);
	__pr(AGG_EXCEEDED);
	__pr(RING_FULL);
	__pr(INVALID_SKB);
	__pr(GET_TOKEN_FAIL);
	__pr(ADDR_TRANS_FAIL);
	__pr(INVALID_WCID);
	__pr(INVALID_LINK);

#undef __pr
out:
	for (i = 0; i < __MT_MAX_BAND; i++) {
		if (!mdev->phys[i] && phy_stats[i])
			kfree(phy_stats[i]);
	}

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(mt7999_tx_drop);

static int
mt7999_rx_drop_show(struct seq_file *s, void *data)
{
	struct mt7999_dev *dev = s->private;
	struct mt76_dev *mdev = &dev->mt76;
	struct mt76_rx_debug *stats[__MT_MAX_BAND];
	struct mt76_queue *q[2];
	int i = 0;

	if (mtk_wed_device_active(&mdev->mmio.wed) &&
	    mdev->mmio.wed.version == MTK_WED_HW_V3_1)
		q[0] = &mdev->q_rx[MT_RXQ_WED_RX_DATA];
	else
		q[0] = &mdev->q_rx[MT_RXQ_MAIN];

	q[1] = &mdev->q_rx[MT_RXQ_BAND2];

	seq_printf(s, "\t\t\t\t   ");
	for (i = 0; i < 2; i++) {
		seq_printf(s, "        RXQ%d", q[i]->hw_idx);
	}
	seq_printf(s, "\n");

#define __pr(t) seq_printf(s, "Drop due to %-22s%12d%12d\n", #t, \
			   q[0]->rx_drop[MT_RX_DROP_##t],	\
			   q[1]->rx_drop[MT_RX_DROP_##t]);
	__pr(DMAD_RRO_REPEAT);
	__pr(DMAD_RRO_OLDPKT);
	__pr(DMAD_RRO_PN_CHK_FAIL);
	__pr(DMAD_WO_FRAG);
	__pr(DMAD_WO_DROP);
	__pr(DMAD_ADDR_NOT_FOUND);
	__pr(DMAD_TOKEN_NOT_FOUND);
	__pr(DMAD_GET_TOKEN_FAIL);
	__pr(DMAD_GET_RXWI_FAIL);
	__pr(DMAD_NOMEM);
	__pr(DMAD_DMA_MAPPING_FAIL);
	__pr(FRAG);
	__pr(BUILD_SKB_FAIL);
#undef __pr

	seq_printf(s, "\n\t\t\t\t   ");
	for (i = 0; i < __MT_MAX_BAND; i++) {
		seq_printf(s, "       Band%d", i);
		if (mdev->phys[i]) {
			stats[i] = &mdev->phys[i]->rx_dbg_stats;
		} else {
			stats[i] = kzalloc(sizeof(struct mt76_rx_debug),
					       GFP_KERNEL);
			if (!stats[i])
				goto out;
		}
	}
	seq_printf(s, "\n");
	seq_printf(s, "%-35s%12d%12d%12d\n", "Receive from hw",
		       stats[MT_BAND0]->data[MT_RX_FROM_HW],
		       stats[MT_BAND1]->data[MT_RX_FROM_HW],
		       stats[MT_BAND2]->data[MT_RX_FROM_HW]);
	seq_printf(s, "%-35s%12d%12d%12d\n\n", "Send to mac80211",
		       stats[MT_BAND0]->data[MT_RX_TO_MAC80211],
		       stats[MT_BAND1]->data[MT_RX_TO_MAC80211],
		       stats[MT_BAND2]->data[MT_RX_TO_MAC80211]);
#define __pr(t) seq_printf(s, "Drop due to %-22s%12d%12d%12d\n", #t, \
			   stats[MT_BAND0]->data[MT_RX_DROP_##t],	\
			   stats[MT_BAND1]->data[MT_RX_DROP_##t],	\
			   stats[MT_BAND2]->data[MT_RX_DROP_##t])
	__pr(RXD_ERR);
	__pr(STATE_ERR);
	__pr(RFC_PKT);
	__pr(AGG_SN_LESS);
	__pr(AGG_DUP);
#undef __pr

out:
	for (i = 0; i < __MT_MAX_BAND; i++) {
		if (!mdev->phys[i] && stats[i])
			kfree(stats[i]);
	}

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(mt7999_rx_drop);
/* DRR */
static int
mt7999_drr_info(struct seq_file *s, void *data)
{
	/* TODO: Wait MIB counter API implement complete */
	return 0;
}

static ssize_t mt7999_muru_dbg_info_set(struct file *file,
					const char __user *user_buf,
					size_t count, loff_t *ppos)
{
	struct mt7999_dev *dev = file->private_data;
	char buf[10];
	u16 item;
	u8 val;
	int ret;

	if (count >= sizeof(buf))
		return -EINVAL;

	if (copy_from_user(buf, user_buf, count))
		return -EFAULT;

	if (count && buf[count - 1] == '\n')
		buf[count - 1] = '\0';
	else
		buf[count] = '\0';

	if (sscanf(buf, "%hu-%hhu", &item, &val) != 2) {
		dev_warn(dev->mt76.dev,"format: item-value\n");
		return -EINVAL;
	}

	ret = mt7999_mcu_muru_dbg_info(dev, item, val);
	if (ret) {
		dev_warn(dev->mt76.dev, "Fail to send mcu cmd.\n");
		return -EFAULT;
	}

	return count;
}

static const struct file_operations fops_muru_dbg_info = {
	.write = mt7999_muru_dbg_info_set,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

static int mt7999_pp_alg_show(struct seq_file *s, void *data)
{
	struct mt7999_phy *phy = s->private;
	struct mt7999_dev *dev = phy->dev;

	dev_info(dev->mt76.dev, "pp_mode = %d\n", phy->pp_mode);
	mt7999_mcu_set_pp_alg_ctrl(phy, PP_ALG_GET_STATISTICS);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(mt7999_pp_alg);

static int mt7999_afc_table_show(struct seq_file *s, void *data)
{
	struct mt7999_dev *dev = s->private;

	char str[200] = {0}, *pos;
	char *end = str + sizeof(str);
	int i, j;

	if (!dev->mt76.afc_power_table || !dev->mt76.afc_power_table[0]) {
		seq_printf(s, "afc table doesn't exist.\n");
		return 0;
	}

	seq_printf(s, "bw/ru :    20    40    80   160  320-1 320-2   26    52    78   "
		   "106   132   726  1480  1772  2476  2988  3472\n");
	for(i = 0; i < MAX_CHANNEL_NUM_6G; i ++) {
		pos = str;
		for (j = 0; j < afc_power_table_num; j ++) {
			pos += snprintf(pos, end - pos, "%5d ",
					dev->mt76.afc_power_table[i][j]);
		}
		seq_printf(s, "ch %3d: %s\n", i * 4 + 1, str);
		memset(str, 0, sizeof(str));
	}
	seq_printf(s, "Unit : 0.5 dBm\n");
	seq_printf(s, "NOTE : power of the table is translated to single path.\n");

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(mt7999_afc_table);

#define info_or_seq_printf(seq, fmt, ...)	do {	\
	if (seq)					\
		seq_printf(seq, fmt, ##__VA_ARGS__);	\
	else						\
		pr_info(fmt, ##__VA_ARGS__);		\
} while (0)

static void info_or_seq_hex_dump(struct seq_file *seq, int prefix_type,
				 int rowsize, int groupsize, const void *buf,
				 size_t len, bool ascii)
{
	if (seq)
		seq_hex_dump(seq, "", prefix_type, rowsize, groupsize,
			     buf, len, ascii);
	else
		print_hex_dump(KERN_INFO, "", prefix_type,
			       rowsize, groupsize, buf, len, ascii);
}

//bmac dump mac txp
static void mt7999_dump_bmac_mac_txp_info(struct seq_file *s, struct mt7999_dev *dev,
					  __le32 *txp)
{
	struct mt7999_txp_token {
		__le16 msdu[4];
	} *msdu;
	struct mt7999_txp_ptr {
		__le32 addr1;
		__le32 addr_info;
		__le32 addr2;
	} *ptr;
	int i = 0;

	for (i = 0; i < 12; i = i+2 ) {
		if (i == 0 || i == 4) {
			msdu = (struct mt7999_txp_token *) txp;
			info_or_seq_printf(s, "msdu token(%d-%d)=%ld %ld %ld %ld (0x%08x-0x%08x)\n", i, i+3,
				(msdu->msdu[0] & GENMASK(14, 0)),
				(msdu->msdu[1] & GENMASK(14, 0)),
				(msdu->msdu[2] & GENMASK(14, 0)),
				(msdu->msdu[3] & GENMASK(14, 0)), *txp, *(txp+1));
			txp = txp + 2;
		}
		ptr = (struct mt7999_txp_ptr *) txp;
		info_or_seq_printf(s, "ptr%02d : addr(0x%08x) len(%ld) addr_h(%02lx) SRC(%d) ML(%d) \n",
			i, ptr->addr1,
			FIELD_GET(GENMASK(11, 0), ptr->addr_info),
			FIELD_GET(GENMASK(13, 12), ptr->addr_info),
			!!(ptr->addr_info & BIT(14)),
			!!(ptr->addr_info & BIT(15)));
		info_or_seq_printf(s, "ptr%02d : addr(0x%08x) len(%ld) addr_h(%02lx) SRC(%d) ML(%d) \n",
			i+1, ptr->addr2,
			FIELD_GET(GENMASK(27, 16), ptr->addr_info),
			FIELD_GET(GENMASK(29, 28), ptr->addr_info),
			!!(ptr->addr_info & BIT(30)),
			!!(ptr->addr_info & BIT(31)));
		txp = txp + 3;
	}
}

//bmac dump hif txp
static void
mt7999_dump_bmac_hif_txp_info(struct seq_file *s, struct mt7999_dev *dev,
			      __le32 *txp, u32 hif_txp_ver)
{
	int i, j = 0;
	u32 dw;

	info_or_seq_printf(s, "txp raw data: size=%d\n", HIF_TXP_V2_SIZE);
	info_or_seq_hex_dump(s, DUMP_PREFIX_OFFSET, 16, 1, (u8 *)txp, HIF_TXP_V2_SIZE, false);

	info_or_seq_printf(s, "BMAC_TXP Fields:\n");

	/* dw0 */
	if (hif_txp_ver == 2) {
		dw = le32_to_cpu(txp[0]);
		info_or_seq_printf(s, "HIF_TXP_PRIORITY = %d\n",
				GET_FIELD(HIF_TXP_PRIORITY, dw));
		info_or_seq_printf(s, "HIF_TXP_FIXED_RATE = %d\n",
				GET_FIELD(HIF_TXP_FIXED_RATE, dw));
		info_or_seq_printf(s, "HIF_TXP_TCP = %d\n",
				GET_FIELD(HIF_TXP_TCP, dw));
		info_or_seq_printf(s, "HIF_TXP_NON_CIPHER = %d\n",
				GET_FIELD(HIF_TXP_NON_CIPHER, dw));
		info_or_seq_printf(s, "HIF_TXP_VLAN = %d\n",
				GET_FIELD(HIF_TXP_VLAN, dw));
		info_or_seq_printf(s, "HIF_TXP_BC_MC_FLAG = %d\n",
				GET_FIELD(HIF_TXP_BC_MC_FLAG, dw));
		info_or_seq_printf(s, "HIF_TXP_FR_HOST = %d\n",
				GET_FIELD(HIF_TXP_FR_HOST, dw));
		info_or_seq_printf(s, "HIF_TXP_ETYPE = %d\n",
				GET_FIELD(HIF_TXP_ETYPE, dw));
		info_or_seq_printf(s, "HIF_TXP_TXP_AMSDU = %d\n",
				GET_FIELD(HIF_TXP_TXP_AMSDU, dw));
		info_or_seq_printf(s, "HIF_TXP_TXP_MC_CLONE = %d\n",
				GET_FIELD(HIF_TXP_TXP_MC_CLONE, dw));
		info_or_seq_printf(s, "HIF_TXP_TOKEN_ID = %d\n",
				GET_FIELD(HIF_TXP_TOKEN_ID, dw));

		/* dw1 */
		dw = le32_to_cpu(txp[1]);
		info_or_seq_printf(s, "HIF_TXP_BSS_IDX = %d\n",
				GET_FIELD(HIF_TXP_BSS_IDX, dw));
		info_or_seq_printf(s, "HIF_TXP_USER_PRIORITY = %d\n",
				GET_FIELD(HIF_TXP_USER_PRIORITY, dw));
		info_or_seq_printf(s, "HIF_TXP_BUF_NUM = %d\n",
				GET_FIELD(HIF_TXP_BUF_NUM, dw));
		info_or_seq_printf(s, "HIF_TXP_MSDU_CNT = %d\n",
				GET_FIELD(HIF_TXP_MSDU_CNT, dw));
		info_or_seq_printf(s, "HIF_TXP_SRC = %d\n",
				GET_FIELD(HIF_TXP_SRC, dw));

		/* dw2 */
		dw = le32_to_cpu(txp[2]);
		info_or_seq_printf(s, "HIF_TXP_ETH_TYPE(network-endian) = 0x%x\n",
				GET_FIELD(HIF_TXP_ETH_TYPE, dw));
		info_or_seq_printf(s, "HIF_TXP_WLAN_IDX = %d\n",
				GET_FIELD(HIF_TXP_WLAN_IDX, dw));

		/* dw3 */
		dw = le32_to_cpu(txp[3]);
		info_or_seq_printf(s, "HIF_TXP_PPE_INFO = 0x%x\n",
				GET_FIELD(HIF_TXP_PPE_INFO, dw));

		for (i = 0; i < 13; i++) {
			if (i % 2 == 0) {
				info_or_seq_printf(s, "HIF_TXP_BUF_PTR%d_L = 0x%x\n",
						i, GET_FIELD(HIF_TXP_BUF_PTR0_L,
						le32_to_cpu(txp[4 + j])));
				j++;
				info_or_seq_printf(s, "HIF_TXP_BUF_LEN%d = %d\n",
						i, GET_FIELD(HIF_TXP_BUF_LEN0, le32_to_cpu(txp[4 + j])));
				info_or_seq_printf(s, "HIF_TXP_BUF_PTR%d_H = 0x%x\n",
						i, GET_FIELD(HIF_TXP_BUF_PTR0_H, le32_to_cpu(txp[4 + j])));
				if (i <= 10) {
					info_or_seq_printf(s, "HIF_TXP_BUF_LEN%d = %d\n",
							i + 1, GET_FIELD(HIF_TXP_BUF_LEN1, le32_to_cpu(txp[4 + j])));
					info_or_seq_printf(s, "HIF_TXP_BUF_PTR%d_H = 0x%x\n",
							i + 1, GET_FIELD(HIF_TXP_BUF_PTR1_H, le32_to_cpu(txp[4 + j])));
				}
				j++;
			} else {
				info_or_seq_printf(s, "HIF_TXP_BUF_PTR%d_L = 0x%x\n",
					i, GET_FIELD(HIF_TXP_BUF_PTR1_L,
					le32_to_cpu(txp[4 + j])));
				j++;
			}
		}

		info_or_seq_printf(s, "ml = 0x%x\n",
			GET_FIELD(HIF_TXP_ML, le32_to_cpu(txp[23])));
	} else {
		struct mt76_connac_txp_common *txp_v1 = (struct mt76_connac_txp_common *)txp;

		info_or_seq_printf(s, "FLAGS = (%04x)\n", txp_v1->fw.flags);

		info_or_seq_printf(s, "MSDU = %d\n", txp_v1->fw.token);

		info_or_seq_printf(s, "BSS_IDX = %d\n", txp_v1->fw.bss_idx);

		info_or_seq_printf(s, "WCID = %d\n",txp_v1->fw.rept_wds_wcid);

		info_or_seq_printf(s, "MSDU_CNT = %d\n", txp_v1->fw.nbuf);

		for (i = 0; i < MT_TXP_MAX_BUF_NUM; i++)
			info_or_seq_printf(s, "ptr%02d : addr(0x%08x) len(%d)\n", i, le32_to_cpu(txp_v1->fw.buf[i]),
				le16_to_cpu(txp_v1->fw.len[i]));
	}
}

/* bmac txd dump */
static void
mt7999_dump_bmac_txd_info(struct seq_file *s, struct mt7999_dev *dev,
			  __le32 *txd, bool is_hif_txd, bool dump_txp)
{
	u32 hif_txp_ver = 0;

	info_or_seq_printf(s, "txd raw data: size=%d\n", MT_TXD_SIZE);
	info_or_seq_hex_dump(s, DUMP_PREFIX_OFFSET, 16, 1, (u8 *)txd, MT_TXD_SIZE, false);

	info_or_seq_printf(s, "BMAC_TXD Fields:\n");
	/* dw0 */
	if (is_hif_txd) {
		hif_txp_ver = FIELD_GET(GENMASK(22, 19), txd[0]);
		info_or_seq_printf(s, "HIF TXD VER = %d\n", hif_txp_ver);
	}
	info_or_seq_printf(s, "TX_BYTE_COUNT = %d\n",
			GET_FIELD(WF_TX_DESCRIPTOR_TX_BYTE_COUNT, txd[0]));
	info_or_seq_printf(s, "ETHER_TYPE_OFFSET(word) = %d\n",
			GET_FIELD(WF_TX_DESCRIPTOR_ETHER_TYPE_OFFSET, txd[0]));
	info_or_seq_printf(s, "PKT_FT = %d%s%s%s%s\n",
			GET_FIELD(WF_TX_DESCRIPTOR_PKT_FT, txd[0]),
			GET_FIELD(WF_TX_DESCRIPTOR_PKT_FT, txd[0]) == 0 ? "(ct)" : "",
			GET_FIELD(WF_TX_DESCRIPTOR_PKT_FT, txd[0]) == 1 ? "(s&f)" : "",
			GET_FIELD(WF_TX_DESCRIPTOR_PKT_FT, txd[0]) == 2 ? "(cmd)" : "",
			GET_FIELD(WF_TX_DESCRIPTOR_PKT_FT, txd[0]) == 3 ? "(redirect)" : "");
	info_or_seq_printf(s, "Q_IDX = %d%s%s%s\n",
			GET_FIELD(WF_TX_DESCRIPTOR_Q_IDX, txd[0]),
			GET_FIELD(WF_TX_DESCRIPTOR_Q_IDX, txd[0]) == 0x10 ? "(ALTX)" : "",
			GET_FIELD(WF_TX_DESCRIPTOR_Q_IDX, txd[0]) == 0x11 ? "(BMC)" : "",
			GET_FIELD(WF_TX_DESCRIPTOR_Q_IDX, txd[0]) == 0x12 ? "(BCN)" : "");

	/* dw1 */
	info_or_seq_printf(s, "MLD_ID = %d\n",
			GET_FIELD(WF_TX_DESCRIPTOR_MLD_ID, txd[1]));
	info_or_seq_printf(s, "TGID = %d\n",
			GET_FIELD(WF_TX_DESCRIPTOR_TGID, txd[1]));
	info_or_seq_printf(s, "HF = %d%s%s%s%s\n",
			GET_FIELD(WF_TX_DESCRIPTOR_HF, txd[1]),
			GET_FIELD(WF_TX_DESCRIPTOR_HF, txd[1]) == 0 ? "(eth/802.3)" : "",
			GET_FIELD(WF_TX_DESCRIPTOR_HF, txd[1]) == 1 ? "(cmd)" : "",
			GET_FIELD(WF_TX_DESCRIPTOR_HF, txd[1]) == 2 ? "(802.11)" : "",
			GET_FIELD(WF_TX_DESCRIPTOR_HF, txd[1]) == 3 ? "(802.11 enhanced" : "");
	info_or_seq_printf(s, "802.11 HEADER_LENGTH = %d\n",
			GET_FIELD(WF_TX_DESCRIPTOR_HF, txd[1]) == 2 ?
			GET_FIELD(WF_TX_DESCRIPTOR_HEADER_LENGTH, txd[1]) : 0);
	info_or_seq_printf(s, "MRD = %d\n",
			GET_FIELD(WF_TX_DESCRIPTOR_HF, txd[1]) == 0 ?
			GET_FIELD(WF_TX_DESCRIPTOR_MRD, txd[1]) : 0);
	info_or_seq_printf(s, "EOSP = %d\n",
			GET_FIELD(WF_TX_DESCRIPTOR_HF, txd[1]) == 0 ?
			GET_FIELD(WF_TX_DESCRIPTOR_EOSP, txd[1]) : 0);
	info_or_seq_printf(s, "AMS = %d\n",
			GET_FIELD(WF_TX_DESCRIPTOR_HF, txd[1]) == 3 ?
			GET_FIELD(WF_TX_DESCRIPTOR_AMS, txd[1]) : 0);
	info_or_seq_printf(s, "RMVL = %d\n",
			GET_FIELD(WF_TX_DESCRIPTOR_HF, txd[1]) == 0 ?
			GET_FIELD(WF_TX_DESCRIPTOR_RMVL, txd[1]): 0);
	info_or_seq_printf(s, "VLAN = %d\n",
			GET_FIELD(WF_TX_DESCRIPTOR_HF, txd[1]) == 0 ?
			GET_FIELD(WF_TX_DESCRIPTOR_VLAN, txd[1]) : 0);
	info_or_seq_printf(s, "ETYP = %d\n",
			GET_FIELD(WF_TX_DESCRIPTOR_HF, txd[1]) == 0 ?
			GET_FIELD(WF_TX_DESCRIPTOR_ETYP, txd[1]) : 0);
	info_or_seq_printf(s, "TID_MGMT_TYPE = %d\n",
			GET_FIELD(WF_TX_DESCRIPTOR_TID_MGMT_TYPE, txd[1]));
	info_or_seq_printf(s, "OM = %d\n",
			GET_FIELD(WF_TX_DESCRIPTOR_OM, txd[1]));
	info_or_seq_printf(s, "FR = %d\n",
			GET_FIELD(WF_TX_DESCRIPTOR_FR, txd[1]));

	/* dw2 */
	info_or_seq_printf(s, "SUBTYPE = %d%s%s%s%s\n",
			GET_FIELD(WF_TX_DESCRIPTOR_SUBTYPE, txd[2]),
			(GET_FIELD(WF_TX_DESCRIPTOR_FTYPE, txd[2]) == 0) &&
			(GET_FIELD(WF_TX_DESCRIPTOR_SUBTYPE, txd[2]) == 13) ?
			"(action)" : "",
			(GET_FIELD(WF_TX_DESCRIPTOR_FTYPE, txd[2]) == 1) &&
			(GET_FIELD(WF_TX_DESCRIPTOR_SUBTYPE, txd[2]) == 8) ?
			"(bar)" : "",
			(GET_FIELD(WF_TX_DESCRIPTOR_FTYPE, txd[2]) == 2) &&
			(GET_FIELD(WF_TX_DESCRIPTOR_SUBTYPE, txd[2]) == 4) ?
			"(null)" : "",
			(GET_FIELD(WF_TX_DESCRIPTOR_FTYPE, txd[2]) == 2) &&
			(GET_FIELD(WF_TX_DESCRIPTOR_SUBTYPE, txd[2]) == 12) ?
			"(qos null)" : "");

	info_or_seq_printf(s, "FTYPE = %d%s%s%s\n",
			GET_FIELD(WF_TX_DESCRIPTOR_FTYPE, txd[2]),
			GET_FIELD(WF_TX_DESCRIPTOR_FTYPE, txd[2]) == 0 ? "(mgmt)" : "",
			GET_FIELD(WF_TX_DESCRIPTOR_FTYPE, txd[2]) == 1 ? "(ctl)" : "",
			GET_FIELD(WF_TX_DESCRIPTOR_FTYPE, txd[2]) == 2 ? "(data)" : "");
	info_or_seq_printf(s, "BF_TYPE = %d\n",
			GET_FIELD(WF_TX_DESCRIPTOR_BF_TYPE, txd[2]));
	info_or_seq_printf(s, "OM_MAP = %d\n",
			GET_FIELD(WF_TX_DESCRIPTOR_OM_MAP, txd[2]));
	info_or_seq_printf(s, "RTS = %d\n",
			GET_FIELD(WF_TX_DESCRIPTOR_RTS, txd[2]));
	info_or_seq_printf(s, "HEADER_PADDING = %d\n",
			GET_FIELD(WF_TX_DESCRIPTOR_HEADER_PADDING, txd[2]));
	info_or_seq_printf(s, "DU = %d\n",
			GET_FIELD(WF_TX_DESCRIPTOR_DU, txd[2]));
	info_or_seq_printf(s, "HE = %d\n",
			GET_FIELD(WF_TX_DESCRIPTOR_HE, txd[2]));
	info_or_seq_printf(s, "FRAG = %d\n",
			GET_FIELD(WF_TX_DESCRIPTOR_FRAG, txd[2]));
	info_or_seq_printf(s, "REMAINING_TX_TIME = %d\n",
			GET_FIELD(WF_TX_DESCRIPTOR_REMAINING_TX_TIME, txd[2]));
	info_or_seq_printf(s, "POWER_OFFSET = %d\n",
			GET_FIELD(WF_TX_DESCRIPTOR_POWER_OFFSET, txd[2]));

	/* dw3 */
	info_or_seq_printf(s, "NA = %d\n",
			GET_FIELD(WF_TX_DESCRIPTOR_NA, txd[3]));
	info_or_seq_printf(s, "PF = %d\n",
			GET_FIELD(WF_TX_DESCRIPTOR_PF, txd[3]));
	info_or_seq_printf(s, "EMRD = %d\n",
			GET_FIELD(WF_TX_DESCRIPTOR_EMRD, txd[3]));
	info_or_seq_printf(s, "EEOSP = %d\n",
			GET_FIELD(WF_TX_DESCRIPTOR_EEOSP, txd[3]));
	info_or_seq_printf(s, "BM = %d\n",
			GET_FIELD(WF_TX_DESCRIPTOR_BM, txd[3]));
	info_or_seq_printf(s, "HW_AMSDU_CAP = %d\n",
			GET_FIELD(WF_TX_DESCRIPTOR_HW_AMSDU_CAP, txd[3]));
	info_or_seq_printf(s, "TX_COUNT = %d\n",
			GET_FIELD(WF_TX_DESCRIPTOR_TX_COUNT, txd[3]));
	info_or_seq_printf(s, "REMAINING_TX_COUNT = %d\n",
			GET_FIELD(WF_TX_DESCRIPTOR_REMAINING_TX_COUNT, txd[3]));
	info_or_seq_printf(s, "SN = %d\n",
			GET_FIELD(WF_TX_DESCRIPTOR_SN, txd[3]));
	info_or_seq_printf(s, "BA_DIS = %d\n",
			GET_FIELD(WF_TX_DESCRIPTOR_BA_DIS, txd[3]));
	info_or_seq_printf(s, "PM = %d\n",
			GET_FIELD(WF_TX_DESCRIPTOR_PM, txd[3]));
	info_or_seq_printf(s, "PN_VLD = %d\n",
			GET_FIELD(WF_TX_DESCRIPTOR_PN_VLD, txd[3]));
	info_or_seq_printf(s, "SN_VLD = %d\n",
			GET_FIELD(WF_TX_DESCRIPTOR_SN_VLD, txd[3]));

	/* dw4 */
	info_or_seq_printf(s, "PN_31_0 = 0x%x\n",
			GET_FIELD(WF_TX_DESCRIPTOR_PN_31_0_, txd[4]));

	/* dw5 */
	info_or_seq_printf(s, "PID = %d\n",
			GET_FIELD(WF_TX_DESCRIPTOR_PID, txd[5]));
	info_or_seq_printf(s, "TXSFM = %d\n",
			GET_FIELD(WF_TX_DESCRIPTOR_TXSFM, txd[5]));
	info_or_seq_printf(s, "TXS2M = %d\n",
			GET_FIELD(WF_TX_DESCRIPTOR_TXS2M, txd[5]));
	info_or_seq_printf(s, "TXS2H = %d\n",
			GET_FIELD(WF_TX_DESCRIPTOR_TXS2H, txd[5]));
	info_or_seq_printf(s, "FBCZ = %d\n",
			GET_FIELD(WF_TX_DESCRIPTOR_FBCZ, txd[5]));
	info_or_seq_printf(s, "BYPASS_RBB = %d\n",
			GET_FIELD(WF_TX_DESCRIPTOR_BYPASS_RBB, txd[5]));

	info_or_seq_printf(s, "FL = %d\n",
			GET_FIELD(WF_TX_DESCRIPTOR_FL, txd[5]));
	info_or_seq_printf(s, "PN_47_32 = 0x%x\n",
			GET_FIELD(WF_TX_DESCRIPTOR_PN_47_32_, txd[5]));

	/* dw6 */
	info_or_seq_printf(s, "AMSDU_CAP_UTXB = %d\n",
			GET_FIELD(WF_TX_DESCRIPTOR_AMSDU_CAP_UTXB, txd[6]));
	info_or_seq_printf(s, "DAS = %d\n",
			GET_FIELD(WF_TX_DESCRIPTOR_DAS, txd[6]));
	info_or_seq_printf(s, "DIS_MAT = %d\n",
			GET_FIELD(WF_TX_DESCRIPTOR_DIS_MAT, txd[6]));
	info_or_seq_printf(s, "MSDU_COUNT = %d\n",
			GET_FIELD(WF_TX_DESCRIPTOR_MSDU_COUNT, txd[6]));
	info_or_seq_printf(s, "TIMESTAMP_OFFSET = %d\n",
			GET_FIELD(WF_TX_DESCRIPTOR_TIMESTAMP_OFFSET_IDX, txd[6]));
	info_or_seq_printf(s, "FIXED_RATE_IDX = %d\n",
			GET_FIELD(WF_TX_DESCRIPTOR_FIXED_RATE_IDX, txd[6]));
	info_or_seq_printf(s, "BW = %d\n",
			GET_FIELD(WF_TX_DESCRIPTOR_BW, txd[6]));
	info_or_seq_printf(s, "VTA = %d\n",
			GET_FIELD(WF_TX_DESCRIPTOR_VTA, txd[6]));
	info_or_seq_printf(s, "SRC = %d\n",
			GET_FIELD(WF_TX_DESCRIPTOR_SRC, txd[6]));

	/* dw7 */
	info_or_seq_printf(s, "SW_TX_TIME(unit:65536ns) = %d\n",
			GET_FIELD(WF_TX_DESCRIPTOR_SW_TX_TIME , txd[7]));
	info_or_seq_printf(s, "UT = %d\n",
			GET_FIELD(WF_TX_DESCRIPTOR_UT, txd[7]));
	info_or_seq_printf(s, "CTXD_CNT = %d\n",
			GET_FIELD(WF_TX_DESCRIPTOR_CTXD_CNT, txd[7]));
	info_or_seq_printf(s, "HM = %d\n",
			GET_FIELD(WF_TX_DESCRIPTOR_HM, txd[7]));
	info_or_seq_printf(s, "DP = %d\n",
			GET_FIELD(WF_TX_DESCRIPTOR_DP, txd[7]));
	info_or_seq_printf(s, "IP = %d\n",
			GET_FIELD(WF_TX_DESCRIPTOR_IP, txd[7]));
	info_or_seq_printf(s, "TXD_LEN = %d\n",
			GET_FIELD(WF_TX_DESCRIPTOR_TXD_LEN, txd[7]));

	if (dump_txp) {
		__le32 *txp = txd + 8;

		if (is_hif_txd)
			mt7999_dump_bmac_hif_txp_info(s, dev, txp, hif_txp_ver);
		else
			mt7999_dump_bmac_mac_txp_info(s, dev, txp);
	}
}

static int mt7999_ple_fid_read(struct seq_file *s, void *data)
{
	struct mt7999_dev *dev = dev_get_drvdata(s->private);

	mt7999_show_txd(s, dev->dbg.fid_idx);

	return 0;
}

static int mt7999_token_txd_read(struct seq_file *s, void *data)
{
	struct mt7999_dev *dev = dev_get_drvdata(s->private);
	struct mt76_txwi_cache *t;
	u8* txwi;

	seq_printf(s, "\n");
	spin_lock_bh(&dev->mt76.token_lock);

	t = idr_find(&dev->mt76.token, dev->dbg.token_idx);
	if (t != NULL) {
		struct mt76_dev *mdev = &dev->mt76;
		txwi = ((u8*)(t)) - (mdev->drv->txwi_size);
		/* dump one txd info */
		mt7999_dump_bmac_txd_info(s, dev, (__le32 *)txwi, true, true);
		seq_printf(s, "\n");
		seq_printf(s, "[SKB]\n");
		seq_hex_dump(s, "", DUMP_PREFIX_OFFSET, 16, 1, (u8 *)t->skb->data, t->skb->len, false);
		seq_printf(s, "\n");
	}
	spin_unlock_bh(&dev->mt76.token_lock);
	return 0;
}

static int mt7999_rx_msdu_pg_read(struct seq_file *s, void *data)
{
	struct mt7999_dev *dev = dev_get_drvdata(s->private);
	struct list_head *p;
	int i, count = 0, total = 0;

	seq_printf(s, "Rx Msdu page:\n");
	spin_lock(&dev->wed_rro.lock);
	for (i = 0; i < MT7999_RRO_MSDU_PG_HASH_SIZE; i++) {
		list_for_each(p, &dev->wed_rro.page_map[i]) {
			count++;
		}
	}

	total = count;
	list_for_each(p, &dev->wed_rro.page_cache) {
		total++;
	}
	seq_printf(s, "\ttotal:%8d used:%8d\n", total, count);
	spin_unlock(&dev->wed_rro.lock);

	return 0;
}

static int
mt7999_mat_table_show(struct seq_file *s, void *data)
{
#define MT_MAX_MAT_TABLE_SIZE	63
	struct mt7999_dev *dev = s->private;
	int i;

	for (i = 0; i < MT_MAX_MAT_TABLE_SIZE; i++) {
		u32 req = MT_WF_UWTBL_ITCR_SET |
			  u32_encode_bits(i, MT_WF_UWTBL_ITCR_INDEX);
		u32 dw[2];
		u8 *addr = (u8 *)dw;

		mt76_wr(dev, MT_WF_UWTBL_ITCR, req);
		dw[0] = mt76_rr(dev, MT_WF_UWTBL_ITCR0);
		dw[1] = mt76_rr(dev, MT_WF_UWTBL_ITCR1);

		if (dw[0] || dw[1])
			seq_printf(s, "own_mld_id%d\tAddr: %pM\n", i, addr);
	}
	return 0;
}
DEFINE_SHOW_ATTRIBUTE(mt7999_mat_table);

static int
mt7999_rmac_table_show(struct seq_file *s, void *data)
{
	struct mt7999_phy *phy = s->private;
	struct mt7999_dev *dev = phy->dev;
	unsigned long usage_bitmap[2] = {0};
	int i, j;
	u8 band = phy->mt76->band_idx;

	usage_bitmap[0] = (unsigned long)mt76_rr(dev, MT_WF_RMAC_SRAM_BITMAP0(band));
	usage_bitmap[1] = (unsigned long)mt76_rr(dev, MT_WF_RMAC_SRAM_BITMAP1(band));

	for (i = 0; i < 2; i++) {
		for_each_set_bit(j, &usage_bitmap[i], 32) {
			u32 req = MT_WF_RMAC_MEM_CRTL_TRIG |
				  u32_encode_bits(i * 32 + j, MT_WF_RMAC_MEM_CRTL_TDX);
			u32 dw[2];
			u8 *addr = (u8 *)dw;

			mt76_wr(dev, MT_WF_RMAC_MEM_CTRL(band), req);
			dw[0] = mt76_rr(dev, MT_WF_RMAC_SRAM_DATA0(band));
			dw[1] = mt76_rr(dev, MT_WF_RMAC_SRAM_DATA1(band));

			seq_printf(s, "omac_idx%d\tAddr: %pM\n", i * 32 + j, addr);
		}
	}

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(mt7999_rmac_table);

static int
mt7999_agg_table_show(struct seq_file *s, void *data)
{
	struct mt7999_phy *phy = s->private;
	struct mt7999_dev *dev = phy->dev;
	int i, j;
	u8 band = phy->mt76->band_idx;

	for (i = 0; i < 4; i++) {
		u32 value = mt76_rr(dev, MT_AGG_REMAP_CTRL(band) + 4 * i);

		for (j = 0; j < 4; j++) {
			u8 shift = 8 * j;
			u32 mask = MT_AGG_REMAP_CTRL_OM_REMAP << shift;

			seq_printf(s, "idx%d: %d\n", i * 4 + j,
				      (value & mask) >> shift);
		}
	}

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(mt7999_agg_table);

static ssize_t mt7999_mlo_agc_tx_set(struct file *file,
				     const char __user *user_buf,
				     size_t count, loff_t *ppos)
{
	struct mt7999_dev *dev = file->private_data;
	struct mt7999_mlo_agc_set req;
	char buf[100];
	int ret;
	u16 mgf;

	memset(&req, 0, sizeof(req));

	if (count >= sizeof(buf))
		return -EINVAL;

	if (copy_from_user(buf, user_buf, count))
		return -EFAULT;

	if (count && buf[count - 1] == '\n')
		buf[count - 1] = '\0';
	else
		buf[count] = '\0';

	if (sscanf(buf, "%hhu %hhu %hhu %hhu %hu %hhu %hhu",
		   &req.mld_id, &req.link_id, &req.ac, &req.disp_pol,
		   &mgf, &req.ratio, &req.order) != 7) {
		dev_warn(dev->mt76.dev,
			 "format: [MldRecIdx] [Link] [Ac] [DispPol] [MGF] [Ratio] [Order]\n");
		goto out;
	}

	req.tag = cpu_to_le16(UNI_MLO_AGC_TX);
	req.len = cpu_to_le16(sizeof(req) - 4);
	req.mgf = cpu_to_le16(mgf);

	ret = mt7999_mcu_mlo_agc(dev, &req, sizeof(req));
	if (ret)
		return -EFAULT;

out:
	return count;
}

static const struct file_operations fops_mlo_agc_tx = {
	.write = mt7999_mlo_agc_tx_set,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

static ssize_t mt7999_be_txop_set(struct file *file,
			       const char __user *user_buf,
			       size_t count, loff_t *ppos)
{
	struct mt7999_dev *dev = file->private_data;
	char buf[100], role[4];
	u32 ofs;
	u16 txop, decimal;
	int i = 0;

	if (count >= sizeof(buf))
		return -EINVAL;

	if (copy_from_user(buf, user_buf, count))
		return -EFAULT;

	if (count && buf[count - 1] == '\n')
		buf[count - 1] = '\0';
	else
		buf[count] = '\0';

	if (sscanf(buf, "%3s %hu.%hu", role, &txop, &decimal) != 3)
		goto err;

	if (!strncmp(role, "ap", 2))
		ofs = MT_WF_TMAC_WMM0_OFFSET;
	else if (!strncmp(role, "sta", 3))
		ofs = MT_WF_TMAC_WMM3_OFFSET;
	else
		goto err;

	/* Change unit to 32 us */
	txop = (txop * 1000 + decimal * 100 + 16) >> 5;

	for (i = 0; i < __MT_MAX_BAND; i++) {
		if (!dev->mt76.phys[i])
			continue;

		mt76_rmw(dev, MT_WF_TMAC(i, ofs), MT_WF_TMAC_WMM_TXOP_MASK,
			 txop << MT_WF_TMAC_WMM_TXOP_SHIFT);
	}

	return count;
err:
	dev_warn(dev->mt76.dev,
		 "format: [ap|sta] [tx_queue_data2_burst]\n");
	return -EINVAL;
}

static ssize_t mt7999_be_txop_dump(struct file *file, char __user *user_buf,
				size_t count, loff_t *ppos)
{
	struct mt7999_dev *dev = file->private_data;
	static const size_t size = 2048;
	int len = 0, i, ret;
	char *buf;
	enum {
		AP,
		STA,
		MAX_IF_TYPE,
	};

	buf = kzalloc(size, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	len += scnprintf(buf + len, size - len, "Band\tAP (WMM0)\t\tSTA (WMM3)\n");

	for (i = 0; i < __MT_MAX_BAND; i++) {
		u32 txop[MAX_IF_TYPE], tx_burst[MAX_IF_TYPE];

		if (!dev->mt76.phys[i])
			continue;

#define MT7999_READ_TXOP(role, base)						\
do {										\
	txop[role] = mt76_rr(dev, MT_WF_TMAC(i, base));				\
	tx_burst[role] = u32_get_bits(txop[role], MT_WF_TMAC_WMM_TXOP_MASK);	\
	tx_burst[role] = tx_burst[role] ? ((tx_burst[role] << 5) - 16) / 100 : 0;\
} while (0)
		MT7999_READ_TXOP(AP, MT_WF_TMAC_WMM0_OFFSET);
		MT7999_READ_TXOP(STA, MT_WF_TMAC_WMM3_OFFSET);
#undef MT7999_READ_TXOP

		len += scnprintf(buf + len, size - len,
			"%d\t0x%08x (%1u.%1u) \t0x%08x (%1u.%1u)\n", i,
			txop[AP], tx_burst[AP] / 10, tx_burst[AP] % 10,
			txop[STA], tx_burst[STA] / 10, tx_burst[STA] % 10);
	}
	ret = simple_read_from_buffer(user_buf, count, ppos, buf, len);

	kfree(buf);
	return ret;
}

static const struct file_operations fops_mt7999_txop = {
	.write = mt7999_be_txop_set,
	.read = mt7999_be_txop_dump,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

static ssize_t mt7999_pbc_lower_bound_set(struct file *file,
					  const char __user *user_buf,
					  size_t count, loff_t *ppos)
{
	struct mt7999_dev *dev = file->private_data;
	char buf[100];
	u16 val[5];

	if (count >= sizeof(buf))
		return -EINVAL;

	if (copy_from_user(buf, user_buf, count))
		return -EFAULT;

	if (count && buf[count - 1] == '\n')
		buf[count - 1] = '\0';
	else
		buf[count] = '\0';

	if (sscanf(buf, "%hu %hu %hu %hu %hu",
		   &val[0], &val[1], &val[2], &val[3], &val[4]) != 5)
		goto err;

	memcpy(&dev->pbc_bound, &val, sizeof(dev->pbc_bound));

	return count;
err:
	dev_warn(dev->mt76.dev,
		 "format: [BK] [BE] [VI] [VO] [Mgmt]\n");
	return -EINVAL;
}

static ssize_t mt7999_pbc_lower_bound_dump(struct file *file,
					   char __user *user_buf,
					   size_t count, loff_t *ppos)
{
	struct mt7999_dev *dev = file->private_data;
	static const size_t size = 64;
	int len = 0, ret;
	char *buf;

	buf = kzalloc(size, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	len += scnprintf(buf + len, size - len, "pbc_bound = %d %d %d %d %d\n",
			 dev->pbc_bound[0], dev->pbc_bound[1], dev->pbc_bound[2],
			 dev->pbc_bound[3], dev->pbc_bound[4]);
	ret = simple_read_from_buffer(user_buf, count, ppos, buf, len);

	kfree(buf);
	return ret;
}

static const struct file_operations fops_mt7999_pbc_lower_bound = {
	.write = mt7999_pbc_lower_bound_set,
	.read = mt7999_pbc_lower_bound_dump,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

static ssize_t mt7999_mlo_agc_trig_set(struct file *file,
				       const char __user *user_buf,
				       size_t count, loff_t *ppos)
{
	struct mt7999_dev *dev = file->private_data;
	struct mt7999_mlo_agc_set req;
	char buf[100];
	int ret;
	u16 mgf;

	memset(&req, 0, sizeof(req));

	if (count >= sizeof(buf))
		return -EINVAL;

	if (copy_from_user(buf, user_buf, count))
		return -EFAULT;

	if (count && buf[count - 1] == '\n')
		buf[count - 1] = '\0';
	else
		buf[count] = '\0';

	if (sscanf(buf, "%hhu %hhu %hhu %hhu %hu %hhu",
		   &req.mld_id, &req.link_id, &req.ac, &req.disp_pol,
		   &mgf, &req.ratio) != 6) {
		dev_warn(dev->mt76.dev,
			 "format: [MldRecIdx] [Link] [Ac] [DispPol] [MGF] [Ratio]\n");
		goto out;
	}

	req.tag = cpu_to_le16(UNI_MLO_AGC_TRIG);
	req.len = cpu_to_le16(sizeof(req) - 4);
	req.mgf = cpu_to_le16(mgf);

	ret = mt7999_mcu_mlo_agc(dev, &req, sizeof(req));
	if (ret)
		return -EFAULT;

out:
	return count;
}

static const struct file_operations fops_mlo_agc_trig = {
	.write = mt7999_mlo_agc_trig_set,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

static int
mt7999_sr_pp_enable_get(void *data, u64 *val)
{
	struct mt7999_dev *dev = data;

	*val = dev->sr_pp_enable;

	return 0;
}
static int
mt7999_sr_pp_enable_set(void *data, u64 val)
{
	struct mt7999_dev *dev = data;
	int ret;
	bool en = !!val;

	if (en == dev->sr_pp_enable)
		return 0;

	ret = mt7999_mcu_set_sr_pp_en(dev, en);
	if (ret)
		return ret;

	dev->sr_pp_enable = en;

	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(fops_sr_pp_enable, mt7999_sr_pp_enable_get,
			 mt7999_sr_pp_enable_set, "%lld\n");

static int
mt7999_pcie_l1ss_enable_get(void *data, u64 *val)
{
	struct mt7999_dev *dev = data;

	*val = dev->dbg.lp.pcie_l1ss_enable;

	return 0;
}
static int
mt7999_pcie_l1ss_enable_set(void *data, u64 val)
{
	struct mt7999_dev *dev = data;

	mt7999_set_pcie_l1ss(dev, !!val);

	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(fops_pcie_l1ss_enable, mt7999_pcie_l1ss_enable_get,
			 mt7999_pcie_l1ss_enable_set, "%lld\n");

static ssize_t mt7999_tpt_option_set(struct file *file,
				     const char __user *user_buf,
				     size_t count, loff_t *ppos)
{
	struct mt7999_dev *dev = file->private_data;
	int ret = count, mcu_ret;
	u8 type, val;
	char *buf;

	buf = kzalloc(count + 1, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	if (copy_from_user(buf, user_buf, count)) {
		ret = -EFAULT;
		goto out;
	}

	if (count && buf[count - 1] == '\n')
		buf[count - 1] = '\0';
	else
		buf[count] = '\0';

	if (sscanf(buf, "%hhu %hhu", &type, &val) != 2 || type > 4 || val > 7) {
		dev_warn(dev->mt76.dev, "format: type val\n");
		goto out;
	}

	mcu_ret = mt7999_mcu_set_tpo(dev, type, val);
	ret = mcu_ret ? mcu_ret : ret;
out:
	kfree(buf);
	return ret;
}

static const struct file_operations fops_tpt_option = {
	.write = mt7999_tpt_option_set,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

static ssize_t mt7999_lp_option_set(struct file *file,
				   const char __user *user_buf,
				   size_t count, loff_t *ppos)
{
	struct mt7999_dev *dev = file->private_data;
	int ret = count, mcu_ret;
	char *buf;
	u8 arg[4];

	buf = kzalloc(count + 1, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	if (copy_from_user(buf, user_buf, count)) {
		ret = -EFAULT;
		goto out;
	}

	if (count && buf[count - 1] == '\n')
		buf[count - 1] = '\0';
	else
		buf[count] = '\0';

	if (sscanf(buf, "%hhu %hhu %hhu %hhu",
		   &arg[0], &arg[1], &arg[2], &arg[3]) != 4) {
		dev_warn(dev->mt76.dev, "format: arg0 arg1 arg2 arg3\n");
		goto out;
	}

	mcu_ret = mt7999_mcu_set_lp_option(dev, arg);
	ret = mcu_ret ? mcu_ret : ret;
out:
	kfree(buf);
	return ret;
}

static const struct file_operations fops_lp_option = {
	.write = mt7999_lp_option_set,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

static ssize_t mt7999_muru_low_pwr_set(struct file *file,
				       const char __user *user_buf,
				       size_t count, loff_t *ppos)
{
	struct mt7999_dev *dev = file->private_data;
	int ret = count, mcu_ret;
	u32 band, cmd, val;
	char *buf;

	buf = kzalloc(count + 1, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	if (copy_from_user(buf, user_buf, count)) {
		ret = -EFAULT;
		goto out;
	}

	if (count && buf[count - 1] == '\n')
		buf[count - 1] = '\0';
	else
		buf[count] = '\0';

	if (sscanf(buf, "%u %u %u", &band, &cmd, &val) != 3) {
		dev_warn(dev->mt76.dev, "format: band command value\n");
		goto out;
	}

	mcu_ret = mt7999_mcu_set_pst(dev, band, cmd, val);
	ret = mcu_ret ? mcu_ret : ret;
out:
	kfree(buf);
	return ret;
}

static const struct file_operations fops_muru_low_pwr = {
	.write = mt7999_muru_low_pwr_set,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

static int
mt7999_low_power_info_read(struct seq_file *s, void *data)
{
	struct mt7999_dev *dev = dev_get_drvdata(s->private);

#define _pr(_str, _cond) seq_printf(s, "%s %s\n", _str, \
				    dev->dbg.lp._cond ? "enable" : "disable");

	seq_printf(s, "Low Power Info:\n");

	_pr("PCIe ASPM:", pcie_l1ss_enable);

	seq_printf(s, "TPO: %d\n", dev->dbg.lp.tpo);
	seq_printf(s, "\tPB-TPO: %d\n", dev->dbg.lp.pb_tpo);
	seq_printf(s, "\tLP-TPO: %d \n", dev->dbg.lp.lp_tpo);
	seq_printf(s, "\tMinTx-TPO: %d \n", dev->dbg.lp.min_tx_tpo);

	_pr("Ultra Save:", ultra_save);
	_pr("\t1RPD:", one_rpd);
	_pr("\tMMPS:", mmps);
	_pr("\tMDPC:", mdpc);
	_pr("\tDCM:", dcm);
	_pr("\tALPL:", alpl);

	seq_printf(s, "PST band 0: %s\n",
		   dev->dbg.lp.pst & BIT(MT_BAND0) ? "enable" : "disable");
	seq_printf(s, "PST band 1: %s\n",
		   dev->dbg.lp.pst & BIT(MT_BAND1) ? "enable" : "disable");
	seq_printf(s, "PST band 2: %s\n",
		   dev->dbg.lp.pst & BIT(MT_BAND2) ? "enable" : "disable");
#undef _pr

	return 0;
}

static int
mt7999_mru_probe_enable_get(void *data, u64 *val)
{
	struct mt7999_phy *phy = data;

	*val = phy->mru_probe_enable;

	return 0;
}
static int
mt7999_mru_probe_enable_set(void *data, u64 val)
{
#define MRU_PROBE_ENABLE 1
	struct mt7999_phy *phy = data;
	int ret;
	bool en = !!val;

	if (en == phy->mru_probe_enable)
		return 0;

	if (en != MRU_PROBE_ENABLE)
		return 0;

	ret = mt7999_mcu_set_mru_probe_en(phy);
	if (ret)
		return ret;

	phy->mru_probe_enable = en;
	/* When enabling MRU probe, PP would also enter FW mode */
	phy->pp_mode = PP_FW_MODE;

	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(fops_mru_probe_enable, mt7999_mru_probe_enable_get,
			 mt7999_mru_probe_enable_set, "%lld\n");

static int
mt7999_cp_mode_set(void *data, u64 val)
{
	struct mt7999_dev *dev = data;

	if (val > IEEE80211_AC_BK)
		return -EINVAL;

	return mt7999_mcu_cp_support(dev, val);
}

DEFINE_DEBUGFS_ATTRIBUTE(fops_cp_mode, NULL,
			 mt7999_cp_mode_set, "%llu\n");

static int
mt7999_fw_time_sync_set(void *data, u64 val)
{
	struct mt7999_dev *dev = data;

	return mt7999_mcu_fw_time_sync(&dev->mt76);
}

DEFINE_DEBUGFS_ATTRIBUTE(fops_fw_time_sync, NULL,
			 mt7999_fw_time_sync_set, "%llu\n");

static ssize_t mt7999_ple_flush_sta_set(struct file *file,
					const char __user *user_buf,
					size_t count, loff_t *ppos)
{
	struct mt7999_dev *dev = file->private_data;
	struct mt76_wcid *wcid;
	char buf[100];
	u16 idx, threshold;
	u8 sta_bitmap[MT7999_WTBL_GROUP_SIZE];

	memset(&sta_bitmap, 0, sizeof(sta_bitmap));

	if (count >= sizeof(buf))
		return -EINVAL;

	if (copy_from_user(buf, user_buf, count))
		return -EFAULT;

	if (count && buf[count - 1] == '\n')
		buf[count - 1] = '\0';
	else
		buf[count] = '\0';

	if (sscanf(buf, "%hu %hu", &idx, &threshold) != 2)
		goto out;

	if (idx >= mt7999_wtbl_size(dev) || threshold > dev->mt76.token_size)
		goto out;

	rcu_read_lock();
	wcid = rcu_dereference(dev->mt76.wcid[idx]);
	if (wcid)
		wcid->token_flush_count++;
	rcu_read_unlock();

	sta_bitmap[idx >> 3] |= BIT(idx % 8);
	mt7999_mcu_flush_ple(dev, threshold, sta_bitmap, 1);

	return count;
out:
	dev_warn(dev->mt76.dev, "format: [wcid] [threshold]\n");
	return -EINVAL;
}

static const struct file_operations fops_ple_flush_sta = {
	.write = mt7999_ple_flush_sta_set,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

void mt7999_mtk_init_band_debugfs(struct mt7999_phy *phy, struct dentry *dir)
{
	/* agg */
	debugfs_create_file("agginfo", 0400, dir, phy, &mt7999_agginfo_fops);
	debugfs_create_file("mibinfo", 0400, dir, phy, &mt7999_mibinfo_fops);
	debugfs_create_file("txpower_level", 0600, dir, phy, &fops_txpower_level);
	debugfs_create_file("txpower_info", 0600, dir, phy, &mt7999_txpower_info_fops);
	debugfs_create_file("txpower_sku", 0600, dir, phy, &mt7999_txpower_sku_fops);
	debugfs_create_file("txpower_path", 0600, dir, phy, &mt7999_txpower_path_fops);

	debugfs_create_file("sr_enable", 0600, dir, phy, &fops_sr_enable);
	debugfs_create_file("sr_enhanced_enable", 0600, dir, phy, &fops_sr_enhanced_enable);
	debugfs_create_file("sr_stats", 0400, dir, phy, &mt7999_sr_stats_fops);
	debugfs_create_file("sr_scene_cond", 0400, dir, phy, &mt7999_sr_scene_cond_fops);

	debugfs_create_file("bf_txsnd_info", 0600, dir, phy, &fops_bf_txsnd_info);
	debugfs_create_file("bf_starec_read", 0600, dir, phy, &fops_starec_bf_read);
	debugfs_create_file("bf_fbk_rpt", 0600, dir, phy, &fops_bf_fbk_rpt);
	debugfs_create_file("pfmu_tag_read", 0600, dir, phy, &fops_bf_pfmu_tag_read);

	debugfs_create_file("thermal_enable", 0600, dir, phy, &fops_thermal_enable);
	debugfs_create_file("thermal_recal", 0200, dir, phy, &fops_thermal_recal);
	debugfs_create_file("scs_enable", 0200, dir, phy, &fops_scs_enable);

	debugfs_create_file("pp_alg", 0200, dir, phy, &mt7999_pp_alg_fops);

	debugfs_create_file("rmac_table", 0400, dir, phy, &mt7999_rmac_table_fops);
	debugfs_create_file("agg_table", 0400, dir, phy, &mt7999_agg_table_fops);

	debugfs_create_file("mru_probe_enable", 0600, dir, phy, &fops_mru_probe_enable);
}

void mt7999_mtk_init_dev_debugfs(struct mt7999_dev *dev, struct dentry *dir)
{
	u32 device_id = (dev->mt76.rev) >> 16;
	int i = 0;
	static const struct mt7999_dbg_reg_desc dbg_reg_s[] = {
		{ MT7999_DEVICE_ID, mt7999_dbg_offs },
	};

	for (i = 0; i < ARRAY_SIZE(dbg_reg_s); i++) {
		if (device_id == dbg_reg_s[i].id) {
			dev->dbg_reg = &dbg_reg_s[i];
			break;
		}
	}

	/* amsdu */
	debugfs_create_devm_seqfile(dev->mt76.dev, "amsdu_info", dir,
				    mt7999_amsdu_result_read);

	debugfs_create_file("fw_debug_module", 0600, dir, dev,
			    &fops_fw_debug_module);
	debugfs_create_file("fw_debug_level", 0600, dir, dev,
			    &fops_fw_debug_level);
	debugfs_create_file("fw_wa_query", 0600, dir, dev, &fops_wa_query);
	debugfs_create_file("fw_wa_set", 0600, dir, dev, &fops_wa_set);
	debugfs_create_devm_seqfile(dev->mt76.dev, "fw_version", dir,
				    mt7999_dump_version);
	debugfs_create_devm_seqfile(dev->mt76.dev, "fw_wm_info", dir,
				    mt7999_fw_wm_info_read);
	debugfs_create_file("fw_time_sync", 0200, dir, dev, &fops_fw_time_sync);

	debugfs_create_devm_seqfile(dev->mt76.dev, "sta_info", dir,
				    mt7999_sta_info);

	debugfs_create_devm_seqfile(dev->mt76.dev, "tr_info", dir,
				    mt7999_trinfo_read);

	debugfs_create_devm_seqfile(dev->mt76.dev, "eeprom_mode", dir,
				    mt7999_show_eeprom_mode);

	debugfs_create_devm_seqfile(dev->mt76.dev, "wtbl_info", dir,
				    mt7999_wtbl_read);
	debugfs_create_bool("wtbl_query_cr", 0600, dir, &dev->wtbl_query_cr);
	debugfs_create_bool("ple_query_cr", 0600, dir, &dev->ple_query_cr);

	debugfs_create_devm_seqfile(dev->mt76.dev, "token", dir, mt7999_token_read);
	debugfs_create_file("red", 0600, dir, dev, &fops_red_config);
	debugfs_create_file("vow_drr_dbg", 0200, dir, dev, &fops_vow_drr_dbg);

	debugfs_create_bool("beacon_loss_keep_conn", 0600, dir,
			    &dev->dbg.beacon_loss_keep_conn);
	debugfs_create_bool("ignore_radar_event", 0600, dir,
			    &dev->dbg.ignore_radar_event);

	debugfs_create_file("muru_prot_thr", 0200, dir, dev, &fops_muru_prot_thr);
	debugfs_create_file("muru_fixed_rate_enable", 0600, dir, dev,
			    &fops_muru_fixed_rate_enable);
	debugfs_create_file("muru_fixed_group_rate", 0600, dir, dev,
			    &fops_muru_fixed_group_rate);

	if (mt7999_has_hwrro(dev)) {
		debugfs_create_u32("rro_sid", 0600, dir, &dev->dbg.sid);
		debugfs_create_devm_seqfile(dev->mt76.dev, "rro_sid_info", dir,
					    mt7999_rro_session_read);
		debugfs_create_devm_seqfile(dev->mt76.dev, "rro_mib", dir,
					    mt7999_show_rro_mib);
	}

	debugfs_create_file("reset_counter", 0200, dir, dev, &fops_reset_counter);

	debugfs_create_devm_seqfile(dev->mt76.dev, "drr_info", dir,
				    mt7999_drr_info);

	debugfs_create_u32("token_idx", 0600, dir, &dev->dbg.token_idx);
	debugfs_create_devm_seqfile(dev->mt76.dev, "rx_token", dir,
				    mt7999_rx_token_read);

	debugfs_create_devm_seqfile(dev->mt76.dev, "ple_info", dir,
				    mt7999_pleinfo_read);
	debugfs_create_devm_seqfile(dev->mt76.dev, "pse_info", dir,
				    mt7999_pseinfo_read);
	/* amsdu */
	debugfs_create_file("amsdu_algo", 0600, dir, dev, &fops_amsdu_algo);
	debugfs_create_file("amsdu_para", 0600, dir, dev, &fops_amsdu_para);

	/* mesh HW amsdu control */
	debugfs_create_file("mesh_hw_amsdu_ctrl", 0600, dir, dev, &fops_mesh_hw_amsdu_ctrl);

	/* Drop counters */
	debugfs_create_file("tx_drop_stats", 0400, dir, dev, &mt7999_tx_drop_fops);
	debugfs_create_file("rx_drop_stats", 0400, dir, dev, &mt7999_rx_drop_fops);

	debugfs_create_file("muru_dbg", 0200, dir, dev, &fops_muru_dbg_info);
	debugfs_create_bool("mgmt_pwr_enhance", 0600, dir, &dev->mt76.mgmt_pwr_enhance);
	debugfs_create_file("afc_table", 0200, dir, dev, &mt7999_afc_table_fops);

	debugfs_create_devm_seqfile(dev->mt76.dev, "token_txd", dir,
				    mt7999_token_txd_read);
	debugfs_create_devm_seqfile(dev->mt76.dev, "rx_msdu_pg", dir,
				    mt7999_rx_msdu_pg_read);

	/* ple/pse fid raw data dump */
	debugfs_create_u32("fid_idx", 0600, dir, &dev->dbg.fid_idx);
	debugfs_create_devm_seqfile(dev->mt76.dev, "ple_fid", dir,
				    mt7999_ple_fid_read);

	debugfs_create_u8("dump_ple_txd", 0600, dir, &dev->dbg.dump_ple_txd);
	debugfs_create_file("txop", 0600, dir, dev, &fops_mt7999_txop);
	debugfs_create_file("cp_mode", 0200, dir, dev, &fops_cp_mode);

	/* MLO related Table */
	debugfs_create_file("mat_table", 0400, dir, dev, &mt7999_mat_table_fops);
	debugfs_create_file("mlo_agc_tx", 0200, dir, dev, &fops_mlo_agc_tx);
	debugfs_create_file("mlo_agc_trig", 0200, dir, dev, &fops_mlo_agc_trig);
	debugfs_create_file("pbc_lower_bound", 0600, dir, dev,
			     &fops_mt7999_pbc_lower_bound);

	debugfs_create_file("sr_pp_enable", 0600, dir, dev, &fops_sr_pp_enable);

	/* Low power related */
	debugfs_create_file("pci_l1ss", 0600, dir, dev, &fops_pcie_l1ss_enable);
	debugfs_create_file("tpt_option", 0200, dir, dev, &fops_tpt_option);
	debugfs_create_file("lp_option", 0200, dir, dev, &fops_lp_option);
	debugfs_create_file("muru_low_pwr", 0200, dir, dev, &fops_muru_low_pwr);
	debugfs_create_devm_seqfile(dev->mt76.dev, "low_power_info",
				    dir, mt7999_low_power_info_read);

	debugfs_create_file("ple_flush_sta", 0200, dir, dev,
			    &fops_ple_flush_sta);
}

#endif
