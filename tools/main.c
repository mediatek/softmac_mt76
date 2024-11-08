// SPDX-License-Identifier: ISC
/* Copyright (C) 2020 Felix Fietkau <nbd@nbd.name> */
#define _GNU_SOURCE

#include <sys/types.h>
#include <sys/uio.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>
#include <stdio.h>
#include <stdbool.h>
#include <errno.h>
#include <poll.h>
#include <fcntl.h>
#include <signal.h>
#include <net/if.h>
#include "mt76-test.h"

struct unl unl;
static uint32_t tm_changed[DIV_ROUND_UP(NUM_MT76_TM_ATTRS, 32)];
static const char *progname;

static void parse_radio_config(struct radio_config *radio, struct nlattr *freqs)
{
	struct nlattr *freq;
	int rem;

	nla_for_each_nested(freq, freqs, rem) {
		static struct nla_policy freq_policy[NL80211_WIPHY_RADIO_FREQ_ATTR_MAX + 1] = {
			[NL80211_WIPHY_RADIO_FREQ_ATTR_START] = { .type = NLA_U32 },
			[NL80211_WIPHY_RADIO_FREQ_ATTR_END] = { .type = NLA_U32 },
		};
		struct nlattr *tb[NL80211_WIPHY_RADIO_FREQ_ATTR_MAX + 1];
		uint32_t start, end;

		if (nla_type(freq) != NL80211_WIPHY_RADIO_ATTR_FREQ_RANGE)
			continue;

		if (nla_parse_nested(tb, NL80211_WIPHY_RADIO_ATTR_MAX + 1,
				     freq, freq_policy) ||
		    !tb[NL80211_WIPHY_RADIO_FREQ_ATTR_START] ||
		    !tb[NL80211_WIPHY_RADIO_FREQ_ATTR_END])
			continue;

		start = nla_get_u32(tb[NL80211_WIPHY_RADIO_FREQ_ATTR_START]) / 1000;
		end = nla_get_u32(tb[NL80211_WIPHY_RADIO_FREQ_ATTR_END]) / 1000;

		if (start >= 2400 && end <= 2500) {
			radio->band = NL80211_BAND_2GHZ;
			radio->parking_freq = 2412;
			return;
		}
		if (start >= 5000 && end <= 5900) {
			radio->band = NL80211_BAND_5GHZ;
			radio->parking_freq = 5180;
			return;
		}
		if (start >= 5925 && end <= 7200) {
			radio->band = NL80211_BAND_6GHZ;
			radio->parking_freq = 5955;
			return;
		}
	}

	radio->band = -1;
	radio->parking_freq = 0;
}

static void parse_radios(struct wiphy_config *wiphy, struct nlattr *radios)
{
	struct nlattr *radio;
	int radio_idx, rem;

	nla_for_each_nested(radio, radios, rem) {
		static struct nla_policy radio_policy[NL80211_WIPHY_RADIO_ATTR_MAX + 1] = {
			[NL80211_WIPHY_RADIO_ATTR_INDEX] = { .type = NLA_U32 },
		};
		struct nlattr *tb[NL80211_WIPHY_RADIO_ATTR_MAX + 1];

		if (nla_parse_nested(tb, NL80211_WIPHY_RADIO_ATTR_MAX + 1,
				     radio, radio_policy) ||
		    !tb[NL80211_WIPHY_RADIO_ATTR_INDEX])
			continue;

		radio_idx = nla_get_u32(tb[NL80211_WIPHY_RADIO_ATTR_INDEX]);
		parse_radio_config(&wiphy->radio[radio_idx], radio);
		wiphy->radio_num++;
	}
}

