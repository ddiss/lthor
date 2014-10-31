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

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <fcntl.h>
#include <termios.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>
#include <sys/mman.h>
#include <poll.h>
#include <errno.h>
#include <dirent.h>
#include <libgen.h>

#include <archive.h>
#include <archive_entry.h>

#include "thor-proto.h"

#define KB			(1024)
#define MB			(1024*KB)
#define DEFAULT_PKT_SIZE	(1*MB)
#define DEFAULT_TIMEOUT	(60000)			/* 1 minute */

/* data abstraction */
struct data_src {
	size_t (*get_data_length)(struct data_src *src);
	size_t (*get_data_block)(struct data_src *src, void *data, size_t len);
	const char* (*get_data_name)(struct data_src *src);
};

int opt_verbose = 0;
int opt_test = 0;
size_t trans_unit_size = DEFAULT_PKT_SIZE;

static void dump(const char *msg, void *bytes, int len)
{
	int i;

	if (!opt_verbose)
		return;

	fprintf(stderr, "%s :", msg);
	for (i=0; i<len; i++) {
		if (i && (0==i%16))
			fprintf(stderr, "      ");
		fprintf(stderr, "%02x%c", ((unsigned char*)bytes)[i],
				((len == i+1) || (i+1)%16==0) ? '\n': ' ');
	}
}

static off_t file_size(const char *filename)
{
	struct stat st;
	if (stat(filename, &st) < 0)
		return -1;

	return st.st_size;
}

int serial_read(int fd, void *addr, size_t len, int timeout)
{
	unsigned char *p = addr;
	int n = 0, r;

	while (n < len) {
		struct pollfd fds;
		fds.fd = fd;
		fds.events = POLLIN;
		fds.revents = 0;
		r = poll(&fds, 1, timeout);
		if (r < 0) {
			fprintf(stderr, "line %d: poll failed\n", __LINE__);
			return -1;
		}

		if (r == 0) {
			if (opt_verbose)
				fprintf(stderr, "line %d: timeout after reading %d bytes\n", __LINE__, n);
			return n;
		}

		if (!(fds.revents & POLLIN)) {
			fprintf(stderr, "line %d: poll returned non-read event %08x\n", __LINE__, fds.revents);
			continue;
		}

		r = read(fd, &p[n], len - n);
		if (r < 0) {
			fprintf(stderr, "line %d: read failed\n", __LINE__);
			return -1;
		}

		n += r;
	}

	return n;
}

static double timediff (struct timeval *atv, struct timeval *btv)
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

