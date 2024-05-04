/*
 * MIT License
 *
 * Copyright (c) 2022 Wesley Ellis
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
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFtomato_ringEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <stdlib.h>
#include <string.h>
#include "tomato_face.h"
#include "watch_utility.h"

static uint8_t focus_min = 1; // 25;
static uint8_t break_min = 1; // 5;
static uint8_t long_break_min = 1; // 20;
static uint8_t rounds = 2; // 4;

static inline int32_t get_tz_offset(movement_settings_t *settings) {
    return movement_timezone_offsets[settings->bit.time_zone] * 60;
}

static uint8_t get_length(tomato_state_t *state) {
    switch (state->phase) {
        case tomato_break:
            return break_min;
        case tomato_long_break:
            return long_break_min;
        default:
            return focus_min;
    }
}

static void _tomato_start(tomato_state_t *state, movement_settings_t *settings) {
    watch_date_time now = watch_rtc_get_date_time();
    int8_t length = (int8_t) get_length(state);

    if (state->phase == tomato_focus) {
        state->count++;
    }
    state->is_started = true;
    state->now_ts = watch_utility_date_time_to_unix_time(now, get_tz_offset(settings));
    state->target_ts = watch_utility_offset_timestamp(state->now_ts, 0, length, 0);
    watch_date_time target_dt = watch_utility_date_time_from_unix_time(state->target_ts, get_tz_offset(settings));
    movement_schedule_background_task(target_dt);
    watch_set_indicator(WATCH_INDICATOR_BELL);
}

static void _tomato_pause(tomato_state_t *state) {
    state->is_paused = true;
    state->remainder = state->target_ts - state->now_ts;
    // printf("pause, remainder: %d\n", state->remainder);
    movement_cancel_background_task();
    // watch_clear_indicator(WATCH_INDICATOR_BELL);
}

static void _tomato_resume(tomato_state_t *state, movement_settings_t *settings) {
    // printf("unpause, remainder: %d\n", state->remainder);
    watch_date_time now = watch_rtc_get_date_time();
    state->is_paused = false;
    state->now_ts = watch_utility_date_time_to_unix_time(now, get_tz_offset(settings));
    div_t result = div(state->remainder, 60);
    state->target_ts = watch_utility_offset_timestamp(state->now_ts, 0, result.quot, result.rem);
    // printf("target_ts: %d\n", state->target_ts);
    // printf("now_ts: %d\n", state->now_ts);
    watch_date_time target_dt = watch_utility_date_time_from_unix_time(state->target_ts, get_tz_offset(settings));
    movement_schedule_background_task(target_dt);
    // watch_set_indicator(WATCH_INDICATOR_BELL);
}

static int _rounds(int num) {
    if (num == 0) return 0;
    int result = num % rounds;
    return result > 0 ? result : rounds;
}

static void _tomato_draw(tomato_state_t *state) {
    char buf[16];

    uint32_t delta;
    div_t result;
    uint8_t min = 0;
    uint8_t sec = 0;

    // char phase;
    // if (state->phase == tomato_break) {
    //     phase = 'b';
    // } else {
    //     phase = 'f';
    // }

    if (state->is_started) {
        if (state->is_paused) {
            result = div(state->remainder, 60);
            min = result.quot;
            sec = result.rem;
            // printf("remainder: %d\n", state->remainder);
            // printf("min: %d\n", min);
            // printf("sec: %d\n", sec);
        } else {
            delta = state->target_ts - state->now_ts;
            result = div(delta, 60);
            min = result.quot;
            sec = result.rem;
            // printf("target_ts: %d\n", state->target_ts);
            // printf("now_ts: %d\n", state->now_ts);
            // printf("delta: %d\n", delta);
            // printf("min: %d\n", min);
            // printf("sec: %d\n", sec);
        }
    } else {
        min = get_length(state);
        sec = 0;
    }

    char title[3];
    // printf("now_ts: %d\n", state->now_ts);
    uint8_t rest = state->now_ts%2;
    // printf("rest: %d\n", rest);
    if (state->is_paused && rest == 1) {
        strcpy(title, "PA");
    } else {
        switch(state->phase) {
            case tomato_focus:
                strcpy(title, "FO");
                break;
            case tomato_break:
                strcpy(title, "BR");
                break;
            case tomato_long_break:
                strcpy(title, "LB");
                break;
        }
    }
    
    if (state->is_visible) {
        sprintf(buf, "%2s%2d%2d%02d%2d", title, state->count, min, sec, _rounds(state->count));
        // printf("%s\n", buf);
        watch_display_string(buf, 0);
    }
}

static void _tomato_reset_state(tomato_state_t *state) {
    state->is_started = false;
    state->is_paused = false;
    movement_cancel_background_task();
    watch_clear_indicator(WATCH_INDICATOR_BELL);
}

static void tomato_ring(tomato_state_t *state, movement_settings_t *settings) {
    movement_play_signal();
    
    if (state->phase == tomato_focus) {
        if (_rounds(state->count) == rounds) {
            state->phase = tomato_long_break;
        } else {
            state->phase = tomato_break;
        }
    } else {
        state->phase = tomato_focus;
    }

    _tomato_reset_state(state);

    if (state->is_autorun) {
         _tomato_start(state, settings);
    } 
}

static void _set_autorun(tomato_state_t *state, bool autorun) {
    state->is_autorun = autorun;
    if (autorun) {
        watch_set_indicator(WATCH_INDICATOR_LAP);
    } else {
        watch_clear_indicator(WATCH_INDICATOR_LAP);
    }
}

void tomato_face_setup(movement_settings_t *settings, uint8_t watch_face_index, void ** context_ptr) {
    (void) settings;
    (void) watch_face_index;

    if (*context_ptr == NULL) {
        *context_ptr = malloc(sizeof(tomato_state_t));
        tomato_state_t *state = (tomato_state_t*)*context_ptr;
        memset(*context_ptr, 0, sizeof(tomato_state_t));
        state->is_started = false;
        state->is_paused = false;
        state->phase= tomato_focus;
        state->count = 0;
        state->is_visible = true;
        state->is_autorun = false;
    }
}

void tomato_face_activate(movement_settings_t *settings, void *context) {
    tomato_state_t *state = (tomato_state_t *)context;
    if (state->is_started) {
        watch_date_time now = watch_rtc_get_date_time();
        state->now_ts = watch_utility_date_time_to_unix_time(now, get_tz_offset(settings));
        watch_set_indicator(WATCH_INDICATOR_BELL);
    }
    watch_set_colon();
    state->is_visible = true;
}

bool tomato_face_loop(movement_event_t event, movement_settings_t *settings, void *context) {
    tomato_state_t *state = (tomato_state_t *)context;

    switch (event.event_type) {
        case EVENT_ACTIVATE:
            _tomato_draw(state);
            break;
        case EVENT_TICK:
            state->now_ts++;
            _tomato_draw(state);
            break;
        case EVENT_LIGHT_BUTTON_DOWN:
            movement_illuminate_led();
            if (!state->is_started) {
                switch(state->phase) {
                    case tomato_focus:
                        state->phase = tomato_break;
                        break;
                    case tomato_break:
                        state->phase = tomato_long_break;
                        break;
                    case tomato_long_break:
                        state->phase = tomato_focus;
                        break;
                }
            }
            _tomato_draw(state);
            break;
        case EVENT_LIGHT_LONG_PRESS:
            if (!state->is_started) {
                state->count = 0;
            }
            break;
        case EVENT_ALARM_BUTTON_UP:
            if (state->is_started) {
                if (state->is_paused) {
                    _tomato_resume(state, settings);
                } else {
                    _tomato_pause(state);
                }
            } else {
                // TODO: cycle through presets
            }
            _tomato_draw(state);
            break;
        case EVENT_ALARM_LONG_PRESS:
            if (state->is_started) {
                if (state->is_paused) {
                    if (state->phase == tomato_focus) {
                        state->count--;
                    }
                    _tomato_reset_state(state);
                } else {
                    if (state->is_autorun) {
                        _set_autorun(state, false);
                    } else {
                        _set_autorun(state, true);
                    }
                } 
            } else {
                _tomato_start(state, settings);
            }
            _tomato_draw(state);
            break;
        case EVENT_BACKGROUND_TASK:
            tomato_ring(state, settings);
            _tomato_draw(state);
            break;
        case EVENT_TIMEOUT:
            movement_move_to_face(0);
            break;
        default:
            movement_default_loop_handler(event, settings);
            break;
    }

    return true;
}

void tomato_face_resign(movement_settings_t *settings, void *context) {
    tomato_state_t *state = (tomato_state_t *)context;
    state->is_visible = false;
    (void) settings;
    (void) context;
}

