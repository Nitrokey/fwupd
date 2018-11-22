/*
 * Copyright (C) 2018-2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <string.h>
#include <gio/gio.h>

#include "fu-chunk.h"
#include "fu-wacom-common.h"
#include "fu-wacom-aes-device.h"

struct _FuWacomAesDevice {
	FuWacomDevice		 parent_instance;
	guint32			 hwid;
};

G_DEFINE_TYPE (FuWacomAesDevice, fu_wacom_aes_device, FU_TYPE_WACOM_DEVICE)

static gboolean
fu_wacom_aes_device_obtain_hwid (FuWacomAesDevice *self, GError **error)
{
	guint8 cmd[FU_WACOM_DEVICE_FW_MAINTAIN_REPORT_SZ] = { 0x0 };
	guint8 buf[FU_WACOM_DEVICE_FW_MAINTAIN_REPORT_SZ] = { 0x0 };

	cmd[0] = FU_WACOM_DEVICE_FW_MAINTAIN_REPORT_ID;
	cmd[1] = 0x01; /* ?? */
	cmd[2] = 0x01; /* ?? */
	cmd[3] = 0x0f; /* ?? */

	if (!fu_wacom_device_set_feature (FU_WACOM_DEVICE (self),
					  cmd, sizeof(cmd), error)) {
		g_prefix_error (error, "failed to send: ");
		return FALSE;
	}
	buf[0] = FU_WACOM_DEVICE_FW_MAINTAIN_REPORT_ID;
	if (!fu_wacom_device_get_feature (FU_WACOM_DEVICE (self),
					  buf, sizeof(buf), error)) {
		g_prefix_error (error, "failed to receive: ");
		return FALSE;
	}
	if (buf[1] == 0xff) {
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_NOT_SUPPORTED,
				     "firmware does not support this feature");
		return FALSE;
	}

	/* check magic number */
	if (memcmp (buf, "\x34\x12\x78\x56\x65\x87\x21\x43", 8) != 0) {
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_NOT_SUPPORTED,
				     "incorrect magic number");
		return FALSE;
	}

	/* format the value */
	self->hwid = ((guint32) buf[9]) << 24 |
		     ((guint32) buf[8]) << 16 |
		     ((guint32) buf[11]) << 8 |
		     ((guint32) buf[10]);
	return TRUE;

}

static gboolean
fu_wacom_aes_device_setup (FuDevice *device, GError **error)
{
	FuWacomAesDevice *self = FU_WACOM_AES_DEVICE (device);

	/* get firmware version */
	if (fu_device_has_flag (device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER)) {
		fu_device_set_version (device, "0.0");
	} else {
		guint32 fw_ver;
		guint8 data[FU_WACOM_DEVICE_STATUS_REPORT_SZ] = {
			FU_WACOM_DEVICE_STATUS_REPORT_ID,
			0x0
		};
		g_autofree gchar *version = NULL;
		g_autoptr(GError) error_local = NULL;

		if (!fu_wacom_device_get_feature (FU_WACOM_DEVICE (self),
						  data, sizeof(data), error))
			return FALSE;
		fw_ver = fu_common_read_uint16 (data + 11, G_LITTLE_ENDIAN);
		version = g_strdup_printf ("%04x.%02x", fw_ver, data[13]);
		fu_device_set_version (device, version);

		/* get the optional 32 byte HWID and add it as a GUID */
		if (!fu_wacom_aes_device_obtain_hwid (self, &error_local)) {
			g_debug ("failed to get HwID: %s", error_local->message);
		} else {
			g_autofree gchar *guid = NULL;
			guid = g_strdup_printf ("WACOM\\HWID_%04X", self->hwid);
			fu_device_add_guid (device, guid);
		}
	}

	/* success */
	return TRUE;
}

