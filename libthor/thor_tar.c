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
#include <archive.h>
#include <archive_entry.h>
#include <errno.h>
#include <string.h>
#include <sys/queue.h>

#include "thor.h"
#include "thor_internal.h"

struct entry_container {
	struct thor_data_src_entry entry;
	STAILQ_ENTRY(entry_container) node;
};

struct tar_data_src {
	struct thor_data_src src;
	struct archive *ar;
	struct archive_entry *ae;
	off_t total_size;
	struct thor_data_src_entry **entries;
	STAILQ_HEAD(ent, entry_container) ent;
};

static off_t tar_get_file_length(struct thor_data_src *src)
{
	struct tar_data_src *tardata =
		container_of(src, struct tar_data_src, src);

	return archive_entry_size(tardata->ae);
}

static off_t tar_get_size(struct thor_data_src *src)
{
	struct tar_data_src *tardata =
		container_of(src, struct tar_data_src, src);

	return tardata->total_size;
}

static off_t tar_get_data_block(struct thor_data_src *src,
				 void *data, off_t len)
{
	struct tar_data_src *tardata =
		container_of(src, struct tar_data_src, src);

	return archive_read_data(tardata->ar, data, len);
}

static const char *tar_get_file_name(struct thor_data_src *src)
{
	struct tar_data_src *tardata =
		container_of(src, struct tar_data_src, src);

	return archive_entry_pathname(tardata->ae);
}

static int tar_next_file(struct thor_data_src *src)
{
	struct tar_data_src *tardata =
		container_of(src, struct tar_data_src, src);
	int ret;

	ret = archive_read_next_header2(tardata->ar, tardata->ae);
	if (ret == ARCHIVE_OK)
		return 1;

	if (ret == ARCHIVE_EOF)
		return 0;

	return -EINVAL;
}

static void tar_release(struct thor_data_src *src)
{
	struct tar_data_src *tardata =
		container_of(src, struct tar_data_src, src);
	struct entry_container *container;

	while (!STAILQ_EMPTY(&tardata->ent)) {
		container = STAILQ_FIRST(&tardata->ent);
		STAILQ_REMOVE_HEAD(&tardata->ent, node);
		free(container);
	}
	free(tardata->entries);
	archive_read_close(tardata->ar);
	archive_read_finish(tardata->ar);
	archive_entry_free(tardata->ae);
	free(tardata);
}

static int tar_prep_read(const char *path, struct archive **archive,
			 struct archive_entry **aentry)
{
	struct archive *ar;
	struct archive_entry *ae;
	int ret;

	ar = archive_read_new();
	if (!ar)
		return -ENOMEM;

	ae = archive_entry_new();
	if (!ae)
		goto read_finish;

	archive_read_support_format_tar(ar);
	archive_read_support_compression_gzip(ar);
	archive_read_support_compression_bzip2(ar);

	if (!strcmp(path, "-"))
		ret = archive_read_open_FILE(ar, stdin);
	else
		ret = archive_read_open_filename(ar, path, 512);

	if (ret)
		goto cleanup;

	*archive = ar;
	*aentry = ae;
	return 0;
cleanup:
	archive_entry_free(ae);
read_finish:
	archive_read_finish(ar);
	return ret;
}

static int tar_calculate_total(const char *path, struct tar_data_src *tardata)
{
	struct archive *ar;
	struct archive_entry *ae;
	char *name;
	off_t size;
	struct entry_container *container;
	int i;
	int ret;

	STAILQ_INIT(&tardata->ent);
	tardata->entries = NULL;

	/*
	 * Yes this is very ugly but libarchive doesn't
	 * allow to reset position :(
	 */
	ret = tar_prep_read(path, &ar, &ae);
	if (ret)
		goto out;

	tardata->total_size = 0;
	for (i = 0; 1; ++i) {
		ret = archive_read_next_header2(ar, ae);
		if (ret == ARCHIVE_EOF) {
			break;
		} else if (ret != ARCHIVE_OK) {
			tardata->total_size = -EINVAL;
			goto cleanup;
		}

		name = strdup(archive_entry_pathname(ae));
		if (!name) {
			ret = -ENOMEM;
			goto cleanup;
		}

		size = archive_entry_size(ae);
		tardata->total_size += size;

		container = calloc(1, sizeof(*container));
		if (!container) {
			free(name);
			ret = -ENOMEM;
			goto cleanup;
		}

		container->entry.name = name;
		container->entry.size = size;
		STAILQ_INSERT_TAIL(&tardata->ent, container, node);
	}

	tardata->entries = calloc(i + 1, sizeof(*(tardata->entries)));
	if (!tardata->entries) {
		ret = -ENOMEM;
		goto cleanup;
	}

	i = 0;
	STAILQ_FOREACH(container, &tardata->ent, node)
		tardata->entries[i++] = &container->entry;

	ret = 0;
cleanup:
	if (ret) {
		while (!STAILQ_EMPTY(&tardata->ent)) {
			container = STAILQ_FIRST(&tardata->ent);
			STAILQ_REMOVE_HEAD(&tardata->ent, node);
			free(container);
		}
	}
	archive_read_close(ar);
	archive_read_finish(ar);
	archive_entry_free(ae);
out:
	return ret;
}

static struct thor_data_src_entry **tar_get_entries(struct thor_data_src *src)
{
	struct tar_data_src *tardata =
		container_of(src, struct tar_data_src, src);

	return tardata->entries;
}

int t_tar_get_data_src(const char *path, struct thor_data_src **data)
{
	struct tar_data_src *tdata;
	int ret;

	tdata = calloc(1, sizeof(*tdata));
	if (!tdata)
		return -ENOMEM;

	/* open the tar archive */
	ret = tar_prep_read(path, &tdata->ar, &tdata->ae);
	if (ret)
		goto free_tdata;

	tdata->src.get_file_length = tar_get_file_length;
	tdata->src.get_size = tar_get_size;
	tdata->src.get_block = tar_get_data_block;
	tdata->src.get_name = tar_get_file_name;
	tdata->src.next_file = tar_next_file;
	tdata->src.get_entries = tar_get_entries;
	tdata->src.release = tar_release;

	ret = tar_calculate_total(path, tdata);
	if (ret < 0)
		goto read_close;

	*data = &tdata->src;
	return 0;

read_close:
	archive_read_close(tdata->ar);
	archive_entry_free(tdata->ae);
free_tdata:
	free(tdata);
	return ret;
}

