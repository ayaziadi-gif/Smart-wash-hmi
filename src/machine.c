#include "machine.h"
#include "lvgl/lvgl.h"

#define MIN_TEMP       30
#define MAX_TEMP       90

#define MIN_DURATION   5
#define MAX_DURATION   60

#define STEP_TIME_MS   1000

static machine_state_t state;
static program_t program;
static machine_mode_t mode;

static int target_temperature;
static int current_temperature;

static int duration;
static int elapsed_time;
static int progress;
static int total_time;

static int phase_time;

static uint32_t last_update;

/* --------------------------------------------------------- */

static int heating_time(void)
{
    int delta = target_temperature - 20;

    if (delta <= 0)
        return 1;

    return (delta + 1) / 2;
}

/* --------------------------------------------------------- */

static int calculate_total_time(void)
{
    switch (program)
    {
        case PROGRAM_LAVAGE:
            return 3 + heating_time() + duration + 5 + 5;

        case PROGRAM_RINCAGE:
            return 3 + 5;

        case PROGRAM_ESSORAGE:
            return 5;

        default:
            return duration;
    }
}

/* --------------------------------------------------------- */

void machine_init(void)
{
    state = MACHINE_READY;

    program = PROGRAM_LAVAGE;
    mode = MACHINE_AUTO;

    target_temperature = 60;
    current_temperature = 20;

    duration = 30;

    elapsed_time = 0;
    progress = 0;

    phase_time = 0;

    total_time = calculate_total_time();

    last_update = lv_tick_get();
}

/* --------------------------------------------------------- */

void machine_start(void)
{
    if (mode != MACHINE_AUTO)
        return;

    if (state == MACHINE_READY ||
        state == MACHINE_STOPPED ||
        state == MACHINE_FINISHED)
    {
        current_temperature = 20;

        elapsed_time = 0;
        progress = 0;
        phase_time = 0;

        total_time = calculate_total_time();

        last_update = lv_tick_get();

        if (program == PROGRAM_ESSORAGE)
            state = MACHINE_SPINNING;
        else
            state = MACHINE_FILLING;
    }
}

/* --------------------------------------------------------- */

void machine_stop(void)
{
    state = MACHINE_STOPPED;

    elapsed_time = 0;
    progress = 0;
    phase_time = 0;
}

/* --------------------------------------------------------- */

void machine_emergency_stop(void)
{
    state = MACHINE_EMERGENCY;

    elapsed_time = 0;
    progress = 0;
    phase_time = 0;
}

/* --------------------------------------------------------- */

void machine_reset(void)
{
    state = MACHINE_READY;

    current_temperature = 20;

    elapsed_time = 0;
    progress = 0;
    phase_time = 0;

    total_time = calculate_total_time();

    last_update = lv_tick_get();
}

/* --------------------------------------------------------- */

void machine_update(void)
{
    if (mode != MACHINE_AUTO)
        return;

    if (state == MACHINE_READY ||
        state == MACHINE_STOPPED ||
        state == MACHINE_FINISHED ||
        state == MACHINE_EMERGENCY)
        return;

    uint32_t now = lv_tick_get();

    if ((now - last_update) < STEP_TIME_MS)
        return;

    last_update = now;

    elapsed_time++;
    phase_time++;

    switch (state)
    {
        case MACHINE_FILLING:

            if (phase_time >= 3)
            {
                phase_time = 0;

                if (program == PROGRAM_RINCAGE)
                    state = MACHINE_RINSING;
                else
                    state = MACHINE_HEATING;
            }

            break;

        case MACHINE_HEATING:

            if (current_temperature < target_temperature)
                current_temperature += 2;

            if (current_temperature >= target_temperature)
            {
                current_temperature = target_temperature;
                phase_time = 0;
                state = MACHINE_WASHING;
            }

            break;

        case MACHINE_WASHING:

            if (phase_time >= duration)
            {
                phase_time = 0;
                state = MACHINE_RINSING;
            }

            break;

        case MACHINE_RINSING:

            if (phase_time >= 5)
            {
                phase_time = 0;
                state = MACHINE_SPINNING;
            }

            break;

        case MACHINE_SPINNING:

            if (phase_time >= 5)
            {
                phase_time = 0;
                progress = 100;
                state = MACHINE_FINISHED;
            }

            break;

        default:
            break;
    }

    if (state != MACHINE_FINISHED)
    {
        progress = (elapsed_time * 100) / total_time;

        if (progress > 99)
            progress = 99;
    }
}

/* --------------------------------------------------------- */

machine_state_t machine_get_state(void)
{
    return state;
}

program_t machine_get_program(void)
{
    return program;
}

machine_mode_t machine_get_mode(void)
{
    return mode;
}

int machine_get_temperature(void)
{
    return current_temperature;
}

int machine_get_target_temperature(void)
{
    return target_temperature;
}

int machine_get_duration(void)
{
    return duration;
}

int machine_get_elapsed_time(void)
{
    return elapsed_time;
}

int machine_get_progress(void)
{
    return progress;
}

int machine_get_total_time(void)
{
    return total_time;
}

/* --------------------------------------------------------- */

void machine_set_temperature(int temperature)
{
    if (temperature >= MIN_TEMP &&
        temperature <= MAX_TEMP)
    {
        target_temperature = temperature;
        total_time = calculate_total_time();
    }
}

/* --------------------------------------------------------- */

void machine_set_duration(int value)
{
    if (value >= MIN_DURATION &&
        value <= MAX_DURATION)
    {
        duration = value;
        total_time = calculate_total_time();
    }
}

/* --------------------------------------------------------- */

void machine_set_program(program_t value)
{
    program = value;
    total_time = calculate_total_time();
}

/* --------------------------------------------------------- */

void machine_set_mode(machine_mode_t value)
{
    mode = value;
}