int send_chunks(int fd, struct data_src *senddata, size_t size)
{
	unsigned char *chunk;
	struct data_res_pkt resp;
	size_t n = 0;
	int chunksize, chunk_number = 1;
	struct timeval stv, etv, ltv;
	int r;

	chunk = malloc(trans_unit_size);
	if (chunk == NULL) {
		fprintf(stderr, "Unable to allocate memory.\n");
		return -1;
	}

	if (opt_verbose)
		fprintf(stderr, "Downloading: %zd bytes\n", size);

	gettimeofday(&stv, NULL);
	memcpy(&ltv, &stv, sizeof(struct timeval));

	while (n < size) {
		if (size - n >= trans_unit_size)
			chunksize = trans_unit_size;
		else
			chunksize = size - n;

		chunksize = senddata->get_data_block(senddata, chunk, chunksize);
		if (chunksize <= 0) {
			fprintf(stderr, "\nline %d: Broken tar archive - check download!\n", __LINE__);
			exit(1);
		}

		if (opt_verbose)
			fprintf(stderr, "read %d bytes\n", chunksize);

		assert(chunksize > 0 && chunksize <= trans_unit_size);
		memset(&chunk[chunksize], 0, trans_unit_size - chunksize);

		if (opt_verbose)
			fprintf(stderr, "sending %zd/%zd\n", n + chunksize, size);

		r = write(fd, chunk, trans_unit_size);
		if (r != trans_unit_size) {
			if (opt_verbose) {
				if (r > 0)
					fprintf(stderr, "line %d: %d bytes requested, %d written\n",
							__LINE__, trans_unit_size, r);
				fprintf(stderr, "line %d: failed to send data\n", __LINE__);
			}
			free(chunk);
			return -1;
		}

		memset(&resp, 0, DATA_RES_PKT_SIZE);
		r = serial_read(fd, &resp, DATA_RES_PKT_SIZE, DEFAULT_TIMEOUT);
		if (r != DATA_RES_PKT_SIZE) {
			fprintf(stderr, "line %d: read failed %d\n", __LINE__, r);
			free(chunk);
			return -1;
		}

		if (resp.cnt != chunk_number) {
			printf("\nsending %zd/%zd : sequence wrong %08x, expected %08x\n",
				n, size, resp.cnt, chunk_number);
			//return -1;
		}

		n += chunksize;

		if (opt_verbose) {
			printf("response %08x %08x\n", resp.ack, resp.cnt);

		} else {
			int now = n/KB;
			int total = size/KB;
			char progress [4] = { '-', '\\', '|', '/' };
			char c = progress[(now/30)%4];
			double elaps;

			gettimeofday(&etv, NULL);
			if (n >= size) {
				elaps = timediff(&stv, &etv);
				fprintf(stderr, "\x1b[1A\x1b[16C%c sending %6dk/%6dk %3d%% block %-6d", 
						c, now, total, ((now*100)/total), chunk_number);
				fprintf(stderr, " [avg %.2f MB/s]\n", (double)(size/elaps)/(MB));
			} else {
				elaps = timediff(&ltv, &etv);
				memcpy(&ltv, &etv, sizeof(struct timeval));
				fprintf(stderr, "\x1b[1A\x1b[16C%c sending %6dk/%6dk %3d%% block %-6d", 
						c, now, total, ((now*100)/total), chunk_number);
				fprintf(stderr, " [%.2f MB/s]\n", (double)(chunksize/elaps)/(MB));
			}
		}
		chunk_number++;
	}

	free(chunk);

	return 0;
}

/*
 * function that doesn't wait for response data, used by standard requests and
 * REQ_PIT_DATA_START, which doesn't want a response
 */
int write_request(int fd, request_type req_id, int req_sub_id,
	int *idata, int icnt, char **sdata, int scnt)
{
	struct rqt_pkt req;
	int r, i;

	assert(icnt <= sizeof(req.int_data)/sizeof(req.int_data[0]));
	assert(icnt >= 0);
	assert(scnt <= sizeof(req.str_data)/sizeof(req.str_data[0]));
	assert(scnt >= 0);

	memset(&req, 0, sizeof(req));

	req.id = req_id;
	req.sub_id = req_sub_id;

	if (idata) {
		for (i=0; i<icnt; i++)
			req.int_data[i] = idata[i];
	}

	if (sdata) {
		for (i=0; i<scnt; i++)
			strcpy(req.str_data[i],sdata[i]);
	}

	dump("send", &req, 0x20);

	r = write(fd, &req, RQT_PKT_SIZE);
	if (r != RQT_PKT_SIZE) {
		fprintf(stderr, "line %d: failed to send data r = %d\n",
				__LINE__, r);
		return -1;
	}

	return 0;
}

int send_request_timeout(int fd, request_type req_id, int req_sub_id,
	int *idata, int icnt, char **sdata, int scnt, struct res_pkt *pres, int timeout)
{
	struct res_pkt resp;
	int r;

	r = write_request(fd, req_id, req_sub_id, idata, icnt, sdata, scnt);
	if (r < 0) {
		fprintf(stderr, "line %d: failed to write request r = %d\n",
				__LINE__, r);
		return -1;
	}

	r = serial_read(fd, &resp, sizeof resp, timeout);
	if (r >= 0)
		dump("recv", &resp, sizeof resp);
	if (r != sizeof resp) {
		fprintf(stderr, "line %d: failed to receive data r = %d\n",
				__LINE__, r);
		return -1;
	}

	if (pres)
		memcpy(pres, &resp, RES_PKT_SIZE);

	if (resp.ack != 0) {
		if (opt_verbose) {
			fprintf(stderr, "\x1b[0;31;1mMissmatch\x1b[0m\n");
			fprintf(stderr, "line %d: ack reports fail. ack = %d\n",
					__LINE__, resp.ack);
		} else {
			fprintf(stderr, "\x1b[1A\x1b[16C\x1b[0;31;1mMissmatch\x1b[0m\n");
		}
		return -2;
	}

	return resp.ack;
}

