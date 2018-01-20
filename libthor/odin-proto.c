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
#include <endian.h>
#include <errno.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>

#include "odin-proto.h"

static inline uint32_t
htoul(uint32_t hl)
{
	/* FIXME endianess */
	return hl;
}

static inline uint32_t
utohl(uint32_t ul)
{
	/* FIXME endianess */
	return ul;
}

int
rqt_odin_pack_dl_init(const struct rqt_odin_dl_init *rqt,
		      uint8_t *buf,
		      size_t buf_len)
{
	if ((buf_len < RQT_ODIN_PACKED_DL_INIT_LEN)
			|| (rqt == NULL)
			|| (rqt->id != RQT_ODIN_DL_INIT)) {
		return -EINVAL;
	}

	memset(buf, 0, RQT_ODIN_PACKED_DL_INIT_LEN);
	*((uint32_t *)buf) = htoul(rqt->id);
	*((uint32_t *)(buf + 4)) = htoul(rqt->subid);
	*((uint32_t *)(buf + 8)) = htoul(rqt->xfer_size);

	return 0;
}

int
rsp_odin_unpack_dl_init(const uint8_t *buf,
		      size_t buf_len,
		      struct rsp_odin_dl_init *rsp)
{
	if (buf_len < RSP_ODIN_PACKED_DL_INIT_LEN) {
		return -EINVAL;
	}

	rsp->id = utohl(*((uint32_t *)buf));
	if (rsp->id != RQT_ODIN_DL_INIT) {
		return -EFAULT;
	}

	rsp->xfer_size = utohl(*((uint32_t *)(buf + 4)));

	return 0;
}

int
rqt_odin_pack_dl_end(const struct rqt_odin_dl_end *rqt,
		     uint8_t *buf,
		     size_t buf_len)
{
	if ((buf_len < RQT_ODIN_PACKED_DL_END_LEN)
			|| (rqt == NULL)
			|| (rqt->id != RQT_ODIN_DL_END)) {
		return -EINVAL;
	}

	memset(buf, 0, RQT_ODIN_PACKED_DL_END_LEN);
	*((uint32_t *)buf) = htoul(rqt->id);
	*((uint32_t *)(buf + 4)) = htoul(rqt->subid);

	return 0;
}

int
rsp_odin_unpack_dl_end(const uint8_t *buf,
		      size_t buf_len,
		      struct rsp_odin_dl_end *rsp)
{
	if (buf_len < RSP_ODIN_PACKED_DL_END_LEN) {
		return -EINVAL;
	}

	rsp->id = utohl(*((uint32_t *)buf));
	if (rsp->id != RQT_ODIN_DL_END) {
		return -EFAULT;
	}

	rsp->unknown = utohl(*((uint32_t *)(buf + 4)));

	return 0;
}

int
rqt_odin_pack_pit(const struct rqt_odin_pit *rqt,
		  uint8_t *buf,
		  size_t buf_len)
{
	if ((buf_len < RQT_ODIN_PACKED_PIT_LEN)
			|| (rqt == NULL)
			|| (rqt->id != RQT_ODIN_PIT)) {
		return -EINVAL;
	}

	memset(buf, 0, RQT_ODIN_PACKED_PIT_LEN);
	*((uint32_t *)buf) = htoul(rqt->id);
	*((uint32_t *)(buf + 4)) = htoul(rqt->subid);
	*((uint32_t *)(buf + 8)) = htoul(rqt->part_off);

	return 0;
}

int
rsp_odin_unpack_pit(const uint8_t *buf,
		      size_t buf_len,
		      struct rsp_odin_pit *rsp)
{
	if (buf_len < RSP_ODIN_PACKED_PIT_LEN) {
		return -EINVAL;
	}

	rsp->id = utohl(*((uint32_t *)buf));
	if (rsp->id != RQT_ODIN_PIT) {
		return -EFAULT;
	}

	rsp->total_len = utohl(*((uint32_t *)(buf + 4)));

	return 0;
}