static int mt76_get_wiphy_cb(struct nl_msg *msg, void *arg)
{
	struct nlattr *tb_msg[NL80211_ATTR_MAX + 1];
	struct genlmsghdr *gnlh = nlmsg_data(nlmsg_hdr(msg));
	struct wiphy_list *wiphys = (struct wiphy_list *)arg;
	int wiphy_idx, idx = wiphys->wiphy_num - 1;

	nla_parse(tb_msg, NL80211_ATTR_MAX, genlmsg_attrdata(gnlh, 0),
		  genlmsg_attrlen(gnlh, 0), NULL);

	if (!tb_msg[NL80211_ATTR_WIPHY])
		return NL_STOP;

	wiphy_idx = nla_get_u32(tb_msg[NL80211_ATTR_WIPHY]);
	if (idx < 0 || wiphys->wiphy[idx].wiphy_idx != wiphy_idx) {
		idx++;
		wiphys->wiphy[idx].wiphy_idx = wiphy_idx;
		wiphys->wiphy_num++;
	}

	if (tb_msg[NL80211_ATTR_MAC])
		nla_memcpy(wiphys->mac_addr, tb_msg[NL80211_ATTR_MAC], ETH_ALEN);

	if (tb_msg[NL80211_ATTR_WIPHY_RADIOS]) {
		wiphys->is_single_wiphy = true;
		parse_radios(&wiphys->wiphy[idx], tb_msg[NL80211_ATTR_WIPHY_RADIOS]);
	}

	return NL_SKIP;
}

static void mt76_get_wiphy(struct phy_config *config, int phy_idx)
{
	struct wiphy_list wiphys = {0};
	struct nl_msg *msg;
	int i, radio_num;
	int idx = 0;

	msg = unl_genl_msg(&unl, NL80211_CMD_GET_WIPHY, true);
	nla_put_flag(msg, NL80211_ATTR_SPLIT_WIPHY_DUMP);
	unl_genl_request(&unl, msg, mt76_get_wiphy_cb, (void *)&wiphys);

	config->wiphy_idx = -1;
	config->radio_idx = -1;
	config->parking_freq = 0;
	config->band = -1;
	memcpy(config->mac_addr, wiphys.mac_addr, ETH_ALEN);

	if (!wiphys.is_single_wiphy) {
		config->wiphy_idx = phy_idx;
		return;
	}

	/* wiphys is in reversed order */
	for (i = wiphys.wiphy_num - 1; i >= 0; i--) {
		struct wiphy_config *wiphy = &wiphys.wiphy[i];

		radio_num = wiphy->radio_num;
		if (idx <= phy_idx && phy_idx < idx + radio_num) {
			config->wiphy_idx = wiphy->wiphy_idx;
			config->radio_idx = phy_idx - idx;
			config->mac_addr[5] += config->radio_idx;
			config->parking_freq = wiphy->radio[config->radio_idx].parking_freq;
			config->band = wiphy->radio[config->radio_idx].band;
			config->radio_num = radio_num;
			return;
		}
		idx += radio_num;
	}
}

void usage(void)
{
	static const char *const commands[] = {
		"add <interface>",
		"del <interface>",
		"set <var>=<val> [...]",
		"dump [stats]",
		"eeprom file",
		"eeprom set <addr>=<val> [...]",
		"eeprom changes",
		"eeprom reset",
		"fwlog <ip> <fw_debug_bin input> <fwlog name>",
	};
	int i;

	fprintf(stderr, "Usage:\n");
	for (i = 0; i < ARRAY_SIZE(commands); i++)
		printf("  %s phyX %s\n", progname, commands[i]);

	exit(1);
}

static int mt76_dump_cb(struct nl_msg *msg, void *arg)
{
	struct nlattr *attr;

	attr = unl_find_attr(&unl, msg, NL80211_ATTR_TESTDATA);
	if (!attr) {
		fprintf(stderr, "Testdata attribute not found\n");
		return NL_SKIP;
	}

	msg_field.print(&msg_field, attr);

	return NL_SKIP;
}

static int mt76_dump(struct phy_config *config, int argc, char **argv)
{
	struct nl_msg *msg;
	void *data;

	msg = unl_genl_msg(&unl, NL80211_CMD_TESTMODE, true);
	nla_put_u32(msg, NL80211_ATTR_WIPHY, config->wiphy_idx);

	data = nla_nest_start(msg, NL80211_ATTR_TESTDATA);

	if (config->radio_idx >= 0)
		nla_put_u32(msg, MT76_TM_ATTR_RADIO_IDX, config->radio_idx);

	for (; argc > 0; argc--, argv++) {
		if (!strcmp(argv[0], "stats"))
			nla_put_flag(msg, MT76_TM_ATTR_STATS);
	}

	nla_nest_end(msg, data);

	unl_genl_request(&unl, msg, mt76_dump_cb, NULL);

	return 0;
}

