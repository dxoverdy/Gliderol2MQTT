/*
Name:		Ota.cpp
Author:		Daniel Young

This file is part of Gliderol2MQTT (G2M) which is released under GNU GENERAL PUBLIC LICENSE.
See file LICENSE or go to https://choosealicense.com/licenses/gpl-3.0/ for full license details.

See Ota.h for usage.
*/

#include "Ota.h"
#include "Definitions.h"
#include "Log.h"

#include <Arduino.h>
#include <ArduinoOTA.h>

namespace ota
{
	namespace
	{
		Hooks _hooks;
		bool _started = false;
		bool _inProgress = false;
		int _lastPercent = -1;

		// Only used in log messages, so unused when DEBUG is off
		__attribute__((unused)) const char* errorDescription(ota_error_t error)
		{
			switch (error)
			{
			case OTA_AUTH_ERROR:	return "Auth failed (check ota_password in private.ini)";
			case OTA_BEGIN_ERROR:	return "Begin failed (not enough space?)";
			case OTA_CONNECT_ERROR:	return "Connect failed";
			case OTA_RECEIVE_ERROR:	return "Receive failed";
			case OTA_END_ERROR:		return "End failed";
			default:				return "Unknown error";
			}
		}
	}

	void begin(const char* hostname, const char* password, uint16_t port, const Hooks& hooks)
	{
		if (_started)
		{
			return;
		}

		_hooks = hooks;

		// The hostname is also advertised over mDNS, which is how PlatformIO finds us as <hostname>.local
		ArduinoOTA.setHostname(hostname);
		ArduinoOTA.setPort(port);
		if (password != nullptr && password[0] != '\0')
		{
			ArduinoOTA.setPassword(password);
		}

		ArduinoOTA.onStart([]()
		{
			_inProgress = true;
			_lastPercent = -1;
			LOG("OTA: Update starting (%s)\r\n", ArduinoOTA.getCommand() == U_FLASH ? "firmware" : "filesystem");
			if (_hooks.onStart)
			{
				_hooks.onStart();
			}
		});

		ArduinoOTA.onProgress([](unsigned int progress, unsigned int total)
		{
			if (total == 0)
			{
				return;
			}

			// 64-bit maths so large images can't overflow progress * 100
			int percent = (int)(((uint64_t)progress * 100) / total);
			if (percent != _lastPercent)
			{
				_lastPercent = percent;
				if (_hooks.onProgress)
				{
					_hooks.onProgress((uint8_t)percent);
				}
			}
		});

		ArduinoOTA.onEnd([]()
		{
			_inProgress = false;
			LOG("OTA: Update complete, rebooting\r\n");
			if (_hooks.onEnd)
			{
				_hooks.onEnd(true);
			}
		});

		ArduinoOTA.onError([](ota_error_t error)
		{
			_inProgress = false;
			LOG("OTA: Error %u - %s\r\n", (unsigned)error, errorDescription(error));
			if (_hooks.onEnd)
			{
				_hooks.onEnd(false);
			}
		});

		ArduinoOTA.begin();
		_started = true;

		LOG("OTA: Listening as %s.local on port %u\r\n", hostname, (unsigned)port);
	}

	void handle()
	{
		if (_started)
		{
			ArduinoOTA.handle();
		}
	}

	bool inProgress()
	{
		return _inProgress;
	}
}
