// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (C) 2016 Felix Fietkau <nbd@nbd.name>
 */
#include <linux/of.h>
#include <linux/of_net.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/nvmem-consumer.h>
#include <linux/etherdevice.h>
#include <linux/fs.h>
#include "mt76.h"
#include "mt76_connac.h"

enum mt76_sku_type {
	MT76_SKU_RATE,
	MT76_SKU_BACKOFF,
	MT76_SKU_BACKOFF_BF_OFFSET,
};

struct chan_section {
	int chan[2];
	int chan_num;
	int txs_delta[4];
	int txs_delta_num;
};

static int mt76_get_of_eeprom_data(struct mt76_dev *dev, void *eep, int len)
{
	struct device_node *np = dev->dev->of_node;
	const void *data;
	int size;

	data = of_get_property(np, "mediatek,eeprom-data", &size);
	if (!data)
		return -ENOENT;

	if (size > len)
		return -EINVAL;

	memcpy(eep, data, size);

	return 0;
}

int mt76_get_of_data_from_mtd(struct mt76_dev *dev, void *eep, int offset, int len)
{
#ifdef CONFIG_MTD
	struct device_node *np = dev->dev->of_node;
	struct mtd_info *mtd;
	const __be32 *list;
	const char *part;
	phandle phandle;
	size_t retlen;
	int size;
	int ret;

	list = of_get_property(np, "mediatek,mtd-eeprom", &size);
	if (!list)
		return -ENOENT;

	phandle = be32_to_cpup(list++);
	if (!phandle)
		return -ENOENT;

	np = of_find_node_by_phandle(phandle);
	if (!np)
		return -EINVAL;

	part = of_get_property(np, "label", NULL);
	if (!part)
		part = np->name;

	mtd = get_mtd_device_nm(part);
	if (IS_ERR(mtd)) {
		ret =  PTR_ERR(mtd);
		goto out_put_node;
	}

	if (size <= sizeof(*list)) {
		ret = -EINVAL;
		goto out_put_node;
	}

	offset += be32_to_cpup(list);
	ret = mtd_read(mtd, offset, len, &retlen, eep);
	put_mtd_device(mtd);
	if (mtd_is_bitflip(ret))
		ret = 0;
	if (ret) {
		dev_err(dev->dev, "reading EEPROM from mtd %s failed: %i\n",
			part, ret);
		goto out_put_node;
	}

	if (retlen < len) {
		ret = -EINVAL;
		goto out_put_node;
	}

	if (of_property_read_bool(dev->dev->of_node, "big-endian")) {
		u8 *data = (u8 *)eep;
		int i;

		/* convert eeprom data in Little Endian */
		for (i = 0; i < round_down(len, 2); i += 2)
			put_unaligned_le16(get_unaligned_be16(&data[i]),
					   &data[i]);
	}

#ifdef CONFIG_NL80211_TESTMODE
	if (len == dev->eeprom.size) {
		dev->test_mtd.name = devm_kstrdup(dev->dev, part, GFP_KERNEL);
		dev->test_mtd.offset = offset;
	}
#endif

out_put_node:
	of_node_put(np);
	return ret;
#else
	return -ENOENT;
#endif
}
EXPORT_SYMBOL_GPL(mt76_get_of_data_from_mtd);

static void mt76_get_nvmem_part(struct mt76_dev *dev)
{
#ifdef CONFIG_NL80211_TESTMODE
	struct device_node *np = dev->dev->of_node;
	const __be32 *list;
	const char *part;
	phandle phandle;
	int offset, size;
	u32 reg[2];

	list = of_get_property(np, "nvmem-cells", NULL);
	if (!list)
		return;

	phandle = be32_to_cpup(list++);
	if (!phandle)
		return;

	np = of_find_node_by_phandle(phandle);
	if (!np)
		return;

	if (of_property_read_u32_array(np, "reg", reg, 2))
		return;

	offset = reg[0];
	size = reg[1];

	np = of_get_parent(of_get_parent(np));
	if (!np)
		return;

	part = of_get_property(np, "partname", NULL);
	if (!part)
		part = of_get_property(np, "volname", NULL);
	if (!part)
		return;

	if (size != dev->eeprom.size)
		return;

	dev->test_mtd.name = devm_kstrdup(dev->dev, part, GFP_KERNEL);
	dev->test_mtd.offset = offset;
#endif
}

