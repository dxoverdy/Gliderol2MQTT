/*
Name:		BootReport.cpp
Author:		Daniel Young

This file is part of Gliderol2MQTT (G2M) which is released under GNU GENERAL PUBLIC LICENSE.
See file LICENSE or go to https://choosealicense.com/licenses/gpl-3.0/ for full license details.

See BootReport.h for usage.
*/

#include "BootReport.h"
#include "Definitions.h"
#include "Log.h"

#include <Arduino.h>
#include <Preferences.h>
#include <esp_system.h>
#include <time.h>

namespace bootreport
{
	namespace
	{
		const char* _resetReason = "unknown";
		unsigned long _bootCount = 0;

		// Any time before this (2024-01-01) means NTP hasn't synced yet and the clock is still counting from 1970.
		const time_t VALID_TIME_THRESHOLD = 1704067200;

		const char* describe(esp_reset_reason_t reason)
		{
			switch (reason)
			{
			case ESP_RST_POWERON:	return "power_on";		// Power applied
			case ESP_RST_EXT:		return "external";		// EN/reset pin
			case ESP_RST_SW:		return "software";		// esp_restart() - includes the reboot after an OTA update
			case ESP_RST_PANIC:		return "crash";			// Exception/panic
			case ESP_RST_INT_WDT:	return "watchdog";		// Interrupt watchdog
			case ESP_RST_TASK_WDT:	return "watchdog";		// Task watchdog
			case ESP_RST_WDT:		return "watchdog";		// Other watchdog
			case ESP_RST_DEEPSLEEP:	return "deep_sleep";
			case ESP_RST_BROWNOUT:	return "brownout";		// Supply voltage dipped too low
			case ESP_RST_SDIO:		return "sdio";
			default:				return "unknown";
			}
		}
	}

	void begin()
	{
		_resetReason = describe(esp_reset_reason());

		// One small NVS write per boot - negligible flash wear.
		Preferences preferences;
		if (preferences.begin("g2m", false))
		{
			_bootCount = preferences.getULong("boots", 0) + 1;
			preferences.putULong("boots", _bootCount);
			preferences.end();
		}

		LOG("Boot #%lu, reset reason: %s\r\n", _bootCount, _resetReason);
	}

	void startClock()
	{
		// UTC, no daylight saving.  Home automation tools convert to local time for display.
		configTime(0, 0, "pool.ntp.org", "time.google.com");
	}

	const char* resetReason()
	{
		return _resetReason;
	}

	unsigned long bootCount()
	{
		return _bootCount;
	}

	bool toJson(char* buffer, size_t length, const char* version, const char* ip, const char* mac, int rssi)
	{
		char timeText[32] = "null";
		time_t now = time(nullptr);
		if (now > VALID_TIME_THRESHOLD)
		{
			struct tm utc;
			gmtime_r(&now, &utc);
			strftime(timeText, sizeof(timeText), "\"%Y-%m-%dT%H:%M:%SZ\"", &utc);
		}

		// MD5 of the running firmware image - changes with every build, so it confirms exactly which firmware is
		// running (e.g. that an OTA update really landed).  The ESP32 core computes it once and caches it.
		// (__DATE__/__TIME__ would be misleading: they only change when this particular file is recompiled.)
		String firmwareMd5 = ESP.getSketchMD5();

		int written = snprintf(buffer, length,
			"{\"time\": %s, \"reset_reason\": \"%s\", \"boot_count\": %lu, \"online_after_ms\": %lu, "
			"\"version\": \"%s\", \"firmware_md5\": \"%s\", \"ip\": \"%s\", \"mac\": \"%s\", \"rssi\": %d}",
			timeText, _resetReason, _bootCount, millis(),
			version, firmwareMd5.c_str(), ip, mac, rssi);

		return written > 0 && (size_t)written < length;
	}
}
