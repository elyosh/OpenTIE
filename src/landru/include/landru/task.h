#ifndef LANDRU_TASK_H
#define LANDRU_TASK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define LANDRU_TASK_STACK_DEPTH 8
#define LANDRU_TASK_STORAGE_BYTES 256

typedef enum LandruTaskStepResult {
	LANDRU_TASK_STEP_CONTINUE,
	LANDRU_TASK_STEP_DONE,
	LANDRU_TASK_STEP_YIELD,
	/* A timed engine frame completed. Return control to the host before
	 * servicing another one, even when its deadline is already overdue. */
	LANDRU_TASK_STEP_FRAME_COMPLETE,
} LandruTaskStepResult;

typedef struct LandruTaskVtable {
	LandruTaskStepResult (*step)(void* self);
	void (*end)(void* self);
	uint64_t (*next_wake_delay_us)(const void* self);
} LandruTaskVtable;

typedef struct LandruTaskFrame {
	const LandruTaskVtable* vtable;
	uint8_t storage[LANDRU_TASK_STORAGE_BYTES];
} LandruTaskFrame;

void* landru_task_push(const LandruTaskVtable* vtable);
void* landru_task_top(void);
void landru_task_pop(void);
bool landru_task_stack_empty(void);
int landru_task_depth(void);
LandruTaskStepResult landru_task_step_once(void);
void landru_task_run_frame(void);
uint64_t landru_task_next_wake_delay_us(void);
void landru_task_clear_all(void);

#endif
