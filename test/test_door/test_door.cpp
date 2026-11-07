/*
Unit tests for the hardware-free door logic (lib/DoorLogic).  Run on your computer with:

	pio test -e native

Each test drives the Door state machine with sensor readings and times, as the firmware's loop would, and checks
what it reports.  Times are in milliseconds.  The sensors are "active" when the magnet is present / switch is made.
*/

#include <unity.h>
#include <climits>
#include <string>
#include "Door.h"
#include "Debouncer.h"
#include "CommandGuard.h"

using door::Door;
using door::State;

namespace
{
	const door::Config WITH_TOP = { true, 10000, 10000, 5000 };
	const door::Config NO_TOP = { false, 10000, 10000, 5000 };

	// Sensor positions
	struct Sensors { bool bottom; bool top; };
	const Sensors AT_BOTTOM = { true, false };
	const Sensors AT_TOP = { false, true };
	const Sensors MID_WAY = { false, false };

	// Everything the door reported during a run, as a string of HomeKit codes with consecutive duplicates removed,
	// e.g. "CoO".  Makes "did it ever report Stopped?" style assertions easy to read.
	struct Recorder
	{
		std::string states;
		std::string targets;

		void observe(Door& d)
		{
			std::string code = door::homekitCode(d.state());
			if (states.empty() || states.substr(states.size() - 1) != code)
			{
				states += code;
			}
			char target;
			if (d.takeTargetChange(target))
			{
				targets += target;
			}
			d.takePublishRequest();
		}
	};

	// Feed the same sensor reading every 50ms from fromMs up to (not including) toMs.
	void hold(Door& d, Recorder& r, Sensors s, unsigned long fromMs, unsigned long toMs)
	{
		for (unsigned long t = fromMs; t < toMs; t += 50)
		{
			d.update(s.bottom, s.top, t);
			r.observe(d);
		}
	}
}


// ---------------------------------------------------------------- Boot

void test_boot_closed_reports_closed_without_a_target_change()
{
	Door d(WITH_TOP);
	Recorder r;
	hold(d, r, AT_BOTTOM, 0, 1000);
	TEST_ASSERT_EQUAL_STRING("C", r.states.c_str());
	TEST_ASSERT_EQUAL_STRING("", r.targets.c_str());
}

void test_boot_open_reports_open()
{
	Door d(WITH_TOP);
	Recorder r;
	hold(d, r, AT_TOP, 0, 1000);
	TEST_ASSERT_EQUAL_STRING("O", r.states.c_str());
}

void test_boot_mid_way_reports_stopped()
{
	Door d(WITH_TOP);
	Recorder r;
	hold(d, r, MID_WAY, 0, 1000);
	TEST_ASSERT_EQUAL_STRING("S", r.states.c_str());
}

void test_both_sensors_active_reports_stopped()
{
	Door d(WITH_TOP);
	Recorder r;
	hold(d, r, { true, true }, 0, 1000);
	TEST_ASSERT_EQUAL_STRING("S", r.states.c_str());
}


// ---------------------------------------------------------------- MQTT commands (top sensor)

void test_mqtt_open_slightly_slower_than_setting_never_reports_stopped()
{
	// The real-world case from the Homebridge logs: top switch made at 10.5s against a 10s setting.
	Door d(WITH_TOP);
	Recorder r;
	hold(d, r, AT_BOTTOM, 0, 1000);
	d.commandOpen(1000);
	hold(d, r, AT_BOTTOM, 1000, 2000);		// Door starts moving
	hold(d, r, MID_WAY, 2000, 11500);		// Travelling (timer expires at 11000 - inside the grace period)
	hold(d, r, AT_TOP, 11500, 13000);		// Top switch made
	TEST_ASSERT_EQUAL_STRING("CoO", r.states.c_str());
}

void test_mqtt_open_reports_open_the_moment_the_top_switch_is_made()
{
	Door d(WITH_TOP);
	Recorder r;
	hold(d, r, AT_BOTTOM, 0, 1000);
	d.commandOpen(1000);
	hold(d, r, MID_WAY, 1000, 9000);		// Arrives after 8s, well inside the 10s setting
	d.update(false, true, 9000);
	TEST_ASSERT_EQUAL(State::Open, d.state());
}

void test_mqtt_open_that_never_arrives_reports_stopped_after_time_plus_grace()
{
	Door d(WITH_TOP);
	Recorder r;
	hold(d, r, AT_BOTTOM, 0, 1000);
	d.commandOpen(1000);
	hold(d, r, MID_WAY, 1000, 15950);		// 10s travel + 5s grace = until 16000
	TEST_ASSERT_EQUAL(State::Opening, d.state());
	d.update(false, false, 16000);
	TEST_ASSERT_EQUAL(State::Stopped, d.state());
}

