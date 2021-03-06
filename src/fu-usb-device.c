/*
 * Copyright (C) 2017-2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN				"FuUsbDevice"

#include "config.h"

#include "fu-usb-device-private.h"

/**
 * SECTION:fu-device
 * @short_description: a USB device
 *
 * An object that represents a USB device.
 *
 * See also: #FuDevice
 */

typedef struct
{
	GUsbDevice		*usb_device;
	FuDeviceLocker		*usb_device_locker;
} FuUsbDevicePrivate;

G_DEFINE_TYPE_WITH_PRIVATE (FuUsbDevice, fu_usb_device, FU_TYPE_DEVICE)
enum {
	PROP_0,
	PROP_USB_DEVICE,
	PROP_LAST
};

#define GET_PRIVATE(o) (fu_usb_device_get_instance_private (o))

static void
fu_usb_device_get_property (GObject *object, guint prop_id,
			    GValue *value, GParamSpec *pspec)
{
	FuUsbDevice *device = FU_USB_DEVICE (object);
	FuUsbDevicePrivate *priv = GET_PRIVATE (device);
	switch (prop_id) {
	case PROP_USB_DEVICE:
		g_value_set_object (value, priv->usb_device);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
fu_usb_device_set_property (GObject *object, guint prop_id,
			    const GValue *value, GParamSpec *pspec)
{
	FuUsbDevice *device = FU_USB_DEVICE (object);
	switch (prop_id) {
	case PROP_USB_DEVICE:
		fu_usb_device_set_dev (device, g_value_get_object (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
fu_usb_device_finalize (GObject *object)
{
	FuUsbDevice *device = FU_USB_DEVICE (object);
	FuUsbDevicePrivate *priv = GET_PRIVATE (device);

	if (priv->usb_device_locker != NULL)
		g_object_unref (priv->usb_device_locker);
	if (priv->usb_device != NULL)
		g_object_unref (priv->usb_device);

	G_OBJECT_CLASS (fu_usb_device_parent_class)->finalize (object);
}

static void
fu_usb_device_init (FuUsbDevice *device)
{
}

/**
 * fu_usb_device_is_open:
 * @device: A #FuUsbDevice
 *
 * Finds out if a USB device is currently open.
 *
 * Returns: %TRUE if the device is open.
 *
 * Since: 1.0.3
 **/
gboolean
fu_usb_device_is_open (FuUsbDevice *device)
{
	FuUsbDevicePrivate *priv = GET_PRIVATE (device);
	g_return_val_if_fail (FU_IS_USB_DEVICE (device), FALSE);
	return priv->usb_device_locker != NULL;
}

static gboolean
fu_usb_device_open (FuDevice *device, GError **error)
{
	FuUsbDevice *self = FU_USB_DEVICE (device);
	FuUsbDevicePrivate *priv = GET_PRIVATE (self);
	FuUsbDeviceClass *klass = FU_USB_DEVICE_GET_CLASS (device);
	guint idx;
	g_autoptr(FuDeviceLocker) locker = NULL;

	g_return_val_if_fail (FU_IS_USB_DEVICE (self), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* already open */
	if (priv->usb_device_locker != NULL)
		return TRUE;

	/* open */
	locker = fu_device_locker_new (priv->usb_device, error);
	if (locker == NULL)
		return FALSE;

	/* get vendor */
	if (fu_device_get_vendor (device) == NULL) {
		idx = g_usb_device_get_manufacturer_index (priv->usb_device);
		if (idx != 0x00) {
			g_autofree gchar *tmp = NULL;
			tmp = g_usb_device_get_string_descriptor (priv->usb_device,
								  idx, error);
			if (tmp == NULL)
				return FALSE;
			fu_device_set_vendor (device, tmp);
		}
	}

	/* get product */
	if (fu_device_get_name (device) == NULL) {
		idx = g_usb_device_get_product_index (priv->usb_device);
		if (idx != 0x00) {
			g_autofree gchar *tmp = NULL;
			tmp = g_usb_device_get_string_descriptor (priv->usb_device,
								  idx, error);
			if (tmp == NULL)
				return FALSE;
			fu_device_set_name (device, tmp);
		}
	}

	/* get serial number */
	if (fu_device_get_serial (device) == NULL) {
		idx = g_usb_device_get_serial_number_index (priv->usb_device);
		if (idx != 0x00) {
			g_autofree gchar *tmp = NULL;
			tmp = g_usb_device_get_string_descriptor (priv->usb_device,
								  idx, error);
			if (tmp == NULL)
				return FALSE;
			fu_device_set_serial (device, tmp);
		}
	}

	/* get version number, falling back to the USB device release */
	idx = g_usb_device_get_custom_index (priv->usb_device,
					     G_USB_DEVICE_CLASS_VENDOR_SPECIFIC,
					     'F', 'W', NULL);
	if (idx != 0x00) {
		g_autofree gchar *tmp = NULL;
		tmp = g_usb_device_get_string_descriptor (priv->usb_device, idx, NULL);
		fu_device_set_version (device, tmp);
	}

	/* get GUID from the descriptor if set */
	idx = g_usb_device_get_custom_index (priv->usb_device,
					     G_USB_DEVICE_CLASS_VENDOR_SPECIFIC,
					     'G', 'U', NULL);
	if (idx != 0x00) {
		g_autofree gchar *tmp = NULL;
		tmp = g_usb_device_get_string_descriptor (priv->usb_device, idx, NULL);
		fu_device_add_guid (device, tmp);
	}

	/* subclassed */
	if (klass->open != NULL) {
		if (!klass->open (self, error))
			return FALSE;
	}

	/* success */
	priv->usb_device_locker = g_steal_pointer (&locker);
	return TRUE;
}

static gboolean
fu_usb_device_close (FuDevice *device, GError **error)
{
	FuUsbDevice *self = FU_USB_DEVICE (device);
	FuUsbDevicePrivate *priv = GET_PRIVATE (self);
	FuUsbDeviceClass *klass = FU_USB_DEVICE_GET_CLASS (device);

	g_return_val_if_fail (FU_IS_USB_DEVICE (self), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* already open */
	if (priv->usb_device_locker == NULL)
		return TRUE;

	/* subclassed */
	if (klass->close != NULL) {
		if (!klass->close (self, error))
			return FALSE;
	}

	g_clear_object (&priv->usb_device_locker);
	return TRUE;
}

static gboolean
fu_usb_device_probe (FuDevice *device, GError **error)
{
	FuUsbDevice *self = FU_USB_DEVICE (device);
	FuUsbDeviceClass *klass = FU_USB_DEVICE_GET_CLASS (device);
	FuUsbDevicePrivate *priv = GET_PRIVATE (self);
	guint16 release;
	g_autofree gchar *devid0 = NULL;
	g_autofree gchar *devid1 = NULL;
	g_autofree gchar *devid2 = NULL;
	g_autofree gchar *vendor_id = NULL;
	g_autoptr(GPtrArray) intfs = NULL;

	/* set vendor ID */
	vendor_id = g_strdup_printf ("USB:0x%04X", g_usb_device_get_vid (priv->usb_device));
	fu_device_set_vendor_id (device, vendor_id);

	/* set the version if the release has been set */
	release = g_usb_device_get_release (priv->usb_device);
	if (release != 0x0) {
		g_autofree gchar *version = NULL;
		version = fu_common_version_from_uint16 (release, FU_VERSION_FORMAT_BCD);
		fu_device_set_version (device, version);
	}

	/* add GUIDs in order of priority */
	devid2 = g_strdup_printf ("USB\\VID_%04X&PID_%04X&REV_%04X",
				  g_usb_device_get_vid (priv->usb_device),
				  g_usb_device_get_pid (priv->usb_device),
				  release);
	fu_device_add_guid (device, devid2);
	devid1 = g_strdup_printf ("USB\\VID_%04X&PID_%04X",
				  g_usb_device_get_vid (priv->usb_device),
				  g_usb_device_get_pid (priv->usb_device));
	fu_device_add_guid (device, devid1);
	devid0 = g_strdup_printf ("USB\\VID_%04X",
				  g_usb_device_get_vid (priv->usb_device));
	fu_device_add_guid (device, devid0);

	/* add the interface GUIDs */
	intfs = g_usb_device_get_interfaces (priv->usb_device, error);
	if (intfs == NULL)
		return FALSE;
	for (guint i = 0; i < intfs->len; i++) {
		GUsbInterface *intf = g_ptr_array_index (intfs, i);
		g_autofree gchar *intid1 = NULL;
		g_autofree gchar *intid2 = NULL;
		g_autofree gchar *intid3 = NULL;
		intid1 = g_strdup_printf ("USB\\CLASS_%02X&SUBCLASS_%02X&PROT_%02X",
					  g_usb_interface_get_class (intf),
					  g_usb_interface_get_subclass (intf),
					  g_usb_interface_get_protocol (intf));
		fu_device_add_guid (device, intid1);
		intid2 = g_strdup_printf ("USB\\CLASS_%02X&SUBCLASS_%02X",
					  g_usb_interface_get_class (intf),
					  g_usb_interface_get_subclass (intf));
		fu_device_add_guid (device, intid2);
		intid3 = g_strdup_printf ("USB\\CLASS_%02X",
					  g_usb_interface_get_class (intf));
		fu_device_add_guid (device, intid3);
	}

	/* subclassed */
	if (klass->probe != NULL) {
		if (!klass->probe (self, error))
			return FALSE;
	}

	/* success */
	return TRUE;
}

/**
 * fu_usb_device_get_vid:
 * @self: A #FuUsbDevice
 *
 * Gets the device vendor code.
 *
 * Returns: integer, or 0x0 if unset or invalid
 *
 * Since: 1.1.2
 **/
guint16
fu_usb_device_get_vid (FuUsbDevice *self)
{
	FuUsbDevicePrivate *priv = GET_PRIVATE (self);
	g_return_val_if_fail (FU_IS_USB_DEVICE (self), 0x0000);
	return g_usb_device_get_vid (priv->usb_device);
}

/**
 * fu_usb_device_get_pid:
 * @self: A #FuUsbDevice
 *
 * Gets the device product code.
 *
 * Returns: integer, or 0x0 if unset or invalid
 *
 * Since: 1.1.2
 **/
guint16
fu_usb_device_get_pid (FuUsbDevice *self)
{
	FuUsbDevicePrivate *priv = GET_PRIVATE (self);
	g_return_val_if_fail (FU_IS_USB_DEVICE (self), 0x0000);
	return g_usb_device_get_pid (priv->usb_device);
}

/**
 * fu_usb_device_get_platform_id:
 * @self: A #FuUsbDevice
 *
 * Gets the device platform ID.
 *
 * Returns: string, or NULL if unset or invalid
 *
 * Since: 1.1.2
 **/
const gchar *
fu_usb_device_get_platform_id (FuUsbDevice *self)
{
	FuUsbDevicePrivate *priv = GET_PRIVATE (self);
	g_return_val_if_fail (FU_IS_USB_DEVICE (self), NULL);
	return g_usb_device_get_platform_id (priv->usb_device);
}

/**
 * fu_usb_device_set_dev:
 * @device: A #FuUsbDevice
 * @usb_device: A #GUsbDevice, or %NULL
 *
 * Sets the #GUsbDevice to use.
 *
 * Since: 1.0.2
 **/
void
fu_usb_device_set_dev (FuUsbDevice *device, GUsbDevice *usb_device)
{
	FuUsbDevicePrivate *priv = GET_PRIVATE (device);

	g_return_if_fail (FU_IS_USB_DEVICE (device));

	/* need to re-probe hardware */
	fu_device_probe_invalidate (FU_DEVICE (device));

	/* allow replacement */
	g_set_object (&priv->usb_device, usb_device);
	if (usb_device == NULL) {
		g_clear_object (&priv->usb_device_locker);
		return;
	}

	/* set device ID automatically */
	fu_device_set_physical_id (FU_DEVICE (device),
				   g_usb_device_get_platform_id (usb_device));
}

/**
 * fu_usb_device_get_dev:
 * @device: A #FuUsbDevice
 *
 * Gets the #GUsbDevice.
 *
 * Returns: (transfer none): a #GUsbDevice, or %NULL
 *
 * Since: 1.0.2
 **/
GUsbDevice *
fu_usb_device_get_dev (FuUsbDevice *device)
{
	FuUsbDevicePrivate *priv = GET_PRIVATE (device);
	g_return_val_if_fail (FU_IS_USB_DEVICE (device), NULL);
	return priv->usb_device;
}

static void
fu_usb_device_incorporate (FuDevice *self, FuDevice *donor)
{
	g_return_if_fail (FU_IS_USB_DEVICE (self));
	g_return_if_fail (FU_IS_USB_DEVICE (donor));
	fu_usb_device_set_dev (FU_USB_DEVICE (self),
			       fu_usb_device_get_dev (FU_USB_DEVICE (donor)));
}

/**
 * fu_usb_device_new:
 * @usb_device: A #GUsbDevice
 *
 * Creates a new #FuUsbDevice.
 *
 * Returns: (transfer full): a #FuUsbDevice
 *
 * Since: 1.0.2
 **/
FuUsbDevice *
fu_usb_device_new (GUsbDevice *usb_device)
{
	FuUsbDevice *device = g_object_new (FU_TYPE_USB_DEVICE, NULL);
	fu_usb_device_set_dev (device, usb_device);
	return FU_USB_DEVICE (device);
}

static void
fu_usb_device_class_init (FuUsbDeviceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	FuDeviceClass *device_class = FU_DEVICE_CLASS (klass);
	GParamSpec *pspec;

	object_class->finalize = fu_usb_device_finalize;
	object_class->get_property = fu_usb_device_get_property;
	object_class->set_property = fu_usb_device_set_property;
	device_class->open = fu_usb_device_open;
	device_class->close = fu_usb_device_close;
	device_class->probe = fu_usb_device_probe;
	device_class->incorporate = fu_usb_device_incorporate;

	pspec = g_param_spec_object ("usb-device", NULL, NULL,
				     G_USB_TYPE_DEVICE,
				     G_PARAM_READWRITE |
				     G_PARAM_CONSTRUCT |
				     G_PARAM_STATIC_NAME);
	g_object_class_install_property (object_class, PROP_USB_DEVICE, pspec);
}