int send_request(int fd, request_type req_id, int req_sub_id, int *idata, int icnt)
{
	return send_request_timeout(fd, req_id, req_sub_id, idata, icnt,
		NULL, 0, NULL, DEFAULT_TIMEOUT);
}

int request_reboot(int fd)
{
	int r;
	r = send_request(fd, RQT_CMD, RQT_CMD_REBOOT, NULL, 0);
	if (r)
		fprintf(stderr, "RQT_CMD_REBOOT,   status = %08x\n", r);
	return r;
}

int thor_handshake(int fd, int timeout, int log_on)
{
	char buffer[4];
	int r;

	r = write(fd, "THOR", 4);
	if (r != 4) {
		if (log_on)
			fprintf(stderr, "line %d: failed to write signature bytes\n",
					__LINE__);
		return -1;
	}

	r = serial_read(fd, buffer, 4, timeout);
	if (r != 4) {
		if (log_on)
			fprintf(stderr, "line %d: failed to read signature bytes\n",
					__LINE__);
		return -1;
	}

	if (memcmp(buffer, "ROHT", 4)) {
		if (log_on)
			fprintf(stderr, "line %d: signature byte mismatch\n",
					__LINE__);
		return -1;
	}

	return 0;
}

int getfile(const char *devpath, char *buffer, size_t bufsize)
{
	int fd, r = 0;

	fd = open(devpath, O_RDONLY);
	if (fd >= 0) {
		r = read(fd, buffer, bufsize - 1);
		if (r < 0)
			r = 0;
		close(fd);
	}
	else if (opt_verbose)
		fprintf(stderr, "failed to open %s\n", devpath);
	if (r && buffer[r - 1] == '\n')
		r--;
	buffer[r] = 0;
	return r;
}

/*
 * Given USB path, return tty name
 * FIXME: probably should look up the /dev from device major:minor
 * There seems to be two styles of device name:
 *  1. /sys/bus/usb/devices/1-2/1-2\:2.0/tty/ttyACM0/dev
 *  2. /sys/bus/usb/devices/1-2/1-2\:2.0/tty:ttyACM0
 */
char *device_from_usb_tty_directory(const char *usbpath)
{
	static char ttypath[0x400];
	static char devpath[0x400];
	char *ret = NULL;
	DIR *d;

	strcpy(ttypath, usbpath);
	strcat(ttypath, "/tty");

	d = opendir(ttypath);
	if (d) {
		if (opt_verbose)
			fprintf(stderr, "listing %s:\n", ttypath);

		/* device name is under tty directory */
		while (!ret) {
			struct dirent *de;

			de = readdir(d);
			if (!de)
				break;

			if (opt_verbose)
				fprintf(stderr, "%s\n", de->d_name);

			if (de->d_name[0] == '.')
				continue;

			strcpy(devpath, "/dev/");
			strcat(devpath, de->d_name);

			ret = devpath;
		}
	} else	{
		/* device name is in directory */
		d = opendir(usbpath);
		if (!d)
			return NULL;

		if (opt_verbose)
			fprintf(stderr, "listing %s:\n", usbpath);

		while (!ret) {
			struct dirent *de;

			de = readdir(d);
			if (!de)
				break;

			if (opt_verbose)
				fprintf(stderr, "%s\n", de->d_name);

			if (strncmp(de->d_name, "tty:", 4))
				continue;

			strcpy(devpath, "/dev/");
			strcat(devpath, &de->d_name[4]);

			ret = devpath;
		}
	}

	closedir(d);

	return ret;
}

