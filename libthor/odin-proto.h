#ifndef __ODIN_PROTO_H__
#define __ODIN_PROTO_H__

/* the Odin protocol shares many data types with Thor */

enum {
	RQT_ODIN_DL_INIT = 0x64,
	RQT_ODIN_PIT = 0x65,
	RQT_ODIN_FILE_XFER = 0x66,
	RQT_ODIN_DL_END = 0x67,
};

/* RQT_ODIN_DL_INIT */
enum {
	RQT_ODIN_DL_INIT_BEGIN = 0,
	RQT_ODIN_DL_INIT_DEVICE_TYPE = 1,
	RQT_ODIN_DL_INIT_BYTES = 2,
	RQT_ODIN_DL_INIT_UNKNOWN_A = 3,
	RQT_ODIN_DL_INIT_UNKNOWN_B = 4,
	RQT_ODIN_DL_INIT_XFER_SIZE = 5,
	RQT_ODIN_DL_INIT_UNKNOWN_C = 6,
	RQT_ODIN_DL_INIT_UNKNOWN_D = 7,
	RQT_ODIN_DL_INIT_TF = 8,
};

/* RQT_ODIN_PIT and RQT_ODIN_FILE_XFER */
enum {
	RQT_ODIN_PIT_FLASH = 0,
	RQT_ODIN_PIT_DUMP = 1,
	RQT_ODIN_PIT_PART = 2,
	RQT_ODIN_PIT_XFER_END = 3,
};

struct rsp_odin_pit_dump {
	int32_t size;
};

struct req_odin_pit_part {
	int32_t xfer_index;
};

/* RQT_ODIN_DL_END */
enum {
	RQT_ODIN_DL_END_REG = 0,
	RQT_ODIN_DL_END_REBOOT,
};

#endif /* __ODIN_PROTO_H__ */
