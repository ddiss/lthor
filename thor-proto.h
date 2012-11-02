#ifndef __THOR_PROTO_H__
#define __THOR_PROTO_H__

typedef enum {
	RQT_INFO = 200,	
	RQT_CMD,
	RQT_DL,
	RQT_UL,
} request_type;

/* Request Data */
/* RQT_INFO */
enum {
	RQT_INFO_VER_PROTOCOL = 1,
	RQT_INFO_VER_HW,
	RQT_INFO_VER_BOOT,
	RQT_INFO_VER_KERNEL,
	RQT_INFO_VER_PLATFORM,
	RQT_INFO_VER_CSC,
};

/* RQT_CMD */
enum {
	RQT_CMD_REBOOT = 1,
	RQT_CMD_POWEROFF,
};

/* RQT_DL */
enum {
	RQT_DL_INIT = 1,
	RQT_DL_FILE_INFO,
	RQT_DL_FILE_START,
	RQT_DL_FILE_END,
	RQT_DL_EXIT,
};

/* RQT_UL */
enum {
	RQT_UL_INIT = 1,
	RQT_UL_START,
	RQT_UL_END,
	RQT_UL_EXIT,
};

enum __binary_type {
	BINARY_TYPE_NORMAL = 0,
	BINARY_TYPE_PIT,
};

struct rqt_pkt {
	int32_t	id;			/* Request Group ID. */
	int32_t	sub_id;			/* Request Data ID. */
	int32_t	int_data[14];		/* Int. Datas. */
	char	str_data[5][32];	/* Str. Data. */
	char	md5[32];		/* MD5 Checksum. */
};


struct res_pkt {
	int32_t	id;			/* Response Group ID == Request Group ID. */
	int32_t	sub_id;			/* Response Data ID == Request Data ID. */
	int32_t	ack;			/* Ack. */
	int32_t	int_data[5];		/* Int. Datas. */
	char	str_data[3][32];	/* Str. Data. */
};


struct data_res_pkt {
	int32_t	ack;			/* Ack. */
	int32_t	cnt;			/* Int. Datas. */
};


#define RQT_PKT_SIZE		sizeof(struct rqt_pkt)
#define RES_PKT_SIZE		sizeof(struct res_pkt)
#define DATA_RES_PKT_SIZE	sizeof(struct data_res_pkt)

#endif /* __THOR_PROTO_H__ */
