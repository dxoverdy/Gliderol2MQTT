/*
Name:		HomeAssistant.h
Author:		Daniel Young

This file is part of Gliderol2MQTT (G2M) which is released under GNU GENERAL PUBLIC LICENSE.
See file LICENSE or go to https://choosealicense.com/licenses/gpl-3.0/ for full license details.

Home Assistant MQTT discovery.  Publishes retained config messages under HOME_ASSISTANT_DISCOVERY_PREFIX so that
Home Assistant creates, grouped under one device:
	- a garage door cover (open / close / stop, with Opening / Closing / Stopped states)
	- a reboot button
	- diagnostic sensors: WiFi signal, free memory, last boot time, last reset reason
They all reuse G2M's existing topics; nothing new is needed on the Home Assistant side.
*/

#pragma once

namespace homeassistant
{
	// How to publish a retained message (supplied by the MQTT interface).
	using PublishFn = bool (*)(const char* topic, const char* payload);

	// Publish all the discovery configs.  deviceName is the MQTT base topic; macHex (12 hex digits) makes the IDs unique.
	// Returns false if any publish failed.
	bool publishDiscovery(const char* deviceName, const char* version, const char* macHex, PublishFn publish);
}
