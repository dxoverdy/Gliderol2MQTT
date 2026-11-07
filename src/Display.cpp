/*
Name:		Display.cpp
Author:		Daniel Young

This file is part of Gliderol2MQTT (G2M) which is released under GNU GENERAL PUBLIC LICENSE.
See file LICENSE or go to https://choosealicense.com/licenses/gpl-3.0/ for full license details.

See Display.h for usage.
*/

#include "Display.h"
#include "Definitions.h"
#include "Log.h"
#include "Debouncer.h"

#include <Wire.h>
#include <Adafruit_SH110X.h>

namespace display
{
	namespace
	{
		constexpr int SCREEN_WIDTH = 128;
		constexpr int SCREEN_HEIGHT = 64;
		constexpr int OLED_RESET = -1;
		constexpr uint8_t I2C_ADDRESS = 0x3c;		// Typical for eBay OLEDs
		constexpr int LINE_HEIGHT = 16;				// Pixels per line at text size 2

		Adafruit_SH1106G _display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
		Debouncer _wakeButton(BUTTON_DEBOUNCE_MS);

		// What we want on screen
		char _lines[3][OLED_CHARACTER_WIDTH] = { "", "", "" };
		char _activityIndicator = '*';
		bool _wifiConnected = false;
		bool _mqttConnected = false;

		// What is actually on screen, so we only redraw when something has changed.  Each redraw pushes the whole
		// 1KB screen buffer over I2C (~25ms), so skipping identical frames matters.
		char _drawn[4][OLED_CHARACTER_WIDTH] = { "", "", "", "" };
		bool _forceRedraw = true;

		// Sleep (see DISPLAY_TIMEOUT_MS)
		bool _asleep = false;
		unsigned long _lastActivity = 0;

		void redrawIfChanged()
		{
			// While asleep just keep the content up to date - wake() redraws it
			if (_asleep)
			{
				return;
			}

			char statusBar[OLED_CHARACTER_WIDTH];
			snprintf(statusBar, sizeof(statusBar), "G2M    %c%c%c", _activityIndicator, _wifiConnected ? 'W' : ' ', _mqttConnected ? 'M' : ' ');

			const char* lines[4] = { statusBar, _lines[0], _lines[1], _lines[2] };

			bool changed = _forceRedraw;
			for (int i = 0; i < 4 && !changed; i++)
			{
				changed = (strcmp(lines[i], _drawn[i]) != 0);
			}
			if (!changed)
			{
				return;
			}

			_display.clearDisplay();
			_display.setTextSize(2);
			_display.setTextColor(SH110X_WHITE);
			for (int i = 0; i < 4; i++)
			{
				_display.setCursor(0, i * LINE_HEIGHT);
				_display.println(lines[i]);
				strlcpy(_drawn[i], lines[i], sizeof(_drawn[i]));
			}
			_display.display();
			_forceRedraw = false;
		}

		void sleep()
		{
			LOG("Display sleeping (timeout)\r\n");
			_display.oled_command(SH110X_DISPLAYOFF);
			_asleep = true;
		}
	}


	void begin()
	{
		pinMode(PIN_FOR_DISPLAY_WAKE_BUTTON, INPUT_PULLUP);

		_display.begin(I2C_ADDRESS, true);
		_display.clearDisplay();
		_display.display();
		_lastActivity = millis();
	}

	void showLines(const char* line2, const char* line3, const char* line4)
	{
		strlcpy(_lines[0], line2, sizeof(_lines[0]));
		strlcpy(_lines[1], line3, sizeof(_lines[1]));
		strlcpy(_lines[2], line4, sizeof(_lines[2]));
		redrawIfChanged();
	}

	void wake()
	{
		_lastActivity = millis();

		if (_asleep)
		{
			_display.oled_command(SH110X_DISPLAYON);
			_asleep = false;
			_forceRedraw = true;
			redrawIfChanged();
		}
	}

	void service(bool wifiConnected, bool mqttConnected, door::State doorState)
	{
		static unsigned long lastIndicatorFlip = 0;
		static bool initialised = false;
		static door::State lastDoorState;

		unsigned long now = millis();
		bool buttonPressed = (_wakeButton.update(digitalRead(PIN_FOR_DISPLAY_WAKE_BUTTON), now) == LOW);

		if (!initialised)
		{
			initialised = true;
			lastDoorState = doorState;
			_wifiConnected = wifiConnected;
			_mqttConnected = mqttConnected;
		}

		if (buttonPressed && _asleep)
		{
			LOG("Display woken by button\r\n");
		}

		// Holding the button keeps the screen on; a door state change or connection change wakes it; and it stays on
		// while WiFi or MQTT is down so a problem is visible to anyone who looks.
		if (buttonPressed || doorState != lastDoorState || wifiConnected != _wifiConnected || mqttConnected != _mqttConnected
			|| !wifiConnected || !mqttConnected)
		{
			wake();
		}

		lastDoorState = doorState;
		_wifiConnected = wifiConnected;
		_mqttConnected = mqttConnected;

		// Flip the activity indicator every half second
		if (now - lastIndicatorFlip >= UPDATE_STATUS_BAR_INTERVAL)
		{
			lastIndicatorFlip = now;
			_activityIndicator = (_activityIndicator == '*') ? ' ' : '*';
		}

		if (DISPLAY_TIMEOUT_MS > 0 && !_asleep && now - _lastActivity >= DISPLAY_TIMEOUT_MS)
		{
			sleep();
		}

		redrawIfChanged();
	}
}
