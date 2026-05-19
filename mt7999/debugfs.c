// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (C) 2026 MediaTek Inc.
 */

#include <linux/relay.h>
#include "mt7999.h"
#include "eeprom.h"
#include "mcu.h"
#include "mac.h"

#define FW_BIN_LOG_MAGIC	0x44d9c99a
static ssize_t
mt7999_sku_filename_set(struct file *file, const char __user *user_buf,
		    size_t count, loff_t *ppos)
{
	struct mt7999_phy *phy = file->private_data;
	struct mt76_phy *mphy = phy->mt76;
	char buf[SKU_FILENAME_MAX];
	ssize_t ret;
	bool en;
	s8 delta, limit;

	/* It’s possible that the user sets power without setting AP channel */
	if (!cfg80211_chandef_valid(&mphy->chandef) ||
	    !cfg80211_chandef_valid(&mphy->main_chandef))
		return -EFAULT;

	if (count >= sizeof(buf))
		return -EINVAL;

	if (copy_from_user(buf, user_buf, count))
		return -EFAULT;

	if (count && buf[count - 1] == '\n')
		buf[count - 1] = '\0';
	else
		buf[count] = '\0';

	if (buf[0])
		snprintf(mphy->sku_filename, SKU_FILENAME_MAX, "%s", buf);
	else
		mphy->sku_filename[0] = '\0';

	en = !!buf[0];

	mt7999_init_txpower(phy);
	if (!en)
		mt76_phy_dbg(mphy, "Clear sku_filename to disable the sku!");

	phy->sku_path_en = en;
	phy->sku_limit_en = en;

	ret = mt7999_mcu_set_tx_power_ctrl(phy, UNI_TXPOWER_SKU_POWER_LIMIT_CTRL,
					   phy->sku_limit_en);
	if (ret)
		goto error;

	ret = mt7999_mcu_set_tx_power_ctrl(phy, UNI_TXPOWER_BACKOFF_POWER_LIMIT_CTRL,
					   phy->sku_path_en);
	if (ret)
		goto error;

	limit = mphy->main_chandef.chan->max_power;

	delta = mt76_tx_power_path_delta(hweight16(mphy->chainmask));
	delta = DIV_ROUND_UP(mphy->txpower_cur + delta, 2);
	limit = min_t(s8, delta, limit);

	ret = mt7999_mcu_set_txpower_sku(phy, limit);
	if (ret)
		goto error;

	return count;

error:
	mphy->sku_filename[0] = '\0';
	phy->sku_path_en = false;
	phy->sku_limit_en = false;
	return ret;
}

static ssize_t
mt7999_sku_filename_get(struct file *file, char __user *user_buf,
		    size_t count, loff_t *ppos)
{
	struct mt7999_phy *phy = file->private_data;
	struct mt76_phy *mphy = phy->mt76;
	char buf[SKU_FILENAME_MAX + 8];
	int len;

	len = snprintf(buf, sizeof(buf), "%s\n",
		       mphy->sku_filename[0] ? mphy->sku_filename : "");

	return simple_read_from_buffer(user_buf, count, ppos, buf, len);
}

static const struct file_operations fops_mt7999_sku_filename = {
	.write = mt7999_sku_filename_set,
	.read = mt7999_sku_filename_get,
	.open = simple_open,
	.llseek = default_llseek,
};

/* UHR MCS translation table for rate adaptation index
 * 0xff indicates unsupported MCS
 */
static const u8 mt7999_uhr_mcs_trans_ra_index[24] = {
	0, 1, 3, 4, 6, 8, 9, 10, 12, 13,
	14, 15, 16, 17, 0xff, 0xff, 0xff, 2, 0xff, 5, 7, 0xff, 0xff, 11
};

static int
mt7999_validate_rate_params(struct mt7999_dev *dev, u8 phy_mode, u8 *mcs)
{
	enum {
		RA_PHY_CCK,
		RA_PHY_OFDM,
		RA_PHY_HT,
		RA_PHY_VHT,
		RA_PHY_PLR,
		RA_PHY_HE,
		RA_PHY_EHT,
		RA_PHY_UHR,
		RA_PHY_ELR,
		__RA_PHY_MAX,
	};
	static const u8 mcs_limits[] = {
		[RA_PHY_CCK] = 3,
		[RA_PHY_OFDM] = 7,
		[RA_PHY_HT] = 9,
		[RA_PHY_VHT] = 11,
		[RA_PHY_HE] = 11,
		[RA_PHY_EHT] = 13,
		[RA_PHY_ELR] = 1,
	};

	if (phy_mode >= __RA_PHY_MAX) {
		dev_err(dev->mt76.dev, "Invalid phy mode %u\n", phy_mode);
		return -EINVAL;
	}

	switch (phy_mode) {
	case RA_PHY_UHR:
		if (*mcs >= ARRAY_SIZE(mt7999_uhr_mcs_trans_ra_index)) {
			dev_err(dev->mt76.dev, "Exceed mcs index %u\n", *mcs);
			return -EINVAL;
		}

		if (mt7999_uhr_mcs_trans_ra_index[*mcs] == 0xff) {
			dev_err(dev->mt76.dev, "Invalid mcs index %u\n", *mcs);
			return -EINVAL;
		}

		*mcs = mt7999_uhr_mcs_trans_ra_index[*mcs];
		break;
	case RA_PHY_PLR:
		if (*mcs != 1) {
			dev_err(dev->mt76.dev, "PLR mode only supports MCS 1 (got %u)\n", *mcs);
			return -EINVAL;
		}
		break;
	default:
		if (*mcs > mcs_limits[phy_mode]) {
			dev_err(dev->mt76.dev, "Unknown MCS %u for PHY mode %u\n",
				*mcs, phy_mode);
			return -EINVAL;
		}
	}

	return 0;
}


/** global debugfs **/

static const char * const mt76_wcid_flag_names[] = {
#define FLAG(F) [MT_WCID_FLAG_##F] = #F
	FLAG(CHECK_PS),
	FLAG(PS),
	FLAG(4ADDR),
	FLAG(HDR_TRANS),
	FLAG(TDLS_PEER),
#undef FLAG
};

struct hw_queue_map {
	const char *name;
	u8 index;
	u8 pid;
	u8 qid;
};

static int
mt7999_implicit_txbf_set(void *data, u64 val)
{
	struct mt7999_dev *dev = data;

	/* The existing connected stations shall reconnect to apply
	 * new implicit txbf configuration.
	 */
	dev->ibf = !!val;

	return mt7999_mcu_set_txbf(dev, UNI_BF_HW_EN_UPDATE);
}

