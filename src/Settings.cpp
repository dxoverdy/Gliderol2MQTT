/*
Name:		Settings.cpp
Author:		Daniel Young

This file is part of Gliderol2MQTT (G2M) which is released under GNU GENERAL PUBLIC LICENSE.
See file LICENSE or go to https://choosealicense.com/licenses/gpl-3.0/ for full license details.

See Settings.h for usage.

settings.txt format (every key optional - anything missing keeps its compiled-in default):
{
	"WIFI_SSID":"MyNetwork",
	"WIFI_PASSWORD":"My password!",
	"MQTT_SERVER":"192.168.1.135",
	"MQTT_PORT":1883,
	"MQTT_USERNAME":"Garage",
	"MQTT_PASSWORD":"DoorOpener1",
	"DEVICE_NAME":"Gliderol2MQTT",
	"USING_TOP_SENSOR":"Yes",
	"TIME_TO_FULLY_OPEN_FROM_FULLY_CLOSED":10000,
	"TIME_TO_FULLY_CLOSED_FROM_FULLY_OPEN":10000,
	"RELAY_ACTIVE_LEVEL":"HIGH",
	"BOOT_UP_TARGET_STATE":0
}
For compatibility with older settings files, numbers may also be written as strings ("1883"), USING_TOP_SENSOR
may be true/false as well as "Yes"/"No", and the old NORMALLY_OPEN key is accepted in place of RELAY_ACTIVE_LEVEL.
*/

#include "Settings.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <SD.h>

namespace settings
{
	namespace
	{
		// Copy a string setting if present and valid.  Returns true if applied.
		bool readString(JsonDocument& doc, const char* key, char* destination, size_t length, bool secret = false)
		{
			JsonVariant value = doc[key];
			if (value.isNull())
			{
				return false;
			}
			if (!value.is<const char*>())
			{
#ifdef DEBUG
				Serial.printf("Settings: %s must be a string - ignored\r\n", key);
#endif
				return false;
			}

			const char* text = value.as<const char*>();
			if (strlen(text) >= length)
			{
#ifdef DEBUG
				Serial.printf("Settings: %s is longer than %u characters - ignored\r\n", key, (unsigned)(length - 1));
#endif
				return false;
			}

			strlcpy(destination, text, length);
#ifdef DEBUG
			if (secret)
			{
				Serial.printf("Settings: %s set (value hidden)\r\n", key);
			}
			else
			{
				Serial.printf("Settings: %s = \"%s\"\r\n", key, destination);
			}
#endif
			return true;
		}

		// Read a whole number, accepting either a JSON number or a numeric string (older files quoted numbers).
		bool readNumber(JsonDocument& doc, const char* key, long minimum, long maximum, long& result)
		{
			JsonVariant value = doc[key];
			if (value.isNull())
			{
				return false;
			}

			long number;
			if (value.is<long>())
			{
				number = value.as<long>();
			}
			else if (value.is<const char*>())
			{
				char* end;
				const char* text = value.as<const char*>();
				number = strtol(text, &end, 10);
				if (end == text || *end != '\0')
				{
#ifdef DEBUG
					Serial.printf("Settings: %s is not a whole number - ignored\r\n", key);
#endif
					return false;
				}
			}
			else
			{
#ifdef DEBUG
				Serial.printf("Settings: %s is not a whole number - ignored\r\n", key);
#endif
				return false;
			}

			if (number < minimum || number > maximum)
			{
#ifdef DEBUG
				Serial.printf("Settings: %s = %ld is outside %ld..%ld - ignored\r\n", key, number, minimum, maximum);
#endif
				return false;
			}

			result = number;
#ifdef DEBUG
			Serial.printf("Settings: %s = %ld\r\n", key, number);
#endif
			return true;
		}

		// Read a yes/no setting, accepting true/false or "Yes"/"No" (any case).
		bool readYesNo(JsonDocument& doc, const char* key, bool& result)
		{
			JsonVariant value = doc[key];
			if (value.isNull())
			{
				return false;
			}
			if (value.is<bool>())
			{
				result = value.as<bool>();
			}
			else if (value.is<const char*>() && strcasecmp(value.as<const char*>(), "Yes") == 0)
			{
				result = true;
			}
			else if (value.is<const char*>() && strcasecmp(value.as<const char*>(), "No") == 0)
			{
				result = false;
			}
			else
			{
#ifdef DEBUG
				Serial.printf("Settings: %s must be \"Yes\" or \"No\" - ignored\r\n", key);
#endif
				return false;
			}
#ifdef DEBUG
			Serial.printf("Settings: %s = %s\r\n", key, result ? "Yes" : "No");
#endif
			return true;
		}

		// Read "HIGH"/"LOW" (any case).
		bool readLevel(JsonDocument& doc, const char* key, int& result)
		{
			JsonVariant value = doc[key];
			if (value.isNull())
			{
				return false;
			}
			if (value.is<const char*>() && strcasecmp(value.as<const char*>(), "HIGH") == 0)
			{
				result = HIGH;
			}
			else if (value.is<const char*>() && strcasecmp(value.as<const char*>(), "LOW") == 0)
			{
				result = LOW;
			}
			else
			{
#ifdef DEBUG
				Serial.printf("Settings: %s must be \"HIGH\" or \"LOW\" - ignored\r\n", key);
#endif
				return false;
			}
#ifdef DEBUG
			Serial.printf("Settings: %s = %s\r\n", key, result == HIGH ? "HIGH" : "LOW");
#endif
			return true;
		}
	}