void test_mqtt_close_reports_closed_on_arrival()
{
	Door d(WITH_TOP);
	Recorder r;
	hold(d, r, AT_TOP, 0, 1000);
	d.commandClose(1000);
	hold(d, r, AT_TOP, 1000, 1500);
	hold(d, r, MID_WAY, 1500, 9000);
	hold(d, r, AT_BOTTOM, 9000, 9500);
	TEST_ASSERT_EQUAL_STRING("OcC", r.states.c_str());
	// An MQTT command's target is announced by the MQTT layer, not detected from the sensors
	TEST_ASSERT_EQUAL_STRING("", r.targets.c_str());
}

void test_mqtt_close_on_an_already_closed_door_reports_closed_immediately()
{
	Door d(WITH_TOP);
	Recorder r;
	hold(d, r, AT_BOTTOM, 0, 1000);
	d.commandClose(1000);
	d.update(true, false, 1000);
	TEST_ASSERT_EQUAL(State::Closed, d.state());
}

void test_mqtt_stop_mid_way_reports_stopped_immediately()
{
	Door d(WITH_TOP);
	Recorder r;
	hold(d, r, AT_BOTTOM, 0, 1000);
	d.commandOpen(1000);
	hold(d, r, MID_WAY, 1000, 4000);
	d.commandStop(4000);
	d.update(false, false, 4000);
	TEST_ASSERT_EQUAL(State::Stopped, d.state());
}

void test_commands_request_a_publish()
{
	Door d(WITH_TOP);
	Recorder r;
	hold(d, r, AT_BOTTOM, 0, 1000);
	d.commandOpen(1000);
	TEST_ASSERT_TRUE(d.takePublishRequest());
	TEST_ASSERT_FALSE(d.takePublishRequest());		// Only once
}


// ---------------------------------------------------------------- Fob / Glidermatic buttons

void test_fob_open_from_closed_announces_target_open()
{
	Door d(WITH_TOP);
	Recorder r;
	hold(d, r, AT_BOTTOM, 0, 1000);
	hold(d, r, MID_WAY, 1000, 9000);
	hold(d, r, AT_TOP, 9000, 9500);
	TEST_ASSERT_EQUAL_STRING("O", r.targets.c_str());
	TEST_ASSERT_EQUAL_STRING("CoO", r.states.c_str());
}

void test_fob_close_from_open_announces_target_closed()
{
	Door d(WITH_TOP);
	Recorder r;
	hold(d, r, AT_TOP, 0, 1000);
	hold(d, r, MID_WAY, 1000, 9000);
	hold(d, r, AT_BOTTOM, 9000, 9500);
	TEST_ASSERT_EQUAL_STRING("C", r.targets.c_str());
	TEST_ASSERT_EQUAL_STRING("OcC", r.states.c_str());
}

void test_coming_out_of_stopped_by_fob_goes_straight_to_the_end_state()
{
	// Door stopped mid-way, then the fob sends it to the top: no Opening timer is started (we only learn of the
	// movement on arrival), but the target is announced.
	Door d(WITH_TOP);
	Recorder r;
	hold(d, r, MID_WAY, 0, 1000);
	hold(d, r, AT_TOP, 1000, 1500);
	TEST_ASSERT_EQUAL_STRING("SO", r.states.c_str());
	TEST_ASSERT_EQUAL_STRING("O", r.targets.c_str());
}


// ---------------------------------------------------------------- No top sensor

void test_no_top_sensor_open_times_out_to_open()
{
	Door d(NO_TOP);
	Recorder r;
	hold(d, r, AT_BOTTOM, 0, 1000);
	d.commandOpen(1000);
	hold(d, r, MID_WAY, 1000, 12000);		// Can't confirm arrival - reports Opening for the full 10s (no grace)
	TEST_ASSERT_EQUAL_STRING("CoO", r.states.c_str());
}

void test_no_top_sensor_fob_close_is_reported_on_arrival()
{
	Door d(NO_TOP);
	Recorder r;
	hold(d, r, MID_WAY, 0, 1000);			// Without a top sensor, "not closed" = open
	hold(d, r, AT_BOTTOM, 1000, 1500);
	TEST_ASSERT_EQUAL_STRING("OC", r.states.c_str());
	TEST_ASSERT_EQUAL_STRING("C", r.targets.c_str());
}

