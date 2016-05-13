/*
 * libthor - Tizen Thor communication protocol
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
#include <stdlib.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <libgen.h>
#include <errno.h>
#include <unistd.h>

#include "thor.h"
#include "thor_internal.h"

struct file_data_src {
	struct thor_data_src src;
	int fd;
	const char* filename;
	off_t filesize;
	int pos;
};

static off_t file_get_file_length(struct thor_data_src *src)
{
	struct file_data_src *filedata =
		container_of(src, struct file_data_src, src);

	return filedata->filesize;
}

static off_t file_get_data_block(struct thor_data_src *src,
				  void *data, off_t len)
{
	struct file_data_src *filedata =
		container_of(src, struct file_data_src, src);

	return read(filedata->fd, data, len);
}

static const char *file_get_file_name(struct thor_data_src *src)
{
	struct file_data_src *filedata =
		container_of(src, struct file_data_src, src);

	return filedata->filename;
}

static void file_release(struct thor_data_src *src)
{
	struct file_data_src *filedata =
		container_of(src, struct file_data_src, src);

	close(filedata->fd);
	free((void *)filedata->filename);
	free(filedata);
}

static int file_next_file(struct thor_data_src *src)
{
	struct file_data_src *filedata =
		container_of(src, struct file_data_src, src);

	return !filedata->pos ? ++filedata->pos : 0;
}

int t_file_get_data_src(const char *path, struct thor_data_src **data)
{
	int ret;
	char *basefile;
	struct file_data_src *fdata;

	fdata = calloc(sizeof(*fdata), 1);
	if (!fdata)
		return -1;

	ret = open(path, O_RDONLY);
	if (ret < 0)
		goto close_file;

	fdata->fd = ret;

	/*
	 * According to the man page basename() might modify the argument or
	 * return a pointer to statically allocated memory. Thus two strdup()s
	 */
	basefile = strdup(path);
	if (!basefile)
		goto close_file;

	fdata->filename = strdup(basename(basefile));
	free(basefile);
	if (!fdata->filename)
		goto close_file;

	fdata->filesize = lseek(fdata->fd, 0, SEEK_END);
	lseek(fdata->fd, 0, SEEK_SET);

	fdata->src.get_file_length = file_get_file_length;
	fdata->src.get_size = file_get_file_length;
	fdata->src.get_block = file_get_data_block;
	fdata->src.get_name = file_get_file_name;
	fdata->src.release = file_release;
	fdata->src.next_file = file_next_file;
	fdata->pos = 0;

	*data = &fdata->src;
	return 0;

close_file:
	close(ret);
	free(fdata);
	return -EINVAL;
}

