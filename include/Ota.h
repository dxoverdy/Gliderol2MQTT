/*
Name:		Ota.h
Author:		Daniel Young

This file is part of Gliderol2MQTT (G2M) which is released under GNU GENERAL PUBLIC LICENSE.
See file LICENSE or go to https://choosealicense.com/licenses/gpl-3.0/ for full license details.

Over-the-air firmware updates (ArduinoOTA / espota).

The module knows nothing about relays, MQTT or the display.  The application supplies hooks so it can
make the hardware safe before an update starts and show progress however it likes.

Usage:
	ota::Hooks hooks;
	hooks.onStart = myMakeSafeFunction;
	ota::begin("Gliderol2MQTT", "password", hooks);   // once WiFi is connected; safe to call repeatedly
	ota::handle();                                    // call often - from loop() and from any long wait

Note that while an update is being received, ota::handle() does not return until the transfer finishes
(successfully or not).  On success the device reboots itself.
*/

#pragma once

#include <stdint.h>

namespace ota
{
	struct Hooks
	{
		// Called once, just before the update begins writing to flash.  Put hardware into a safe state here.
		void (*onStart)() = nullptr;

		// Called whenever the whole-number percentage complete changes (0-100).
		void (*onProgress)(uint8_t percent) = nullptr;

		// Called when the update finishes.  success == true means the device is about to reboot into the new firmware.
		void (*onEnd)(bool success) = nullptr;
	};

	// Start listening for OTA uploads.  Call after WiFi has connected.  Subsequent calls are ignored.
	void begin(const char* hostname, const char* password, uint16_t port, const Hooks& hooks);

	// Service the OTA listener.  Cheap when idle; blocks for the duration of an upload.
	void handle();

	// True while an upload is being received.
	bool inProgress();
}