void test_no_top_sensor_never_reports_stopped()
{
	Door d(NO_TOP);
	Recorder r;
	hold(d, r, AT_BOTTOM, 0, 1000);
	d.commandOpen(1000);
	hold(d, r, MID_WAY, 1000, 4000);
	d.commandStop(4000);
	hold(d, r, MID_WAY, 4000, 5000);
	TEST_ASSERT_TRUE(r.states.find('S') == std::string::npos);
}


// ---------------------------------------------------------------- Timing edge cases

void test_movement_timer_survives_millis_wrapping()
{
	Door d(WITH_TOP);
	Recorder r;
	unsigned long start = ULONG_MAX - 3000;		// Wraps 3s into the movement
	hold(d, r, AT_BOTTOM, start - 1000, start);
	d.commandOpen(start);
	for (unsigned long i = 0; i < 9000; i += 50)
	{
		d.update(false, false, start + i);		// start + i wraps past zero
	}
	TEST_ASSERT_EQUAL(State::Opening, d.state());
	d.update(false, true, start + 9000);
	TEST_ASSERT_EQUAL(State::Open, d.state());
}


// ---------------------------------------------------------------- Debouncer

void test_debouncer_trusts_the_first_reading()
{
	Debouncer db(100);
	TEST_ASSERT_EQUAL(0, db.update(0, 0));
}

void test_debouncer_ignores_a_short_glitch()
{
	Debouncer db(100);
	db.update(1, 0);
	db.update(0, 10);			// Glitch...
	db.update(0, 60);
	db.update(1, 70);			// ...gone after 60ms
	TEST_ASSERT_EQUAL(1, db.update(1, 500));
}

void test_debouncer_accepts_a_change_held_long_enough()
{
	Debouncer db(100);
	db.update(1, 0);
	db.update(0, 10);
	TEST_ASSERT_EQUAL(1, db.update(0, 109));	// Not yet
	TEST_ASSERT_EQUAL(0, db.update(0, 110));	// Held 100ms
}


// ---------------------------------------------------------------- Command guard

void test_commands_ignored_just_after_subscribing()
{
	TEST_ASSERT_TRUE(command_guard::shouldIgnore(1000, 0, 1500));
	TEST_ASSERT_FALSE(command_guard::shouldIgnore(1500, 0, 1500));
}

void test_command_guard_survives_millis_wrapping()
{
	TEST_ASSERT_TRUE(command_guard::shouldIgnore(500, ULONG_MAX - 499, 1500));		// 1000ms after subscribing
	TEST_ASSERT_FALSE(command_guard::shouldIgnore(2000, ULONG_MAX - 499, 1500));	// 2500ms after
}


int main()
{
	UNITY_BEGIN();
	RUN_TEST(test_boot_closed_reports_closed_without_a_target_change);
	RUN_TEST(test_boot_open_reports_open);
	RUN_TEST(test_boot_mid_way_reports_stopped);
	RUN_TEST(test_both_sensors_active_reports_stopped);
	RUN_TEST(test_mqtt_open_slightly_slower_than_setting_never_reports_stopped);
	RUN_TEST(test_mqtt_open_reports_open_the_moment_the_top_switch_is_made);
	RUN_TEST(test_mqtt_open_that_never_arrives_reports_stopped_after_time_plus_grace);
	RUN_TEST(test_mqtt_close_reports_closed_on_arrival);
	RUN_TEST(test_mqtt_close_on_an_already_closed_door_reports_closed_immediately);
	RUN_TEST(test_mqtt_stop_mid_way_reports_stopped_immediately);
	RUN_TEST(test_commands_request_a_publish);
	RUN_TEST(test_fob_open_from_closed_announces_target_open);
	RUN_TEST(test_fob_close_from_open_announces_target_closed);
	RUN_TEST(test_coming_out_of_stopped_by_fob_goes_straight_to_the_end_state);
	RUN_TEST(test_no_top_sensor_open_times_out_to_open);
	RUN_TEST(test_no_top_sensor_fob_close_is_reported_on_arrival);
	RUN_TEST(test_no_top_sensor_never_reports_stopped);
	RUN_TEST(test_movement_timer_survives_millis_wrapping);
	RUN_TEST(test_debouncer_trusts_the_first_reading);
	RUN_TEST(test_debouncer_ignores_a_short_glitch);
	RUN_TEST(test_debouncer_accepts_a_change_held_long_enough);
	RUN_TEST(test_commands_ignored_just_after_subscribing);
	RUN_TEST(test_command_guard_survives_millis_wrapping);
	return UNITY_END();
}
