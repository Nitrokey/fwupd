/*
 * Copyright (C) 2018-2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#ifndef __FU_WACOM_COMMON_H
#define __FU_WACOM_COMMON_H

#include <glib-object.h>

G_BEGIN_DECLS

#define FU_WACOM_DEVICE_VID			0x056A
#define FU_WACOM_RAW_CMD_RETRIES		1000

#define FU_WACOM_RAW_STATUS_REPORT_ID		0x04
#define FU_WACOM_RAW_STATUS_REPORT_SZ		16

#define FU_WACOM_RAW_FW_REPORT_ID		0x02
#define FU_WACOM_RAW_FW_CMD_QUERY_MODE		0x00	/* 0x00=rt, 0x02=bl */
#define FU_WACOM_RAW_FW_CMD_DETACH		0x02

#define FU_WACOM_RAW_FW_MAINTAIN_REPORT_ID	0x09
#define FU_WACOM_RAW_FW_MAINTAIN_REPORT_SZ	64

#define FU_WACOM_RAW_BL_REPORT_ID_SET		0x07
#define FU_WACOM_RAW_BL_REPORT_ID_GET		0x08

#define FU_WACOM_RAW_BL_CMD_ERASE_FLASH		0x00
#define FU_WACOM_RAW_BL_CMD_WRITE_FLASH		0x01
#define FU_WACOM_RAW_BL_CMD_VERIFY_FLASH	0x02
#define FU_WACOM_RAW_BL_CMD_ATTACH		0x03
#define FU_WACOM_RAW_BL_CMD_GET_BLVER		0x04
#define FU_WACOM_RAW_BL_CMD_GET_MPUTYPE		0x05
#define FU_WACOM_RAW_BL_CMD_CHECK_MODE		0x07
#define FU_WACOM_RAW_BL_CMD_ERASE_DATAMEM	0x0e
#define FU_WACOM_RAW_BL_CMD_ALL_ERASE		0x90

#define FU_WACOM_RAW_RC_OK			0x00
#define FU_WACOM_RAW_RC_BUSY			0x80
#define FU_WACOM_RAW_RC_MCUTYPE			0x0c
#define FU_WACOM_RAW_RC_PID			0x0d
#define FU_WACOM_RAW_RC_CHECKSUM1		0x81
#define FU_WACOM_RAW_RC_CHECKSUM2		0x82
#define FU_WACOM_RAW_RC_TIMEOUT			0x87
#define FU_WACOM_RAW_RC_IN_PROGRESS		0xff

#define FU_WACOM_RAW_ECHO_DEFAULT		0xee

#define REQ_REPORT				0
#define REQ_CMD					1
#define REQ_ECH					2

#define RTRN_REPORT				0
#define RTRN_CMD				1
#define RTRN_ECH				2
#define RTRN_RESP				3
#define FU_WACOM_RAW_BL_RESPONSE_SZ		6

gboolean	 fu_wacom_common_rc_set_error	(guint8		 rc,
						 GError		**error);
gboolean	 fu_wacom_common_check_reply	(const guint8	*cmd,
						 const guint8	*rsp,
						 GError		**error);
gboolean	 fu_wacom_common_block_is_empty	(const guint8	*data,
						 guint16	 datasz);

G_END_DECLS

#endif /* __FU_WACOM_COMMON_H */
