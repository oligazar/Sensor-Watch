/*
 * MIT License
 *
 * Copyright (c) 2022 Joey Castillo
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Sunrise/sunset calculations are public domain code by Paul Schlyter, December 1992
 *
 */

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "sunrise_sunset_face.h"
#include "watch.h"
#include "watch_utility.h"
#include "sunriset.h"

static void _sunrise_sunset_face_update(movement_settings_t *settings, sunrise_sunset_state_t *state) {
    char buf[14];
    double rise, set, minutes, seconds;
    bool show_next_match = false;
    movement_location_t movement_location = (movement_location_t) watch_get_backup_data(1);

    // some locations for testing, can remove eventually
    // NYC
    // movement_location.bit.latitude = 4073; movement_location.bit.longitude = -7393;

    // Seattle
    // movement_location.bit.latitude = 4760; movement_location.bit.longitude = -12233;

    // Pago Pago
    // movement_location.bit.latitude = -1428; movement_location.bit.longitude = -17070;

    // Fairbanks
    // movement_location.bit.latitude = 6484; movement_location.bit.longitude = -14765;

    // Auckland
    // movement_location.bit.latitude = -3685; movement_location.bit.longitude = 17478;

    if (movement_location.reg == 0) {
        watch_display_string("RI  no Loc", 0);
        return;
    }

    watch_date_time date_time = watch_rtc_get_date_time(); // the current local date / time
    watch_date_time utc_now = watch_utility_date_time_convert_zone(date_time, movement_timezone_offsets[settings->bit.time_zone] * 60, 0); // the current date / time in UTC
    watch_date_time scratch_time; // scratchpad, contains different values at different times
    scratch_time.reg = utc_now.reg;

    // Weird quirky unsigned things were happening when I tried to cast these directly to doubles below.
    // it looks redundant, but extracting them to local int16's seemed to fix it.
    int16_t lat_centi = (int16_t)movement_location.bit.latitude;
    int16_t lon_centi = (int16_t)movement_location.bit.longitude;

    double lat = (double)lat_centi / 100.0;
    double lon = (double)lon_centi / 100.0;

    // sunriset returns the rise/set times as signed decimal hours in UTC.
    // this can mean hours below 0 or above 31, which won't fit into a watch_date_time struct.
    // to deal with this, we set aside the offset in hours, and add it back before converting it to a watch_date_time.
    double hours_from_utc = ((double)movement_timezone_offsets[settings->bit.time_zone]) / 60.0;

    // we loop twice because if it's after sunset today, we need to recalculate to display values for tomorrow.
    for(int i = 0; i < 2; i++) {
        uint8_t result = sun_rise_set(scratch_time.unit.year + WATCH_RTC_REFERENCE_YEAR, scratch_time.unit.month, scratch_time.unit.day, lon, lat, &rise, &set);

        if (result != 0) {
            watch_clear_colon();
            watch_clear_indicator(WATCH_INDICATOR_PM);
            watch_clear_indicator(WATCH_INDICATOR_24H);
            sprintf(buf, "%s%d none ", (result == 1) ? "SE" : "rI", scratch_time.unit.day);
            watch_display_string(buf, 0);
            return;
        }

        watch_set_colon();
        if (settings->bit.clock_mode_24h) watch_set_indicator(WATCH_INDICATOR_24H);

        rise += hours_from_utc;
        set += hours_from_utc;

        minutes = 60.0 * fmod(rise, 1);
        seconds = 60.0 * fmod(minutes, 1);
        scratch_time.unit.hour = floor(rise);
        if (seconds < 30) scratch_time.unit.minute = floor(minutes);
        else scratch_time.unit.minute = ceil(minutes);

        if (date_time.reg < scratch_time.reg || show_next_match) {
            if (state->rise_index == 0 || show_next_match) {
                if (!settings->bit.clock_mode_24h) {
                    if (watch_utility_convert_to_12_hour(&scratch_time)) watch_set_indicator(WATCH_INDICATOR_PM);
                    else watch_clear_indicator(WATCH_INDICATOR_PM);
                }
                sprintf(buf, "rI%2d%2d%02d  ", scratch_time.unit.day, scratch_time.unit.hour, scratch_time.unit.minute);
                watch_display_string(buf, 0);
                return;
            } else {
                show_next_match = true;
            }
        }

        minutes = 60.0 * fmod(set, 1);
        seconds = 60.0 * fmod(minutes, 1);
        scratch_time.unit.hour = floor(set);
        if (seconds < 30) scratch_time.unit.minute = floor(minutes);
        else scratch_time.unit.minute = ceil(minutes);

        if (date_time.reg < scratch_time.reg || show_next_match) {
            if (state->rise_index == 0 || show_next_match) {
                if (!settings->bit.clock_mode_24h) {
                    if (watch_utility_convert_to_12_hour(&scratch_time)) watch_set_indicator(WATCH_INDICATOR_PM);
                    else watch_clear_indicator(WATCH_INDICATOR_PM);
                }
                sprintf(buf, "SE%2d%2d%02d  ", scratch_time.unit.day, scratch_time.unit.hour, scratch_time.unit.minute);
                watch_display_string(buf, 0);
                return;
            } else {
                show_next_match = true;
            }
        }

        // it's after sunset. we need to display sunrise/sunset for tomorrow.
        uint32_t timestamp = watch_utility_date_time_to_unix_time(utc_now, 0);
        timestamp += 86400;
        scratch_time = watch_utility_date_time_from_unix_time(timestamp, 0);
    }
}

