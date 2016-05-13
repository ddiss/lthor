/*
 * lthor - Tizen Linux Thor Downloader
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *
 * Licensed under the Apache License, Version 2.0 (the License);
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <sys/types.h>
#include <stdio.h>
#include <getopt.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <stdint.h>
#include <sys/time.h>

#include "thor.h"

#define KB			(1024)
#define MB			(1024*KB)
#define GB			((off_t)1024*MB)

#define TERM_YELLOW      "\x1b[0;33;1m"
#define TERM_LIGHT_GREEN "\x1b[0;32;1m"
#define TERM_RED         "\x1b[0;31;1m"
#define TERM_NORMAL      "\x1b[0m"

struct helper {
	struct thor_data_src *data;
	enum thor_data_type type;
	const char *name;
};

struct time_data {
	struct timeval start_time;
	struct timeval last_time;
	int last_sent;
};

static int test_tar_file_list(char **tarfilelist)
{
	struct thor_data_src *data;
	int ret;

	while (*tarfilelist) {
		ret = thor_get_data_src(*tarfilelist, THOR_FORMAT_TAR, &data);
		if (ret)
			goto error;

		ret = thor_send_data(NULL, data, THOR_NORMAL_DATA,
				     NULL, NULL, NULL, NULL);
		thor_release_data_src(data);
		if (ret)
			goto error;
	}

	return 0;
error:
	fprintf(stderr, "Unable to load file %s: %d\n", *tarfilelist, ret);
	return ret;
}

/* Check if LHOR protocol is in working state */
static int check_proto(struct thor_device_id *dev_id)
{
	thor_device_handle *handle;
	int ret;

	ret = thor_open(dev_id, 0, &handle);
	if (ret)
		fprintf(stderr, "Unable to open device: %d\n", ret);
	else
		thor_close(handle);

	return ret;
}

static int count_files(char **list)
{
	int i = 0;

	while (list && list[i])
		++i;

	return i;
}

static int init_data_parts(const char *pitfile, char **tarfilelist,
		    struct helper *data_parts)
{
	int i;
	int entry = 0;
	int ret;

	if (pitfile) {
		data_parts[0].type = THOR_PIT_DATA;
		data_parts[0].name = pitfile;
		ret = thor_get_data_src(pitfile, THOR_FORMAT_RAW,
					&(data_parts[0].data));
		if (ret) {
			fprintf(stderr, "Unable to open pit file %s : %d\n",
				pitfile, ret);
			return -EINVAL;
		}
		++entry;
	}

	while (*tarfilelist) {
		printf(TERM_YELLOW "%s :" TERM_NORMAL "\n" , *tarfilelist);
		data_parts[entry].type = THOR_NORMAL_DATA;
		data_parts[0].name = *tarfilelist;
		ret = thor_get_data_src(*tarfilelist, THOR_FORMAT_TAR,
					&(data_parts[entry].data));
		if (ret) {
			fprintf(stderr, "Unable to open file %s : %d\n",
				*tarfilelist, ret);
			goto err_cleanup_parts;
		}

		++entry;
		++tarfilelist;
	}

	return entry;
err_cleanup_parts:
	for (i = 0; i < entry; ++i)
		thor_release_data_src(data_parts[i].data);
	return ret;
}

static void init_time_data(struct time_data *tdata)
{
	gettimeofday(&tdata->start_time, NULL);
	memcpy(&tdata->last_time, &tdata->start_time, sizeof(tdata->start_time));
	tdata->last_sent = 0;
}

static void report_next_entry(thor_device_handle *th,
			      struct thor_data_src *data, void *user_data)
{
	struct time_data *tdata = user_data;

	printf("[" TERM_LIGHT_GREEN "%s" TERM_NORMAL"]\n",
	       data->get_name(data));
	init_time_data(tdata);
}

static double timediff(struct timeval *atv, struct timeval *btv)
{
	double diff;

	if (btv->tv_usec < atv->tv_usec) {
		diff = btv->tv_sec - atv->tv_sec - 1;
		diff += (double)(1*1000*1000 - atv->tv_usec +
				btv->tv_usec)/(1000*1000);
	} else {
		diff = btv->tv_sec - atv->tv_sec;
		diff += (double)(btv->tv_usec - atv->tv_usec)/(1000*1000);
	}

	return diff;
}