int mt76_get_of_data_from_nvmem(struct mt76_dev *dev, void *eep,
				const char *cell_name, int len)
{
	struct device_node *np = dev->dev->of_node;
	struct nvmem_cell *cell;
	const void *data;
	size_t retlen;
	int ret = 0;

	cell = of_nvmem_cell_get(np, cell_name);
	if (IS_ERR(cell))
		return PTR_ERR(cell);

	mt76_get_nvmem_part(dev);

	data = nvmem_cell_read(cell, &retlen);
	nvmem_cell_put(cell);

	if (IS_ERR(data))
		return PTR_ERR(data);

	if (retlen < len) {
		ret = -EINVAL;
		goto exit;
	}

	memcpy(eep, data, len);

exit:
	kfree(data);

	return ret;
}
EXPORT_SYMBOL_GPL(mt76_get_of_data_from_nvmem);

static int mt76_get_of_eeprom(struct mt76_dev *dev, void *eep, int len)
{
	struct device_node *np = dev->dev->of_node;
	int ret;

	if (!np)
		return -ENOENT;

	ret = mt76_get_of_eeprom_data(dev, eep, len);
	if (!ret)
		return 0;

	ret = mt76_get_of_data_from_mtd(dev, eep, 0, len);
	if (!ret)
		return 0;

	return mt76_get_of_data_from_nvmem(dev, eep, "eeprom", len);
}

bool mt76_check_bin_file_mode(struct mt76_dev *dev)
{
	struct device_node *np = dev->dev->of_node;
	const char *bin_file_name = NULL;

	if (!np)
		return false;

	of_property_read_string(np, "bin_file_name", &bin_file_name);

	dev->bin_file_name = bin_file_name;
	if (dev->bin_file_name) {
		dev_info(dev->dev, "Using bin file %s\n", dev->bin_file_name);
#ifdef CONFIG_NL80211_TESTMODE
		dev->test_mtd.name = devm_kstrdup(dev->dev, bin_file_name, GFP_KERNEL);
		dev->test_mtd.offset = -1;
#endif
	}

	return dev->bin_file_name ? true : false;
}
EXPORT_SYMBOL_GPL(mt76_check_bin_file_mode);

int
mt76_eeprom_override(struct mt76_phy *phy)
{
	struct mt76_dev *dev = phy->dev;
	struct device_node *np = dev->dev->of_node, *band_np;
	bool found_mac = false;
	u32 reg;
	int ret;

	for_each_child_of_node(np, band_np) {
		ret = of_property_read_u32(band_np, "reg", &reg);
		if (ret)
			continue;

		if (reg != phy->band_idx)
			continue;

		ret = of_get_mac_address(band_np, phy->macaddr);
		if (ret == -EPROBE_DEFER)
			return ret;

		found_mac = !ret;
		break;
	}

	if (!found_mac) {
		ret = of_get_mac_address(np, phy->macaddr);
		if (ret == -EPROBE_DEFER)
			return ret;
	}

	if (!is_valid_ether_addr(phy->macaddr)) {
		eth_random_addr(phy->macaddr);
		dev_info(dev->dev,
			 "Invalid MAC address, using random address %pM\n",
			 phy->macaddr);
	}

	return 0;
}
EXPORT_SYMBOL_GPL(mt76_eeprom_override);

static bool mt76_string_prop_find(struct property *prop, const char *str)
{
	const char *cp = NULL;

	if (!prop || !str || !str[0])
		return false;

	while ((cp = of_prop_next_string(prop, cp)) != NULL)
		if (!strcasecmp(cp, str))
			return true;

	return false;
}