static int16_t _sunrise_sunset_face_latlon_from_struct(sunrise_sunset_lat_lon_settings_t val) {
    int16_t retval = (val.sign ? -1 : 1) *
                        (
                            val.hundreds * 10000 +
                            val.tens * 1000 +
                            val.ones * 100 +
                            val.tenths * 10 +
                            val.hundredths
                        );
    return retval;
}

static sunrise_sunset_lat_lon_settings_t _sunrise_sunset_face_struct_from_latlon(int16_t val) {
    sunrise_sunset_lat_lon_settings_t retval;

    retval.sign = val < 0;
    val = abs(val);
    retval.hundredths = val % 10;
    val /= 10;
    retval.tenths = val % 10;
    val /= 10;
    retval.ones = val % 10;
    val /= 10;
    retval.tens = val % 10;
    val /= 10;
    retval.hundreds = val % 10;

    return retval;
}

static void _sunrise_sunset_face_update_location_register(sunrise_sunset_state_t *state) {
    if (state->location_changed) {
        movement_location_t movement_location;
        int16_t lat = _sunrise_sunset_face_latlon_from_struct(state->working_latitude);
        int16_t lon = _sunrise_sunset_face_latlon_from_struct(state->working_longitude);
        movement_location.bit.latitude = lat;
        movement_location.bit.longitude = lon;
        watch_store_backup_data(movement_location.reg, 1);
        state->location_changed = false;
    }
}

static void _sunrise_sunset_face_update_settings_display(movement_event_t event, sunrise_sunset_state_t *state) {
    char buf[12];

    switch (state->page) {
        case 1:
            sprintf(buf, "LA  %c %04d", state->working_latitude.sign ? '-' : 'F', abs(_sunrise_sunset_face_latlon_from_struct(state->working_latitude))); // F looks sorta like a plus sign in position 1
            break;
        case 2:
            sprintf(buf, "LO  %c%05d", state->working_longitude.sign ? '-' : 'F', abs(_sunrise_sunset_face_latlon_from_struct(state->working_longitude)));
            break;
    }
    if (event.subsecond % 2) {
        buf[state->active_digit + 4] = ' ';
    }
    watch_display_string(buf, 0);
}

static void _sunrise_sunset_face_advance_digit(sunrise_sunset_state_t *state) {
    state->location_changed = true;
    switch (state->page) {
        case 1: // latitude
            switch (state->active_digit) {
                case 0:
                    state->working_latitude.sign++;
                    break;
                case 1:
                    // we skip this digit
                    break;
                case 2:
                    state->working_latitude.tens = (state->working_latitude.tens + 1) % 10;
                    if (abs(_sunrise_sunset_face_latlon_from_struct(state->working_latitude)) > 9000) {
                        // prevent latitude from going over ±90.
                        // TODO: perform these checks when advancing the digit?
                        state->working_latitude.ones = 0;
                        state->working_latitude.tenths = 0;
                        state->working_latitude.hundredths = 0;
                    }
                    break;
                case 3:
                    state->working_latitude.ones = (state->working_latitude.ones + 1) % 10;
                    if (abs(_sunrise_sunset_face_latlon_from_struct(state->working_latitude)) > 9000) state->working_latitude.ones = 0;
                    break;
                case 4:
                    state->working_latitude.tenths = (state->working_latitude.tenths + 1) % 10;
                    if (abs(_sunrise_sunset_face_latlon_from_struct(state->working_latitude)) > 9000) state->working_latitude.tenths = 0;
                    break;
                case 5:
                    state->working_latitude.hundredths = (state->working_latitude.hundredths + 1) % 10;
                    if (abs(_sunrise_sunset_face_latlon_from_struct(state->working_latitude)) > 9000) state->working_latitude.hundredths = 0;
                    break;
            }
            break;
        case 2: // longitude
            switch (state->active_digit) {
                case 0:
                    state->working_longitude.sign++;
                    break;
                case 1:
                    state->working_longitude.hundreds = (state->working_longitude.hundreds + 1) % 10;
                    if (abs(_sunrise_sunset_face_latlon_from_struct(state->working_longitude)) > 18000) {
                        // prevent longitude from going over ±180
                        state->working_longitude.tens = 8;
                        state->working_longitude.ones = 0;
                        state->working_longitude.tenths = 0;
                        state->working_longitude.hundredths = 0;
                    }
                    break;
                case 2:
                    state->working_longitude.tens = (state->working_longitude.tens + 1) % 10;
                    if (abs(_sunrise_sunset_face_latlon_from_struct(state->working_longitude)) > 18000) state->working_longitude.tens = 0;
                    break;
                case 3:
                    state->working_longitude.ones = (state->working_longitude.ones + 1) % 10;
                    if (abs(_sunrise_sunset_face_latlon_from_struct(state->working_longitude)) > 18000) state->working_longitude.ones = 0;
                    break;
                case 4:
                    state->working_longitude.tenths = (state->working_longitude.tenths + 1) % 10;
                    if (abs(_sunrise_sunset_face_latlon_from_struct(state->working_longitude)) > 18000) state->working_longitude.tenths = 0;
                    break;
                case 5:
                    state->working_longitude.hundredths = (state->working_longitude.hundredths + 1) % 10;
                    if (abs(_sunrise_sunset_face_latlon_from_struct(state->working_longitude)) > 18000) state->working_longitude.hundredths = 0;
                    break;
            }
            break;
    }
}

