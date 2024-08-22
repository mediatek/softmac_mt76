// SPDX-License-Identifier: ISC
/*
 * Copyright (C) 2022 MediaTek Inc.
 */

#include <linux/firmware.h>
#include "mt7996.h"
#include "eeprom.h"
#include <linux/moduleparam.h>

static bool testmode_enable;
module_param(testmode_enable, bool, 0644);
MODULE_PARM_DESC(testmode_enable, "Enable testmode");

const struct ieee80211_channel dpd_2g_ch_list_bw20[] = {
	CHAN2G(3, 2422),
	CHAN2G(7, 2442),
	CHAN2G(11, 2462)
};

const struct ieee80211_channel dpd_5g_skip_ch_list[] = {
	CHAN5G(68, 5340),
	CHAN5G(72, 5360),
	CHAN5G(76, 5380),
	CHAN5G(80, 5400),
	CHAN5G(84, 5420),
	CHAN5G(88, 5440),
	CHAN5G(92, 5460),
	CHAN5G(96, 5480)
};

const struct ieee80211_channel dpd_5g_ch_list_bw80[] = {
	CHAN5G(42, 5210),
	CHAN5G(58, 5290),
	CHAN5G(106, 5530),
	CHAN5G(122, 5610),
	CHAN5G(138, 5690),
	CHAN5G(155, 5775),
	CHAN5G(171, 5855)
};

const struct ieee80211_channel dpd_5g_ch_list_bw160[] = {
	CHAN5G(50, 5250),
	CHAN5G(114, 5570),
	CHAN5G(163, 5815)
};

const struct ieee80211_channel dpd_6g_ch_list_bw80[] = {
	CHAN6G(7, 5985),
	CHAN6G(23, 6065),
	CHAN6G(39, 6145),
	CHAN6G(55, 6225),
	CHAN6G(71, 6305),
	CHAN6G(87, 6385),
	CHAN6G(103, 6465),
	CHAN6G(119, 6545),
	CHAN6G(135, 6625),
	CHAN6G(151, 6705),
	CHAN6G(167, 6785),
	CHAN6G(183, 6865),
	CHAN6G(199, 6945),
	CHAN6G(215, 7025)
};

const struct ieee80211_channel dpd_6g_ch_list_bw160[] = {
	CHAN6G(15, 6025),
	CHAN6G(47, 6185),
	CHAN6G(79, 6345),
	CHAN6G(111, 6505),
	CHAN6G(143, 6665),
	CHAN6G(175, 6825),
	CHAN6G(207, 6985)
};

const struct ieee80211_channel dpd_6g_ch_list_bw320[] = {
	CHAN6G(31, 6105),
	CHAN6G(63, 6265),
	CHAN6G(95, 6425),
	CHAN6G(127, 6585),
	CHAN6G(159, 6745),
	CHAN6G(191, 6905)
};

static int mt7996_check_eeprom(struct mt7996_dev *dev)
{
	u8 *eeprom = dev->mt76.eeprom.data;
	u16 val = get_unaligned_le16(eeprom);

	switch (val) {
	case MT7996_DEVICE_ID:
		return is_mt7996(&dev->mt76) ? 0 : -EINVAL;
	case MT7992_DEVICE_ID:
		return is_mt7992(&dev->mt76) ? 0 : -EINVAL;
	case MT7990_DEVICE_ID:
		return is_mt7990(&dev->mt76) ? 0 : -EINVAL;
	default:
		return -EINVAL;
	}
}

const char *mt7996_eeprom_name(struct mt7996_dev *dev)
{
	switch (mt76_chip(&dev->mt76)) {
	case MT7992_DEVICE_ID:
		switch (dev->var.type) {
		case MT7992_VAR_TYPE_23:
			if (dev->var.fem == MT7996_FEM_INT)
				return MT7992_EEPROM_DEFAULT_23_INT;
			return MT7992_EEPROM_DEFAULT_23;
		case MT7992_VAR_TYPE_24:
			return MT7992_EEPROM_DEFAULT_24;
		case MT7992_VAR_TYPE_44:
		default:
			if (dev->var.fem == MT7996_FEM_INT)
				return MT7992_EEPROM_DEFAULT_INT;
			if (dev->var.fem == MT7996_FEM_MIX)
				return MT7992_EEPROM_DEFAULT_MIX;
			return MT7992_EEPROM_DEFAULT;
		}
	case MT7990_DEVICE_ID:
		if (dev->var.fem == MT7996_FEM_INT)
			return MT7990_EEPROM_DEFAULT_INT;
		return MT7990_EEPROM_DEFAULT;
	case MT7996_DEVICE_ID:
	default:
		switch (dev->var.type) {
		case MT7996_VAR_TYPE_233:
			if (dev->var.fem == MT7996_FEM_INT)
				return MT7996_EEPROM_DEFAULT_233_INT;
			return MT7996_EEPROM_DEFAULT_233;
		case MT7996_VAR_TYPE_404:
			return MT7996_EEPROM_DEFAULT_404;
		case MT7996_VAR_TYPE_444:
		default:
			if (dev->var.fem == MT7996_FEM_INT)
				return MT7996_EEPROM_DEFAULT_INT;
			return MT7996_EEPROM_DEFAULT;
		}
	}
}