int find_usb_device(void)
{
	DIR *usb_dir;
	DIR *usb_dir_in;
	char *p;
	char buffer[11];
	const char *dirname = "/sys/bus/usb/devices";
	char usbpath[0x400];
	char usbdir[0x100];
	char *tty = NULL;
	int fd, r;

	usb_dir = opendir(dirname);
	if (!usb_dir)
		return -1;

	while (1) {
		struct dirent *de;

		de = readdir(usb_dir);
		if (!de)
			break;

		if (de->d_name[0] == '.')
			continue;

		strcpy(usbdir, de->d_name);
		strcpy(usbpath, dirname);
		strcat(usbpath, "/");
		strcat(usbpath, usbdir);
		strcat(usbpath, "/");
		p = &usbpath[strlen(usbpath)];
		p[0] = 0x00;

		usb_dir_in = opendir(usbpath);
		if (opt_verbose)
			fprintf(stderr, "at %s\n", usbpath);

		while ((de = readdir(usb_dir_in))) {
			if (strlen(de->d_name) < strlen(usbdir))
				continue;

			if (de->d_type != DT_DIR)
				continue;

			if (strncmp(de->d_name, usbdir, strlen(usbdir)))
				continue;

			strcpy(p, de->d_name);
			if (opt_verbose)
				fprintf(stderr, "search for tty on %s\n", usbpath);

			tty = device_from_usb_tty_directory(usbpath);
			if (tty) {
				fd = get_termios(tty);
				if (fd < 0)
					continue;

				r = thor_handshake(fd, 2000, 0);
				if (r < 0) {
					close(fd);
					continue;
				}

				closedir(usb_dir_in);
				return fd;
			}
		}

		closedir(usb_dir_in);
	}

	closedir(usb_dir);

	if (opt_verbose)
		fprintf(stderr, "No USB device found with matching\n");

	return -1;
}

int get_termios(const char *portname)
{
	struct termios tios;
	int fd, r;

	/* On OS X open serial port with O_NONBLOCK flag */
	fd = open(portname, O_RDWR | O_NONBLOCK);

	if (fd == -1) {
		perror("port open error!!\n");
		return -1;
	}

	/*now clear the O_NONBLOCK flag to enable writing big data chunks at once*/
	if (fcntl(fd, F_SETFL, 0)) {
		printf("line %d: error clearing O_NONBLOCK\n", __LINE__);
		return -1;
	}

	r = tcgetattr(fd, &tios);
	if (r < 0) {
		fprintf(stderr, "line %d: tcgetattr failed\n", __LINE__);
		close(fd);
		return -1;
	}

	/*
	 * Firmware BUG alert!!!
	 * Flow control (RTS/CTS) should be disabled, because
	 * the firmware doesn't deal with it properly.
	 */
        cfmakeraw(&tios);
	r = tcsetattr(fd, TCSANOW, &tios);
	if (r < 0) {
		fprintf(stderr, "line %d: tcsetattr failed\n", __LINE__);
		close(fd);
		return -1;
	}

	r = tcflush(fd, TCIOFLUSH);
	if (r < 0) {
		fprintf(stderr, "line %d: tcflush failed\n", __LINE__);
		close(fd);
		return -1;
	}

	return fd;
}

int open_port(const char *portname, int wait)
{
	int once = 0;
	int fd, r;

	if (opt_test)
		return open("/dev/null", O_RDWR);

	while (1) {
		if (!portname) {
			fd = find_usb_device();
			if (fd >= 0)
				return fd;

		} else {
			fd = get_termios(portname);
			if (fd < 0) {
				fprintf(stderr, "USB port is "
					"\x1b[0;31;1mnot\x1b[0m detected !\n\n");
				return fd;
			}

			r = thor_handshake(fd, DEFAULT_TIMEOUT, 1);
			if (r < 0) {
				close(fd);
				return -1;
			}

			return fd;
		}

		if (!wait) {
			fprintf(stderr, "line %d: device not found\n", __LINE__);
			return -1;
		}

		if (!once) {
			if (!wait)
			    return -1;
			printf("\nUSB port is not detected yet... \n");
			printf("Make sure phone(device) should be in a download mode \n");
			printf("before connecting phone to PC with USB cable.\n");
			printf("(How to enter download mode : press <volume-down> + <power> key)\n\n");
			once = 1;
		}
		sleep(1);
	}
}

