/*
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2019 Aleksander Morgado <aleksander@aleksander.es>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <string.h>

#include "fu-io-channel.h"
#include "fu-archive.h"
#include "fu-mm-device.h"
#include "fu-qmi-pdc-updater.h"

struct _FuMmDevice {
	FuDevice			 parent_instance;
	FuIOChannel			*io_channel;
	MMManager			*manager;
	MMObject			*omodem;
	MMModemFirmwareUpdateMethod	 update_method;
	gchar				*detach_fastboot_at;
	gchar				*detach_port_at;
	gchar				*port_qmi;
	FuQmiPdcUpdater			*qmi_pdc_updater;
	gchar				*inhibition_uid;
};

G_DEFINE_TYPE (FuMmDevice, fu_mm_device, FU_TYPE_DEVICE)

static void
fu_mm_device_to_string (FuDevice *device, GString *str)
{
	FuMmDevice *self = FU_MM_DEVICE (device);
	g_string_append (str, "  FuMmDevice:\n");
	g_string_append_printf (str, "    path:\t\t\t%s\n",
				mm_object_get_path (self->omodem));
	if (self->update_method != MM_MODEM_FIRMWARE_UPDATE_METHOD_NONE) {
		g_autofree gchar *tmp = NULL;
		tmp = mm_modem_firmware_update_method_build_string_from_mask (self->update_method);
		g_string_append_printf (str, "    detach-kind:\t\t%s\n", tmp);
	}
	if (self->detach_port_at != NULL) {
		g_string_append_printf (str, "    at-port:\t\t\t%s\n",
					self->detach_port_at);
	}
	if (self->port_qmi != NULL) {
		g_string_append_printf (str, "    qmi-port:\t\t\t%s\n",
					self->port_qmi);
	}
}

static gboolean
fu_mm_device_probe (FuDevice *device, GError **error)
{
	FuMmDevice *self = FU_MM_DEVICE (device);
	MMModemFirmware *modem_fw;
	MMModem *modem = mm_object_peek_modem (self->omodem);
	MMModemPortInfo *ports = NULL;
	const gchar **device_ids;
	const gchar *version;
	guint n_ports = 0;
	g_autoptr(MMFirmwareUpdateSettings) update_settings = NULL;

	/* find out what detach method we should use */
	modem_fw = mm_object_peek_modem_firmware (self->omodem);
	update_settings = mm_modem_firmware_get_update_settings (modem_fw);
	self->update_method = mm_firmware_update_settings_get_method (update_settings);
	if (self->update_method == MM_MODEM_FIRMWARE_UPDATE_METHOD_NONE) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "modem cannot be put in programming mode");
		return FALSE;
	}

	/* various fastboot commands
	 * qmi-pdc is not expected to be supported alone by itself (for now) */
	if (self->update_method & MM_MODEM_FIRMWARE_UPDATE_METHOD_FASTBOOT) {
		const gchar *tmp;
		tmp = mm_firmware_update_settings_get_fastboot_at (update_settings);
		if (tmp == NULL) {
			g_set_error_literal (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_NOT_SUPPORTED,
					     "modem does not set fastboot command");
			return FALSE;
		}
		self->detach_fastboot_at = g_strdup (tmp);
	} else {
		g_autofree gchar *str = NULL;
		str = mm_modem_firmware_update_method_build_string_from_mask (self->update_method);
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "modem detach method %s not supported", str);
		return FALSE;
	}

	/* get GUIDs */
	device_ids = mm_firmware_update_settings_get_device_ids (update_settings);
	if (device_ids == NULL || device_ids[0] == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "modem did not specify any device IDs");
		return FALSE;
	}

	/* get version string, which is fw_ver+config_ver */
	version = mm_firmware_update_settings_get_version (update_settings);
	if (version == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "modem did not specify a firmware version");
		return FALSE;
	}

	/* add properties to fwupd device */
	fu_device_set_physical_id (device, mm_modem_get_device (modem));
	fu_device_set_vendor (device, mm_modem_get_manufacturer (modem));
	fu_device_set_name (device, mm_modem_get_model (modem));
	fu_device_set_version (device, version);
	for (guint i = 0; device_ids[i] != NULL; i++)
		fu_device_add_guid (device, device_ids[i]);

	/* look for the AT and QMI/MBIM ports */
	if (!mm_modem_get_ports (modem, &ports, &n_ports)) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "failed to get port information");
		return FALSE;
	}
	for (guint i = 0; i < n_ports; i++) {
		if (ports[i].type == MM_MODEM_PORT_TYPE_AT) {
			self->detach_port_at = g_strdup_printf ("/dev/%s", ports[i].name);
			break;
		}
	}
	if (self->update_method & MM_MODEM_FIRMWARE_UPDATE_METHOD_QMI_PDC) {
		for (guint i = 0; i < n_ports; i++) {
			if ((ports[i].type == MM_MODEM_PORT_TYPE_QMI) ||
			    (ports[i].type == MM_MODEM_PORT_TYPE_MBIM)) {
				self->port_qmi = g_strdup_printf ("/dev/%s", ports[i].name);
				break;
			}
		}
	}
	mm_modem_port_info_array_free (ports, n_ports);

	/* this is required for detaching */
	if (self->detach_port_at == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "failed to find AT port");
		return FALSE;
	}

	/* a qmi port is required for qmi-pdc */
	if ((self->update_method & MM_MODEM_FIRMWARE_UPDATE_METHOD_QMI_PDC) &&
	    (self->port_qmi == NULL)) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "failed to find QMI port");
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_mm_device_at_cmd (FuMmDevice *self, const gchar *cmd, GError **error)
{
	const gchar *buf;
	gsize bufsz = 0;
	g_autoptr(GBytes) at_req  = NULL;
	g_autoptr(GBytes) at_res  = NULL;
	g_autofree gchar *cmd_cr = g_strdup_printf ("%s\r\n", cmd);

	/* command */
	at_req = g_bytes_new (cmd_cr, strlen (cmd_cr));
	if (g_getenv ("FWUPD_MODEM_MANAGER_VERBOSE") != NULL)
		fu_common_dump_bytes (G_LOG_DOMAIN, "writing", at_req);
	if (!fu_io_channel_write_bytes (self->io_channel, at_req, 1500,
					FU_IO_CHANNEL_FLAG_FLUSH_INPUT, error)) {
		g_prefix_error (error, "failed to write %s: ", cmd);
		return FALSE;
	}

	/* response */
	at_res = fu_io_channel_read_bytes (self->io_channel, -1, 1500,
					   FU_IO_CHANNEL_FLAG_SINGLE_SHOT, error);
	if (at_res == NULL) {
		g_prefix_error (error, "failed to read response for %s: ", cmd);
		return FALSE;
	}
	if (g_getenv ("FWUPD_MODEM_MANAGER_VERBOSE") != NULL)
		fu_common_dump_bytes (G_LOG_DOMAIN, "read", at_res);
	buf = g_bytes_get_data (at_res, &bufsz);
	if (bufsz < 6) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "failed to read valid response for %s", cmd);
		return FALSE;
	}
	if (memcmp (buf, "\r\nOK\r\n", 6) != 0) {
		g_autofree gchar *tmp = g_strndup (buf + 2, bufsz - 4);
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "failed to read valid response for %s: %s",
			     cmd, tmp);
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_mm_device_io_open (FuMmDevice *self, GError **error)
{
	/* open device */
	self->io_channel = fu_io_channel_new_file (self->detach_port_at, error);
	if (self->io_channel == NULL)
		return FALSE;

	/* success */
	return TRUE;
}