static void
mt7996_eeprom_parse_stream(const u8 *eeprom, u8 band_idx, u8 *path,
			   u8 *rx_path, u8 *nss)
{
	switch (band_idx) {
	case MT_BAND1:
		*path = FIELD_GET(MT_EE_WIFI_CONF2_TX_PATH_BAND1,
				  eeprom[MT_EE_WIFI_CONF + 2]);
		*rx_path = FIELD_GET(MT_EE_WIFI_CONF3_RX_PATH_BAND1,
				     eeprom[MT_EE_WIFI_CONF + 3]);
		*nss = FIELD_GET(MT_EE_WIFI_CONF5_STREAM_NUM_BAND1,
				 eeprom[MT_EE_WIFI_CONF + 5]);
		break;
	case MT_BAND2:
		*path = FIELD_GET(MT_EE_WIFI_CONF2_TX_PATH_BAND2,
				  eeprom[MT_EE_WIFI_CONF + 2]);
		*rx_path = FIELD_GET(MT_EE_WIFI_CONF4_RX_PATH_BAND2,
				     eeprom[MT_EE_WIFI_CONF + 4]);
		*nss = FIELD_GET(MT_EE_WIFI_CONF5_STREAM_NUM_BAND2,
				 eeprom[MT_EE_WIFI_CONF + 5]);
		break;
	default:
		*path = FIELD_GET(MT_EE_WIFI_CONF1_TX_PATH_BAND0,
				  eeprom[MT_EE_WIFI_CONF + 1]);
		*rx_path = FIELD_GET(MT_EE_WIFI_CONF3_RX_PATH_BAND0,
				     eeprom[MT_EE_WIFI_CONF + 3]);
		*nss = FIELD_GET(MT_EE_WIFI_CONF4_STREAM_NUM_BAND0,
				 eeprom[MT_EE_WIFI_CONF + 4]);
		break;
	}
}

static bool mt7996_eeprom_variant_valid(struct mt7996_dev *dev, const u8 *def)
{
#define FEM_INT	0
#define FEM_EXT	3
	u8 *eeprom = dev->mt76.eeprom.data, fem[2];
	int i;

	for (i = 0; i < 2; i++)
		fem[i] = u8_get_bits(eeprom[MT_EE_WIFI_CONF + 6 + i],
				     MT_EE_WIFI_PA_LNA_CONFIG);

	if (dev->var.fem == MT7996_FEM_EXT &&
	    !(fem[0] == FEM_EXT && fem[1] == FEM_EXT))
		return false;
	else if (dev->var.fem == MT7996_FEM_INT &&
		 !(fem[0] == FEM_INT && fem[1] == FEM_INT))
		return false;
	else if (dev->var.fem == MT7996_FEM_MIX &&
		 !(fem[0] == FEM_INT && fem[1] == FEM_EXT))
		return false;

	for (i = 0; i < __MT_MAX_BAND; i++) {
		u8 path, rx_path, nss;
		u8 def_path, def_rx_path, def_nss;

		if (!dev->mt76.phys[i])
			continue;

		mt7996_eeprom_parse_stream(eeprom, i, &path, &rx_path, &nss);
		mt7996_eeprom_parse_stream(def, i, &def_path, &def_rx_path,
					   &def_nss);
		if (path > def_path || rx_path > def_rx_path || nss > def_nss)
			return false;
	}

	return true;
}