int wait_and_open_port(const char *portname)
{
	return open_port(portname, 1);
}

/*
 * Write a data source (file) into a partition
 *
 * data_src abstraction provided so sending a single file
 * from a non-tar source is possible one day
 */
int download_single_file(int fd, struct data_src *senddata, int filetype)
{
	int r;
	size_t filesize;
	const char *filename;
	struct res_pkt resp;
	int32_t	int_data[2];

	filesize = senddata->get_data_length(senddata);
	filename = senddata->get_data_name(senddata);

	int_data[0] = filetype;
	int_data[1] = filesize;
	r = send_request_timeout(fd, RQT_DL, RQT_DL_FILE_INFO, int_data, 2,
		(char **)&filename, 1, &resp, DEFAULT_TIMEOUT);
	if (r < 0) {
		if (opt_verbose)
			fprintf(stderr, "RQT_DL_FILE_INFO, status = %08x\n", r);
		return r;
	}
	trans_unit_size = resp.int_data[0];

	r = send_request(fd, RQT_DL, RQT_DL_FILE_START, NULL, 0);
	if (r < 0) {
		fprintf(stderr, "RQT_DL_FILE_START, status = %08x\n", r);
		return -1;
	}

	r = send_chunks(fd, senddata, filesize);
	if (r < 0) {
		fprintf(stderr, "send_chunks(), status = %08x\n", r);
		return -1;
	}

	r = send_request(fd, RQT_DL, RQT_DL_FILE_END, NULL, 0);
	if (r < 0) {
		fprintf(stderr, "RQT_DL_FILE_END, status = %08x\n", r);
		return -1;
	}

	return 0;
}

struct file_data_src {
	struct data_src src;
	int fd;
	const char* filename;
	size_t filesize;
};

size_t file_get_data_length(struct data_src *src)
{
	struct file_data_src *filedata = (struct file_data_src *) src;

	return filedata->filesize;
}

size_t file_get_data_block(struct data_src *src, void *data, size_t len)
{
	struct file_data_src *filedata = (struct file_data_src *) src;

	return read(filedata->fd, data, len);
}

const char *file_get_data_name(struct data_src *src)
{
	struct file_data_src *filedata = (struct file_data_src *) src;

	return filedata->filename;
}

int filedata_open(struct file_data_src *filedata, const char *filename)
{
	int r;
	char *basefile;

	if (!filedata)
		return -1;

	memset((void*)filedata, 0x00, sizeof(struct file_data_src));

	r = open(filename, O_RDONLY);
	if (r < 0)
		return r;

	filedata->fd = r;

	/* According to the man page basename() might modify the argument or
	 * return a pointer to statically allocated memory. Thus two strdup()s */
	basefile = strdup(filename);
	filedata->filename = strdup(basename(basefile));
	free(basefile);

	filedata->filesize = lseek(filedata->fd, 0, SEEK_END);
	lseek(filedata->fd, 0, SEEK_SET);

	filedata->src.get_data_length = &file_get_data_length;
	filedata->src.get_data_block = &file_get_data_block;
	filedata->src.get_data_name = &file_get_data_name;

	return r;
}

int filedata_close(struct file_data_src *filedata)
{
	close(filedata->fd);
	free((void *)filedata->filename);
	return 0;
}

struct tar_data_src {
	struct data_src src;
	struct archive *ar;
	struct archive_entry *ae;
};

size_t te_get_data_length(struct data_src *src)
{
	struct tar_data_src *tardata = (struct tar_data_src *) src;

	return archive_entry_size(tardata->ae);
}

size_t te_get_data_block(struct data_src *src, void *data, size_t len)
{
	struct tar_data_src *tardata = (struct tar_data_src *) src;

	return archive_read_data(tardata->ar, data, len);
}

const char *te_get_data_name(struct data_src *src)
{
	struct tar_data_src *tardata = (struct tar_data_src *) src;

	return archive_entry_pathname(tardata->ae);
}