static gboolean
fu_mm_device_io_close (FuMmDevice *self, GError **error)
{
	if (!fu_io_channel_shutdown (self->io_channel, error))
		return FALSE;
	g_clear_object (&self->io_channel);
	return TRUE;
}

static gboolean
fu_mm_device_detach_fastboot (FuDevice *device, GError **error)
{
	FuMmDevice *self = FU_MM_DEVICE (device);
	g_autoptr(FuDeviceLocker) locker  = NULL;

	/* boot to fastboot mode */
	locker = fu_device_locker_new_full (device,
					    (FuDeviceLockerFunc) fu_mm_device_io_open,
					    (FuDeviceLockerFunc) fu_mm_device_io_close,
					    error);
	if (locker == NULL)
		return FALSE;
	if (!fu_mm_device_at_cmd (self, "AT", error))
		return FALSE;
	if (!fu_mm_device_at_cmd (self, self->detach_fastboot_at, error)) {
		g_prefix_error (error, "rebooting into fastboot not supported: ");
		return FALSE;
	}

	/* success */
	fu_device_set_remove_delay (device, FU_DEVICE_REMOVE_DELAY_RE_ENUMERATE);
	fu_device_add_flag (device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	return TRUE;
}

static gboolean
fu_mm_device_inhibit (FuMmDevice *self, GError **error)
{
	MMModem *modem = mm_object_peek_modem (self->omodem);

	/* cache inhibition uid to be used when uninhibiting */
	self->inhibition_uid = mm_modem_dup_device (modem);

	/* prevent NM from activating the modem */
	g_debug ("inhibit %s", self->inhibition_uid);
	if (!mm_manager_inhibit_device_sync (self->manager,
					     self->inhibition_uid,
					     NULL, error))
		return FALSE;

	/* success: the device will disappear */
	return TRUE;
}

static void
fu_mm_device_uninhibit (FuMmDevice *self)
{
	g_autoptr(GError) error = NULL;

	if (self->inhibition_uid == NULL)
		return;

	/* allow NM to activate the modem */
	g_debug ("uninhibit %s", self->inhibition_uid);
	if (!mm_manager_uninhibit_device_sync (self->manager,
					       self->inhibition_uid,
					       NULL, &error)) {
		g_warning ("failed uninhibiting %s: %s",
			   self->inhibition_uid, error->message);
	}
}

static gboolean
fu_mm_device_qmi_open (FuMmDevice *self, GError **error)
{
	self->qmi_pdc_updater = fu_qmi_pdc_updater_new (self->port_qmi);
	return fu_qmi_pdc_updater_open (self->qmi_pdc_updater, error);
}

static gboolean
fu_mm_device_qmi_close (FuMmDevice *self, GError **error)
{
	g_autoptr(FuQmiPdcUpdater) updater = g_steal_pointer (&self->qmi_pdc_updater);
	g_assert (updater != NULL);
	return fu_qmi_pdc_updater_close (updater, error);
}

typedef struct {
	FuMmDevice	*device;
	GError		*error;
} ArchiveIterateContext;

static void
fu_mm_qmi_pdc_archive_iterate_mcfg (FuArchive *archive, const gchar *filename, GBytes *bytes, gpointer user_data)
{
	ArchiveIterateContext *ctx = user_data;

	/* filenames should be named as 'mcfg.*.mbn', e.g.: mcfg.A2.018.mbn */
	if (!g_str_has_prefix (filename, "mcfg.") || !g_str_has_suffix (filename, ".mbn"))
		return;

	/* if there has already been an error for a previous file, abort */
	if (ctx->error != NULL) {
		g_warning ("Skipping mcfg file '%s': aborted due to previous error", filename);
		return;
	}

	g_debug ("Writing mcfg file '%s'", filename);
	if (!fu_qmi_pdc_updater_write (ctx->device->qmi_pdc_updater, filename, bytes, &ctx->error))
		g_warning ("Failed to write file '%s': %s", filename, ctx->error->message);
}

static gboolean
fu_mm_device_write_firmware_qmi_pdc (FuDevice *device, GBytes *fw, GError **error)
{
	g_autoptr(FuArchive) archive = NULL;
	g_autoptr(FuDeviceLocker) locker = NULL;
	ArchiveIterateContext iterate_context = {
		.device = FU_MM_DEVICE (device),
	};

	/* decompress entire archive ahead of time */
	archive = fu_archive_new (fw, FU_ARCHIVE_FLAG_IGNORE_PATH, error);
	if (archive == NULL)
		return FALSE;

	/* boot to fastboot mode */
	locker = fu_device_locker_new_full (device,
					    (FuDeviceLockerFunc) fu_mm_device_qmi_open,
					    (FuDeviceLockerFunc) fu_mm_device_qmi_close,
					    error);
	if (locker == NULL)
		return FALSE;

	/* Write all MCFG files found via QMI PDC */
	fu_archive_iterate (archive, fu_mm_qmi_pdc_archive_iterate_mcfg, &iterate_context);

	if (iterate_context.error != NULL) {
		g_propagate_error (error, iterate_context.error);
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_mm_device_write_firmware (FuDevice *device, GBytes *fw, GError **error)
{
	FuMmDevice *self = FU_MM_DEVICE (device);
	g_autoptr(FuArchive) archive = NULL;
	g_autoptr(GPtrArray) array = NULL;

	/* updating firmware in the MM plugin only supported for the QMI PDC method */
	if (self->update_method & MM_MODEM_FIRMWARE_UPDATE_METHOD_QMI_PDC)
		return fu_mm_device_write_firmware_qmi_pdc (device, fw, error);

	g_set_error_literal (error,
			     G_IO_ERROR,
			     G_IO_ERROR_NOT_SUPPORTED,
			     "unsupported update method");
	return FALSE;
}

static gboolean
fu_mm_device_detach (FuDevice *device, GError **error)
{
	FuMmDevice *self = FU_MM_DEVICE (device);
	g_autoptr(FuDeviceLocker) locker  = NULL;

	/* inhibit device */
	if (!fu_mm_device_inhibit (self, error))
		return FALSE;

	/* at this point, the modem object is no longer valid */
	g_clear_object (&self->omodem);

	/* boot to fastboot mode */
	locker = fu_device_locker_new (device, error);
	if (locker == NULL)
		return FALSE;

	/* qmi pdc doesn't require any detach */
	if (self->update_method & MM_MODEM_FIRMWARE_UPDATE_METHOD_QMI_PDC)
		return TRUE;

	/* fastboot */
	if (self->update_method & MM_MODEM_FIRMWARE_UPDATE_METHOD_FASTBOOT)
		return fu_mm_device_detach_fastboot (device, error);

	/* should not get here */
	g_set_error_literal (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "modem does not support detach");
	return FALSE;
}

static void
fu_mm_device_init (FuMmDevice *self)
{
	fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_NEEDS_REBOOT);
	fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_USE_RUNTIME_VERSION);
	fu_device_set_summary (FU_DEVICE (self), "Mobile broadband device");
	fu_device_add_icon (FU_DEVICE (self), "network-modem");
}

static void
fu_mm_device_finalize (GObject *object)
{
	FuMmDevice *self = FU_MM_DEVICE (object);
	fu_mm_device_uninhibit (self);
	g_free (self->inhibition_uid);
	g_object_unref (self->manager);
	g_clear_object (&self->omodem);
	g_free (self->detach_fastboot_at);
	g_free (self->detach_port_at);
	g_free (self->port_qmi);
	G_OBJECT_CLASS (fu_mm_device_parent_class)->finalize (object);
}

static void
fu_mm_device_class_init (FuMmDeviceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	FuDeviceClass *klass_device = FU_DEVICE_CLASS (klass);
	object_class->finalize = fu_mm_device_finalize;
	klass_device->to_string = fu_mm_device_to_string;
	klass_device->probe = fu_mm_device_probe;
	klass_device->detach = fu_mm_device_detach;
	klass_device->write_firmware = fu_mm_device_write_firmware;
}

FuMmDevice *
fu_mm_device_new (MMManager *manager, MMObject *omodem)
{
	FuMmDevice *self = g_object_new (FU_TYPE_MM_DEVICE, NULL);
	self->manager = g_object_ref (manager);
	self->omodem = g_object_ref (omodem);
	return self;
}