static int
mt7996_eeprom_check_or_use_default(struct mt7996_dev *dev, bool use_default)
{
	u8 *eeprom = dev->mt76.eeprom.data;
	const struct firmware *fw = NULL;
	int ret;

	ret = request_firmware(&fw, mt7996_eeprom_name(dev), dev->mt76.dev);
	if (ret)
		return ret;

	if (!fw || !fw->data) {
		dev_err(dev->mt76.dev, "Invalid default bin\n");
		ret = -EINVAL;
		goto out;
	}

	if (!use_default && mt7996_eeprom_variant_valid(dev, fw->data))
		goto out;

	dev_warn(dev->mt76.dev, "eeprom load fail, use default bin\n");
	memcpy(eeprom, fw->data, MT7996_EEPROM_SIZE);
	dev->bin_file_mode = false;
	dev->flash_mode = true;
	dev->eeprom_mode = DEFAULT_BIN_MODE;
	eeprom[MT_EE_TESTMODE_EN] = dev->testmode_enable;

out:
	release_firmware(fw);

	return ret;
}

static int
mt7996_eeprom_load_bin(struct mt7996_dev *dev)
{
	u8 *eeprom = dev->mt76.eeprom.data;
	const struct firmware *fw = NULL;
	int ret;

	ret = request_firmware(&fw, dev->mt76.bin_file_name, dev->mt76.dev);
	if (ret)
		return ret;

	if (!fw || !fw->data) {
		dev_err(dev->mt76.dev, "Invalid bin %s\n", dev->mt76.bin_file_name);
		ret = -EINVAL;
		goto out;
	}

	memcpy(eeprom, fw->data, MT7996_EEPROM_SIZE);
	dev->flash_mode = true;

out:
	release_firmware(fw);
	return ret;
}

static int mt7996_eeprom_load_flash(struct mt7996_dev *dev)
{
	int ret = 1;

	/* return > 0 for load success, return 0 for load failed, return < 0 for no memory */
	dev->bin_file_mode = mt76_check_bin_file_mode(&dev->mt76);
	if (dev->bin_file_mode) {
		dev->mt76.eeprom.size = MT7996_EEPROM_SIZE;
		dev->mt76.eeprom.data = devm_kzalloc(dev->mt76.dev, dev->mt76.eeprom.size,
						     GFP_KERNEL);
		if (!dev->mt76.eeprom.data)
			return -ENOMEM;

		if (mt7996_eeprom_load_bin(dev))
			return 0;
	} else {
		ret = mt76_eeprom_init(&dev->mt76, MT7996_EEPROM_SIZE);
	}

	if (mt7996_check_eeprom(dev))
		return 0;

	return ret;
}

int mt7996_eeprom_check_fw_mode(struct mt7996_dev *dev)
{
	u8 *eeprom;
	int ret;

	dev->testmode_enable = testmode_enable;

	/* load eeprom in flash or bin file mode to determine fw mode */
	ret = mt7996_eeprom_load_flash(dev);
	if (ret <= 0)
		goto out;

	dev->flash_mode = true;
	dev->eeprom_mode = dev->bin_file_mode ? BIN_FILE_MODE : FLASH_MODE;
	eeprom = dev->mt76.eeprom.data;
	/* testmode enable priority: eeprom field > module parameter */
	dev->testmode_enable = eeprom[MT_EE_TESTMODE_EN];

out:
	return ret;
}

static int mt7996_eeprom_load(struct mt7996_dev *dev)
{
	bool use_default = false;
	int ret;

	/* flash or bin file mode eeprom is loaded before mcu init */
	if (!dev->flash_mode) {
		u32 eeprom_blk_size, block_num;
		u8 free_block_num;
		int i;

		memset(dev->mt76.eeprom.data, 0, MT7996_EEPROM_SIZE);
		if (!mt7996_has_ext_eeprom(dev)) {
			/* efuse mode */
			dev->eeprom_mode = EFUSE_MODE;
			eeprom_blk_size = MT7996_EEPROM_BLOCK_SIZE;
			ret = mt7996_mcu_get_efuse_free_block(dev, &free_block_num);
			if (ret < 0)
				return ret;

			/* efuse info isn't enough */
			if (free_block_num >= 59) {
				use_default = true;
				goto out;
			}
		} else {
			/* external eeprom mode */
			dev->eeprom_mode = EXT_EEPROM_MODE;
			eeprom_blk_size = MT7996_EXT_EEPROM_BLOCK_SIZE;
		}

		/* check if eeprom data from fw is valid */
		if (mt7996_mcu_get_eeprom(dev, 0, NULL, eeprom_blk_size,
					  dev->eeprom_mode) ||
		    mt7996_check_eeprom(dev)) {
			use_default = true;
			goto out;
		}

		/* read eeprom data from fw */
		block_num = DIV_ROUND_UP(MT7996_EEPROM_SIZE, eeprom_blk_size);
		for (i = 1; i < block_num; i++) {
			u32 len = eeprom_blk_size;

			if (i == block_num - 1)
				len = MT7996_EEPROM_SIZE % eeprom_blk_size;
			ret = mt7996_mcu_get_eeprom(dev, i * eeprom_blk_size,
						    NULL, len, dev->eeprom_mode);
			if (ret && ret != -EINVAL) {
				use_default = true;
				goto out;
			}
		}
	}

out:
	return mt7996_eeprom_check_or_use_default(dev, use_default);
}