int tardata_open(struct tar_data_src *tardata, const char *tarfile)
{
	int r;

	/* open the tar archive */
	tardata->ar = archive_read_new();

	archive_read_support_format_tar(tardata->ar);
	archive_read_support_compression_gzip(tardata->ar);
	archive_read_support_compression_bzip2(tardata->ar);

	if (!strcmp(tarfile, "-"))
		r = archive_read_open_FILE(tardata->ar, stdin);
	else
		r = archive_read_open_filename(tardata->ar, tarfile, 512);

	if (r) {
		fprintf(stderr, "line %d: failed to open %s\n", __LINE__, tarfile);
		return r;
	}

	tardata->src.get_data_length = &te_get_data_length;
	tardata->src.get_data_block = &te_get_data_block;
	tardata->src.get_data_name = &te_get_data_name;
	tardata->ae = NULL;

	return r;
}

const char *tardata_next(struct tar_data_src *tardata)
{
	int64_t partsize;
	int r;

	r = archive_read_next_header(tardata->ar, &tardata->ae);
	if (r == ARCHIVE_EOF)
		return NULL;

	if (r != ARCHIVE_OK) {
		fprintf(stderr, "line %d: archive_read_next_header error %d\n",
				__LINE__, r);
		return NULL;
	}

	partsize = archive_entry_size(tardata->ae);
	if (partsize > SIZE_MAX) {
		/*
		 * FIXME: fix source (and maybe firmware) to deal with files
		 * bigger than 4G
		 */
		fprintf(stderr, "line %d: Large file %llx (>4GB) is not"
				" supported\n", __LINE__, (unsigned long long)
				partsize);
		return NULL;
	}

	return archive_entry_pathname(tardata->ae);
}

int tardata_close(struct tar_data_src *tardata)
{
	archive_read_close(tardata->ar);
	archive_read_finish(tardata->ar);
	return 0;
}

int get_entry_size_in_tar(const char *tarfile, unsigned long long *total)
{
	struct tar_data_src tardata;
	const char *filename;
	int r;

	r = tardata_open(&tardata, tarfile);
	if (r < 0) {
		fprintf(stderr, "line %d: failed to open %s\n", __LINE__,
				tarfile);
		return r;
	}

	*total = 0;

	while (1) {
		size_t size = 0;

		filename = tardata_next(&tardata);
		if (!filename)
			break;

		size = tardata.src.get_data_length(&tardata.src);
		printf("[\x1b[0;32;1m%s\x1b[0m]\t%zuk\n", filename, size/KB);

		*total += size;
	}

	tardata_close(&tardata);

	return 0;
}

int download_pitfile(int fd, const char *pitfile)
{
	struct file_data_src filedata;
	int r;

	r = filedata_open(&filedata, pitfile);
	if (r < 0) {
		fprintf(stderr, "line %d: failed to open %s\n", __LINE__, pitfile);
		return r;
	}


	printf("[\x1b[0;32;1m%s\x1b[0m]\n", pitfile);
	r = download_single_file(fd, &filedata.src, BINARY_TYPE_PIT);
	if (r < 0) {
		fprintf(stderr, "line %d: failed to download %s\n", __LINE__,
				pitfile);
		filedata_close(&filedata);
		return r;
	}

	fprintf(stderr, "\n%s completed\n", pitfile);

	filedata_close(&filedata);

	return r;
}

int download_single_tarfile(int fd, const char *tarfile)
{
	struct tar_data_src tardata;
	const char *filename;
	int r;

	r = tardata_open(&tardata, tarfile);
	if (r < 0) {
		fprintf(stderr, "line %d: failed to open %s\n", __LINE__, tarfile);
		return r;
	}

	while (1) {
		filename = tardata_next(&tardata);
		if (!filename)
			break;

		printf("[\x1b[0;32;1m%s\x1b[0m]\n", filename);
		r = download_single_file(fd, &tardata.src, BINARY_TYPE_NORMAL);
		if (r == -1) {
			fprintf(stderr, "line %d: failed to download %s\n", __LINE__, filename);
			fprintf(stderr, "\nIn some cases, lthor needs enough memory\n");
			fprintf(stderr, "Please check free memory in your Host PC ");
			fprintf(stderr, "and unload some heavy applications\n");
			tardata_close(&tardata);
			return r;
		}
	}

	fprintf(stderr, "\n%s completed\n", tarfile);

	tardata_close(&tardata);

	return 0;
}