static gboolean
fu_wacom_aes_device_erase_all (FuWacomAesDevice *self, GError **error)
{
	guint8 rsp[RSP_SIZE];
	guint8 cmd[] = {
		FU_WACOM_DEVICE_BL_REPORT_ID_SET,
		FU_WACOM_DEVICE_BL_CMD_ALL_ERASE,
		0x01,				/* echo */
		0x00,				/* blkNo */
	};
	if (!fu_wacom_device_cmd (FU_WACOM_DEVICE (self),
				  cmd, sizeof(cmd), rsp, sizeof(rsp),
				  2000 * 1000, /* this takes a long time */
				  FU_WACOM_DEVICE_CMD_FLAG_POLL_ON_WAITING, error)) {
		g_prefix_error (error, "failed to send eraseall command: ");
		return FALSE;
	}
	if (!fu_wacom_common_rc_set_error (rsp[RTRN_RSP], error)) {
		g_prefix_error (error, "failed to erase");
		return FALSE;
	}
	g_usleep (2 * G_USEC_PER_SEC);
	return TRUE;
}

static gboolean
fu_wacom_aes_device_write_block (FuWacomAesDevice *self,
				 guint32 idx,
				 guint32 address,
				 const guint8 *data,
				 guint16 datasz,
				 GError **error)
{
	guint blocksz = fu_wacom_device_get_block_sz (FU_WACOM_DEVICE (self));
	guint8 rsp[RSP_SIZE];
	g_autofree guint8 *cmd = g_malloc0 (blocksz + 8);
	cmd[0] = FU_WACOM_DEVICE_BL_REPORT_ID_SET;
	cmd[1] = FU_WACOM_DEVICE_BL_CMD_WRITE_FLASH;
	cmd[2] = (guint8) idx;	/* echo */

	/* address */
	address += fu_wacom_device_get_base_addr (FU_WACOM_DEVICE (self));
	fu_common_write_uint32 (cmd + 3, address, G_LITTLE_ENDIAN);

	/* size */
	if (datasz != blocksz) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_FAILED,
			     "block size 0x%x != 0x%x untested",
			     datasz, (guint) blocksz);
		return FALSE;
	}
	cmd[7] = datasz / 8;

	/* data */
	memcpy (cmd + 8, data, datasz);

	/* write */
	if (!fu_wacom_device_cmd (FU_WACOM_DEVICE (self),
				  cmd, sizeof(cmd), rsp, sizeof(rsp), 1000,
				  FU_WACOM_DEVICE_CMD_FLAG_NONE, error)) {
		g_prefix_error (error, "failed to write block %u: ", idx);
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_wacom_aes_device_write_firmware (FuDevice *device, GPtrArray *chunks, GError **error)
{
	FuWacomAesDevice *self = FU_WACOM_AES_DEVICE (device);

	/* erase */
	fu_device_set_status (device, FWUPD_STATUS_DEVICE_ERASE);
	if (!fu_wacom_aes_device_erase_all (self, error))
		return FALSE;

	/* write */
	fu_device_set_status (device, FWUPD_STATUS_DEVICE_WRITE);
	for (guint i = 0; i < chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index (chunks, i);
		if (!fu_wacom_aes_device_write_block (self,
						      chk->idx,
						      chk->address,
						      chk->data,
						      chk->data_sz,
						      error))
			return FALSE;
		fu_device_set_progress_full (device, (gsize) i, (gsize) chunks->len);
	}
	return TRUE;
}

static void
fu_wacom_aes_device_init (FuWacomAesDevice *self)
{
	fu_device_set_name (FU_DEVICE (self), "Embedded Wacom AES Device");
}

static void
fu_wacom_aes_device_class_init (FuWacomAesDeviceClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS (klass);
	FuWacomDeviceClass *klass_wac_device = FU_WACOM_DEVICE_CLASS (klass);
	klass_device->setup = fu_wacom_aes_device_setup;
	klass_wac_device->write_firmware = fu_wacom_aes_device_write_firmware;
}

FuWacomAesDevice *
fu_wacom_aes_device_new (FuUdevDevice *device)
{
	FuWacomAesDevice *self = g_object_new (FU_TYPE_WACOM_AES_DEVICE, NULL);
	fu_device_incorporate (FU_DEVICE (self), FU_DEVICE (device));
	return self;
}