struct device_node *
mt76_find_power_limits_node(struct mt76_phy *phy)
{
	struct mt76_dev *dev = phy->dev;
	struct device_node *np = dev->dev->of_node;
	const char *const region_names[] = {
		[NL80211_DFS_UNSET] = "ww",
		[NL80211_DFS_ETSI] = "etsi",
		[NL80211_DFS_FCC] = "fcc",
		[NL80211_DFS_JP] = "jp",
	};
	struct device_node *cur, *fallback = NULL;
	const char *region_name = NULL;
	char index[4] = {0};

	if (dev->region < ARRAY_SIZE(region_names))
		region_name = region_names[dev->region];

	np = of_get_child_by_name(np, "power-limits");
	if (!np)
		return NULL;

	snprintf(index, sizeof(index), "%d", phy->sku_idx);
	for_each_child_of_node(np, cur) {
		struct property *country = of_find_property(cur, "country", NULL);
		struct property *regd = of_find_property(cur, "regdomain", NULL);
		struct property *sku_index = of_find_property(cur, "sku-index", NULL);

		if (!country && !regd) {
			fallback = cur;
			continue;
		}

		if (phy->sku_idx && !mt76_string_prop_find(sku_index, index))
			continue;

		if (mt76_string_prop_find(country, dev->alpha2) ||
		    mt76_string_prop_find(regd, region_name)) {
			of_node_put(np);
			return cur;
		}
	}

	of_node_put(np);
	return fallback;
}
EXPORT_SYMBOL_GPL(mt76_find_power_limits_node);

static const __be32 *
mt76_get_of_array(struct device_node *np, char *name, size_t *len, int min)
{
	struct property *prop = of_find_property(np, name, NULL);

	if (!prop || !prop->value || prop->length < min * 4)
		return NULL;

	*len = prop->length;

	return prop->value;
}

static const s8 *
mt76_get_of_array_s8(struct device_node *np, char *name, size_t *len, int min)
{
	struct property *prop = of_find_property(np, name, NULL);

	if (!prop || !prop->value || prop->length < min)
		return NULL;

	*len = prop->length;

	return prop->value;
}

struct device_node *
mt76_find_channel_node(struct device_node *np, struct ieee80211_channel *chan)
{
	struct device_node *cur;
	const __be32 *val;
	size_t len;

	for_each_child_of_node(np, cur) {
		val = mt76_get_of_array(cur, "channels", &len, 2);
		if (!val)
			continue;

		while (len >= 2 * sizeof(*val)) {
			if (chan->hw_value >= be32_to_cpu(val[0]) &&
			    chan->hw_value <= be32_to_cpu(val[1]))
				return cur;

			val += 2;
			len -= 2 * sizeof(*val);
		}
	}

	return NULL;
}
EXPORT_SYMBOL_GPL(mt76_find_channel_node);

static s8
mt76_get_txs_delta(struct device_node *np, u8 nss)
{
	const __be32 *val;
	size_t len;

	val = mt76_get_of_array(np, "txs-delta", &len, nss);
	if (!val)
		return 0;

	return be32_to_cpu(val[nss - 1]);
}

static inline u8 mt76_backoff_n_chains(struct mt76_dev *dev, u8 idx)
{
	/* 0:1T1ss, 1:2T1ss, ..., 14:5T5ss */
	static const u8 connac3_table[] = {
		1, 2, 3, 4, 5, 2, 3, 4, 5, 3, 4, 5, 4, 5, 5};
	static const u8 connac2_table[] = {
		1, 2, 3, 4, 2, 3, 4, 3, 4, 4, 0, 0, 0, 0, 0};

	if (idx >= ARRAY_SIZE(connac3_table))
		return 0;

	return is_mt799x(dev) ? connac3_table[idx] : connac2_table[idx];
}

static void
mt76_apply_array_limit(struct mt76_dev *dev, s8 *pwr, size_t pwr_len,
		       const s8 *data, s8 target_power, s8 nss_delta,
		       s8 *max_power, int n_chains, enum mt76_sku_type type)
{
	int i;

	if (!data)
		return;

	for (i = 0; i < pwr_len; i++) {
		u8 backoff_chain_idx = i;
		int backoff_n_chains;
		s8 backoff_delta;
		s8 delta;

		switch (type) {
		case MT76_SKU_RATE:
			delta = 0;
			backoff_delta = 0;
			backoff_n_chains = 0;
			break;
		case MT76_SKU_BACKOFF_BF_OFFSET:
			backoff_chain_idx += 1;
			fallthrough;
		case MT76_SKU_BACKOFF:
			delta = mt76_tx_power_path_delta(n_chains);
			backoff_n_chains = mt76_backoff_n_chains(dev, backoff_chain_idx);
			backoff_delta = mt76_tx_power_path_delta(backoff_n_chains);
			break;
		default:
			return;
		}

		pwr[i] = min_t(s8, target_power + delta - backoff_delta, data[i] + nss_delta);

		/* used for padding, doesn't need to be considered */
		if (data[i] >= S8_MAX - 1)
			continue;

		/* only consider backoff value for the configured chain number */
		if (type != MT76_SKU_RATE && n_chains != backoff_n_chains)
			continue;

		*max_power = max(*max_power, pwr[i]);
	}
}

