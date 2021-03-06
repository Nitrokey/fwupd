Wacom USB Support
=================

Introduction
------------

Wacom provides interactive pen displays, pen tablets, and styluses to equip and
inspire everyone make the world a more creative place.

From 2016 Wacom has been using a HID-based proprietary flashing algorithm which
has been documented by support team at Wacom and provided under NDA under the
understanding it would be used to build a plugin under a LGPLv2+ licence.

Wacom devices are actually composite devices, with the main ARM CPU being
programmed using a more complicated erase, write, verify algorithm based
on a historical update protocol. The "sub-module" devices use a newer protocol,
again based on HID, but are handled differently depending on thier type.

Firmware Format
---------------

The daemon will decompress the cabinet archive and extract a firmware blob in
SREC file format, with a custom vendor header.

GUID Generation
---------------

These devices use the standard USB DeviceInstanceId values, e.g.

 * `USB\VID_056A&PID_0378&REV_0001`
 * `USB\VID_056A&PID_0378`
 * `USB\VID_056A`
