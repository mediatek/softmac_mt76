// SPDX-License-Identifier: ISC
/* Copyright (C) 2022 Felix Fietkau <nbd@nbd.name> */
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
#include "mt76-test.h"

bool done = false;

static const char *debugfs_path(const char *phyname, const char *file)
{
	static char path[256];

	snprintf(path, sizeof(path), "/sys/kernel/debug/ieee80211/%s/mt76/%s", phyname, file);

	return path;
}

static int mt76_set_fwlog_en(const char *phyname, bool en, char *val)
{
	FILE *f = fopen(debugfs_path(phyname, "fw_debug_bin"), "w");
	if (!f) {
		perror("fopen");
		return -1;
	}

	if (en && val)
		fprintf(f, "%s", val);
	else if (en)
		fprintf(f, "7");
	else
		fprintf(f, "0");

	fclose(f);

	return 0;
}

static int mt76_set_idxlog_enable(const char *phyname, bool enable)
{
	FILE *f = fopen(debugfs_path(phyname, "idxlog_enable"), "w");
	if (!f) {
		perror("fopen");
		return -1;
	}

	fprintf(f, "%hhu", enable);

	fclose(f);

	return 0;
}

int read_retry(int fd, void *buf, int len)
{
	int out_len = 0;
	int r;

	while (len > 0) {
		if (done)
			return -1;

		r = read(fd, buf, len);
		if (r < 0) {
			if (errno == EINTR || errno == EAGAIN)
				continue;

			return -1;
		}

		if (!r)
			return 0;

		out_len += r;
		len -= r;
		buf += r;
	}

	return out_len;
}

static void handle_signal(int sig)
{
	done = true;
}

static int mt76_log_socket(struct sockaddr_in *remote, char *ip, unsigned short port)
{
	struct sockaddr_in local = {
		.sin_family = AF_INET,
		.sin_addr.s_addr = INADDR_ANY,
	};
	int s, ret;

	remote->sin_family = AF_INET;
	remote->sin_port = htons(port);
	if (!inet_aton(ip, &remote->sin_addr)) {
		fprintf(stderr, "Invalid destination IP address: %s\n", ip);
		return -EINVAL;
	}

	s = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (s < 0) {
		perror("socket");
		return s;
	}

	ret = bind(s, (struct sockaddr *)&local, sizeof(local));
	if (ret) {
		perror("bind");
		close(s);
		return ret;
	}

	return s;
}

static int mt76_log_relay(int in_fd, int out_fd, struct sockaddr_in *remote)
{
	char *buf = malloc(FWLOG_BUF_SIZE);
	int ret = 0;

	if (!buf) {
		perror("malloc");
		return -ENOMEM;
	}

	signal(SIGTERM, handle_signal);
	signal(SIGINT, handle_signal);
	signal(SIGQUIT, handle_signal);

	while (!done) {
		struct pollfd pfd = {
			.fd = in_fd,
			.events = POLLIN,
		};
		uint32_t len;
		int rc;

		poll(&pfd, 1, -1);

		rc = read_retry(in_fd, &len, sizeof(len));
		if (rc < 0) {
			if (!done) {
				fprintf(stderr, "Failed to read relay file.\n");
				ret = -1;
			}
			break;
		}
		if (!rc)
			continue;

		if (len > FWLOG_BUF_SIZE) {
			fprintf(stderr, "Log size was too large: %u bytes\n", len);
			ret = -ENOMEM;
			break;
		}

		rc = read_retry(in_fd, buf, len);
		if (rc < 0) {
			if (!done) {
				fprintf(stderr, "Failed to read relay file.\n");
				ret = -1;
			}
			break;
		}
		if (rc != len) {
			fprintf(stderr, "Expected log size: %u bytes\n", len);
			fprintf(stderr, "Read log size: %u bytes\n", rc);
			ret = -EIO;
			break;
		}

		if (remote)
			rc = sendto(out_fd, buf, len, 0, (struct sockaddr *)remote, sizeof(*remote));
		else
			rc = write(out_fd, buf, len);
		if (rc < 0) {
			perror("sendto/write");
			ret = -1;
			break;
		}
	}

	free(buf);

	return ret;
}

int mt76_fwlog(const char *phyname, int argc, char **argv)
{
	struct sockaddr_in remote;
	int in_fd, out_fd, ret;
	char dev_ip[16] = {};
	unsigned short port;

	if (argc < 2) {
		fprintf(stderr, "need destination address and fw_debug_bin\n");
		return -EINVAL;
	}


	if (argc == 2) {
		/* support ip:port format */
		if (strchr(argv[0], ':')) {
			sscanf(argv[0], "%[^:]:%hu", dev_ip, &port);
		} else {
			strncpy(dev_ip, argv[0], sizeof(dev_ip) - 1);
			port = 55688;
		}

		out_fd = mt76_log_socket(&remote, dev_ip, port);
		if (out_fd < 0)
			return out_fd;
	} else if (argc == 3) {
		out_fd = open(argv[2], O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR);
		if (out_fd < 0) {
			perror("open");
			return out_fd;
		}
	} else {
		fprintf(stderr, "Too many arguments.\n");
		return -EINVAL;
	}

	ret = mt76_set_fwlog_en(phyname, true, argv[1]);
	if (ret)
		goto close;

	in_fd = open(debugfs_path(phyname, "fwlog_data"), O_RDONLY);
	if (in_fd < 0) {
		perror("open");
		goto disable;
	}

	if (mt76_log_relay(in_fd, out_fd, argc == 3 ? NULL : &remote))
		fprintf(stderr, "Failed to relay FW log.\n");

	close(in_fd);
disable:
	ret = mt76_set_fwlog_en(phyname, false, NULL);
close:
	close(out_fd);

	return ret;
}

int mt76_idxlog(const char *phyname, int argc, char **argv)
{
#define IDXLOG_FILE_PATH	"/tmp/log/WIFI_FW.clog"
	struct sockaddr_in remote;
	int in_fd, out_fd, ret;

	if (argc) {
		out_fd = mt76_log_socket(&remote, argv[0], 55688);
		if (out_fd < 0)
			return out_fd;
	} else {
		out_fd = open(IDXLOG_FILE_PATH, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR);
		if (out_fd < 0) {
			perror("open");
			return -1;
		}
	}

	ret = mt76_set_idxlog_enable(phyname, true);
	if (ret)
		goto close;

	in_fd = open(debugfs_path(phyname, "idxlog_data"), O_RDONLY);
	if (in_fd < 0) {
		perror("open");
		goto disable;
	}

	if (mt76_log_relay(in_fd, out_fd, argc ? &remote : NULL))
		fprintf(stderr, "Failed to relay index log.\n");

	close(in_fd);
disable:
	ret = mt76_set_idxlog_enable(phyname, false);
close:
	close(out_fd);

	if (argc)
		system("timestamp=$(date +\"%y%m%d_%H%M%S\");"
		       "clog_dir=/tmp/log/clog_${timestamp};"
		       "mkdir ${clog_dir};"
		       "dmesg > ${clog_dir}/WIFI_KERNEL_${timestamp}.clog");
	else
		system("timestamp=$(date +\"%y%m%d_%H%M%S\");"
		       "clog_dir=/tmp/log/clog_${timestamp};"
		       "mkdir ${clog_dir};"
		       "mv /tmp/log/WIFI_FW.clog ${clog_dir}/WIFI_FW_${timestamp}.clog;"
		       "dmesg > ${clog_dir}/WIFI_KERNEL_${timestamp}.clog");

	return ret;
}