static int mt7996_eeprom_parse_efuse_hw_cap(struct mt7996_phy *phy,
					    u8 *path, u8 *rx_path, u8 *nss)
{
#define MODE_HE_ONLY		BIT(0)
#define WTBL_SIZE_GROUP		GENMASK(31, 28)
#define STREAM_CAP(_offs)	((cap & (0x7 << (_offs))) >> (_offs))
	struct mt7996_dev *dev = phy->dev;
	u32 cap = 0;
	int ret;

	ret = mt7996_mcu_get_chip_config(dev, &cap);
	if (ret)
		return ret;

	/* for internal development */
	dev->has_eht = true;
	if (cap) {
		u8 band_offs = phy->mt76->band_idx * 3;

		dev->has_eht = !(cap & MODE_HE_ONLY);
		dev->wtbl_size_group = u32_get_bits(cap, WTBL_SIZE_GROUP);
		*nss = min_t(u8, *nss, STREAM_CAP(1 + band_offs));
		*path = min_t(u8, *path, STREAM_CAP(10 + band_offs));
		*rx_path = min_t(u8, *rx_path, STREAM_CAP(19 + band_offs));
	}

	if (dev->wtbl_size_group < 2 || dev->wtbl_size_group > 4)
		dev->wtbl_size_group = is_mt7996(&dev->mt76) ? 4 : 2;

	return 0;
}

