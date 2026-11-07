/*
Name:		Watchdog.h
Author:		Daniel Young

This file is part of Gliderol2MQTT (G2M) which is released under GNU GENERAL PUBLIC LICENSE.
See file LICENSE or go to https://choosealicense.com/licenses/gpl-3.0/ for full license details.

Hang protection using the ESP32's task watchdog: if feed() isn't called for WATCHDOG_TIMEOUT_S, the chip resets.
Call feed() every loop and from anything that legitimately takes a while (long waits, OTA progress).
*/

#pragma once

#include <esp_task_wdt.h>

namespace watchdog
{
	// Start watching the calling task (call from setup()).
	inline void begin(uint32_t timeoutSeconds)
	{
		// The ESP32 core has already initialised the task watchdog with a short timeout; this reconfigures it, and
		// makes a timeout reset the chip (panic = true) rather than just log a warning.
		esp_task_wdt_init(timeoutSeconds, true);
		esp_task_wdt_add(nullptr);
	}

	// "Still alive."
	inline void feed()
	{
		esp_task_wdt_reset();
	}
}
