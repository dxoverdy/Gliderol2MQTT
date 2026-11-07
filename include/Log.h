/*
Name:		Log.h
Author:		Daniel Young

This file is part of Gliderol2MQTT (G2M) which is released under GNU GENERAL PUBLIC LICENSE.
See file LICENSE or go to https://choosealicense.com/licenses/gpl-3.0/ for full license details.

Serial logging which compiles to nothing when switched off (see DEBUG / DEBUG_LEVEL2 in Definitions.h).
printf-style:  LOG("Connected to %s\r\n", ssid);
*/

#pragma once

#include <Arduino.h>
#include "Definitions.h"

#ifdef DEBUG
#define LOG(...) Serial.printf(__VA_ARGS__)
#else
#define LOG(...) do {} while (0)
#endif

#ifdef DEBUG_LEVEL2
#define LOG2(...) Serial.printf(__VA_ARGS__)
#else
#define LOG2(...) do {} while (0)
#endif
