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
	const char *filename;
	int pos;
	struct thor_data_src_entry entry;
	struct thor_data_src_entry *ent[2];
};

static off_t file_get_file_length(struct thor_data_src *src)
{
	int ret;
	struct stat buf;
	struct file_data_src *filedata =
		container_of(src, struct file_data_src, src);

	ret = fstat(filedata->fd, &buf);
	if (ret < 0)
		return -errno;

	return buf.st_size;
}

static int file_set_file_length(struct thor_data_src *src,
				off_t len)
{
	int ret;
	struct file_data_src *filedata =
		container_of(src, struct file_data_src, src);

	ret = ftruncate(filedata->fd, len);
	if (ret < 0) {
		return -errno;
	}

	return 0;
}

static off_t file_put_data_block(struct thor_data_src *src,
				  void *data, off_t len)
{
	off_t ret;
	struct file_data_src *filedata =
		container_of(src, struct file_data_src, src);

	ret = write(filedata->fd, data, len);
	if (ret < 0) {
		ret = -errno;
	}
	return ret;
}

static off_t file_get_data_block(struct thor_data_src *src,
				  void *data, off_t len)
{
	off_t ret;
	struct file_data_src *filedata =
		container_of(src, struct file_data_src, src);

	ret = read(filedata->fd, data, len);
	if (ret < 0) {
		ret = -errno;
	}
	return ret;
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

static struct thor_data_src_entry **file_get_entries(struct thor_data_src *src)
{
	struct file_data_src *filedata =
		container_of(src, struct file_data_src, src);

	return filedata->ent;
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
		goto err_free;

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

	fdata->entry.name = (char *)fdata->filename;
	fdata->entry.size = lseek(fdata->fd, 0, SEEK_END);
	fdata->ent[0] = &fdata->entry;
	fdata->ent[1] = NULL;
	fdata->src.get_file_length = file_get_file_length;
	fdata->src.get_size = file_get_file_length;
	fdata->src.get_block = file_get_data_block;
	fdata->src.get_name = file_get_file_name;
	fdata->src.release = file_release;
	fdata->src.next_file = file_next_file;
	fdata->src.get_entries = file_get_entries;
	fdata->pos = 0;
	lseek(fdata->fd, 0, SEEK_SET);

	*data = &fdata->src;
	return 0;

close_file:
	close(ret);
err_free:
	free(fdata);
	return -EINVAL;
}

int t_file_get_data_dest(const char *path, struct thor_data_src **data)
{
	int ret;
	char *basefile;
	struct file_data_src *fdata;
	mode_t old_mask;

	fdata = calloc(sizeof(*fdata), 1);
	if (!fdata)
		return -ENOMEM;

	/* make only visible to user */
	old_mask = umask(0077);
	ret = open(path, O_WRONLY | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR);
	umask(old_mask);
	if (ret < 0) {
		ret = -errno;
		goto err_free;
	}

	fdata->fd = ret;

	basefile = strdup(path);
	if (!basefile) {
		ret = -ENOMEM;
		goto close_file;
	}

	fdata->filename = strdup(basename(basefile));
	free(basefile);
	if (!fdata->filename) {
		ret = -ENOMEM;
		goto close_file;
	}

	fdata->entry.name = (char *)fdata->filename;
	fdata->entry.size = 0;
	fdata->ent[0] = &fdata->entry;
	fdata->ent[1] = NULL;
	fdata->src.get_file_length = file_get_file_length;
	fdata->src.set_file_length = file_set_file_length;
	fdata->src.get_size = file_get_file_length;
	fdata->src.get_block = file_get_data_block;
	fdata->src.put_block = file_put_data_block;
	fdata->src.get_name = file_get_file_name;
	fdata->src.release = file_release;
	fdata->src.next_file = file_next_file;
	fdata->src.get_entries = file_get_entries;
	fdata->pos = 0;
	lseek(fdata->fd, 0, SEEK_SET);

	*data = &fdata->src;
	return 0;

close_file:
	close(fdata->fd);
err_free:
	free(fdata);
	return ret;
}