static void
mt76_apply_multi_array_limit(struct mt76_dev *dev, s8 *pwr, size_t pwr_len,
			     s8 pwr_num, const s8 *data, size_t len,
			     s8 target_power, s8 nss_delta, s8 *max_power,
			     int n_chains, enum mt76_sku_type type)
{
	static const int connac2_backoff_ru_idx = 2;
	int i, cur;

	if (!data)
		return;

	cur = data[0];
	for (i = 0; i < pwr_num; i++) {
		if (len < pwr_len + 1)
			break;

		/* Each RU entry (RU26, RU52, RU106, BW20, ...) in the DTS
		 * corresponds to 10 stream combinations (1T1ss, 2T1ss, 3T1ss,
		 * 4T1ss, 2T2ss, 3T2ss, 4T2ss, 3T3ss, 4T3ss, 4T4ss).
		 *
		 * For beamforming tables:
		 * - In connac2, beamforming entries for BW20~BW160 and OFDM
		 *   do not include 1T1ss.
		 * - In connac3, beamforming entries for BW20~BW160 and RU
		 *   include 1T1ss, but OFDM beamforming does not include 1T1ss.
		 *
		 * Non-beamforming and RU entries for both connac2 and connac3
		 * include 1T1ss.
		 */
		if (!is_mt799x(dev) && type == MT76_SKU_BACKOFF &&
		    i > connac2_backoff_ru_idx)
			type = MT76_SKU_BACKOFF_BF_OFFSET;

		mt76_apply_array_limit(dev, pwr + pwr_len * i, pwr_len, data + 1,
				       target_power, nss_delta, max_power,
				       n_chains, type);
		if (--cur > 0)
			continue;

		data += pwr_len + 1;
		len -= pwr_len + 1;
		if (!len)
			break;

		cur = data[0];
	}
}