static void report_progress(thor_device_handle *th, struct thor_data_src *data,
			    int sent, int left, int chunk_nmb, void *user_data)
{
	struct time_data *tdata = user_data;
	struct timeval *start_time = &tdata->start_time;
	struct timeval *last_time = &tdata->last_time;
	struct timeval current_time;
	double diff;
	int sent_kb = sent/KB;
	int total_kb = (sent + left)/KB;
	char progress [4] = { '-', '\\', '|', '/' };
	char c = progress[(sent_kb/30)%4];

	fprintf(stderr, "\x1b[1A\x1b[16C%c sending %6dk/%6dk %3d%% block %-6d",
		c, sent_kb, total_kb, ((sent_kb*100)/total_kb), chunk_nmb);

	gettimeofday(&current_time, NULL);

	if (left != 0) {
		diff = timediff(last_time, &current_time);
		fprintf(stderr, " [%.2f MB/s]\n",
			(double)((sent - tdata->last_sent)/diff)/(MB));
		tdata->last_sent = sent;
		*last_time = current_time;
	} else {
		diff = timediff(start_time, &current_time);
		fprintf(stderr, " [avg %.2f MB/s]\n",
			(double)(sent/diff)/(MB));
	}
}

static int do_download(thor_device_handle *th, struct helper *data_parts,
		       int entries, off_t total_size)
{
	struct time_data tdata;
	int i;
	int ret;

	ret = thor_start_session(th, total_size);
	if (ret) {
		fprintf(stderr, "Unable to start download session: %d\n", ret);
		goto out;
	}

	for (i = 0; i < entries; ++i) {
		switch (data_parts[i].type) {
		case THOR_PIT_DATA:
			fprintf(stderr, "\nDownload PIT file : %s\n\n",
				data_parts[i].name);
			break;
		case THOR_NORMAL_DATA:
			fprintf(stderr, "\nDownload files from %s\n\n",
				data_parts[i].name);
			break;
		}

		ret = thor_send_data(th, data_parts[i].data, data_parts[i].type,
				     report_progress, &tdata, report_next_entry,
				     &tdata);
		if (ret) {
			fprintf(stderr, "\nfailed to download %s: %d\n",
				data_parts[i].name, ret);
			goto out;
		}

	}

	ret = thor_end_session(th);
	if (ret)
		fprintf(stderr, TERM_YELLOW "missing RQT_DL_EXIT response "
			"from broken bootloader" TERM_NORMAL"\n");

	fprintf(stderr, "\nrequest target reboot : ");

	ret = thor_reboot(th);
	if (ret) {
		fprintf(stderr, TERM_RED "failed" TERM_NORMAL"\n");
		goto out;
	} else {
		fprintf(stderr, TERM_LIGHT_GREEN "success" TERM_NORMAL "\n");
	}
out:
	return ret;
}

static int process_download(struct thor_device_id *dev_id, const char *pitfile,
		     char **tarfilelist)
{
	thor_device_handle *th;
	off_t total_size = 0;
	struct helper *data_parts;
	int nfiles;
	int entries = 0;
	int i;
	int ret;

	ret = thor_open(dev_id, 1, &th);
	if (ret) {
		fprintf(stderr, "Unable to open device: %d\n", ret);
		return ret;
	}

	nfiles = count_files(tarfilelist) + (pitfile ? 1 : 0);

	data_parts = calloc(nfiles, sizeof(*data_parts));
	if (!data_parts) {
		ret = -ENOMEM;
		goto close_dev;
	}

	entries = init_data_parts(pitfile, tarfilelist, data_parts);
	if (entries < 0) {
		ret = entries;
		goto free_data_parts;
	}

	/* Count the total size of data */
	for (i = 0; i < entries; ++i) {
		off_t size = data_parts[i].data->get_size(data_parts[i].data);

		switch (data_parts[i].type) {
		case THOR_PIT_DATA:
			printf(TERM_YELLOW "%s :" TERM_NORMAL "%jdk\n",
			       data_parts[i].name, (intmax_t)(size/KB));
			break;
		case THOR_NORMAL_DATA:
		default:
			break;
		}
		total_size += size;
	}

	printf("-------------------------\n");
	printf("\t" TERM_YELLOW "total" TERM_NORMAL" :\t%.2fMB\n\n",
	       (double)total_size/MB);

	if (total_size > (4*GB - 1*KB)) {
		fprintf(stderr,
			TERM_RED
			"[ERROR] Images over 4GB are not supported by thor protocol.\n"
			TERM_NORMAL);
		ret = -EOVERFLOW;
		goto release_data_srcs;
	}

	if (total_size > (2*GB - 1*KB)) {
		fprintf(stderr,
			TERM_RED
			"[WARNING] Not all bootloaders support images over 2GB.\n"
			"          If your download will fail this may be a reason.\n"
			TERM_NORMAL);
	}

	ret = do_download(th, data_parts, entries, total_size);

release_data_srcs:
	for (i = 0; i < entries; ++i)
		thor_release_data_src(data_parts[i].data);

free_data_parts:
	free(data_parts);
close_dev:
	thor_close(th);

	return ret;
}

