/*
Name:		Debouncer.h
Author:		Daniel Young

This file is part of Gliderol2MQTT (G2M) which is released under GNU GENERAL PUBLIC LICENSE.
See file LICENSE or go to https://choosealicense.com/licenses/gpl-3.0/ for full license details.

Hardware-free debouncing: feed it raw readings and the time, and it only reports a new value once the raw reading
has held steady for the debounce period.  A single glitchy sample (contact bounce, noise on a long cable) is never
reported.  Has no Arduino dependency so it can be unit tested on a computer.
*/

#pragma once

class Debouncer
{
public:
	explicit Debouncer(unsigned long debounceMs) : _debounceMs(debounceMs) {}

	// Feed the latest raw reading; returns the debounced value.
	int update(int raw, unsigned long nowMs)
	{
		if (!_initialised)
		{
			// First reading - nothing to compare against, so trust it
			_stable = raw;
			_last = raw;
			_changedAt = nowMs;
			_initialised = true;
		}
		else if (raw != _last)
		{
			// Raw reading changed - restart the clock
			_last = raw;
			_changedAt = nowMs;
		}
		else if (raw != _stable && nowMs - _changedAt >= _debounceMs)
		{
			// New reading has held steady long enough - accept it.  (Unsigned subtraction handles millis() wrapping.)
			_stable = raw;
		}

		return _stable;
	}

	int value() const { return _stable; }

private:
	unsigned long _debounceMs;
	int _stable = 0;
	int _last = 0;
	unsigned long _changedAt = 0;
	bool _initialised = false;
};
