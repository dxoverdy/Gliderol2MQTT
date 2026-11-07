/*
Name:		CommandGuard.h
Author:		Daniel Young

This file is part of Gliderol2MQTT (G2M) which is released under GNU GENERAL PUBLIC LICENSE.
See file LICENSE or go to https://choosealicense.com/licenses/gpl-3.0/ for full license details.

The rule for ignoring commands just after (re)subscribing to MQTT.  A broker re-delivers retained messages on
subscribe, and a stray retained "open" or "reboot" must never act just because the device reconnected.
Kept hardware-free so it can be unit tested.
*/

#pragma once

namespace command_guard
{
	// True if a command arriving at nowMs should be ignored because we only subscribed at subscribedAtMs.
	inline bool shouldIgnore(unsigned long nowMs, unsigned long subscribedAtMs, unsigned long windowMs)
	{
		// Unsigned subtraction handles millis() wrapping
		return nowMs - subscribedAtMs < windowMs;
	}
}