void sunrise_sunset_face_setup(movement_settings_t *settings, uint8_t watch_face_index, void ** context_ptr) {
    (void) settings;
    (void) watch_face_index;
    if (*context_ptr == NULL) {
        *context_ptr = malloc(sizeof(sunrise_sunset_state_t));
        memset(*context_ptr, 0, sizeof(sunrise_sunset_state_t));
    }
}

void sunrise_sunset_face_activate(movement_settings_t *settings, void *context) {
    (void) settings;
    sunrise_sunset_state_t *state = (sunrise_sunset_state_t *)context;
    movement_location_t movement_location = (movement_location_t) watch_get_backup_data(1);
    state->working_latitude = _sunrise_sunset_face_struct_from_latlon(movement_location.bit.latitude);
    state->working_longitude = _sunrise_sunset_face_struct_from_latlon(movement_location.bit.longitude);
}

bool sunrise_sunset_face_loop(movement_event_t event, movement_settings_t *settings, void *context) {
    sunrise_sunset_state_t *state = (sunrise_sunset_state_t *)context;

    switch (event.event_type) {
        case EVENT_ACTIVATE:
            _sunrise_sunset_face_update(settings, state);
            break;
        case EVENT_LOW_ENERGY_UPDATE:
        case EVENT_TICK:
            if (state->page) _sunrise_sunset_face_update_settings_display(event, state);
            break;
        case EVENT_MODE_BUTTON_UP:
            movement_move_to_next_face();
            break;
        case EVENT_LIGHT_BUTTON_DOWN:
            if (state->page) {
                state->active_digit++;
                if (state->page == 1 && state->active_digit == 1) state->active_digit++; // max latitude is +- 90, no hundreds place
                if (state->active_digit > 5) {
                    state->active_digit = 0;
                    state->page = (state->page + 1) % 3;
                    _sunrise_sunset_face_update_location_register(state);
                }
                _sunrise_sunset_face_update_settings_display(event, context);
            } else {
                movement_illuminate_led();
            }
            if (state->page == 0) {
                movement_request_tick_frequency(1);
                _sunrise_sunset_face_update(settings, state);
            }
            break;
        case EVENT_LIGHT_BUTTON_UP:
            break;
        case EVENT_ALARM_BUTTON_UP:
            if (state->page) {
                _sunrise_sunset_face_advance_digit(state);
                _sunrise_sunset_face_update_settings_display(event, context);
            } else {
                state->rise_index = (state->rise_index + 1) % 2;
                _sunrise_sunset_face_update(settings, state);
            }
            break;
        case EVENT_ALARM_LONG_PRESS:
            if (state->page == 0) {
                state->page++;
                watch_clear_display();
                movement_request_tick_frequency(4);
                _sunrise_sunset_face_update_settings_display(event, context);
            }
            break;
        case EVENT_TIMEOUT:
            if (state->page != 0) {
                movement_move_to_face(0);
            }
        default:
            break;
    }

    return true;
}

void sunrise_sunset_face_resign(movement_settings_t *settings, void *context) {
    (void) settings;
    sunrise_sunset_state_t *state = (sunrise_sunset_state_t *)context;
    state->page = 0;
    state->active_digit = 0;
    state->rise_index = 0;
    _sunrise_sunset_face_update_location_register(state);
}