static void usage(const char *exename)
{
	fprintf(stderr,
		"Usage: %s: [options] [-d port] [-p pitfile] [tar] [tar] ..\n"
		"Options:\n"
		"  -t, --test                         Don't flash, just check if given tar files are correct\n"
		"  -v, --verbose                      Be more verbose\n"
		"  -c, --check                        Don't flash, just check if given tty port is thor capable\n"
		"  -p=<pitfile>, --pitfile=<pitfile>  Flash new partition table\n"
		"  -b=<busid>, --busid=<busid>        Flash device with given busid\n"
		"  --vendor-id=<vid>                  Flash device with given Vendor ID\n"
		"  --product-id=<pid>                 Flash device with given Product ID\n"
		"  --serial=<serialno>                Flash device with given Serial Number\n"
		"  --help                             Print this help message\n",
		exename);
	exit(1);
}

static void d_opt_obsolete()
{
	fprintf(stderr,
		"--port, -p options are obsolete.\n"
		"Instead you may use:"
		"  -b=<busid>, --busid=<busid>        Flash device with given busid\n"
		"  --vendor-id=<vid>                  Flash device with given Vendor ID\n"
		"  --product-id=<pid>                 Flash device with given Product ID\n"
		"  --serial=<serialno>                Flash device with given Serial Number\n");
	exit(1);
}

int main(int argc, char **argv)
{
	const char *exename = NULL, *pitfile = NULL;
	int opt;
	int opt_test = 0;
	int opt_check = 0;
	int opt_verbose = 0; /* unused for now */
	int optindex;
	int ret;
	struct thor_device_id dev_id = {
		.busid = NULL,
		.vid = -1,
		.pid = -1,
		.serial = NULL,
	};

	struct option opts[] = {
		{"test", no_argument, 0, 't'},
		{"verbose", no_argument, 0, 'v'},
		{"check", no_argument, 0, 'c'},
		{"port", required_argument, 0, 'd'},
		{"pitfile", required_argument, 0, 'p'},
		{"busid", required_argument, 0, 'b'},
		{"vendor-id", required_argument, 0, 1},
		{"product-id", required_argument, 0, 2},
		{"serial", required_argument, 0, 3},
		{"help", no_argument, 0, 0},
		{0, 0, 0, 0}
	};

	printf("\n");
	printf("Linux Thor downloader, version %s \n", PACKAGE_VERSION);
	printf("Authors: Jaehoon You <jaehoon.you@samsung.com>\n"
	       "         Krzysztof Opasiak <k.opasiak@samsung.com>\n\n");

	exename = argv[0];

	ret = thor_init();
	if (ret) {
		fprintf(stderr, "Unable to init io backend: %d\n", ret);
		exit(-1);
	}

	while (1) {
		opt = getopt_long(argc, argv, "tvcd:p:b:", opts, &optindex);
		if (opt == -1)
			break;

		switch (opt) {
		case 't':
			opt_test = 1;
			break;
		case 'v':
			opt_verbose = 1;
			break;
		case 'c':
			opt_check = 1;
			break;
		case 'd':
			d_opt_obsolete();
			break;
		case 'p':
			pitfile = optarg;
			break;
		case 'b':
			dev_id.busid = optarg;
			break;
		case 1:
		{
			unsigned long int val;
			char *endptr = NULL;

			val = strtoul(optarg, &endptr, 0);
			if (*optarg == '\0'
			    || (endptr && *endptr != '\0')) {
				fprintf(stderr,
					"Invalid value type for --vendor-id option.\n"
					"Expected a number but got: %s", optarg);
				exit(-1);
			}

			if (val > UINT16_MAX) {
				fprintf(stderr,
					"Value of --vendor-id out of range\n");
				exit(-1);
			}

			dev_id.vid = (int)val;
			break;
		}
		case 2:
		{
			unsigned long int val;
			char *endptr = NULL;

			val = strtoul(optarg, &endptr, 0);
			if (*optarg == '\0'
			    || (endptr && *endptr != '\0')) {
				fprintf(stderr,
					"Invalid value type for --product-id option.\n"
					"Expected a number but got: %s", optarg);
				exit(-1);
			}

			if (val > UINT16_MAX) {
				fprintf(stderr,
					"Value of --product-id out of range\n");
				exit(-1);
			}

			dev_id.vid = (int)val;
			break;
		}
		case 3:
			dev_id.serial = optarg;
			break;
		case 0:
		default:
			usage(exename);
			return 0;
		}
	}

	ret = 0;
	if (opt_test)
		ret = test_tar_file_list(&(argv[optind]));
	else if (opt_check)
		ret = check_proto(&dev_id);
	else if (pitfile || argv[optind])
		ret = process_download(&dev_id, pitfile, &(argv[optind]));
	else
		usage(exename);

	thor_cleanup();
	return ret;
}

