/*
Name:		Display.h
Author:		Daniel Young

This file is part of Gliderol2MQTT (G2M) which is released under GNU GENERAL PUBLIC LICENSE.
See file LICENSE or go to https://choosealicense.com/licenses/gpl-3.0/ for full license details.

The optional 128x64 SH1106 OLED: a status bar (G2M, activity indicator, W for WiFi, M for MQTT) and three lines of
10 characters.  Only redraws when something has changed, switches itself off after DISPLAY_TIMEOUT_MS of nothing
happening, and wakes on anything worth seeing or on the optional wake button (PIN_FOR_DISPLAY_WAKE_BUTTON).
*/

#pragma once

#include "Door.h"

namespace display
{
	// Initialise the screen and the wake button pin.
	void begin();

	// Set the three detail lines (truncated to 10 characters) and redraw if anything changed.
	void showLines(const char* line2, const char* line3, const char* line4);

	// Call every loop: flashes the activity indicator, keeps W/M current, handles the wake button and the timeout.
	void service(bool wifiConnected, bool mqttConnected, door::State doorState);

	// Turn the screen on (if asleep) and restart the timeout.
	void wake();
}