int process_download(const char *portname, const char *pitfile, char **tarfilelist)
{
	int r;
	char **tfl;
	off_t pit_length = 0;
	int fd;
	unsigned long long total;

	/* now connect to the target */
	fd = wait_and_open_port(portname);
	if (fd < 0) {
		fprintf(stderr, "line %d: failed to open port %s\n", __LINE__,
				portname);
		return -1;
	}

	total = 0;
	tfl = tarfilelist;
	while (tfl && *tfl) {
		unsigned long long len = 0;
		printf("\x1b[0;33;1m%s :\x1b[0m\n", *tfl);
		if (get_entry_size_in_tar(*tfl, &len) < 0) {
			perror("Error");
			close(fd);
			return -1;
		}
		total += len;
		tfl++;
	}

	if (pitfile) {
		pit_length = file_size(pitfile);
		if (pit_length < 0) {
			fprintf(stderr, "line %d: failed to get pit length\n"
				, __LINE__);
			close(fd);
			return -1;
		}
		total += pit_length;
		printf("\x1b[0;33;1m%s : \x1b[0m%zuk\n", pitfile,
			(size_t)pit_length/KB);
	}
	printf("-------------------------\n");
	printf("\t\x1b[0;33;1mtotal\x1b[0m :\t%.2fMB\n\n", (double)total/MB);

	/* for updating progress bar in the target */
	r = send_request(fd, RQT_DL, RQT_DL_INIT, (int*)&total, 1);
	/*
	 * FIXME : if total > SIZE_MAX then DL_INIT must change it's protocol to
	 * send/receive 64bit size data.
	 */
	if (r) {
		fprintf(stderr, "RQT_DL_INIT, status = %08x\n", r);
		close(fd);
		return -1;
	}

	if (pitfile) {
		fprintf(stderr, "\nDownload PIT file : %s\n\n", pitfile);
		r = download_pitfile(fd, pitfile);
		if (r < 0) {
			fprintf(stderr, "\nfailed to download %s\n", pitfile);
			close(fd);
			return r;
		}
	}

	while (tarfilelist && *tarfilelist) {
		fprintf(stderr, "\nDownload files from %s\n\n", *tarfilelist);
		r = download_single_tarfile(fd, *tarfilelist);
		if (r < 0) {
			fprintf(stderr, "\nfailed to download %s\n", *tarfilelist);
			close(fd);
			return r;
		}
		tarfilelist++;
	}

	r = send_request(fd, RQT_DL, RQT_DL_EXIT, NULL, 0);
	if (r) {
		fprintf(stderr, "\x1b[0;33;1mmissing RQT_DL_EXIT response "
			"from broken bootloader\x1b[0m\n");
	}

	fprintf(stderr, "\nrequest target reboot : ");

	r = request_reboot(fd);
	if (r)
		fprintf(stderr, "\x1b[0;31;1mfailed\x1b[0m\n");
	else
		fprintf(stderr, "\x1b[0;32;1msuccess\x1b[0m\n");

	close(fd);

	return 0;
}

/* Check if LHOR protocol is in working state */
int check_proto(const char *portname)
{
	int fd;
	/* int r;
        struct res_pkt resp;*/

	/* connect to the target */
	fd = open_port(portname, 0);
        if (fd < 0)
		return -1;

	/* Below I commented out my attempt to check if LHOR protocol is enabled
	 * by quering protocol version.
	 * The main problem is that I didn't find a way to 'close' the session,
	 * to put protocol into previous state.
	 * */

	/*
	r = thor_handshake(fd);
	if (r < 0) {
		fprintf(stderr, "line %d: handshake failed\n", __LINE__);
		return -1;
        }

	r = send_request_timeout(fd, RQT_INFO, RQT_INFO_VER_PROTOCOL, NULL, 0, NULL, 0, &resp, DEFAULT_TIMEOUT);
        if (r) {
		fprintf(stderr, "RQT_INFO_VER_PROTOCOL, status = %08x\n", r);
		return -1;
	} */

	/* Here should be some code, which closes protocol session, resets it somehow */
	
	close(fd);

	return 0;
}

