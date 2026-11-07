/*
Name:		BootReport.h
Author:		Daniel Young

This file is part of Gliderol2MQTT (G2M) which is released under GNU GENERAL PUBLIC LICENSE.
See file LICENSE or go to https://choosealicense.com/licenses/gpl-3.0/ for full license details.

Information about this boot, for publishing once per boot so you can tell remotely when, why and with which
firmware the device last started.

Usage:
	bootreport::begin();          // very early in setup() - records the reset reason and bumps the boot counter
	bootreport::startClock();     // once WiFi is up - starts NTP so the report can carry a real timestamp
	bootreport::toJson(buf, sizeof(buf), version, ip, mac);
*/

#pragma once

#include <stddef.h>

namespace bootreport
{
	// Capture the reset reason and increment the persistent boot counter (stored in NVS flash).
	void begin();

	// Start NTP time sync.  Non-blocking; the time is usually available a second or two later.
	void startClock();

	// Short, stable description of why the chip last reset, e.g. "power_on", "software", "brownout".
	const char* resetReason();

	// Number of boots since the counter was first created (survives reboots and reflashing).
	unsigned long bootCount();

	// Write the report as a JSON object.  Returns false if it didn't fit.
	bool toJson(char* buffer, size_t length, const char* version, const char* ip, const char* mac);
}
