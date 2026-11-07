/*
Name:		Door.cpp
Author:		Daniel Young

This file is part of Gliderol2MQTT (G2M) which is released under GNU GENERAL PUBLIC LICENSE.
See file LICENSE or go to https://choosealicense.com/licenses/gpl-3.0/ for full license details.

See Door.h for usage.
*/

#include "Door.h"

namespace door
{
	const char* description(State state)
	{
		switch (state)
		{
		case State::Opening:	return "Opening";
		case State::Closing:	return "Closing";
		case State::Open:		return "Open";
		case State::Closed:		return "Closed";
		case State::Stopped:	return "Stopped";
		default:				return "Unknown";
		}
	}

	const char* homekitCode(State state)
	{
		switch (state)
		{
		case State::Opening:	return "o";
		case State::Closing:	return "c";
		case State::Open:		return "O";
		case State::Closed:		return "C";
		case State::Stopped:	return "S";
		default:				return "U";
		}
	}


	void Door::update(bool bottomActive, bool topActive, unsigned long nowMs)
	{
		bool isOpen;
		bool isClosed;
		bool isStopped;
		bool stillMoving = false;
		bool skipCloseTimerDueToFobAndNoTopSensor = false;

		// When the bottom sensor is active the door is closed.
		isClosed = bottomActive;

		if (_config.usingTopSensor)
		{
			isOpen = topActive;
			// Stopped is when not open and not closed.
			// And as a safeguard for both open and closed at the same time (in theory not possible), report stopped too
			isStopped = (!isOpen && !isClosed) || (isOpen && isClosed);
		}
		else
		{
			// Not utilising a top sensor, so can only presume open is opposite of closed, and Stopped is indeterminate.
			isOpen = !isClosed;
			isStopped = false;
		}

		if (_firstUpdate)
		{
			// First update, create reliable values to compare against
			_firstUpdate = false;
			_previousIsOpen = isOpen;
			_previousIsStopped = isStopped;
			_previousIsClosed = isClosed;
		}


		// Scenario: if using a top sensor and we know the door is stopped mid-way, then the previously known open and
		// closed values will both be false.  So if coming out of stopped by way of a fob/on-device button, we won't have
		// an idea of this until it reaches a fully open or fully closed position.  We don't want to trigger a full
		// Opening/Closing timer then, so set the previous open/closed to what they are now, and announce the target.
		if (_config.usingTopSensor && _previousIsStopped && !isStopped && _movement == Movement::None)
		{
			_previousIsOpen = isOpen;
			_previousIsClosed = isClosed;

			if (isOpen)
			{
				setTarget('O');
			}
			else if (isClosed)
			{
				setTarget('C');
			}
			_publishPending = true;
		}


		// Routine usage outside of MQTT, i.e. was open, fob pressed, sensor now says not open, etc.
		if (_previousIsOpen && !isOpen && _movement == Movement::None)
		{
			setTarget('C');
			_movement = Movement::Closing;
			_movementStartMs = nowMs;

			// Without a top sensor, the first we know of a fob close is the door arriving at the bottom, so there is
			// no point running the closing timer.
			skipCloseTimerDueToFobAndNoTopSensor = !_config.usingTopSensor;
			_publishPending = true;
		}
		// And likewise the opposite: previously closed and now not, so opening...
		else if (_previousIsClosed && !isClosed && _movement == Movement::None)
		{
			setTarget('O');
			_movement = Movement::Opening;
			_movementStartMs = nowMs;
			_publishPending = true;
		}


		// A movement ends as soon as the destination sensor confirms arrival, or when the travel time has passed.  If
		// the travel time passes with the door mid-way (neither sensor), allow graceMs more before giving up, in case
		// the door is just a little slow today.
		unsigned long elapsed = nowMs - _movementStartMs;		// Unsigned subtraction handles millis() wrapping
		unsigned long grace = (_config.usingTopSensor && isStopped) ? _config.graceMs : 0;

		if (_movement == Movement::Opening)
		{
			// Arrival at the top can only be confirmed with a top sensor (without one, "open" just means "not closed")
			bool arrived = _config.usingTopSensor && isOpen && !isClosed;
			bool timeUp = elapsed >= _config.openTimeMs + grace;

			if (arrived || timeUp)
			{
				_movement = Movement::None;
				_publishPending = true;
			}
			else
			{
				_state = State::Opening;
				stillMoving = true;
			}
		}
		else if (_movement == Movement::Closing)
		{
			// The bottom sensor is always fitted, so arrival at the bottom can always be confirmed
			bool arrived = isClosed && !isOpen;
			bool timeUp = elapsed >= _config.closeTimeMs + grace;

			if (arrived || timeUp || skipCloseTimerDueToFobAndNoTopSensor)
			{
				_movement = Movement::None;
				_publishPending = true;
			}
			else
			{
				_state = State::Closing;
				stillMoving = true;
			}
		}
		else if (_movement == Movement::Stopped)
		{
			_movement = Movement::None;
			_state = State::Stopped;
			_publishPending = true;
		}


		// Unless still within a movement, the sensors have the final say
		if (!stillMoving)
		{
			if (isStopped)
			{
				_state = State::Stopped;
			}
			else if (isOpen)
			{
				_state = State::Open;
			}
			else if (isClosed)
			{
				_state = State::Closed;
			}
		}

		// Save what the current state is, to compare against next time
		_previousIsOpen = isOpen;
		_previousIsStopped = isStopped;
		_previousIsClosed = isClosed;
	}


	void Door::commandOpen(unsigned long nowMs)
	{
		_movement = Movement::Opening;
		_movementStartMs = nowMs;
		_publishPending = true;
	}

	void Door::commandClose(unsigned long nowMs)
	{
		_movement = Movement::Closing;
		_movementStartMs = nowMs;
		_publishPending = true;
	}

	void Door::commandStop(unsigned long nowMs)
	{
		_movement = Movement::Stopped;
		_movementStartMs = nowMs;
		_publishPending = true;
	}


	bool Door::takeTargetChange(char& target)
	{
		if (!_targetPending)
		{
			return false;
		}
		_targetPending = false;
		target = _target;
		return true;
	}

	bool Door::takePublishRequest()
	{
		bool pending = _publishPending;
		_publishPending = false;
		return pending;
	}

	void Door::setTarget(char target)
	{
		_target = target;
		_targetPending = true;
	}
}