static int mt7996_eeprom_parse_band_config(struct mt7996_phy *phy)
{
	u8 *eeprom = phy->dev->mt76.eeprom.data;
	u32 val = eeprom[MT_EE_WIFI_CONF];
	int ret = 0;

	switch (phy->mt76->band_idx) {
	case MT_BAND1:
		val = FIELD_GET(MT_EE_WIFI_CONF1_BAND_SEL, val);
		break;
	case MT_BAND2:
		val = eeprom[MT_EE_WIFI_CONF + 1];
		val = FIELD_GET(MT_EE_WIFI_CONF2_BAND_SEL, val);
		break;
	default:
		val = FIELD_GET(MT_EE_WIFI_CONF0_BAND_SEL, val);
		break;
	}

	switch (val) {
	case MT_EE_BAND_SEL_2GHZ:
		phy->mt76->cap.has_2ghz = true;
		break;
	case MT_EE_BAND_SEL_5GHZ:
		phy->mt76->cap.has_5ghz = true;
		break;
	case MT_EE_BAND_SEL_6GHZ:
		phy->mt76->cap.has_6ghz = true;
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

int mt7996_eeprom_parse_hw_cap(struct mt7996_dev *dev, struct mt7996_phy *phy)
{
	u8 path, rx_path, nss, band_idx = phy->mt76->band_idx;
	u8 *eeprom = dev->mt76.eeprom.data;
	struct mt76_phy *mphy = phy->mt76;
	int max_path = 5, max_nss = 4;
	int ret;

	mt7996_eeprom_parse_stream(eeprom, band_idx, &path, &rx_path, &nss);
	ret = mt7996_eeprom_parse_efuse_hw_cap(phy, &path, &rx_path, &nss);
	if (ret)
		return ret;

	if (!path || path > max_path)
		path = max_path;

	if (!nss || nss > max_nss)
		nss = max_nss;

	nss = min_t(u8, nss, path);

	if (path != rx_path)
		phy->has_aux_rx = true;

	mphy->antenna_mask = BIT(nss) - 1;
	phy->orig_antenna_mask = mphy->antenna_mask;
	mphy->chainmask = (BIT(path) - 1) << dev->chainshift[band_idx];
	phy->orig_chainmask = mphy->chainmask;
	dev->chainmask |= mphy->chainmask;
	if (band_idx < MT_BAND2)
		dev->chainshift[band_idx + 1] = dev->chainshift[band_idx] +
						hweight16(mphy->chainmask);

	return mt7996_eeprom_parse_band_config(phy);
}

static int
mt7996_eeprom_load_precal_binfile(struct mt7996_dev *dev, u32 offs, u32 size)
{
	const struct firmware *fw = NULL;
	int ret;

	ret = request_firmware(&fw, dev->mt76.bin_file_name, dev->mt76.dev);
	if (ret)
		return ret;

	if (!fw || !fw->data) {
		dev_err(dev->mt76.dev, "Invalid bin (bin file mode), load precal fail\n");
		ret = -EINVAL;
		goto out;
	}

	memcpy(dev->cal, fw->data + offs, size);

out:
	release_firmware(fw);

	return ret;
}

static void mt7996_eeprom_init_precal(struct mt7996_dev *dev)
{
#define MT76_CHANNELS_5GHZ_SIZE		36	/* ARRAY_SIZE(mt76_channels_5ghz) */
#define MT76_CHANNELS_6GHZ_SIZE		59	/* ARRAY_SIZE(mt76_channels_6ghz) */

	dev->prek.dpd_ch_num[DPD_CH_NUM_BW20_2G] = ARRAY_SIZE(dpd_2g_ch_list_bw20);
	dev->prek.dpd_ch_num[DPD_CH_NUM_BW20_5G_SKIP] = ARRAY_SIZE(dpd_5g_skip_ch_list);
	dev->prek.dpd_ch_num[DPD_CH_NUM_BW20_5G] = MT76_CHANNELS_5GHZ_SIZE -
						   DPD_CH_NUM(BW20_5G_SKIP);
	dev->prek.dpd_ch_num[DPD_CH_NUM_BW160_5G] = ARRAY_SIZE(dpd_5g_ch_list_bw160);
	dev->prek.dpd_ch_num[DPD_CH_NUM_BW20_6G] = MT76_CHANNELS_6GHZ_SIZE;
	dev->prek.dpd_ch_num[DPD_CH_NUM_BW160_6G] = ARRAY_SIZE(dpd_6g_ch_list_bw160);

	switch (mt76_chip(&dev->mt76)) {
	case MT7996_DEVICE_ID:
		dev->prek.rev = mt7996_prek_rev;
		if (dev->var.type == MT7996_VAR_TYPE_233)
			dev->prek.rev = mt7996_prek_rev_233;
		/* 5g & 6g bw 80 dpd channel list is not used */
		dev->prek.dpd_ch_num[DPD_CH_NUM_BW320_6G] = ARRAY_SIZE(dpd_6g_ch_list_bw320);
		break;
	case MT7992_DEVICE_ID:
		dev->prek.rev  = mt7992_prek_rev;
		dev->prek.dpd_ch_num[DPD_CH_NUM_BW80_5G] = ARRAY_SIZE(dpd_5g_ch_list_bw80);
		/* 6g is not used in current sku */
		dev->prek.dpd_ch_num[DPD_CH_NUM_BW20_6G] = 0;
		dev->prek.dpd_ch_num[DPD_CH_NUM_BW80_6G] = 0;
		dev->prek.dpd_ch_num[DPD_CH_NUM_BW160_6G] = 0;
		break;
	default:
		dev->prek.rev  = mt7996_prek_rev;
		break;
	}
}

static int mt7996_eeprom_load_precal(struct mt7996_dev *dev)
{
	struct mt76_dev *mdev = &dev->mt76;
	u8 *eeprom = mdev->eeprom.data;
	u32 val = eeprom[MT_EE_DO_PRE_CAL];
	u32 size, offs = MT_EE_PRECAL;
	int ret;

	mt7996_eeprom_init_precal(dev);

	if (!dev->flash_mode || !val)
		return 0;

	size = MT_EE_CAL_GROUP_SIZE + MT_EE_CAL_DPD_SIZE;

	dev->cal = devm_kzalloc(mdev->dev, size, GFP_KERNEL);
	if (!dev->cal) {
		ret = -ENOMEM;
		goto fail;
	}

	if (dev->bin_file_mode) {
		ret = mt7996_eeprom_load_precal_binfile(dev, offs, size);
		if (ret)
			goto fail;
	}

	ret = mt76_get_of_data_from_mtd(mdev, dev->cal, offs, size);
	if (!ret)
		return 0;

	ret = mt76_get_of_data_from_nvmem(mdev, dev->cal, "precal", size);
	if (!ret)
		return 0;

fail:
	dev_warn(dev->mt76.dev, "Failed to load precal data: %d\n", ret);
	devm_kfree(dev->mt76.dev, dev->cal);
	dev->cal = NULL;

	return ret;
}

int mt7996_eeprom_init(struct mt7996_dev *dev)
{
	int ret;

	ret = mt7996_eeprom_load(dev);
	if (ret)
		return ret;

	mt7996_eeprom_load_precal(dev);

	ret = mt7996_eeprom_parse_hw_cap(dev, &dev->phy);
	if (ret < 0)
		return ret;

	memcpy(dev->mphy.macaddr, dev->mt76.eeprom.data + MT_EE_MAC_ADDR, ETH_ALEN);
	mt76_eeprom_override(&dev->mphy);

	return 0;
}

int mt7996_eeprom_get_target_power(struct mt7996_dev *dev,
				   struct ieee80211_channel *chan)
{
	u8 *eeprom = dev->mt76.eeprom.data;
	int target_power;

	if (chan->band == NL80211_BAND_5GHZ)
		target_power = eeprom[MT_EE_TX0_POWER_5G +
				      mt7996_get_channel_group_5g(chan->hw_value)];
	else if (chan->band == NL80211_BAND_6GHZ)
		target_power = eeprom[MT_EE_TX0_POWER_6G +
				      mt7996_get_channel_group_6g(chan->hw_value)];
	else
		target_power = eeprom[MT_EE_TX0_POWER_2G];

	return target_power;
}

s8 mt7996_eeprom_get_power_delta(struct mt7996_dev *dev, int band)
{
	u8 *eeprom = dev->mt76.eeprom.data;
	u32 val;
	s8 delta;

	if (band == NL80211_BAND_5GHZ)
		val = eeprom[MT_EE_RATE_DELTA_5G];
	else if (band == NL80211_BAND_6GHZ)
		val = eeprom[MT_EE_RATE_DELTA_6G];
	else
		val = eeprom[MT_EE_RATE_DELTA_2G];

	if (!(val & MT_EE_RATE_DELTA_EN))
		return 0;

	delta = FIELD_GET(MT_EE_RATE_DELTA_MASK, val);

	return val & MT_EE_RATE_DELTA_SIGN ? delta : -delta;
}

bool mt7996_eeprom_has_background_radar(struct mt7996_dev *dev)
{
	switch (mt76_chip(&dev->mt76)) {
	case MT7996_DEVICE_ID:
		if (dev->var.type == MT7996_VAR_TYPE_233)
			return false;
		break;
	case MT7992_DEVICE_ID:
		if (dev->var.type == MT7992_VAR_TYPE_23)
			return false;
		break;
	case MT7990_DEVICE_ID: {
		u8 path, rx_path, nss, *eeprom = dev->mt76.eeprom.data;

		mt7996_eeprom_parse_stream(eeprom, MT_BAND1, &path, &rx_path, &nss);
		/* Disable background radar capability in 3T3R */
		if (path == 3 || rx_path == 3)
			return false;
		break;
		}
	default:
		return false;
	}

	return true;
}

const u8 mt7996_sku_group_len[] = {
	[SKU_CCK] = 4,
	[SKU_OFDM] = 8,
	[SKU_HT20] = 8,
	[SKU_HT40] = 9,
	[SKU_VHT20] = 12,
	[SKU_VHT40] = 12,
	[SKU_VHT80] = 12,
	[SKU_VHT160] = 12,
	[SKU_HE26] = 12,
	[SKU_HE52] = 12,
	[SKU_HE106] = 12,
	[SKU_HE242] = 12,
	[SKU_HE484] = 12,
	[SKU_HE996] = 12,
	[SKU_HE2x996] = 12,
	[SKU_EHT26] = 16,
	[SKU_EHT52] = 16,
	[SKU_EHT106] = 16,
	[SKU_EHT242] = 16,
	[SKU_EHT484] = 16,
	[SKU_EHT996] = 16,
	[SKU_EHT2x996] = 16,
	[SKU_EHT4x996] = 16,
	[SKU_EHT26_52] = 16,
	[SKU_EHT26_106] = 16,
	[SKU_EHT484_242] = 16,
	[SKU_EHT996_484] = 16,
	[SKU_EHT996_484_242] = 16,
	[SKU_EHT2x996_484] = 16,
	[SKU_EHT3x996] = 16,
	[SKU_EHT3x996_484] = 16,
};