	Settings defaults()
	{
		Settings s;
		strlcpy(s.wifiSsid, TOSTRING(WIFI_SSID), sizeof(s.wifiSsid));
		strlcpy(s.wifiPassword, TOSTRING(WIFI_PASSWORD), sizeof(s.wifiPassword));
		strlcpy(s.mqttServer, TOSTRING(MQTT_BROKER), sizeof(s.mqttServer));
		s.mqttPort = MQTT_PORT;
		strlcpy(s.mqttUsername, TOSTRING(MQTT_USERNAME), sizeof(s.mqttUsername));
		strlcpy(s.mqttPassword, TOSTRING(MQTT_PASSWORD), sizeof(s.mqttPassword));
		strlcpy(s.deviceName, DEVICE_NAME, sizeof(s.deviceName));
		s.usingTopSensor = USING_TOP_SENSOR;
		s.doorOpenTimeMs = TIME_TO_FULLY_OPEN_FROM_FULLY_CLOSED;
		s.doorCloseTimeMs = TIME_TO_FULLY_CLOSED_FROM_FULLY_OPEN;
		s.relayActiveLevel = RELAY_ACTIVE_LEVEL;
		s.bootUpTargetState = BOOT_UP_TARGET_STATE;
		return s;
	}


	LoadResult loadFromSd(Settings& settings)
	{
#ifdef DEBUG
		Serial.print("Initialising SD card...");
#endif
		if (!SD.begin(PIN_FOR_SDCARD_VSPI_CS))
		{
#ifdef DEBUG
			Serial.println("Initialisation failed!");
#endif
			return LoadResult::NoCard;
		}
#ifdef DEBUG
		Serial.println("Initialisation complete!");
#endif

		// Open settings.txt in the root.  Note the forward slash.  It is required.
		File file = SD.open("/settings.txt");
		if (!file)
		{
#ifdef DEBUG
			Serial.println("settings.txt was not found.");
#endif
			SD.end();
			return LoadResult::NoFile;
		}

		if (file.size() > SETTINGS_FILE_MAX_BYTES)
		{
#ifdef DEBUG
			Serial.printf("settings.txt is %u bytes, maximum is %d.  Using defaults.\r\n", (unsigned)file.size(), SETTINGS_FILE_MAX_BYTES);
#endif
			file.close();
			SD.end();
			return LoadResult::TooBig;
		}

		// Parse straight from the file - no intermediate buffer needed.
		JsonDocument doc;
		DeserializationError error = deserializeJson(doc, file);
		file.close();
		SD.end();

		if (error)
		{
#ifdef DEBUG
			Serial.printf("settings.txt is not valid JSON (%s).  Using defaults.\r\n", error.c_str());
#endif
			return LoadResult::BadJson;
		}

#ifdef DEBUG
		Serial.println("settings.txt was found and parsed.");
#endif

		// Apply each setting that is present and valid.  Invalid ones are logged and keep their default.
		long number;
		readString(doc, "WIFI_SSID", settings.wifiSsid, sizeof(settings.wifiSsid));
		readString(doc, "WIFI_PASSWORD", settings.wifiPassword, sizeof(settings.wifiPassword), true);
		readString(doc, "MQTT_SERVER", settings.mqttServer, sizeof(settings.mqttServer));
		if (readNumber(doc, "MQTT_PORT", 1, 65535, number))
		{
			settings.mqttPort = (uint16_t)number;
		}
		readString(doc, "MQTT_USERNAME", settings.mqttUsername, sizeof(settings.mqttUsername));
		readString(doc, "MQTT_PASSWORD", settings.mqttPassword, sizeof(settings.mqttPassword), true);
		readString(doc, "DEVICE_NAME", settings.deviceName, sizeof(settings.deviceName));
		readYesNo(doc, "USING_TOP_SENSOR", settings.usingTopSensor);
		if (readNumber(doc, "TIME_TO_FULLY_OPEN_FROM_FULLY_CLOSED", 1000, 120000, number))
		{
			settings.doorOpenTimeMs = (unsigned long)number;
		}
		if (readNumber(doc, "TIME_TO_FULLY_CLOSED_FROM_FULLY_OPEN", 1000, 120000, number))
		{
			settings.doorCloseTimeMs = (unsigned long)number;
		}
		// RELAY_ACTIVE_LEVEL wins if both it and the old NORMALLY_OPEN name are present.  (NORMALLY_CLOSED is ignored -
		// the idle level is always the opposite of the active level.)
		if (!readLevel(doc, "RELAY_ACTIVE_LEVEL", settings.relayActiveLevel))
		{
			readLevel(doc, "NORMALLY_OPEN", settings.relayActiveLevel);
		}
		if (readNumber(doc, "BOOT_UP_TARGET_STATE", BOOT_TARGET_STATE_LAST_KNOWN, BOOT_TARGET_STATE_OPEN, number))
		{
			settings.bootUpTargetState = (int)number;
		}

		return LoadResult::Loaded;
	}
}
