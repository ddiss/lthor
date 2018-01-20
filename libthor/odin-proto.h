#ifndef __ODIN_PROTO_H__
#define __ODIN_PROTO_H__

/* the Odin protocol shares many data types with Thor */

enum rqt_odin_id {	/* loke refers to "request id" */
	RQT_ODIN_DL_INIT = 0x64,
	RQT_ODIN_PIT = 0x65,
	RQT_ODIN_FILE_XFER = 0x66,
	RQT_ODIN_DL_END = 0x67,		/* loke: process_rqt_close */
	/* invalid in gtab s2 aboot */
	RQT_ODIN_DEVINFO = 0x69,	/* DEVINFO? */
};

/* XXX loke refers to "data id" instead of "subid" */
enum rqt_odin_subid_devinfo {
	RQT_ODIN_DEVINFO_ZERO = 0,	/* 500 byte response? */
	RQT_ODIN_DEVINFO_ONE = 1,
	RQT_ODIN_DEVINFO_TWO = 2,
};

/* RQT_ODIN_DL_INIT */
enum rqt_odin_subid_dl_init {
	RQT_ODIN_DL_INIT_BEGIN = 0,
	RQT_ODIN_DL_INIT_DEVICE_TYPE = 1,
	RQT_ODIN_DL_INIT_BYTES = 2,
	RQT_ODIN_DL_INIT_UNKNOWN_A = 3,	/* invalid */
	RQT_ODIN_DL_INIT_UNKNOWN_B = 4,	/* invalid */
	RQT_ODIN_DL_INIT_XFER_SIZE = 5,
	RQT_ODIN_DL_INIT_UNKNOWN_C = 6,	/* invalid */
	RQT_ODIN_DL_INIT_FORMAT_ALL = 7,
	RQT_ODIN_DL_INIT_TF = 8,	/* invalid in gtab s2 aboot */
	RQT_ODIN_DL_INIT_ECHO_OR_SALES_CODE = 9,	/* VALID in gtab s2 aboot */
};

struct rqt_odin_dl_init {
	enum rqt_odin_id id;
	enum rqt_odin_subid_dl_init subid;
	off_t xfer_size;
};

#define RQT_ODIN_PACKED_DL_INIT_LEN 1024

struct rsp_odin_dl_init {
	enum rqt_odin_id id;
	off_t xfer_size;
};

#define RSP_ODIN_PACKED_DL_INIT_LEN 8

int
rqt_odin_pack_dl_init(const struct rqt_odin_dl_init *rqt,
		      uint8_t *buf,
		      size_t buf_len);

int
rsp_odin_unpack_dl_init(const uint8_t *buf,
			size_t buf_len,
			struct rsp_odin_dl_init *rsp);

/* RQT_ODIN_DL_END */
enum rqt_odin_subid_dl_end {
	RQT_ODIN_DL_END_REG = 0,
	RQT_ODIN_DL_END_REBOOT = 1,	/* unsupported on gtab s2 */
};

struct rqt_odin_dl_end {
	enum rqt_odin_id id;
	enum rqt_odin_subid_dl_end subid;
};

#define RQT_ODIN_PACKED_DL_END_LEN 1024

struct rsp_odin_dl_end {
	enum rqt_odin_id id;
	uint32_t unknown;
};

#define RSP_ODIN_PACKED_DL_END_LEN 8

int
rqt_odin_pack_dl_end(const struct rqt_odin_dl_end *rqt,
		     uint8_t *buf,
		     size_t buf_len);

int
rsp_odin_unpack_dl_end(const uint8_t *buf,
		       size_t buf_len,
		       struct rsp_odin_dl_end *rsp);

/* RQT_ODIN_PIT and RQT_ODIN_FILE_XFER */
enum rqt_odin_subid_pit {
	RQT_ODIN_PIT_FLASH = 0,
	RQT_ODIN_PIT_DUMP = 1,		/* invalid for RQT_ODIN_FILE_XFER on tab s2 */
	RQT_ODIN_PIT_PART = 2,
	RQT_ODIN_PIT_XFER_END = 3,
};

struct rqt_odin_pit {
	enum rqt_odin_id id;
	enum rqt_odin_subid_pit subid;
	uint32_t part_off;	/* for RQT_ODIN_PIT_PART, otherwise zero */
};

#define RQT_ODIN_PACKED_PIT_LEN 1024

struct rsp_odin_pit {
	enum rqt_odin_id id;
	uint32_t total_len;	/* for RQT_ODIN_PIT_DUMP at least */
};

#define RSP_ODIN_PACKED_PIT_LEN 8

int
rqt_odin_pack_pit(const struct rqt_odin_pit *rqt,
		  uint8_t *buf,
		  size_t buf_len);

int
rsp_odin_unpack_pit(const uint8_t *buf,
		    size_t buf_len,
		    struct rsp_odin_pit *rsp);

enum rqt_odin_pit_xfer_end_dest {
	RQT_ODIN_PIT_XFER_END_DEST_PHONE = 0,	/* PIT app processor type */
	RQT_ODIN_PIT_XFER_END_DEST_MODEM = 1,	/* PIT comm processor type */
};

enum rqt_odin_pit_xfer_end_dev_type {	/* FIXME */
	RQT_ODIN_PIT_XFER_END_DEV_TYPE_X = 0,
	RQT_ODIN_PIT_XFER_END_DEV_TYPE_Y = 1,
};

struct rqt_odin_pit_xfer_end {
	enum rqt_odin_pit_xfer_end_dest dest;
	uint32_t xfer_len;	/* can't exceed 0x20000000 on gtab s2 */
	enum rqt_odin_pit_xfer_end_dev_type dev_type;
	uint32_t file_id;
	uint32_t eof;
};

#endif /* __ODIN_PROTO_H__ */
