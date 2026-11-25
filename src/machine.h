#ifndef MACHINE_H
#define MACHINE_H

#include <stdint.h>

typedef enum
{
    MACHINE_READY,
    MACHINE_FILLING,
    MACHINE_HEATING,
    MACHINE_WASHING,
    MACHINE_RINSING,
    MACHINE_SPINNING,
    MACHINE_FINISHED,
    MACHINE_STOPPED,
    MACHINE_EMERGENCY
} machine_state_t;

typedef enum
{
    PROGRAM_LAVAGE,
    PROGRAM_RINCAGE,
    PROGRAM_ESSORAGE
} program_t;

typedef enum
{
    MACHINE_AUTO,
    MACHINE_MANUAL
} machine_mode_t;

void machine_init(void);

void machine_start(void);
void machine_stop(void);
void machine_emergency_stop(void);
void machine_reset(void);

void machine_update(void);

machine_state_t machine_get_state(void);
program_t machine_get_program(void);
machine_mode_t machine_get_mode(void);

int machine_get_temperature(void);
int machine_get_target_temperature(void);
int machine_get_duration(void);
int machine_get_elapsed_time(void);
int machine_get_progress(void);
int machine_get_total_time(void);

void machine_set_temperature(int temperature);
void machine_set_duration(int duration);
void machine_set_program(program_t program);
void machine_set_mode(machine_mode_t mode);

#endif