s8 mt76_get_rate_power_limits(struct mt76_phy *phy,
			      struct ieee80211_channel *chan,
			      struct mt76_power_limits *dest,
			      s8 target_power)
{
	struct mt76_dev *dev = phy->dev;
	struct device_node *np;
	const s8 *val;
	char name[16];
	char band;
	size_t len;
	s8 max_power = -127;
	s8 txs_delta;
	int n_chains = hweight16(phy->chainmask);

	memset(dest, target_power, sizeof(*dest) - sizeof(dest->path));
	memset(&dest->path, 0, sizeof(dest->path));

	if (!IS_ENABLED(CONFIG_OF))
		return target_power;

	np = mt76_find_power_limits_node(phy);
	if (!np)
		return target_power;

	switch (chan->band) {
	case NL80211_BAND_2GHZ:
		band = '2';
		break;
	case NL80211_BAND_5GHZ:
		band = '5';
		break;
	case NL80211_BAND_6GHZ:
		band = '6';
		break;
	default:
		return target_power;
	}

	snprintf(name, sizeof(name), "txpower-%cg", band);
	np = of_get_child_by_name(np, name);
	if (!np)
		return target_power;

	np = mt76_find_channel_node(np, chan);
	if (!np)
		return target_power;

	txs_delta = mt76_get_txs_delta(np, hweight16(phy->chainmask));

	val = mt76_get_of_array_s8(np, "rates-cck", &len, ARRAY_SIZE(dest->cck));
	mt76_apply_array_limit(dev, dest->cck, ARRAY_SIZE(dest->cck), val,
			       target_power, txs_delta, &max_power, n_chains, MT76_SKU_RATE);

	val = mt76_get_of_array_s8(np, "rates-ofdm", &len, ARRAY_SIZE(dest->ofdm));
	mt76_apply_array_limit(dev, dest->ofdm, ARRAY_SIZE(dest->ofdm), val,
			       target_power, txs_delta, &max_power, n_chains, MT76_SKU_RATE);

	val = mt76_get_of_array_s8(np, "rates-mcs", &len, ARRAY_SIZE(dest->mcs[0]) + 1);
	mt76_apply_multi_array_limit(dev, dest->mcs[0], ARRAY_SIZE(dest->mcs[0]),
				     ARRAY_SIZE(dest->mcs), val, len, target_power,
				     txs_delta, &max_power, n_chains, MT76_SKU_RATE);

	val = mt76_get_of_array_s8(np, "rates-ru", &len, ARRAY_SIZE(dest->ru[0]) + 1);
	mt76_apply_multi_array_limit(dev, dest->ru[0], ARRAY_SIZE(dest->ru[0]),
				     ARRAY_SIZE(dest->ru), val, len, target_power,
				     txs_delta, &max_power, n_chains, MT76_SKU_RATE);

	val = mt76_get_of_array_s8(np, "rates-eht", &len,
				   ARRAY_SIZE(dest->eht[0]) + 1);
	mt76_apply_multi_array_limit(dev, dest->eht[0], ARRAY_SIZE(dest->eht[0]),
				     ARRAY_SIZE(dest->eht), val, len, target_power,
				     txs_delta, &max_power, n_chains, MT76_SKU_RATE);

	val = mt76_get_of_array_s8(np, "paths-cck", &len, ARRAY_SIZE(dest->path.cck));
	mt76_apply_array_limit(dev, dest->path.cck, ARRAY_SIZE(dest->path.cck), val,
			       target_power, txs_delta, &max_power, n_chains, MT76_SKU_BACKOFF);

	val = mt76_get_of_array_s8(np, "paths-ofdm", &len, ARRAY_SIZE(dest->path.ofdm));
	mt76_apply_array_limit(dev, dest->path.ofdm, ARRAY_SIZE(dest->path.ofdm), val,
			       target_power, txs_delta, &max_power, n_chains, MT76_SKU_BACKOFF);

	val = mt76_get_of_array_s8(np, "paths-ofdm-bf", &len, ARRAY_SIZE(dest->path.ofdm_bf));
	mt76_apply_array_limit(dev, dest->path.ofdm_bf, ARRAY_SIZE(dest->path.ofdm_bf), val,
			       target_power, txs_delta, &max_power, n_chains,
			       MT76_SKU_BACKOFF_BF_OFFSET);

	val = mt76_get_of_array_s8(np, "paths-ru", &len, ARRAY_SIZE(dest->path.ru[0]) + 1);
	mt76_apply_multi_array_limit(dev, dest->path.ru[0], ARRAY_SIZE(dest->path.ru[0]),
				     ARRAY_SIZE(dest->path.ru), val, len, target_power,
				     txs_delta, &max_power, n_chains, MT76_SKU_BACKOFF);

	val = mt76_get_of_array_s8(np, "paths-ru-bf", &len, ARRAY_SIZE(dest->path.ru_bf[0]) + 1);
	mt76_apply_multi_array_limit(dev, dest->path.ru_bf[0], ARRAY_SIZE(dest->path.ru_bf[0]),
				     ARRAY_SIZE(dest->path.ru_bf), val, len, target_power,
				     txs_delta, &max_power, n_chains, MT76_SKU_BACKOFF);

	return max_power;
}
EXPORT_SYMBOL_GPL(mt76_get_rate_power_limits);

int
mt76_eeprom_init(struct mt76_dev *dev, int len)
{
	dev->eeprom.size = len;
	dev->eeprom.data = devm_kzalloc(dev->dev, len, GFP_KERNEL);
	if (!dev->eeprom.data)
		return -ENOMEM;

	return !mt76_get_of_eeprom(dev, dev->eeprom.data, len);
}
EXPORT_SYMBOL_GPL(mt76_eeprom_init);

/* read power table from filesystem */