int test_tar_entry(struct data_src *tardata)
{
	static unsigned char *chunk;
	size_t n = 0, size;
	int chunksize;

	chunk = (unsigned char*)malloc(trans_unit_size);
	if (chunk == NULL) {
		fprintf(stderr, "Unable to allocate memory.\n");
		return -1;
	}

	size = tardata->get_data_length(tardata);
	while (n < size) {
		if (size - n >= trans_unit_size)
			chunksize = trans_unit_size;
		else
			chunksize = size - n;

		chunksize = tardata->get_data_block(tardata, chunk, chunksize);
		if (chunksize <= 0) {
			free(chunk);
			return -1;
		}

		if (!(chunksize <= trans_unit_size && chunksize > 0)) {
			free(chunk);
			return -1;
		}

		if ((n/chunksize)%4 == 0)
			fprintf(stderr, ".");

		n += chunksize;
	}

	free(chunk);

	return 0;
}

int test_tar_file(const char *tarfile)
{
	struct tar_data_src tardata;
	const char *filename;
	int r = 0;

	fprintf(stderr, "tar %s\n", tarfile);

	r = tardata_open(&tardata, tarfile);
	if (r < 0) {
		fprintf(stderr, "line %d: failed to open %s\n", __LINE__, tarfile);
		return r;
	}

	while (1) {
		filename = tardata_next(&tardata);
		if (!filename)
			break;

		fprintf(stderr, " entry %s ", filename);

		r = test_tar_entry(&tardata.src);
		if (r) {
			fprintf(stderr, "bad\n");
			break;
		}

		fprintf(stderr, "\n");
	}

	tardata_close(&tardata);

	return r;
}


int test_tar_file_list(char **tarfilelist)
{
	int r;

	while (*tarfilelist) {
		char *tarfile = *tarfilelist;

		r = test_tar_file(tarfile);
		if (r < 0)
			fprintf(stderr, "failed to load %s\n", *tarfilelist);
		tarfilelist++;
	}

	return 0;
}

void usage(const char *exename)
{
	fprintf(stderr, "%s: [-t] [-v] [-c] [-d port] [-p pitfile] [tar] [tar] ..\n",
			exename);
	exit(1);
}

int main(int argc, char **argv)
{
	const char *exename, *portname, *pitfile;
	int opt;
	int opt_check = 0;

	exename = argv[0];

	opt = 1;

	pitfile = NULL;
	portname = NULL;

	printf("\n");
	printf("Linux Thor downloader, version %s \n", PACKAGE_VERSION);
	printf("Authors: Jaehoon You <jaehoon.you@samsung.com>\n\n");

	while (opt < argc) {
		/* check if we're verbose */
		if (!strcmp(argv[opt], "-v")) {
			opt_verbose = 1;
			opt++;
			continue;
		}

		if (!strcmp(argv[opt], "-t")) {
			opt_test = 1;
			opt++;
			continue;
		}

		if  (!strcmp(argv[opt], "-c")) {
		        opt_check = 1;
		        opt++;
		        continue;
		}

		if (!strcmp(argv[opt], "-p")) {
			pitfile = argv[opt+1];
			opt += 2;
			continue;
		}

		if (!strcmp(argv[opt], "-d") && (opt+1) < argc) {
			portname = argv[opt+1];
			opt += 2;
			continue;
		}

		break;
	}

	if (opt_test)
		return test_tar_file_list(&argv[opt]);

	if (opt_check)
		return check_proto(portname);

	if ((pitfile)&&(opt == argc))
		return process_download(portname, pitfile, NULL);

	if (opt < argc)
		return process_download(portname, pitfile, &argv[opt]);

	usage(exename);
	return 0;
}