static inline void tm_set_changed(uint32_t id)
{
	tm_changed[id / 32] |= (1U << (id % 32));
}

static inline bool tm_is_changed(uint32_t id)
{
	return tm_changed[id / 32] & (1U << (id % 32));
}

static int mt76_set(struct phy_config *config, int argc, char **argv)
{
	const struct tm_field *fields = msg_field.fields;
	struct nl_msg *msg;
	void *data;
	int i, ret;

	if (argc < 1)
		return 1;

	msg = unl_genl_msg(&unl, NL80211_CMD_TESTMODE, false);
	nla_put_u32(msg, NL80211_ATTR_WIPHY, config->wiphy_idx);

	data = nla_nest_start(msg, NL80211_ATTR_TESTDATA);

	if (config->radio_idx >= 0)
		nla_put_u32(msg, MT76_TM_ATTR_RADIO_IDX, config->radio_idx);

	for (; argc > 0; argc--, argv++) {
		char *name = argv[0];
		char *val = strchr(name, '=');

		if (!val) {
			fprintf(stderr, "Invalid argument: %s\n", name);
			return 1;
		}

		*(val++) = 0;

		for (i = 0; i < msg_field.len; i++) {
			if (!fields[i].parse)
				continue;

			if (!strcmp(fields[i].name, name))
				break;
		}

		if (i == msg_field.len) {
			fprintf(stderr, "Unknown field: %s\n", name);
			return 1;
		}

		if (tm_is_changed(i)) {
			fprintf(stderr, "Duplicate field '%s'\n", name);
			return 1;
		}

		if (!fields[i].parse(&fields[i], i, msg, val))
			return 1;

		tm_set_changed(i);
	}

	nla_nest_end(msg, data);

	ret = unl_genl_request(&unl, msg, NULL, NULL);
	if (ret)
		fprintf(stderr, "nl80211 call failed: %s\n", strerror(-ret));

	return ret;
}

static int mt76_set_state_all(struct phy_config *config, char *state)
{
	const struct tm_field *fields = msg_field.fields;
	struct nl_msg *msg;
	int i, radio, max_radio, ret = 0;
	void *data;

	max_radio = config->radio_idx >= 0 ? config->radio_num : 1;

	for (radio = 0; radio < max_radio; radio++) {
		msg = unl_genl_msg(&unl, NL80211_CMD_TESTMODE, false);
		nla_put_u32(msg, NL80211_ATTR_WIPHY, config->wiphy_idx);

		data = nla_nest_start(msg, NL80211_ATTR_TESTDATA);

		if (config->radio_idx >= 0)
			nla_put_u32(msg, MT76_TM_ATTR_RADIO_IDX, radio);

		for (i = 0; i < msg_field.len; i++) {
			if (!fields[i].parse)
				continue;

			if (!strcmp(fields[i].name, "state"))
				break;
		}

		if (!fields[i].parse(&fields[i], i, msg, state))
			return 1;

		tm_set_changed(i);
		nla_nest_end(msg, data);

		ret = unl_genl_request(&unl, msg, NULL, NULL);
		if (ret && ret != -ENOTCONN) {
			fprintf(stderr, "Failed to set state %s for radio %d: %s\n",
				state, radio, strerror(-ret));
			break;
		}
	}

	return ret;
}

static void mt76_set_tm_reg(void)
{
	struct nl_msg *msg;
	char reg[3] = "VV\0";
	int ret;

	msg = unl_genl_msg(&unl, NL80211_CMD_REQ_SET_REG, false);
	nla_put_string(msg, NL80211_ATTR_REG_ALPHA2, reg);

	ret = unl_genl_request(&unl, msg, NULL, NULL);
	if (ret)
		fprintf(stderr, "Failed to set reg %s: %s\n", reg, strerror(-ret));
}