static int mt76_parse_list(const char *str, void *arr, int max,
			   long min, long maxv, size_t elem_size)
{
	char *s, *p, *tok;
	long val;
	int ret, cnt = 0;

	s = kstrdup(str, GFP_KERNEL);
	if (!s)
		return -ENOMEM;

	p = s;
	while ((tok = strsep(&p, ",")) && cnt < max) {
		if (!*tok)
			goto err;

		ret = kstrtol(tok, 10, &val);
		if (ret < 0)
			goto err;

		val = clamp(val, min, maxv);

		if (elem_size == sizeof(s8))
			((s8 *)arr)[cnt++] = (s8)val;
		else if (elem_size == sizeof(int))
			((int *)arr)[cnt++] = (int)val;
		else
			goto err;
	}

	kfree(s);
	return cnt;
err:
	kfree(s);
	return -EINVAL;
}

#define mt76_parse_s8(str, arr, max) \
	mt76_parse_list((str), (arr), (max), 0L, 127L, sizeof(s8))

#define mt76_parse_int(str, arr, max) \
	mt76_parse_list((str), (arr), (max), 0L, 233L, sizeof(int))

static int
mt76_parse_s8_2d(const char *str, s8 *arr, int group_max, int elem_max)
{
	int ret, group = 0;
	char *s, *p, *tok;

	s = kstrdup(str, GFP_KERNEL);
	if (!s)
		return -ENOMEM;

	p = s;
	while ((tok = strsep(&p, ";")) && group < group_max) {
		ret = mt76_parse_s8(tok, arr + group * elem_max, elem_max);
		if (ret < 0) {
			kfree(s);
			return ret;
		}
		group++;
	}
	kfree(s);
	return group;
}

static ssize_t
mt76_read_file(const char *path, char *buf, size_t buf_size)
{
	struct file *filp;
	loff_t pos = 0;
	ssize_t ret;

	filp = filp_open(path, O_RDONLY, 0);
	if (IS_ERR(filp))
		return PTR_ERR(filp);

	ret = kernel_read(filp, buf, buf_size - 1, &pos);
	if (ret >= 0)
		buf[ret] = '\0';

	filp_close(filp, NULL);
	return ret;
}

static int mt76_find_file_section(const char *buf, const char *section_name,
				  const char **section_start, size_t *section_len)
{
	const char *start, *end;
	size_t len;

	start = strstr(buf, section_name);
	if (!start)
		return -ENOENT;

	start = strchr(start, '\n');
	if (!start)
		return -ENOENT;

	start++;	/* skip section header */

	end = strchr(start, '[');
	if (!end)
		end = start + strlen(start);

	len = end - start;

	*section_start = start;
	*section_len = len;
	return 0;
}

static int mt76_parse_file_chan_block(const char *section, size_t section_len,
				      struct ieee80211_channel *chan,
				      struct chan_section *chan_sec)
{
	int ret;
	char *data, *line, *p;

	memset(chan_sec, 0, sizeof(*chan_sec));

