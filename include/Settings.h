/*
Name:		Settings.h
Author:		Daniel Young

This file is part of Gliderol2MQTT (G2M) which is released under GNU GENERAL PUBLIC LICENSE.
See file LICENSE or go to https://choosealicense.com/licenses/gpl-3.0/ for full license details.

User-configurable settings: compiled-in defaults (Definitions.h + private.ini), optionally overridden by a
JSON file called settings.txt in the root of a Micro-SD card.

Usage:
	Settings settings = settings::defaults();
	settings::LoadResult result = settings::loadFromSd(settings);   // leaves defaults in place on any failure
*/

#pragma once

#include <Arduino.h>
#include "Definitions.h"

struct Settings
{
	char wifiSsid[SETTING_MAX_WIDTH];
	char wifiPassword[SETTING_MAX_WIDTH];
	char mqttServer[SETTING_MAX_WIDTH];
	uint16_t mqttPort;
	char mqttUsername[SETTING_MAX_WIDTH];
	char mqttPassword[SETTING_MAX_WIDTH];
	char deviceName[SETTING_MAX_WIDTH];			// Also the MQTT base topic, WiFi hostname and OTA/mDNS name
	bool usingTopSensor;
	unsigned long doorOpenTimeMs;				// Time to fully open from fully closed
	unsigned long doorCloseTimeMs;				// Time to fully close from fully open
	int relayActiveLevel;						// HIGH or LOW - the pin level which switches a relay on
	int bootUpTargetState;						// BOOT_TARGET_STATE_LAST_KNOWN / _CLOSED / _OPEN

	// The pin level which releases a relay - always the opposite of the active level
	int relayIdleLevel() const { return relayActiveLevel == HIGH ? LOW : HIGH; }

	// The longer of the two door travel times
	unsigned long longestDoorTimeMs() const { return doorOpenTimeMs > doorCloseTimeMs ? doorOpenTimeMs : doorCloseTimeMs; }
};

namespace settings
{
	enum class LoadResult
	{
		Loaded,			// settings.txt read and applied (individual invalid values are skipped and logged)
		NoCard,			// No SD card, or it couldn't be initialised
		NoFile,			// Card present but no /settings.txt
		TooBig,			// File larger than SETTINGS_FILE_MAX_BYTES
		BadJson			// File isn't valid JSON
	};

	// The compiled-in defaults.
	Settings defaults();

	// Override `settings` with any values found in /settings.txt on the SD card.  On failure `settings` is untouched.
	LoadResult loadFromSd(Settings& settings);
}