static int
mt7999_implicit_txbf_get(void *data, u64 *val)
{
	struct mt7999_dev *dev = data;

	*val = dev->ibf;

	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(fops_implicit_txbf, mt7999_implicit_txbf_get,
			 mt7999_implicit_txbf_set, "%lld\n");

/* test knob of system error recovery */
static ssize_t
mt7999_sys_recovery_set(struct file *file, const char __user *user_buf,
			size_t count, loff_t *ppos)
{
#define	SER_SET_TRACKING	BIT(0)
	struct mt7999_phy *phy = file->private_data;
	struct mt7999_dev *dev = phy->dev;
	bool band = phy->mt76->band_idx;
	char buf[16];
	int ret = 0;
	u16 val;

	if (count >= sizeof(buf))
		return -EINVAL;

	if (copy_from_user(buf, user_buf, count))
		return -EFAULT;

	if (count && buf[count - 1] == '\n')
		buf[count - 1] = '\0';
	else
		buf[count] = '\0';

	if (kstrtou16(buf, 0, &val))
		return -EINVAL;

	switch (val) {
	case SER_USER_CMD_QUERY:
		ret = mt7999_mcu_set_ser(dev, UNI_SER_QUERY, 0, band);
		break;
	case SER_USER_CMD_L1_RECOVERY:
	case SER_USER_CMD_L2_RECOVERY:
	case SER_USER_CMD_L3_RX_ABORT:
	case SER_USER_CMD_L3_TX_ABORT:
	case SER_USER_CMD_L3_TX_DISABLE:
	case SER_USER_CMD_L3_BF_RECOVERY:
	case SER_USER_CMD_L4_MDP_RECOVERY:
		ret = mt7999_mcu_set_ser(dev, UNI_SER_SET, BIT(val), band);
		if (ret)
			return ret;

		ret = mt7999_mcu_set_ser(dev, UNI_SER_TRIGGER, val, band);
		break;

	/* enable full chip reset */
	case SER_USER_CMD_FULL_RECOVERY:
		mt76_set(dev, MT_WFDMA0_MCU_HOST_INT_ENA, MT_MCU_CMD_WDT_MASK);
		dev->recovery.state |= MT_MCU_CMD_WM_WDT;
		mt7999_reset(dev);
		break;

	/* WARNING: trigger firmware crash */
	case SER_USER_CMD_SET_SYSTEM_ASSERT:
		// trigger wm assert exception
		mt76_wr(dev, MT_WM_CIRQ_MASK_CLR, MT_WM_TRIGGER_COREDUMP);
		mt76_wr(dev, MT_WM_CIRQ_SOFTIRQ, MT_WM_TRIGGER_COREDUMP);
		break;
	case SER_USER_CMD_FW_COREDUMP_WM:
		mt7999_coredump(dev, MT7999_COREDUMP_MANUAL_WM);
		break;
	case SER_USER_CMD_SET_HW_BIT_DETECT_ONLY:
		ret = mt7999_mcu_set_ser(dev, UNI_SER_SET, SER_SET_TRACKING, band);
		if (ret)
			return ret;
		break;
	default:
		break;
	}

	return ret ? ret : count;
}

static ssize_t
mt7999_sys_recovery_get(struct file *file, char __user *user_buf,
			size_t count, loff_t *ppos)
{
	struct mt7999_phy *phy = file->private_data;
	struct mt7999_dev *dev = phy->dev;
	char *buff;
	int desc = 0;
	ssize_t ret;
	static const size_t bufsz = 1536;

	buff = kmalloc(bufsz, GFP_KERNEL);
	if (!buff)
		return -ENOMEM;

	/* HELP */
	desc += scnprintf(buff + desc, bufsz - desc,
			  "Please echo the correct value ...\n");
	desc += scnprintf(buff + desc, bufsz - desc,
			  "%2d: grab firmware transient SER state\n",
			  SER_USER_CMD_QUERY);
	desc += scnprintf(buff + desc, bufsz - desc,
			  "%2d: trigger system error L1 recovery\n",
			  SER_USER_CMD_L1_RECOVERY);
	desc += scnprintf(buff + desc, bufsz - desc,
			  "%2d: trigger system error L2 recovery\n",
			  SER_USER_CMD_L2_RECOVERY);
	desc += scnprintf(buff + desc, bufsz - desc,
			  "%2d: trigger system error L3 rx abort\n",
			  SER_USER_CMD_L3_RX_ABORT);
	desc += scnprintf(buff + desc, bufsz - desc,
			  "%2d: trigger system error L3 tx abort\n",
			  SER_USER_CMD_L3_TX_ABORT);
	desc += scnprintf(buff + desc, bufsz - desc,
			  "%2d: trigger system error L3 tx disable\n",
			  SER_USER_CMD_L3_TX_DISABLE);
	desc += scnprintf(buff + desc, bufsz - desc,
			  "%2d: trigger system error L3 bf recovery\n",
			  SER_USER_CMD_L3_BF_RECOVERY);
	desc += scnprintf(buff + desc, bufsz - desc,
			  "%2d: trigger system error L4 mdp recovery\n",
			  SER_USER_CMD_L4_MDP_RECOVERY);
	desc += scnprintf(buff + desc, bufsz - desc,
			  "%2d: trigger system error full recovery\n",
			  SER_USER_CMD_FULL_RECOVERY);
	desc += scnprintf(buff + desc, bufsz - desc,
			  "%2d: trigger firmware crash\n",
			  SER_USER_CMD_SET_SYSTEM_ASSERT);
	desc += scnprintf(buff + desc, bufsz - desc,
			  "%2d: trigger grab wm firmware coredump\n",
			  SER_USER_CMD_FW_COREDUMP_WM);
	desc += scnprintf(buff + desc, bufsz - desc,
			  "%2d: hw bit detect only\n",
			  SER_USER_CMD_SET_HW_BIT_DETECT_ONLY);
	/* SER statistics */
	desc += scnprintf(buff + desc, bufsz - desc,
			  "\nlet's dump firmware SER statistics...\n");
	desc += scnprintf(buff + desc, bufsz - desc,
			  "::E  R , SER_STATUS        = 0x%08x\n",
			  mt76_rr(dev, MT_SWDEF_SER_STATS));
	desc += scnprintf(buff + desc, bufsz - desc,
			  "::E  R , SER_PLE_ERR       = 0x%08x\n",
			  mt76_rr(dev, MT_SWDEF_PLE_STATS));
	desc += scnprintf(buff + desc, bufsz - desc,
			  "::E  R , SER_PLE_ERR_1     = 0x%08x\n",
			  mt76_rr(dev, MT_SWDEF_PLE1_STATS));
	desc += scnprintf(buff + desc, bufsz - desc,
			  "::E  R , SER_PLE_ERR_AMSDU = 0x%08x\n",
			  mt76_rr(dev, MT_SWDEF_PLE_AMSDU_STATS));
	desc += scnprintf(buff + desc, bufsz - desc,
			  "::E  R , SER_PSE_ERR       = 0x%08x\n",
			  mt76_rr(dev, MT_SWDEF_PSE_STATS));
	desc += scnprintf(buff + desc, bufsz - desc,
			  "::E  R , SER_PSE_ERR_1     = 0x%08x\n",
			  mt76_rr(dev, MT_SWDEF_PSE1_STATS));
	desc += scnprintf(buff + desc, bufsz - desc,
			  "::E  R , SER_LMAC_WISR6_B0 = 0x%08x\n",
			  mt76_rr(dev, MT_SWDEF_LAMC_WISR6_BN0_STATS));
	desc += scnprintf(buff + desc, bufsz - desc,
			  "::E  R , SER_LMAC_WISR6_B1 = 0x%08x\n",
			  mt76_rr(dev, MT_SWDEF_LAMC_WISR6_BN1_STATS));
	desc += scnprintf(buff + desc, bufsz - desc,
			  "::E  R , SER_LMAC_WISR6_B2 = 0x%08x\n",
			  mt76_rr(dev, MT_SWDEF_LAMC_WISR6_BN2_STATS));
	desc += scnprintf(buff + desc, bufsz - desc,
			  "::E  R , SER_LMAC_WISR7_B0 = 0x%08x\n",
			  mt76_rr(dev, MT_SWDEF_LAMC_WISR7_BN0_STATS));
	desc += scnprintf(buff + desc, bufsz - desc,
			  "::E  R , SER_LMAC_WISR7_B1 = 0x%08x\n",
			  mt76_rr(dev, MT_SWDEF_LAMC_WISR7_BN1_STATS));
	desc += scnprintf(buff + desc, bufsz - desc,
			  "::E  R , SER_LMAC_WISR7_B2 = 0x%08x\n",
			  mt76_rr(dev, MT_SWDEF_LAMC_WISR7_BN2_STATS));
	desc += scnprintf(buff + desc, bufsz - desc,
			  "\nSYS_RESET_COUNT: WM %d, WA %d\n",
			  dev->recovery.wm_reset_count,
			  dev->recovery.wa_reset_count);

	ret = simple_read_from_buffer(user_buf, count, ppos, buff, desc);
	kfree(buff);
	return ret;
}

static const struct file_operations mt7999_sys_recovery_ops = {
	.write = mt7999_sys_recovery_set,
	.read = mt7999_sys_recovery_get,
	.open = simple_open,
	.llseek = default_llseek,
};

static int
mt7999_radar_trigger(void *data, u64 val)
{
#define RADAR_MAIN_CHAIN	1
#define RADAR_BACKGROUND	2
	struct mt7999_phy *phy = data;
	struct mt7999_dev *dev = phy->dev;
	struct cfg80211_chan_def *chandef;
	int rdd_idx, ret;

	if (test_bit(MT76_SCANNING, &phy->mt76->state))
		return -EBUSY;

	if (!val || val > RADAR_BACKGROUND)
		return -EINVAL;

	if (val == RADAR_BACKGROUND) {
		if (!dev->rdd2_phy || !cfg80211_chandef_valid(&dev->rdd2_chandef)) {
			dev_err(dev->mt76.dev, "Background radar is not enabled\n");
			return -EINVAL;
		}
		chandef = &dev->rdd2_chandef;
	} else {
		chandef = &phy->mt76->chandef;
	}

	rdd_idx = mt7999_get_rdd_idx(phy, val == RADAR_BACKGROUND);
	if (rdd_idx < 0) {
		dev_err(dev->mt76.dev, "No RDD found\n");
		return -EINVAL;
	}

	ret = cfg80211_chandef_dfs_required(dev->mt76.hw->wiphy, chandef,
					    NL80211_IFTYPE_AP);
	if (ret <= 0)
		return ret;

	phy->rdd_emulate = true;
	return mt7999_mcu_rdd_cmd(dev, RDD_RADAR_EMULATE, rdd_idx, 0);
}

DEFINE_DEBUGFS_ATTRIBUTE(fops_radar_trigger, NULL,
			 mt7999_radar_trigger, "%lld\n");

static int
mt7999_rdd_monitor(struct seq_file *s, void *data)
{
	struct mt7999_dev *dev = dev_get_drvdata(s->private);
	struct cfg80211_chan_def *chandef = &dev->rdd2_chandef;
	const char *bw;
	int ret = 0;

	mutex_lock(&dev->mt76.mutex);

	if (!mt7999_eeprom_has_background_radar(dev)) {
		seq_puts(s, "no background radar capability\n");
		goto out;
	}

	if (!cfg80211_chandef_valid(chandef)) {
		ret = -EINVAL;
		goto out;
	}

	if (!dev->rdd2_phy) {
		seq_puts(s, "not running\n");
		goto out;
	}

	switch (chandef->width) {
	case NL80211_CHAN_WIDTH_40:
		bw = "40";
		break;
	case NL80211_CHAN_WIDTH_80:
		bw = "80";
		break;
	case NL80211_CHAN_WIDTH_160:
		bw = "160";
		break;
	case NL80211_CHAN_WIDTH_80P80:
		bw = "80P80";
		break;
	default:
		bw = "20";
		break;
	}

	seq_printf(s, "channel %d (%d MHz) width %s MHz center1: %d MHz\n",
		   chandef->chan->hw_value, chandef->chan->center_freq,
		   bw, chandef->center_freq1);
out:
	mutex_unlock(&dev->mt76.mutex);

	return ret;
}

static int
mt7999_fw_debug_wm_set(void *data, u64 val)
{
	struct mt7999_dev *dev = data;
	enum {
		DEBUG_TXCMD = 62,
		DEBUG_CMD_RPT_TX,
		DEBUG_CMD_RPT_TRIG,
		DEBUG_SPL,
		DEBUG_RPT_RX,

		DEBUG_IDS_SND = 84,
		DEBUG_IDS_BSRP,
		DEBUG_IDS_TPUT_MON,

		DEBUG_IDS_PP = 94,
		DEBUG_IDS_RA,
		DEBUG_IDS_BF,
		DEBUG_IDS_SR,
		DEBUG_IDS_RU,
		DEBUG_IDS_MUMIMO,
		DEBUG_IDS_MLO = 101,
		DEBUG_IDS_ERR_LOG,
	};
	u8 debug_category[] = {
		DEBUG_TXCMD,
		DEBUG_CMD_RPT_TX,
		DEBUG_CMD_RPT_TRIG,
		DEBUG_SPL,
		DEBUG_RPT_RX,
		DEBUG_IDS_SND,
		DEBUG_IDS_BSRP,
		DEBUG_IDS_TPUT_MON,
		DEBUG_IDS_PP,
		DEBUG_IDS_RA,
		DEBUG_IDS_BF,
		DEBUG_IDS_SR,
		DEBUG_IDS_RU,
		DEBUG_IDS_MUMIMO,
		DEBUG_IDS_MLO,
		DEBUG_IDS_ERR_LOG,
	};
	bool en;
	int ret;
	u8 i;

#ifdef CONFIG_MTK_DEBUG
	dev->fw_debug_wm = val;
#else
	dev->fw_debug_wm = val ? MCU_FW_LOG_TO_HOST : 0;
#endif

	if (dev->fw_debug_bin)
		val = MCU_FW_LOG_RELAY;
	else
		val = dev->fw_debug_wm;

	en = dev->fw_debug_wm || (dev->fw_debug_bin & BIT(0));

	ret = mt7999_mcu_fw_log_2_host(dev, val);
	if (ret)
		return ret;

	if (!en) {
		for (i = 0; i < ARRAY_SIZE(debug_category); i++) {
			ret = mt7999_mcu_fw_dbg_ctrl(dev, debug_category[i], 0);
			if (ret)
				return ret;
		}

		return 0;
	}

	/* do not enable fwlog if relay is not used, or packet_dump is used */
	if (!dev->fw_debug_bin ||
	    (dev->fw_debug_bin & GENMASK(__PKT_BIN_DEBUG_MAX, PKT_BIN_DEBUG_FIRST)))
		return 0;

#ifdef CONFIG_MULDF_HELPER
	if (dev->mt76.muldf.fwlog_en)
		return 0;
#endif

	for (i = 0; i < ARRAY_SIZE(debug_category); i++) {
		/* only enable necessary fwlog modules to prevent WM busy.
		 * for other modules, user can enable it by using fw_debug_module
		 * and fw_debug_level
		 */
		if (debug_category[i] > DEBUG_RPT_RX)
			continue;

		ret = mt7999_mcu_fw_dbg_ctrl(dev, debug_category[i], 1);
		if (ret)
			return ret;
	}

	return 0;
}

static int
mt7999_fw_debug_wm_get(void *data, u64 *val)
{
	struct mt7999_dev *dev = data;

	*val = dev->fw_debug_wm;

	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(fops_fw_debug_wm, mt7999_fw_debug_wm_get,
			 mt7999_fw_debug_wm_set, "%lld\n");

static struct dentry *
create_buf_file_cb(const char *filename, struct dentry *parent, umode_t mode,
		   struct rchan_buf *buf, int *is_global)
{
	struct dentry *f;

	f = debugfs_create_file(filename[0] == 'f' ? "fwlog_data" : "idxlog_data",
	                        mode, parent, buf, &relay_file_operations);
	if (IS_ERR(f))
		return NULL;

	*is_global = 1;

	return f;
}

static int
remove_buf_file_cb(struct dentry *f)
{
	debugfs_remove(f);

	return 0;
}

static int
mt7999_fw_debug_muru_set(void *data)
{
	struct mt7999_dev *dev = data;
	enum {
		DEBUG_BSRP_STATUS = 256,
		DEBUG_TX_DATA_BYTE_CONUT,
		DEBUG_RX_DATA_BYTE_CONUT,
		DEBUG_RX_TOTAL_BYTE_CONUT,
		DEBUG_INVALID_TID_BSR,
		DEBUG_UL_LONG_TERM_PPDU_TYPE,
		DEBUG_DL_LONG_TERM_PPDU_TYPE,
		DEBUG_PPDU_CLASS_TRIG_ONOFF,
		DEBUG_AIRTIME_BUSY_STATUS,
		DEBUG_UL_OFDMA_MIMO_STATUS,
		DEBUG_RU_CANDIDATE,
		DEBUG_MEC_UPDATE_AMSDU,
	} debug;
	int ret;

	if (dev->fw_debug_muru_disable)
		return 0;

	for (debug = DEBUG_BSRP_STATUS; debug <= DEBUG_MEC_UPDATE_AMSDU; debug++) {
		ret = mt7999_mcu_muru_dbg_info(dev, debug,
					       dev->fw_debug_bin & BIT(0));
		if (ret)
			return ret;
	}

	return 0;
}

static int
mt7999_fw_debug_bin_set(void *data, u64 val)
{
	static struct rchan_callbacks relay_cb = {
		.create_buf_file = create_buf_file_cb,
		.remove_buf_file = remove_buf_file_cb,
	};
	struct mt7999_dev *dev = data;
	int ret;

	if (!dev->relay_fwlog) {
		dev->relay_fwlog = relay_open("fwlog_data", dev->debugfs_dir,
					      1500, 512, &relay_cb, NULL);
		if (!dev->relay_fwlog)
			return -ENOMEM;
	}

	dev->fw_debug_bin = val;

	relay_reset(dev->relay_fwlog);

	ret = mt7999_fw_debug_muru_set(dev);
	if (ret)
		return ret;

	return mt7999_fw_debug_wm_set(dev, dev->fw_debug_wm);
}

static int
mt7999_fw_debug_bin_get(void *data, u64 *val)
{
	struct mt7999_dev *dev = data;

	*val = dev->fw_debug_bin;

	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(fops_fw_debug_bin, mt7999_fw_debug_bin_get,
			 mt7999_fw_debug_bin_set, "%lld\n");

static int
mt7999_idxlog_enable_get(void *data, u64 *val)
{
	struct mt7999_dev *dev = data;

	*val = dev->idxlog_enable;

	return 0;
}

static int
mt7999_idxlog_enable_set(void *data, u64 val)
{
	static struct rchan_callbacks relay_cb = {
		.create_buf_file = create_buf_file_cb,
		.remove_buf_file = remove_buf_file_cb,
	};
	struct mt7999_dev *dev = data;

	if (dev->idxlog_enable == !!val)
		return 0;

	if (!dev->relay_idxlog) {
		dev->relay_idxlog = relay_open("idxlog_data", dev->debugfs_dir,
		                               1500, 512, &relay_cb, NULL);
		if (!dev->relay_idxlog)
			return -ENOMEM;
	}

	dev->idxlog_enable = !!val;

	if (val) {
		int ret = mt7999_mcu_fw_time_sync(&dev->mt76);
		if (ret)
			return ret;

		/* Reset relay channel only when it is not being written to. */
		relay_reset(dev->relay_idxlog);
	}

	return mt7999_mcu_fw_log_2_host(dev, val ? MCU_FW_LOG_RELAY_IDX : 0);
}

DEFINE_DEBUGFS_ATTRIBUTE(fops_idxlog_enable, mt7999_idxlog_enable_get,
	                 mt7999_idxlog_enable_set, "%llu\n");

static void
mt7999_ampdu_stat_read_phy(struct mt7999_phy *phy, struct seq_file *file)
{
	struct mt7999_dev *dev = phy->dev;
	int bound[15], range[8], i;
	u8 band_idx = phy->mt76->band_idx;

	/* Tx ampdu stat */
	for (i = 0; i < ARRAY_SIZE(range); i++)
		range[i] = mt76_rr(dev, MT_MIB_ARNG(band_idx, i));

	for (i = 0; i < ARRAY_SIZE(bound); i++)
		bound[i] = MT_MIB_ARNCR_RANGE(range[i / 2], i % 2) + 1;

	seq_printf(file, "\nPhy %s, Phy band %d\n",
		   wiphy_name(phy->mt76->hw->wiphy), band_idx);

	seq_printf(file, "Length: %8d | ", bound[0]);
	for (i = 0; i < ARRAY_SIZE(bound) - 1; i++)
		seq_printf(file, "%3d -%3d | ",
			   bound[i] + 1, bound[i + 1]);

	seq_puts(file, "\nCount:  ");
	for (i = 0; i < ARRAY_SIZE(bound); i++)
		seq_printf(file, "%8d | ", phy->mt76->aggr_stats[i]);
	seq_puts(file, "\n");

	seq_printf(file, "BA miss count: %d\n", phy->mib.ba_miss_cnt);
}

static void
mt7999_txbf_stat_read_phy(struct mt7999_phy *phy, struct seq_file *s)
{
	struct mt76_mib_stats *mib = &phy->mib;
	static const char * const bw[] = {
		"BW20", "BW40", "BW80", "BW160", "BW320"
	};

	/* Tx Beamformer monitor */
	seq_puts(s, "\nTx Beamformer applied PPDU counts: ");

	seq_printf(s, "iBF: %d, eBF: %d\n",
		   mib->tx_bf_ibf_ppdu_cnt,
		   mib->tx_bf_ebf_ppdu_cnt);

	/* Tx Beamformer Rx feedback monitor */
	seq_puts(s, "Tx Beamformer Rx feedback statistics: ");

	seq_printf(s, "All: %d, EHT: %d, HE: %d, VHT: %d, HT: %d, ",
		   mib->tx_bf_rx_fb_all_cnt,
		   mib->tx_bf_rx_fb_eht_cnt,
		   mib->tx_bf_rx_fb_he_cnt,
		   mib->tx_bf_rx_fb_vht_cnt,
		   mib->tx_bf_rx_fb_ht_cnt);

	seq_printf(s, "%s, NC: %d, NR: %d\n",
		   bw[mib->tx_bf_rx_fb_bw],
		   mib->tx_bf_rx_fb_nc_cnt,
		   mib->tx_bf_rx_fb_nr_cnt);

	/* Tx Beamformee Rx NDPA & Tx feedback report */
	seq_printf(s, "Tx Beamformee successful feedback frames: %d\n",
		   mib->tx_bf_fb_cpl_cnt);
	seq_printf(s, "Tx Beamformee feedback triggered counts: %d\n",
		   mib->tx_bf_fb_trig_cnt);

	/* Tx SU & MU counters */
	seq_printf(s, "Tx multi-user Beamforming counts: %d\n",
		   mib->tx_mu_bf_cnt);
	seq_printf(s, "Tx multi-user MPDU counts: %d\n", mib->tx_mu_mpdu_cnt);
	seq_printf(s, "Tx multi-user successful MPDU counts: %d\n",
		   mib->tx_mu_acked_mpdu_cnt);
	seq_printf(s, "Tx single-user successful MPDU counts: %d\n",
		   mib->tx_su_acked_mpdu_cnt);

	seq_puts(s, "\n");
}

static int
mt7999_tx_stats_show(struct seq_file *file, void *data)
{
	struct mt7999_phy *phy = file->private;
	struct mt7999_dev *dev = phy->dev;
	struct mt76_mib_stats *mib = &phy->mib;
	u32 attempts, success, per;
	int i, j;

	mutex_lock(&dev->mt76.mutex);

	mt7999_mac_update_stats(phy);
	mt7999_ampdu_stat_read_phy(phy, file);

	attempts = mib->tx_mpdu_attempts_cnt;
	success = mib->tx_mpdu_success_cnt;
	per = attempts ? 100 - success * 100 / attempts : 100;
	seq_printf(file, "Tx attempts: %8u (MPDUs)\n", attempts);
	seq_printf(file, "Tx success: %8u (MPDUs)\n", success);
	seq_printf(file, "Tx PER: %u%%\n", per);

	seq_printf(file, "Drop by HW:\n");
	for (i = 0; i < MT7999_HW_DROP_STAT; i++) {
		for (j = 0; j < MT7999_HW_DROP_REASON; j++) {
			if (!phy->hw_drop[i][j])
				continue;

			seq_printf(file,
				   "Stat = %d, reason = %d, drop count = %d\n",
				   i, j, phy->hw_drop[i][j]);
		}
	}

	mt7999_txbf_stat_read_phy(phy, file);

	mutex_unlock(&dev->mt76.mutex);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(mt7999_tx_stats);

static int
mt7999_xmit_queues_show(struct seq_file *file, void *data)
{
	struct mt7999_phy *phy = file->private;
	struct mt7999_dev *dev = phy->dev;
	struct {
		struct mt76_queue *q;
		char *queue;
	} queue_map[] = {
		{ phy->mt76->q_tx[MT_TXQ_BE],	 "   MAIN"  },
		{ dev->mt76.q_mcu[MT_MCUQ_WM],	 "  MCUWM"  },
		{ dev->mt76.q_mcu[MT_MCUQ_WA],	 "  MCUWA"  },
		{ dev->mt76.q_mcu[MT_MCUQ_FWDL], "MCUFWDL" },
	};
	int i;

	seq_puts(file, "     queue | hw-queued |      head |      tail |\n");
	for (i = 0; i < ARRAY_SIZE(queue_map); i++) {
		struct mt76_queue *q = queue_map[i].q;

		if (!q)
			continue;

		seq_printf(file, "   %s | %9d | %9d | %9d |\n",
			   queue_map[i].queue, q->queued, q->head,
			   q->tail);
	}

	return 0;
}

DEFINE_SHOW_ATTRIBUTE(mt7999_xmit_queues);

static int
mt7999_twt_stats(struct seq_file *s, void *data)
{
	struct mt7999_dev *dev = dev_get_drvdata(s->private);
	struct mt7999_twt_flow *iter;
	u8 i;

	rcu_read_lock();

	seq_puts(s, " peer_id_grp_id |  id | flags | exp | mantissa");
	seq_puts(s, " | duration |            tsf");
	seq_puts(s, " | bss_idx | status  | persist | member_cnt | sta_list \n");
	list_for_each_entry_rcu(iter, &dev->twt_list, list) {
		seq_printf(s,
			   "%15x | %3d | %2c%c%c%c | %3d | %8d | %8d | %14lld | %6d ",
			   iter->peer_id_grp_id, iter->id,
			   iter->sched ? 's' : 'u',
			   iter->protection ? 'p' : '-',
			   iter->trigger ? 't' : '-',
			   iter->flowtype ? '-' : 'a',
			   iter->exp, iter->mantissa,
			   iter->duration, iter->tsf, iter->bss_idx);
		if ((iter->peer_id_grp_id & MCU_TWT_BTWT_ID_BIT) == 0) {
			/* Individual TWT: show suspend status */
			seq_printf(s, " | %6s  |         |            |\n",
				   iter->suspended ? "SUSP" : "ACTIVE");
		} else {
			/* Broadcast TWT: show persist and member info */
			seq_printf(s, " |         | %7d | %10d | ",
				   iter->persist, iter->member_cnt);
			for (i = 0; i < iter->member_cnt; i++)
				seq_printf(s, "%3d ", iter->sta_list[i]);
			seq_printf(s, "\n");
		}
	}

	rcu_read_unlock();

	return 0;
}

/* The index of RF registers use the generic regidx, combined with two parts:
 * WF selection [31:24] and offset [23:0].
 */
static int
mt7999_rf_regval_get(void *data, u64 *val)
{
	struct mt7999_dev *dev = data;
	u32 regval;
	int ret;

	ret = mt7999_mcu_rf_regval(dev, dev->mt76.debugfs_reg,
				   &regval, false, false);
	if (ret)
		return ret;

	*val = regval;

	return 0;
}

static int
mt7999_rf_regval_set(void *data, u64 val)
{
	struct mt7999_dev *dev = data;
	u32 val32 = val;

	return mt7999_mcu_rf_regval(dev, dev->mt76.debugfs_reg,
				    &val32, true, false);
}

DEFINE_DEBUGFS_ATTRIBUTE(fops_rf_regval, mt7999_rf_regval_get,
			 mt7999_rf_regval_set, "0x%08llx\n");

static int
mt7999_efem_regval_get(void *data, u64 *val)
{
	struct mt7999_dev *dev = data;
	u32 regval;
	int ret;

	ret = mt7999_mcu_rf_regval(dev, dev->mt76.debugfs_reg,
				   &regval, false, true);
	if (ret)
		return ret;

	*val = regval;

	return 0;
}

static int
mt7999_efem_regval_set(void *data, u64 val)
{
	struct mt7999_dev *dev = data;
	u32 val32 = val;

	return mt7999_mcu_rf_regval(dev, dev->mt76.debugfs_reg,
				    &val32, true, true);
}

DEFINE_DEBUGFS_ATTRIBUTE(fops_efem_regval, mt7999_efem_regval_get,
			 mt7999_efem_regval_set, "0x%08llx\n");

static int
mt7999_fw_debug_muru_disable_set(void *data, u64 val)
{
	struct mt7999_dev *dev = data;

	dev->fw_debug_muru_disable = !!val;

	return 0;
}

static int
mt7999_fw_debug_muru_disable_get(void *data, u64 *val)
{
	struct mt7999_dev *dev = data;

	*val = dev->fw_debug_muru_disable;

	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(fops_fw_debug_muru_disable,
			 mt7999_fw_debug_muru_disable_get,
			 mt7999_fw_debug_muru_disable_set, "%lld\n");

static ssize_t
mt7999_efuse_get(struct file *file, char __user *user_buf,
		 size_t count, loff_t *ppos)
{
	struct mt7999_dev *dev = file->private_data;
	struct mt76_dev *mdev = &dev->mt76;
	u8 *buff = mdev->otp.data;
	int i;
	ssize_t ret;
	u32 block_num;

	mdev->otp.size = MT7999_EFUSE_END_OFFS;
	if (!mdev->otp.data) {
		mdev->otp.data = devm_kzalloc(mdev->dev, mdev->otp.size, GFP_KERNEL);
		if (!mdev->otp.data)
			return -ENOMEM;

		block_num = DIV_ROUND_UP(mdev->otp.size, MT7999_EEPROM_BLOCK_SIZE);
		for (i = 0; i < block_num; i++) {
			buff = mdev->otp.data + i * MT7999_EEPROM_BLOCK_SIZE;
			ret = mt7999_mcu_get_eeprom(dev, i * MT7999_EEPROM_BLOCK_SIZE,
						    buff, MT7999_EEPROM_BLOCK_SIZE,
						    EEPROM_MODE_EFUSE);
			if (ret && ret != -EINVAL)
				return ret;
		}
	}

	ret = simple_read_from_buffer(user_buf, count, ppos, mdev->otp.data, mdev->otp.size);

	return ret;
}

static const struct file_operations mt7999_efuse_ops = {
	.read = mt7999_efuse_get,
	.open = simple_open,
	.llseek = default_llseek,
};

static int
mt7999_efuse_get_free_block(struct seq_file *s, void *data)
{
#define MT_DDIE_BANK_NUM	1
	struct mt7999_dev *dev = dev_get_drvdata(s->private);
	struct efuse_bank_info info[MT_EFUSE_TOTAL_BANK] = {0};
	int i, ret, bank = 0;

	ret = mt7999_mcu_get_efuse_free_block(dev, &info);
	if (ret)
		return ret;

	seq_puts(s, "ddie:\n");
	for (i = 0; i < MT_DDIE_BANK_NUM; i++, bank++)
		seq_printf(s, "\tbank %d: free/total = %d/%d\n",
			   bank, info[i].free_block, info[i].total_block);

	for (i = MT_DDIE_BANK_NUM; i < MT_EFUSE_TOTAL_BANK; i++, bank++) {
		if (i == MT_DDIE_BANK_NUM ||
		    (i > MT_DDIE_BANK_NUM && info[i - 1].efuse_id != info[i].efuse_id)) {
			seq_printf(s, "adie %d:\n", info[i].efuse_id);
			bank = 0;
		}

		seq_printf(s, "\tbank %d: free/total = %d/%d\n",
			   bank, info[i].free_block, info[i].total_block);
	}

	return 0;
}

static ssize_t
mt7999_ext_eeprom_get(struct file *file, char __user *user_buf,
		      size_t count, loff_t *ppos)
{
	struct mt7999_dev *dev = file->private_data;
	struct mt76_dev *mdev = &dev->mt76;
	u8 *buff = mdev->ext_eeprom.data;
	u32 block_num, block_size = MT7999_EXT_EEPROM_BLOCK_SIZE;
	int i;
	ssize_t ret;

	if (!mt7999_has_ext_eeprom(dev)) {
		dev_info(dev->mt76.dev, "No external eeprom device found\n");
		return 0;
	}

	mdev->ext_eeprom.size = MT7999_EEPROM_SIZE;

	if (!mdev->ext_eeprom.data) {
		mdev->ext_eeprom.data = devm_kzalloc(mdev->dev,
						     mdev->ext_eeprom.size,
						     GFP_KERNEL);
		if (!mdev->ext_eeprom.data)
			return -ENOMEM;

		block_num = DIV_ROUND_UP(mdev->ext_eeprom.size, block_size);
		for (i = 0; i < block_num; i++) {
			u32 buf_len = block_size;
			u32 offset = i * block_size;

			if (offset + block_size > mdev->ext_eeprom.size)
				buf_len = mdev->ext_eeprom.size % block_size;
			buff = mdev->ext_eeprom.data + offset;
			ret = mt7999_mcu_get_eeprom(dev, offset, buff, buf_len,
						    EEPROM_MODE_EXT);
			if (ret && ret != -EINVAL)
				return ret;
		}
	}

	ret = simple_read_from_buffer(user_buf, count, ppos,
				      mdev->ext_eeprom.data, mdev->ext_eeprom.size);

	return ret;
}

static const struct file_operations mt7999_ext_eeprom_ops = {
	.read = mt7999_ext_eeprom_get,
	.open = simple_open,
	.llseek = default_llseek,
};

static ssize_t
mt7999_precal_get(struct file *file, char __user *user_buf,
		  size_t count, loff_t *ppos)
{
	struct mt7999_dev *dev = file->private_data;

	if (!dev->cal.data || !dev->cal.size) {
		dev_info(dev->mt76.dev, "No precal data available\n");
		return 0;
	}

	return simple_read_from_buffer(user_buf, count, ppos,
				       dev->cal.data, dev->cal.size);
}

static const struct file_operations mt7999_precal_ops = {
	.read = mt7999_precal_get,
	.open = simple_open,
	.llseek = default_llseek,
};

static int
mt7999_vow_info_read(struct seq_file *s, void *data)
{
	struct mt7999_dev *dev = dev_get_drvdata(s->private);
	struct mt7999_vow_ctrl *vow = &dev->vow;
	int i;

	seq_printf(s, "VoW ATF Configuration:\n");
	seq_printf(s, "ATF: %s\n", vow->atf_enable ? "enabled" : "disabled");
	seq_printf(s, "WATF: %s\n", vow->watf_enable ? "enabled" : "disabled");
	seq_printf(s, "Airtime Quantums (unit: 256 us)\n");
	for (i = 0; i < VOW_DRR_QUANTUM_NUM; ++i)
		seq_printf(s, "\tL%d: %hhu\n", i, vow->drr_quantum[i]);
	seq_printf(s, "Max Airtime Deficit: %hhu (unit: 256 us)\n", vow->max_deficit);

	return 0;
}

static int
mt7999_atf_enable_get(void *data, u64 *val)
{
	struct mt7999_phy *phy = data;

	*val = phy->dev->vow.atf_enable;

	return 0;
}

static int
mt7999_atf_enable_set(void *data, u64 val)
{
	struct mt7999_phy *phy = data;
	unsigned long flags = 0;

	set_bit(VOW_FF_ATF_ENABLE, &flags);
	phy->dev->vow.atf_enable = !!val;
	return mt7999_mcu_set_vow_feature_ctrl(phy, flags);
}

DEFINE_DEBUGFS_ATTRIBUTE(fops_atf_enable, mt7999_atf_enable_get,
	                 mt7999_atf_enable_set, "%llu\n");

static ssize_t mt7999_atf_sch_ctrl_set(struct file *file,
				       const char __user *user_buf,
				       size_t count, loff_t *ppos)
{
	struct mt7999_phy *phy = file->private_data;
	struct mt7999_dev *dev = phy->dev;
	struct mt7999_vow_ctrl *vow = &dev->vow;
	unsigned long flags = 0;
	u8 type, policy;
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

	if (sscanf(buf, "%hhu-%hhu", &type, &policy) != 2) {
		dev_err(dev->mt76.dev, "Format: <type>-<policy>\n");
		return -EINVAL;
	}

	if (type >= VOW_SCH_TYPE_NUM || policy >= VOW_SCH_POLICY_NUM) {
		dev_err(dev->mt76.dev,
			"Invalid scheduling type %hhu or policy %hhu\n", type, policy);
		return -EINVAL;
	}

	set_bit(VOW_FF_ATF_SCH_CTRL, &flags);
	vow->sch_type = type;
	vow->sch_policy = policy;

	ret = mt7999_mcu_set_vow_feature_ctrl(phy, flags);
	if (ret) {
		dev_err(dev->mt76.dev, "Fail to set VoW feature control\n");
		return ret;
	}

	return count;
}

static ssize_t mt7999_atf_sch_ctrl_get(struct file *file, char __user *user_buf,
				       size_t count, loff_t *ppos)
{
	struct mt7999_phy *phy = file->private_data;
	struct mt7999_vow_ctrl *vow = &phy->dev->vow;
	static const size_t size = 128;
	int len = 0, ret;
	char *buf;

	buf = kzalloc(size, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	len += scnprintf(buf + len, size - len, "ATF Scheduling Info:\n");
	len += scnprintf(buf + len, size - len, "Type: %hhu\n", vow->sch_type);
	len += scnprintf(buf + len, size - len, "Policy: %hhu\n", vow->sch_policy);

	ret = simple_read_from_buffer(user_buf, count, ppos, buf, len);
	kfree(buf);
	return ret;
}

static const struct file_operations fops_atf_sch_ctrl = {
	.write = mt7999_atf_sch_ctrl_set,
	.read = mt7999_atf_sch_ctrl_get,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

static int
mt7999_airtime_read(struct seq_file *s, void *data)
{
	struct mt7999_dev *dev = dev_get_drvdata(s->private);
	struct mt76_dev *mdev = &dev->mt76;
	struct mt76_sta_stats *stats;
	struct ieee80211_sta *sta;
	struct mt7999_sta_link *msta_link;
	struct mt76_wcid *wcid;
	struct mt76_vif_link *vif;
	u16 i;

	seq_printf(s, "VoW Airtime Information:\n");
	rcu_read_lock();
	for (i = 1; i < MT7999_WTBL_STA; ++i) {
		wcid = rcu_dereference(mdev->wcid[i]);
		if (!wcid || !wcid->sta)
			continue;

		msta_link = container_of(wcid, struct mt7999_sta_link, wcid);
		sta = container_of((void *)msta_link->sta, struct ieee80211_sta, drv_priv);
		vif = &msta_link->sta->vif->deflink.mt76;
		stats = &wcid->stats;

		seq_printf(s, "%pM WCID: %hu BandIdx: %hhu OmacIdx: 0x%hhx\t"
		              "TxAirtime: %llu\tRxAirtime: %llu\n",
		              sta->addr, i, vif->band_idx, vif->omac_idx,
		              stats->tx_airtime, stats->rx_airtime);

		stats->tx_airtime = 0;
		stats->rx_airtime = 0;
	}
	rcu_read_unlock();

	return 0;
}

static ssize_t mt7999_rdd_radar_th_set(struct file *file,
				       const char __user *user_buf,
				       size_t count, loff_t *ppos)
{
	struct mt7999_dev *dev = file->private_data;
	struct mt7999_dfs_pattern p;
	char buf[100];
	int ret, radar_type;

	if (count >= sizeof(buf))
		return -EINVAL;

	if (copy_from_user(buf, user_buf, count))
		return -EFAULT;

	if (count && buf[count - 1] == '\n')
		buf[count - 1] = '\0';
	else
		buf[count] = '\0';

	if (sscanf(buf,
		   "%d %hhu %hhu %hhu %hhu %hhu %hhu %u %u %hhu %hhu %hhu %hhu %hhu %hhu %u",
		   &radar_type, &p.enable, &p.stgr, &p.min_crpn, &p.max_crpn, &p.min_crpr,
		   &p.min_pw, &p.min_pri, &p.max_pri, &p.max_pw, &p.min_crbn, &p.max_crbn,
		   &p.min_stgpn, &p.max_stgpn, &p.min_stgpr, &p.min_stgpr_diff) != 16) {
		dev_err(dev->mt76.dev, "Invalid format\n");
		return -EINVAL;
	}

	ret = mt7999_mcu_set_radar_th(dev, radar_type, &p);
	if (ret)
		return ret;
	return count;
}

static const struct file_operations fops_rdd_radar_th = {
	.write = mt7999_rdd_radar_th_set,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

static ssize_t mt7999_rdd_pulse_th_set(struct file *file,
				       const char __user *user_buf,
				       size_t count, loff_t *ppos)
{
	struct mt7999_dev *dev = file->private_data;
	struct mt7999_dfs_pulse p;
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

	if (sscanf(buf,
		   "%u %d %d %u %u %u %u",
		   &p.max_width, &p.max_pwr, &p.min_pwr, &p.min_stgr_pri,
		   &p.max_stgr_pri, &p.min_cr_pri, &p.max_cr_pri) != 7) {
		dev_err(dev->mt76.dev, "Invalid format\n");
		return -EINVAL;
	}

	ret = mt7999_mcu_set_pulse_th(dev, &p);
	if (ret)
		return ret;
	return count;
}

static const struct file_operations fops_rdd_pulse_th = {
	.write = mt7999_rdd_pulse_th_set,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

static int mt7999_rdd_fcc5_lpn_set(void *data, u64 val)
{
	struct mt7999_dev *dev = data;

	return mt7999_mcu_set_fcc5_lpn(dev, val);
}

DEFINE_DEBUGFS_ATTRIBUTE(fops_rdd_fcc5_lpn, NULL,
			 mt7999_rdd_fcc5_lpn_set, "%lld\n");

int mt7999_init_band_debugfs(struct mt7999_phy *phy)
{
	struct mt7999_dev *dev = phy->dev;
	struct dentry *dir;
	char dir_name[10];

	if (!dev->debugfs_dir)
		return -EINVAL;

	snprintf(dir_name, sizeof(dir_name), "band%d", phy->mt76->band_idx);

	dir = debugfs_create_dir(dir_name, dev->debugfs_dir);
	if (!dir)
		return -ENOMEM;

	debugfs_create_file("xmit-queues", 0400, dir, phy,
			    &mt7999_xmit_queues_fops);
	debugfs_create_file("sys_recovery", 0600, dir, phy,
			    &mt7999_sys_recovery_ops);
	debugfs_create_file("tx_stats", 0400, dir, phy, &mt7999_tx_stats_fops);
	if (phy->mt76->cap.has_5ghz) {
		debugfs_create_u32("dfs_hw_pattern", 0400, dir,
				   &dev->hw_pattern);
		debugfs_create_file("radar_trigger", 0200, dir, phy,
				    &fops_radar_trigger);
		debugfs_create_devm_seqfile(dev->mt76.dev, "rdd_monitor", dir,
					    mt7999_rdd_monitor);
		debugfs_create_file("rdd_radar_th", 0200, dir, dev, &fops_rdd_radar_th);
		debugfs_create_file("rdd_pulse_th", 0200, dir, dev, &fops_rdd_pulse_th);
		debugfs_create_file("rdd_fcc5_lpn", 0200, dir, dev, &fops_rdd_fcc5_lpn);
	}
	debugfs_create_file("power_table_filename", 0600, dir, phy,
			    &fops_mt7999_sku_filename);
#ifdef CONFIG_MTK_DEBUG
	mt7999_mtk_init_band_debugfs(phy, dir);
#endif
	return 0;
}

int mt7999_init_dev_debugfs(struct mt7999_phy *phy)
{
	struct mt7999_dev *dev = phy->dev;
	struct dentry *dir;

	dir = mt76_register_debugfs_fops(phy->mt76, NULL);
	if (!dir)
		return -ENOMEM;
	debugfs_create_file("fw_debug_wm", 0600, dir, dev, &fops_fw_debug_wm);
	debugfs_create_file("fw_debug_bin", 0600, dir, dev, &fops_fw_debug_bin);
	debugfs_create_file("idxlog_enable", 0600, dir, dev, &fops_idxlog_enable);
	/* TODO: wm fw cpu utilization */
	debugfs_create_file("implicit_txbf", 0600, dir, dev,
			    &fops_implicit_txbf);
	debugfs_create_devm_seqfile(dev->mt76.dev, "twt_stats", dir,
				    mt7999_twt_stats);
	debugfs_create_file("rf_regval", 0600, dir, dev, &fops_rf_regval);
	debugfs_create_file("efem_regval", 0600, dir, dev, &fops_efem_regval);
	debugfs_create_file("otp", 0400, dir, dev, &mt7999_efuse_ops);
	debugfs_create_devm_seqfile(dev->mt76.dev, "efuse_free_block", dir,
				    mt7999_efuse_get_free_block);
	debugfs_create_file("ext_eeprom", 0400, dir, dev, &mt7999_ext_eeprom_ops);
	debugfs_create_file("precal", 0400, dir, dev, &mt7999_precal_ops);
	debugfs_create_devm_seqfile(dev->mt76.dev, "vow_info", dir,
				    mt7999_vow_info_read);
	debugfs_create_file("atf_enable", 0600, dir, phy, &fops_atf_enable);
	debugfs_create_file("atf_sch_ctrl", 0600, dir, phy, &fops_atf_sch_ctrl);
	debugfs_create_devm_seqfile(dev->mt76.dev, "airtime", dir,
				    mt7999_airtime_read);

	/* do not include MURU debug in fwlog by default */
	dev->fw_debug_muru_disable = true;
	debugfs_create_file("fw_debug_muru_disable", 0600, dir, dev,
			    &fops_fw_debug_muru_disable);

	if (phy == &dev->phy)
		dev->debugfs_dir = dir;

#ifdef CONFIG_MTK_DEBUG
	debugfs_create_u16("wlan_idx", 0600, dir, &dev->wlan_idx);
	mt7999_mtk_init_dev_debugfs(dev, dir);
#endif

	return 0;
}

static void
mt7999_debugfs_write_fwlog(struct mt7999_dev *dev, const void *hdr, int hdrlen,
			   const void *data, int len)
{
	static DEFINE_SPINLOCK(lock);
	unsigned long flags;
	void *dest;

#ifdef CONFIG_MULDF_HELPER
	if (dev->mt76.muldf.fwlog_en && dev->mt76.muldf.fwlog_bin_en) {
		if (hdrlen) {
			dest = dev->mt76.muldf.data;
			memcpy(dest, hdr, hdrlen);
			memcpy(dest + hdrlen, data, len);
		} else {
			dest = (void *)data;
		}

		muldf_xmit_ics(dest, hdrlen + len);
		return;
	}
#endif

	if (!dev->relay_fwlog)
		return;

	spin_lock_irqsave(&lock, flags);

	dest = relay_reserve(dev->relay_fwlog, hdrlen + len + 4);
	if (dest) {
		*(u32 *)dest = hdrlen + len;
		dest += 4;

		if (hdrlen) {
			memcpy(dest, hdr, hdrlen);
			dest += hdrlen;
		}

		memcpy(dest, data, len);
		relay_flush(dev->relay_fwlog);
	}
	spin_unlock_irqrestore(&lock, flags);
}

static void
mt7999_debugfs_write_idxlog(struct mt7999_dev *dev, const void *data, int len)
{
	static DEFINE_SPINLOCK(lock);
	unsigned long flags;
	void *dest;

	if (!dev->relay_idxlog)
		return;

	spin_lock_irqsave(&lock, flags);

	dest = relay_reserve(dev->relay_idxlog, len + 4);
	if (!dest)
		dev_err(dev->mt76.dev, "Failed to reserve slot in %s\n",
		        dev->relay_idxlog->base_filename);
	else {
		*(u32 *)dest = len;
		dest += 4;
		memcpy(dest, data, len);
		relay_flush(dev->relay_idxlog);
	}

	spin_unlock_irqrestore(&lock, flags);
}

void mt7999_debugfs_rx_fw_monitor(struct mt7999_dev *dev, const void *data, int len)
{
	struct {
		__le32 magic;
		u8 version;
		u8 _rsv;
		__le16 serial_id;
		__le32 timestamp;
		__le16 msg_type;
		__le16 len;
	} hdr = {
		.version = 0x1,
		.magic = cpu_to_le32(FW_BIN_LOG_MAGIC),
		.msg_type = cpu_to_le16(PKT_TYPE_RX_FW_MONITOR),
	};

	hdr.serial_id = cpu_to_le16(dev->fw_debug_seq++);
	hdr.timestamp = cpu_to_le32(mt76_rr(dev, MT_LPON_FRCR(0)));
	hdr.len = *(__le16 *)data;
	mt7999_debugfs_write_fwlog(dev, &hdr, sizeof(hdr), data, len);
}

bool mt7999_debugfs_rx_log(struct mt7999_dev *dev, const void *data, int len)
{
	bool is_fwlog = get_unaligned_le32(data) == FW_BIN_LOG_MAGIC;
#ifdef CONFIG_MTK_DEBUG
	is_fwlog |= get_unaligned_le32(data) == PKT_BIN_DEBUG_MAGIC;
#endif

	if (is_fwlog) {
		mt7999_debugfs_write_fwlog(dev, NULL, 0, data, len);
	} else if (dev->relay_idxlog)
		mt7999_debugfs_write_idxlog(dev, data, len);
	else
		return false;

	return true;
}

#ifdef CONFIG_MAC80211_DEBUGFS
/** per-station debugfs **/

static int
mt7999_sta_links_info_show(struct seq_file *s, void *data)
{
	struct ieee80211_sta *sta = s->private;
	struct mt7999_sta *msta = (struct mt7999_sta *)sta->drv_priv;
	u64 tx_cnt = 0, tx_fails = 0, tx_retries = 0, rx_cnt = 0;
	struct mt7999_dev *dev = msta->vif->deflink.phy->dev;
	unsigned long valid_links;
	u8 link_id;
#ifdef CONFIG_MTK_DEBUG
	u32 val[3];
	u8 i;
#endif

	seq_printf(s, "primary link, link ID = %d\n", msta->deflink_id);
	seq_printf(s, "secondary link, link ID = %d\n", msta->seclink_id);
	seq_printf(s, "valid links = 0x%x\n", sta->valid_links);

	mutex_lock(&dev->mt76.mutex);
	valid_links = sta->valid_links ?: BIT(0);
	for_each_set_bit(link_id, &valid_links, IEEE80211_MLD_MAX_NUM_LINKS) {
		struct mt7999_sta_link *msta_link =
			mt76_dereference(msta->link[link_id], &dev->mt76);
		struct mt76_wcid *wcid;

		if (!msta_link)
			continue;

		wcid = &msta_link->wcid;

		tx_cnt += wcid->stats.tx_packets;
		tx_fails += wcid->stats.tx_packets_failed;
		tx_retries += wcid->stats.tx_packets_retried;
		rx_cnt += wcid->stats.rx_packets;

		seq_printf(s, "link%d: wcid=%d, phy=%d, link_valid=%d, ampdu_state=0x%lx\n",
			    wcid->link_id, wcid->idx, wcid->phy_idx, wcid->link_valid,
			    wcid->ampdu_state);
	}
	mutex_unlock(&dev->mt76.mutex);

	/* PER may be imprecise, because MSDU total and failed counts
	 * are updated at different times.
	 */
	seq_printf(s, "TX MSDU Count: %llu\n", tx_cnt);
	seq_printf(s, "TX MSDU Drops: %llu (PER: %llu.%llu%%)\n", tx_fails,
		   tx_cnt ? tx_fails * 1000 / tx_cnt / 10 : 0,
		   tx_cnt ? tx_fails * 1000 / tx_cnt % 10 : 0);
	seq_printf(s, "TX MSDU Successful Retries: %llu\n", tx_retries);
	seq_printf(s, "RX MSDU Count: %llu\n", rx_cnt);

#ifdef CONFIG_MTK_DEBUG
	seq_printf(s, "\nUWTBL RLA Information\n");
	mt76_wr(dev, MT_UWTBL_DBG_RLADI,
		u32_encode_bits(msta->peer_wcid[msta->pri_band],
				MT_UWTBL_DBG_RLADI_MLD_ID_MASK));
	mt76_wr(dev, MT_UWTBL_DBG_RLACFG,
		MT_UWTBL_DBG_RLACFG_OP_EXEC |
		MT_UWTBL_DBG_RLACFG_OP_EN |
		u32_encode_bits(9, MT_UWTBL_DBG_RLACFG_OP_ID_MASK));
	if (!mt76_poll(dev, MT_UWTBL_DBG_RLACFG, MT_UWTBL_DBG_RLACFG_OP_EXEC, 0, 5000))
		return 0;

	for (i = 0; i < 3; i++)
		val[i] = mt76_rr(dev, MT_UWTBL_DBG_RLADO + i * 4);

	seq_printf(s, "Primary   MLD ID = %d\n",
		   u32_get_bits(val[0], MT_UWTBL_DBG_RLADO_LOW_MASK));
	seq_printf(s, "Secondary MLD ID = %d\n",
		   u32_get_bits(val[0], MT_UWTBL_DBG_RLADO_HIGH_MASK));
	seq_printf(s, "1st WLAN ID = %d\n",
		   u32_get_bits(val[1], MT_UWTBL_DBG_RLADO_LOW_MASK));
	seq_printf(s, "2nd WLAN ID = %d\n",
		   u32_get_bits(val[1], MT_UWTBL_DBG_RLADO_HIGH_MASK));
	seq_printf(s, "3rd WLAN ID = %d\n",
		   u32_get_bits(val[2], MT_UWTBL_DBG_RLADO_LOW_MASK));
#endif /* CONFIG_MTK_DEBUG */
	return 0;
}
DEFINE_SHOW_ATTRIBUTE(mt7999_sta_links_info);

void mt7999_sta_add_debugfs(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			    struct ieee80211_sta *sta, struct dentry *dir)
{
	/* MTK internal */
	debugfs_create_file("mt76_links_info", 0400, dir, sta,
			    &mt7999_sta_links_info_fops);
}

static int mt7999_set_rate_wcid_hdr(struct mt7999_dev *dev,
				    struct ieee80211_link_sta *link_sta,
				    struct ra_common_hdr *hdr)
{
	struct mt7999_sta *msta = (struct mt7999_sta *)link_sta->sta->drv_priv;
	struct mt7999_sta_link *msta_link;

	lockdep_assert_held(&dev->mt76.mutex);

	msta_link = mt7999_sta_link_protected(dev, msta, link_sta->link_id);
	if (!msta_link)
		return -EINVAL;

	hdr->wlan_idx[0] = cpu_to_le16(msta_link->wcid.idx);
	hdr->target_wlan_idx = cpu_to_le16(msta_link->wcid.idx);
	hdr->wlan_idx_num = 1;

	return 0;
}

static ssize_t mt7999_link_sta_fixed_rate_set(struct file *file,
					      const char __user *user_buf,
					      size_t count, loff_t *ppos)
{
	struct ieee80211_link_sta *link_sta = file->private_data;
	struct mt7999_sta *msta = (struct mt7999_sta *)link_sta->sta->drv_priv;
	struct mt7999_phy *link_phy = mt7999_vif_link_phy(&msta->vif->deflink);
	struct ra_fixed_rate_set rate = {};
	struct mt7999_dev *dev;
	char buf[80];
	int ret;

	if (!link_phy)
		return -EINVAL;

	if (count >= sizeof(buf))
		return -EINVAL;

	if (copy_from_user(buf, user_buf, count))
		return -EFAULT;

	if (count && buf[count - 1] == '\n')
		buf[count - 1] = '\0';
	else
		buf[count] = '\0';

	/* mode - cck: 0, ofdm: 1, ht: 2, gf: 3, vht: 4, he_su: 8, he_er: 9 EHT: 15
	 * bw - bw20: 0, bw40: 1, bw80: 2, bw160: 3, BW320: 4
	 * mcs - cck: 0~4, ofdm: 0~7, ht: 0~32, vht: 0~9, he_su: 0~11, he_er: 0~2, eht: 0~13
	 * nss - vht: 1~4, he: 1~4, eht: 1~4, others: ignore
	 * gi - (ht/vht) lgi: 0, sgi: 1; (he) 0.8us: 0, 1.6us: 1, 3.2us: 2
	 * preamble - short: 1, long: 0
	 * stbc - off: 0, on: 1
	 * ldpc - off: 0, on: 1
	 * spe - off: 0, on: 1
	 * ltf - 1xltf: 0, 2xltf: 1, 4xltf: 2
	 */
	dev = link_phy->dev;
	if (sscanf(buf, "%hhu %hhu %hhu %hhu %hhu %hhu %hhu %hhu %hhu %hhu %hhu %hhu"
			" %hhu %hhu %hhu %hhu", &rate.hdr.traffic_idx,
		   &rate.hdr.extra_traffic_idx, &rate.hdr.ac_idx_bitmap,
		   &rate.params.phy_mode, &rate.params.bw, &rate.params.mcs,
		   &rate.params.nss, &rate.params.gi, &rate.params.stbc,
		   &rate.params.ldpc, &rate.params.ltf, &rate.params.spe,
		   &rate.params.ueqm_en, &rate.params.ueqm_pat,
		   &rate.params.dcm, &rate.type) != 16) {
		dev_warn(dev->mt76.dev,
			 "format: Traffic ExtraTraffic AcIdxBitmap Mode BW MCS NSS"
			 " GI STBC LDPC LTF SPE UeqmEn UeqmPat Dcm Type\n");
		return -EINVAL;
	}

	ret = mt7999_validate_rate_params(dev, rate.params.phy_mode, &rate.params.mcs);
	if (ret)
		return ret;

	mutex_lock(&dev->mt76.mutex);

	ret = mt7999_set_rate_wcid_hdr(dev, link_sta, &rate.hdr);
	if (ret)
		goto out;

	ret = mt7999_mcu_set_fixed_rate_ctrl(dev, &rate, UNI_RA_FIXED_RATE_SET, 1);
	if (ret)
		goto out;

	ret = count;
out:
	mutex_unlock(&dev->mt76.mutex);
	return ret;
}

static const struct file_operations fops_fixed_rate = {
	.write = mt7999_link_sta_fixed_rate_set,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

static ssize_t mt7999_link_sta_fixed_rate_reset(struct file *file,
						const char __user *user_buf,
						size_t count, loff_t *ppos)
{
	struct ieee80211_link_sta *link_sta = file->private_data;
	struct mt7999_sta *msta = (struct mt7999_sta *)link_sta->sta->drv_priv;
	struct mt7999_dev *dev = msta->vif->deflink.phy->dev;
	struct ra_fixed_rate_reset rate = {};
	char buf[20];
	int ret;

	if (count >= sizeof(buf))
		return -EINVAL;

	if (copy_from_user(buf, user_buf, count))
		return -EFAULT;

	if (count && buf[count - 1] == '\n')
		buf[count - 1] = '\0';
	else
		buf[count] = '\0';

	if (sscanf(buf, "%hhu %hhu %hhu", &rate.hdr.traffic_idx,
		   &rate.hdr.extra_traffic_idx, &rate.hdr.ac_idx_bitmap) != 3) {
		dev_warn(dev->mt76.dev,
			 "format: Traffic ExtraTraffic AcIdxBitmap\n");
		return -EINVAL;
	}

	mutex_lock(&dev->mt76.mutex);

	ret = mt7999_set_rate_wcid_hdr(dev, link_sta, &rate.hdr);
	if (ret)
		goto out;

	ret = mt7999_mcu_set_fixed_rate_ctrl(dev, &rate,
					     UNI_RA_FIXED_RATE_RESET, 1);
	if (ret)
		goto out;

	ret = count;
out:
	mutex_unlock(&dev->mt76.mutex);
	return ret;
}

static const struct file_operations fops_fixed_rate_reset = {
	.write = mt7999_link_sta_fixed_rate_reset,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

static ssize_t
mt7999_link_sta_fixed_rate_opt_param_set(struct file *file,
					 const char __user *user_buf,
					 size_t count, loff_t *ppos)
{
	struct ieee80211_link_sta *link_sta = file->private_data;
	struct mt7999_sta *msta = (struct mt7999_sta *)link_sta->sta->drv_priv;
	struct mt7999_dev *dev = msta->vif->deflink.phy->dev;
	struct ra_fixed_rate_opt_param_set rate = {};
	u16 opt_param_bitmap;
	char buf[80];
	int ret;

	if (count >= sizeof(buf))
		return -EINVAL;

	if (copy_from_user(buf, user_buf, count))
		return -EFAULT;

	if (count && buf[count - 1] == '\n')
		buf[count - 1] = '\0';
	else
		buf[count] = '\0';

	/* opt_param_bitmap - BIT(n) to enable fixed specific parameter:
	 *   bit(0): phymode, bit(1): mcs, bit(2): nss, bit(3): bw,
	 *   bit(4): gi, bit(5): ecc, bit(6): stbc, bit(7): ltf,
	 *   bit(8): spe, bit(9): ueqm, bit(10): dcm
	 */
	if (sscanf(buf, "%hhu %hhu %hhu %hu %hhu %hhu %hhu %hhu %hhu %hhu %hhu "
			"%hhu %hhu %hhu %hhu %hhu %hhu", &rate.hdr.traffic_idx,
		   &rate.hdr.extra_traffic_idx, &rate.hdr.ac_idx_bitmap,
		   &opt_param_bitmap, &rate.params.phy_mode, &rate.params.mcs,
		   &rate.params.nss, &rate.params.bw, &rate.params.gi,
		   &rate.params.ldpc, &rate.params.stbc, &rate.params.ltf,
		   &rate.params.spe, &rate.params.ueqm_en, &rate.params.ueqm_pat,
		   &rate.params.dcm, &rate.type) != 17) {
		dev_warn(dev->mt76.dev,
			 "format: Traffic ExtraTraffic AcIdxBitmap OptParamBitmap Mode"
			 " MCS NSS BW GI LDPC STBC LTF SPE UeqmEn UeqmPat Dcm Type\n");
		return -EINVAL;
	}

	if (!!(opt_param_bitmap & RA_OPT_PARAM_MCS) !=
	    !!(opt_param_bitmap & RA_OPT_PARAM_PHYMODE)) {
		dev_err(dev->mt76.dev,
			"MCS and phy mode must be configured at the same time\n");
		return -EINVAL;
	}

	if (opt_param_bitmap & RA_OPT_PARAM_PHYMODE) {
		ret = mt7999_validate_rate_params(dev, rate.params.phy_mode,
						  &rate.params.mcs);
		if (ret)
			return ret;
	}

	rate.opt_param_bitmap = cpu_to_le16(opt_param_bitmap);

	mutex_lock(&dev->mt76.mutex);

	ret = mt7999_set_rate_wcid_hdr(dev, link_sta, &rate.hdr);
	if (ret)
		goto out;

	ret = mt7999_mcu_set_fixed_rate_ctrl(dev, &rate,
					     UNI_RA_FIXED_RATE_OPT_PARAM_SET, 0);
	if (ret)
		goto out;

	ret = count;
out:
	mutex_unlock(&dev->mt76.mutex);
	return ret;
}

static const struct file_operations fops_fixed_rate_opt_param = {
	.write = mt7999_link_sta_fixed_rate_opt_param_set,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

static void
mt7999_parse_rate(struct rate_info *rate, char *buf, size_t size)
{
	u32 bitrate = cfg80211_calculate_bitrate(rate);
	bool legacy = false;
	char *pos = buf;
	enum {
		GI_0_4,
		GI_0_8,
		GI_1_6,
		GI_3_2
	} gi = GI_0_8;

	if (bitrate)
		pos += snprintf(pos, size - (pos - buf), "%u.%u Mbit/s",
				bitrate / 10, bitrate % 10);

	if (rate->flags & RATE_INFO_FLAGS_MCS) {
		pos += snprintf(pos, size - (pos - buf), " HT");

		if (rate->flags & RATE_INFO_FLAGS_SHORT_GI)
			gi = GI_0_4;
	} else if (rate->flags & RATE_INFO_FLAGS_VHT_MCS) {
		pos += snprintf(pos, size - (pos - buf), " VHT");

		if (rate->flags & RATE_INFO_FLAGS_SHORT_GI)
			gi = GI_0_4;
	} else if (rate->flags & RATE_INFO_FLAGS_HE_MCS) {
		pos += snprintf(pos, size - (pos - buf), " HE");

		if (rate->he_gi == NL80211_RATE_INFO_HE_GI_1_6)
			gi = GI_1_6;
		else if (rate->he_gi == NL80211_RATE_INFO_HE_GI_3_2)
			gi = GI_3_2;
	} else if (rate->flags & RATE_INFO_FLAGS_EHT_MCS) {
		pos += snprintf(pos, size - (pos - buf), " EHT");

		if (rate->eht_gi == NL80211_RATE_INFO_EHT_GI_1_6)
			gi = GI_1_6;
		else if (rate->eht_gi == NL80211_RATE_INFO_EHT_GI_3_2)
			gi = GI_3_2;
	} else if (rate->legacy == 0) {
		pos += snprintf(pos, size - (pos - buf), "Proprietary Long Range");
	} else {
		pos += snprintf(pos, size - (pos - buf), " Legacy");
		legacy = true;
	}

	switch (rate->bw) {
	case RATE_INFO_BW_20:
		pos += snprintf(pos, size - (pos - buf), " 20MHz");
		break;
	case RATE_INFO_BW_40:
		pos += snprintf(pos, size - (pos - buf), " 40MHz");
		break;
	case RATE_INFO_BW_80:
		pos += snprintf(pos, size - (pos - buf), " 80MHz");
		break;
	case RATE_INFO_BW_160:
		pos += snprintf(pos, size - (pos - buf), " 160MHz");
		break;
	case RATE_INFO_BW_320:
		pos += snprintf(pos, size - (pos - buf), " 320MHz");
		break;
	case RATE_INFO_BW_HE_RU:
		if (rate->he_ru_alloc == NL80211_RATE_INFO_HE_RU_ALLOC_106) {
			pos += snprintf(pos, size - (pos - buf), " 106-tone RU");
			break;
		}
		fallthrough;
	default:
		pos += snprintf(pos, size - (pos - buf), " (Unknown BW)");
	}

	if (!legacy) {
		pos += snprintf(pos, size - (pos - buf), " MCS %hhu", rate->mcs);
		pos += snprintf(pos, size - (pos - buf), " NSS %hhu", rate->nss);
	}

	switch (gi) {
	case GI_0_4:
		pos += snprintf(pos, size - (pos - buf), " GI 0.4us");
		break;
	case GI_0_8:
		pos += snprintf(pos, size - (pos - buf), " GI 0.8us");
		break;
	case GI_1_6:
		pos += snprintf(pos, size - (pos - buf), " GI 1.6us");
		break;
	default:
		pos += snprintf(pos, size - (pos - buf), " GI 3.2us");
		break;
	}
}

static const char *ac_to_str(enum ieee80211_ac_numbers ac)
{
	static const char *ac_str[] = {
		[IEEE80211_AC_VO] = "VO",
		[IEEE80211_AC_VI] = "VI",
		[IEEE80211_AC_BE] = "BE",
		[IEEE80211_AC_BK] = "BK"
	};

	return ac_str[ac];
}

static int
mt7999_link_sta_info_show(struct seq_file *file, void *data)
{
	struct ieee80211_link_sta *link_sta = file->private;
	struct mt7999_sta *msta = (struct mt7999_sta *)link_sta->sta->drv_priv;
	struct mt7999_dev *dev = msta->vif->deflink.phy->dev;
	struct mt7999_sta_link *msta_link;
	struct mt76_sta_stats *stats;
	struct mt76_wcid *wcid;
	struct mt7999_phy *phy;
	unsigned long rx_mask;
	char buf[100];
	u8 ac;
	enum mt76_wcid_flags flag;
#ifdef CONFIG_MTK_DEBUG
	u32 val;
#endif

	BUILD_BUG_ON(ARRAY_SIZE(mt76_wcid_flag_names) != NUM_MT76_WCID_FLAGS);

	mutex_lock(&dev->mt76.mutex);

	msta_link = mt7999_sta_link_protected(dev, msta, link_sta->link_id);
	if (!msta_link) {
		mutex_unlock(&dev->mt76.mutex);
		return -EINVAL;
	}
	wcid = &msta_link->wcid;
	stats = &wcid->stats;
	phy = __mt7999_phy(dev, wcid->phy_idx);
	rx_mask = mt7999_rx_chainmask(phy);

	seq_printf(file, "WCID: %hu\n", wcid->idx);
	seq_printf(file, "Link ID: %hhu\n", link_sta->link_id);
	seq_printf(file, "Link Address: %pM\n", link_sta->addr);
	seq_printf(file, "Status:\n");

#define dump_chain_signal(type, buf, buflen, sta_link, rx_mask, signed)		\
	do {									\
		char *pos = (buf);						\
		int chain;							\
		size_t left = (buflen);						\
		int n;								\
		n = snprintf(pos, left, "[");					\
		pos += n; left -= n;						\
		for_each_set_bit(chain, &(rx_mask), IEEE80211_MAX_CHAINS) {	\
			if (chain) {						\
				n = snprintf(pos, left, ", ");			\
				pos += n; left -= n;				\
			}							\
			n = snprintf(pos, left, signed ? "%hhd" : "%hhu",	\
				(sta_link)->chain_##type[(chain)]);		\
			pos += n; left -= n;					\
		}								\
		snprintf(pos, left, "] dBm\n");					\
	} while (0)

	dump_chain_signal(signal, buf, sizeof(buf), msta_link, rx_mask, true);
	seq_printf(file, "\tRSSI: %d %s", msta_link->signal, buf);

	dump_chain_signal(ack_signal, buf, sizeof(buf), msta_link, rx_mask, true);
	seq_printf(file, "\tACK RSSI: %d %s", msta_link->ack_signal, buf);

	dump_chain_signal(ack_snr, buf, sizeof(buf), msta_link, rx_mask, false);
	seq_printf(file, "\tACK SNR: %s", buf);
#undef dump_chain_signal

	seq_printf(file, "Rate:\n");

	mt7999_parse_rate(&wcid->rate, buf, sizeof(buf));
	seq_printf(file, "\tTX: %s\n", buf);
	mt7999_parse_rate(&wcid->rx_rate, buf, sizeof(buf));
	seq_printf(file, "\tRX: %s\n", buf);

	seq_printf(file, "Statistics:\n");
	seq_printf(file, "\tTX:\n");
	seq_printf(file, "\t\tByte Count: %llu\n", stats->tx_bytes);
	for (ac = IEEE80211_AC_VO; ac < IEEE80211_NUM_ACS; ++ac)
		seq_printf(file, "\t\t\t%s: %llu\n", ac_to_str(ac), stats->tx_bytes_per_ac[ac]);
	seq_printf(file, "\t\tByte Fails: %llu\n", stats->tx_bytes_failed);
	for (ac = IEEE80211_AC_VO; ac < IEEE80211_NUM_ACS; ++ac)
		seq_printf(file, "\t\t\t%s: %llu\n", ac_to_str(ac), stats->tx_bytes_failed_per_ac[ac]);
	seq_printf(file, "\t\tMPDU Count: %u\n", stats->tx_mpdus);
	seq_printf(file, "\t\tMPDU Fails: %u (PER: %u.%u%%)\n", stats->tx_failed,
		   stats->tx_mpdus ? stats->tx_failed * 1000 / stats->tx_mpdus / 10 : 0,
		   stats->tx_mpdus ? stats->tx_failed * 1000 / stats->tx_mpdus % 10 : 0);
	seq_printf(file, "\t\tMPDU Retries: %u\n", stats->tx_retries);
	seq_printf(file, "\t\tAirtime: %llu (unit: 1.024 us)\n", stats->tx_airtime);
	seq_printf(file, "\t\tNumber of non-AQL packets: %u (Max: %u)\n",
		   atomic_read(&wcid->non_aql_packets), MT_MAX_NON_AQL_PKT);
	seq_printf(file, "\tRX:\n");
	seq_printf(file, "\t\tByte Count: %llu\n", stats->rx_bytes);
	for (ac = IEEE80211_AC_VO; ac < IEEE80211_NUM_ACS; ++ac)
		seq_printf(file, "\t\t\t%s: %llu\n", ac_to_str(ac), stats->rx_bytes_per_ac[ac]);
	seq_printf(file, "\t\tMPDU Count: %u\n", stats->rx_mpdus);
	seq_printf(file, "\t\tMPDU FCS Errors: %u (PER: %u.%u%%)\n", stats->rx_fcs_err,
		   stats->rx_mpdus ? stats->rx_fcs_err * 1000 / stats->rx_mpdus / 10 : 0,
		   stats->rx_mpdus ? stats->rx_fcs_err * 1000 / stats->rx_mpdus % 10 : 0);
	seq_printf(file, "\t\tAirtime: %llu (unit: 1.024 us)\n", stats->rx_airtime);

	seq_printf(file, "\tWCID flags:\n");
	for (flag = 0; flag < NUM_MT76_WCID_FLAGS; flag++) {
		if (test_bit(flag, &wcid->flags))
			seq_printf(file, "\t\t%s\n", mt76_wcid_flag_names[flag]);
	}

#ifdef CONFIG_MTK_DEBUG
	seq_printf(file, "\nWTBL RLA Information\n");
	mt76_wr(dev, MT_WTBL_DBG_RLADI,
		u32_encode_bits(wcid->idx, MT_WTBL_DBG_RLADI_WLAN_ID_MASK));
	mt76_wr(dev, MT_WTBL_DBG_RLACFG,
		MT_WTBL_DBG_RLACFG_OP_EXEC |
		MT_WTBL_DBG_RLACFG_OP_EN |
		u32_encode_bits(9, MT_WTBL_DBG_RLACFG_OP_ID_MASK));
	if (!mt76_poll(dev, MT_WTBL_DBG_RLACFG, MT_WTBL_DBG_RLACFG_OP_EXEC, 0, 5000))
		goto out;

	val = mt76_rr(dev, MT_WTBL_DBG_RLADO);

	seq_printf(file, "Primary   MLD ID = %d\n",
		   u32_get_bits(val, MT_WTBL_DBG_RLADO_LOW_MASK));
	seq_printf(file, "Secondary MLD ID = %d\n",
		   u32_get_bits(val, MT_WTBL_DBG_RLADO_HIGH_MASK));
out:
#endif /* CONFIG_MTK_DEBUG */
	mutex_unlock(&dev->mt76.mutex);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(mt7999_link_sta_info);

void mt7999_link_sta_add_debugfs(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
				 struct ieee80211_link_sta *link_sta,
				 struct dentry *dir)
{
	debugfs_create_file("fixed_rate", 0600, dir, link_sta, &fops_fixed_rate);
	debugfs_create_file("fixed_rate_reset", 0600, dir, link_sta,
			    &fops_fixed_rate_reset);
	debugfs_create_file("fixed_rate_opt_param", 0600, dir, link_sta,
			    &fops_fixed_rate_opt_param);

	/* MTK internal */
	debugfs_create_file("link_sta_info", 0400, dir, link_sta,
			    &mt7999_link_sta_info_fops);
}

static int
mt7999_vif_links_info_show(struct seq_file *s, void *data)
{
	struct ieee80211_vif *vif = s->private;
	struct mt7999_vif *mvif = (struct mt7999_vif *)vif->drv_priv;
	struct mt7999_dev *dev = mvif->deflink.phy->dev;
	struct mt7999_vif_link *mconf;
	struct mt7999_sta_link *msta_link;
	struct mt76_txq *mtxq = (struct mt76_txq *) vif->txq->drv_priv;
	unsigned long valid_links;
	u8 link_id, i;

	static const char* width_to_bw[] = {
		[NL80211_CHAN_WIDTH_40] = "40",
		[NL80211_CHAN_WIDTH_80] = "80",
		[NL80211_CHAN_WIDTH_80P80] = "80+80",
		[NL80211_CHAN_WIDTH_160] = "160",
		[NL80211_CHAN_WIDTH_5] = "5",
		[NL80211_CHAN_WIDTH_10] = "10",
		[NL80211_CHAN_WIDTH_20] = "20",
		[NL80211_CHAN_WIDTH_20_NOHT] = "20_NOHT",
		[NL80211_CHAN_WIDTH_320] = "320",
	};

	seq_printf(s, "master link id = %d\n", mvif->mt76.deflink_id);
	seq_printf(s, "group mld id = %d\n", mvif->mld_group_idx);
	seq_printf(s, "mld remap id = %d\n", mvif->mld_remap_idx);

	seq_printf(s, "valid links = 0x%x\n", vif->valid_links);
	seq_printf(s, "mtxq->wcid = %d\n", mtxq->wcid);
	for (i = 0; i < __MT_MAX_BAND; i++)
		seq_printf(s, "band%d_link_id = %d\n", i, mvif->mt76.band_to_link[i]);

	mutex_lock(&dev->mt76.mutex);
	rcu_read_lock();
	valid_links = vif->valid_links ?: BIT(0);
	for_each_set_bit(link_id, &valid_links, IEEE80211_MLD_MAX_NUM_LINKS) {
		mconf = mt7999_vif_link(dev, vif, link_id);
		if (!mconf)
			continue;

		msta_link = &mconf->msta_link;

		seq_printf(s, "- link[%02d]: bss_idx = %d, wcid = %d\n",
			   msta_link->wcid.link_id, mconf->mt76.idx, msta_link->wcid.idx);
		seq_printf(s, "            omac_idx = %d, mld_idx=%d\n",
			   mconf->mt76.omac_idx, mconf->mld_idx);

		seq_printf(s, "            band_idx=%d, channel=%d, bw%s\n",
			   mconf->mt76.band_idx,
			   mconf->phy->mt76->chandef.chan->hw_value,
			   width_to_bw[mconf->phy->mt76->chandef.width]);
	}
	rcu_read_unlock();
	mutex_unlock(&dev->mt76.mutex);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(mt7999_vif_links_info);

void mt7999_vif_add_debugfs(struct ieee80211_hw *hw, struct ieee80211_vif *vif)
{
	debugfs_create_file("mt76_links_info", 0400, vif->debugfs_dir, vif,
			    &mt7999_vif_links_info_fops);
}

#endif