	data = kstrndup(section, section_len, GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	p = data;
	while ((line = strsep(&p, "\n")) != NULL) {
		char *val = strchr(line, '=');

		if (!val)
			continue;
		val++;

		if (strstr(line, "channels=")) {
			ret = mt76_parse_int(val, chan_sec->chan,
					     ARRAY_SIZE(chan_sec->chan));
			if (ret < 0) {
				kfree(data);
				return ret;
			}
			chan_sec->chan_num = ret;
		} else if (strstr(line, "txs-delta=")) {
			ret = mt76_parse_int(val, chan_sec->txs_delta,
					     ARRAY_SIZE(chan_sec->txs_delta));
			if (ret < 0) {
				kfree(data);
				return ret;
			}
			chan_sec->txs_delta_num = ret;
		}
	}
	kfree(data);

	if (chan_sec->chan_num < 2)
		return -EINVAL;

	return chan->hw_value >= chan_sec->chan[0] &&
		chan->hw_value <= chan_sec->chan[1];
}

static int
mt76_parse_file_txs_delta(struct chan_section *chan_sec, int nss)
{
	if (nss <= 0 || nss > chan_sec->txs_delta_num)
		return 0;

	return chan_sec->txs_delta[nss - 1];
}

static void
mt76_apply_file_limit(struct mt76_dev *dev, s8 *pwr, size_t pwr_len, s8 target_power,
		      s8 nss_delta, s8 *max_power, int n_chains, enum mt76_sku_type type)
{
	int i;

	for (i = 0; i < pwr_len; i++) {
		u8 backoff_chain_idx = i;
		int backoff_n_chains;
		s8 backoff_delta;
		s8 delta;

		switch (type) {
		case MT76_SKU_RATE:
			delta = 0;
			backoff_delta = 0;
			backoff_n_chains = 0;
			break;
		case MT76_SKU_BACKOFF_BF_OFFSET:
			backoff_chain_idx += 1;
			fallthrough;
		case MT76_SKU_BACKOFF:
			delta = mt76_tx_power_path_delta(n_chains);
			backoff_n_chains = mt76_backoff_n_chains(dev, backoff_chain_idx);
			backoff_delta = mt76_tx_power_path_delta(backoff_n_chains);
			break;
		default:
			return;
		}

		pwr[i] = min_t(s8, target_power + delta - backoff_delta,
			       pwr[i] + nss_delta);

		/* used for padding, doesn't need to be considered */
		if (pwr[i] >= S8_MAX - 1)
			continue;

		/* only consider backoff value for the configured chain number */
		if (type != MT76_SKU_RATE && n_chains != backoff_n_chains)
			continue;

		if (max_power)
			*max_power = max(*max_power, pwr[i]);
	}
}

static void
mt76_apply_file_multi_limit(struct mt76_dev *dev, s8 *pwr, size_t pwr_len, size_t pwr_num,
			    s8 target_power, s8 nss_delta, s8 *max_power,
			    int n_chains, enum mt76_sku_type type)
{
	static const int connac2_backoff_ru_idx = 2;
	int i;

	for (i = 0; i < pwr_num; i++) {
		if (!is_mt799x(dev) && type == MT76_SKU_BACKOFF &&
		    i > connac2_backoff_ru_idx)
			type = MT76_SKU_BACKOFF_BF_OFFSET;

		mt76_apply_file_limit(dev, pwr + pwr_len * i, pwr_len, target_power,
				nss_delta, max_power, n_chains, type);
	}
}

static int
mt76_parse_file_chan_power(const char *section, size_t section_len,
			   struct mt76_power_limits *dest)
{
	char *data, *line, *p, *val;
	int r;

	data = kstrndup(section, section_len, GFP_KERNEL);
	if (!data)
		return -ENOMEM;

#define parse_power(key, a)								\
	if (strstr(line, key)) {							\
		r = mt76_parse_s8(val, (s8 *)(a), ARRAY_SIZE(a));			\
		if (r < 0) { kfree(data); return r; }					\
		continue;								\
	}
#define parse_power_2d(key, a)								\
	if (strstr(line, key)) {							\
		r = mt76_parse_s8_2d(val, (s8 *)(a), ARRAY_SIZE(a), ARRAY_SIZE((a)[0]));\
		if (r < 0) { kfree(data); return r; }					\
		continue;								\
	}

	p = data;
	while ((line = strsep(&p, "\n")) != NULL) {
		val = strchr(line, '=');
		if (!val)
			continue;
		val++;

		/* parse rate power table */
		parse_power("rates-cck=", dest->cck)
		parse_power("rates-ofdm=", dest->ofdm)
		parse_power_2d("rates-mcs=", dest->mcs)
		parse_power_2d("rates-ru=", dest->ru)
		parse_power_2d("rates-eht=", dest->eht)

		/* parse backoff table */
		parse_power("paths-cck=", dest->path.cck)
		parse_power("paths-ofdm=", dest->path.ofdm)
		parse_power("paths-ofdm-bf=", dest->path.ofdm_bf)
		parse_power_2d("paths-ru=", dest->path.ru)
		parse_power_2d("paths-ru-bf=", dest->path.ru_bf)
	}
#undef parse_power
#undef parse_power_2d

