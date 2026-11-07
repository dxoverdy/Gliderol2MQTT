/*
Name:		Door.h
Author:		Daniel Young

This file is part of Gliderol2MQTT (G2M) which is released under GNU GENERAL PUBLIC LICENSE.
See file LICENSE or go to https://choosealicense.com/licenses/gpl-3.0/ for full license details.

The garage door state machine: works out Open / Closed / Opening / Closing / Stopped from the sensors, the
travel times and any commands, and spots movements started by the fob or the Glidermatic's own buttons.

It is deliberately hardware-free - no pins, no MQTT, no display, no millis().  The caller feeds it debounced sensor
readings and the time, then collects its outputs (a new target state, a request to publish the current state).
That keeps it unit-testable on a computer:  pio test -e native

Typical use:
	door::Door door(config);
	loop:
		door.update(bottomSensorActive, topSensorActive, millis());
		char target;
		if (door.takeTargetChange(target))   publish target ('O' or 'C')
		if (door.takePublishRequest())       publish door.state()
	on an MQTT command (after pulsing the relay):
		door.commandOpen(millis());
*/

#pragma once

namespace door
{
	enum class State
	{
		Opening,
		Closing,
		Open,
		Closed,
		Stopped,
		Unknown
	};

	// Human readable, for the display: "Opening", "Closed", ...
	const char* description(State state);

	// MQTT-Thing / HomeKit current state code: "o", "c", "O", "C", "S" (or "U" for unknown)
	const char* homekitCode(State state);

	struct Config
	{
		bool usingTopSensor;
		unsigned long openTimeMs;		// Time to fully open from fully closed
		unsigned long closeTimeMs;		// Time to fully close from fully open
		unsigned long graceMs;			// Extra time allowed if the travel time passes with the door part-way (top sensor only)
	};

	class Door
	{
	public:
		explicit Door(const Config& config) : _config(config) {}

		// Feed the latest debounced sensor readings (true = sensor active, i.e. magnet present / switch made).
		// topActive is ignored when the config says there is no top sensor.
		void update(bool bottomActive, bool topActive, unsigned long nowMs);

		// A command has just been carried out (relay pulsed).  The door reports Opening/Closing until it arrives or
		// the travel time passes; a stop is reported straight away.
		void commandOpen(unsigned long nowMs);
		void commandClose(unsigned long nowMs);
		void commandStop(unsigned long nowMs);

		// True if the same command was last given less than windowMs ago - e.g. HomeKit sending the same target twice
		// in quick succession.  The caller can then skip pressing the button again.
		enum class Command { Open, Close, Stop };
		bool isRepeat(Command command, unsigned long nowMs, unsigned long windowMs) const;

		State state() const { return _state; }

		// Outputs.  Each returns true (once) if there is something to act on since it was last called.
		// takeTargetChange: a movement started by the fob/buttons was detected; target is 'O' or 'C'.
		bool takeTargetChange(char& target);
		// takePublishRequest: the current state should be published now rather than waiting for the heartbeat.
		bool takePublishRequest();

	private:
		enum class Movement { None, Opening, Closing, Stopped };

		void setTarget(char target);
		void recordCommand(Command command, unsigned long nowMs);

		bool _hasLastCommand = false;
		Command _lastCommand = Command::Stop;
		unsigned long _lastCommandMs = 0;

		Config _config;
		State _state = State::Unknown;

		bool _firstUpdate = true;
		bool _previousIsOpen = false;
		bool _previousIsStopped = false;
		bool _previousIsClosed = false;

		Movement _movement = Movement::None;
		unsigned long _movementStartMs = 0;

		bool _targetPending = false;
		char _target = 'C';
		bool _publishPending = false;
	};
}