static int mt76_add_iface(struct phy_config *config, int argc, char **argv)
{
	struct nl_msg *msg;
	char *name, cmd[64];
	int ret;

	mt76_set_tm_reg();

	if (argc < 1)
		return 1;

	name = argv[0];
	msg = unl_genl_msg(&unl, NL80211_CMD_NEW_INTERFACE, false);
	nla_put_u32(msg, NL80211_ATTR_WIPHY, config->wiphy_idx);
	nla_put_u32(msg, NL80211_ATTR_IFTYPE, NL80211_IFTYPE_MONITOR);
	nla_put_string(msg, NL80211_ATTR_IFNAME, name);
	if (config->radio_idx >= 0)
		nla_put_u32(msg, NL80211_ATTR_VIF_RADIO_MASK, BIT(config->radio_idx));

	nla_put(msg, NL80211_ATTR_MAC, ETH_ALEN, config->mac_addr);

	ret = unl_genl_request(&unl, msg, NULL, NULL);
	if (ret) {
		fprintf(stderr, "nl80211 call failed: %s\n", strerror(-ret));
		return ret;
	}

	snprintf(cmd, sizeof(cmd), "ifconfig %s up", name);
	system(cmd);

	/* for single wiphy model, parking channel will not be set */
	if (config->parking_freq) {
		snprintf(cmd, sizeof(cmd), "iw dev %s set freq %d HT20", name, config->parking_freq);
		system(cmd);
	}

	/* turn on testmode */
	ret = mt76_set_state_all(config, "idle");
	return ret;
}

static int mt76_delete_iface(struct phy_config *config, int argc, char **argv)
{
	unsigned int devidx;
	struct nl_msg *msg;
	char *name, cmd[64];
	int ret;

	if (argc < 1)
		return 1;

	name = argv[0];
	devidx = if_nametoindex(name);
	if (!devidx) {
		fprintf(stderr, "Failed to find ifindex for %s: %s\n",
			name, strerror(errno));
		return 2;
	}

	/* turn off testmode before deleting interface */
	ret = mt76_set_state_all(config, "off");
	if (ret)
		return ret;

	snprintf(cmd, sizeof(cmd), "ifconfig %s down", name);
	system(cmd);

	/* delete interface */
	msg = unl_genl_msg(&unl, NL80211_CMD_DEL_INTERFACE, false);
	nla_put_u32(msg, NL80211_ATTR_WIPHY, config->wiphy_idx);
	nla_put_u32(msg, NL80211_ATTR_IFINDEX, devidx);

	ret = unl_genl_request(&unl, msg, NULL, NULL);
	if (ret)
		fprintf(stderr, "nl80211 call failed: %s\n", strerror(-ret));

	return ret;
}

int main(int argc, char **argv)
{
	struct phy_config config = {0};
	const char *cmd, *phyname;
	int ret = 0;

	progname = argv[0];
	if (argc < 3)
		usage();

	if (unl_genl_init(&unl, "nl80211") < 0) {
		fprintf(stderr, "Failed to connect to nl80211\n");
		return 2;
	}

	phyname = argv[1];
	mt76_get_wiphy(&config, atoi(phyname + 3));
	if (config.wiphy_idx < 0) {
		fprintf(stderr, "Could not find phy '%s'\n", phyname);
		return 2;
	}

	cmd = argv[2];
	argv += 3;
	argc -= 3;

	if (!strcmp(cmd, "dump"))
		ret = mt76_dump(&config, argc, argv);
	else if (!strcmp(cmd, "set"))
		ret = mt76_set(&config, argc, argv);
	else if (!strcmp(cmd, "add"))
		ret = mt76_add_iface(&config, argc, argv);
	else if (!strcmp(cmd, "del"))
		ret = mt76_delete_iface(&config, argc, argv);
	else if (!strcmp(cmd, "eeprom"))
		ret = mt76_eeprom(config.wiphy_idx, argc, argv);
	else if (!strcmp(cmd, "fwlog"))
		ret = mt76_fwlog(phyname, argc, argv);
	else if (!strcmp(cmd, "idxlog"))
		ret = mt76_idxlog(phyname, argc, argv);
	else
		usage();

	unl_free(&unl);

	return ret;
}