	kfree(data);
	return r;
}

char *mt76_find_power_limits_section_file(struct mt76_phy *phy)
{
	const char *header = "[power-limits]\n";
	char *buf;

#define BUF_SIZE 0x60000 /* SZ_384K */

	if (!phy->sku_filename[0])
		return NULL;

	buf = kzalloc(BUF_SIZE, GFP_KERNEL);
	if (!buf)
		return NULL;

	if (mt76_read_file(phy->sku_filename, buf, BUF_SIZE) <= 0)
		goto err;

	if (strncmp(buf, header, strlen(header)) != 0)
		goto err;

	if (strstr(buf, "country=") || strstr(buf, "regdomain="))
		return buf;

err:
	kfree(buf);
	return NULL;
}
EXPORT_SYMBOL_GPL(mt76_find_power_limits_section_file);

s8 mt76_get_rate_power_limits_file(struct mt76_phy *phy,
				   struct ieee80211_channel *chan,
				   struct mt76_power_limits *dest,
				   s8 target_power)
{
	struct mt76_dev *dev = phy->dev;
	char band, *buf;
	const char *region_name, *search;
	const char *const region_names[] = {
		[NL80211_DFS_UNSET] = "ww",
		[NL80211_DFS_ETSI] = "etsi",
		[NL80211_DFS_FCC] = "fcc",
		[NL80211_DFS_JP] = "jp",
	};
	int section, n_chains = hweight16(phy->chainmask);

	memset(dest, target_power, sizeof(*dest));
	memset(&dest->path, 0, sizeof(dest->path));

	buf = mt76_find_power_limits_section_file(phy);
	if (!buf)
		return target_power;

	switch (chan->band) {
	case NL80211_BAND_2GHZ:
		band = '2';
		break;
	case NL80211_BAND_5GHZ:
		band = '5';
		break;
	case NL80211_BAND_6GHZ:
		band = '6';
		break;
	default:
		goto out;
	}

	if (dev->region >= ARRAY_SIZE(region_names) || !region_names[dev->region])
		goto out;

	region_name = region_names[dev->region];
	search = buf;

#define MAX_SECTION_NUM 64

	for (section = 0; section < MAX_SECTION_NUM; section++) {
		struct chan_section chan_sec;
		const char *ptr;
		size_t len;
		char name[24];
		s8 delta, max_power = -127;
		int ret;

		snprintf(name, sizeof(name), "[txpower-%cg.r%d]", band, section);

		ret = mt76_find_file_section(search, name, &ptr, &len);
		if (ret)
			goto out;

		ret = mt76_parse_file_chan_block(ptr, len, chan, &chan_sec);
		if (ret <= 0)
			continue;

		ret = mt76_parse_file_chan_power(ptr, len, dest);
		if (ret < 0)
			goto out;

		search = ptr + len;
		delta = mt76_parse_file_txs_delta(&chan_sec, n_chains);

#define __apply_array_limit(arr, type)					 \
	mt76_apply_file_limit(dev, (arr), ARRAY_SIZE(arr), target_power, \
			      delta, &max_power, n_chains, type)

#define __apply_multi_array_limit(arr, type)				 \
	mt76_apply_file_multi_limit(dev, (arr)[0], ARRAY_SIZE((arr)[0]), \
				    ARRAY_SIZE(arr), target_power, delta,\
				    &max_power, n_chains, type)

		__apply_array_limit(dest->cck, MT76_SKU_RATE);
		__apply_array_limit(dest->ofdm, MT76_SKU_RATE);
		__apply_multi_array_limit(dest->mcs, MT76_SKU_RATE);
		__apply_multi_array_limit(dest->ru, MT76_SKU_RATE);
		__apply_multi_array_limit(dest->eht, MT76_SKU_RATE);

		__apply_array_limit(dest->path.cck, MT76_SKU_BACKOFF);
		__apply_array_limit(dest->path.ofdm, MT76_SKU_BACKOFF);
		__apply_array_limit(dest->path.ofdm_bf, MT76_SKU_BACKOFF_BF_OFFSET);
		__apply_multi_array_limit(dest->path.ru, MT76_SKU_BACKOFF);
		__apply_multi_array_limit(dest->path.ru_bf, MT76_SKU_BACKOFF);
		target_power = max_power;

		break;
	}
#undef __apply_array_limit
#undef __apply_multi_array_limit

out:
	kfree(buf);
	return target_power;
}
EXPORT_SYMBOL_GPL(mt76_get_rate_power_limits_file);
