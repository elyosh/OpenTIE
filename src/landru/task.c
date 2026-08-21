#include <landru/task.h>

static LandruTaskFrame s_stack[LANDRU_TASK_STACK_DEPTH];
static int s_top;

void* landru_task_push(const LandruTaskVtable* vtable) {
	if (!vtable || !vtable->step || s_top >= LANDRU_TASK_STACK_DEPTH)
		return NULL;
	LandruTaskFrame* frame = &s_stack[s_top++];
	frame->vtable = vtable;
	return frame->storage;
}

void* landru_task_top(void) { return s_top > 0 ? s_stack[s_top - 1].storage : NULL; }

void landru_task_pop(void) {
	if (s_top == 0)
		return;
	LandruTaskFrame* frame = &s_stack[--s_top];
	if (frame->vtable && frame->vtable->end)
		frame->vtable->end(frame->storage);
	frame->vtable = NULL;
}

bool landru_task_stack_empty(void) { return s_top == 0; }

int landru_task_depth(void) { return s_top; }

LandruTaskStepResult landru_task_step_once(void) {
	if (s_top == 0)
		return LANDRU_TASK_STEP_DONE;
	LandruTaskFrame* frame = &s_stack[s_top - 1];
	LandruTaskStepResult result = frame->vtable->step(frame->storage);
	if (result == LANDRU_TASK_STEP_DONE)
		landru_task_pop();
	return result;
}

void landru_task_run_frame(void) {
	int budget = 64;
	while (budget-- > 0 && s_top > 0) {
		LandruTaskStepResult result = landru_task_step_once();
		if (result == LANDRU_TASK_STEP_YIELD || result == LANDRU_TASK_STEP_FRAME_COMPLETE)
			return;
	}
}

uint64_t landru_task_next_wake_delay_us(void) {
	if (s_top == 0)
		return UINT64_MAX;
	const LandruTaskFrame* frame = &s_stack[s_top - 1];
	if (!frame->vtable || !frame->vtable->next_wake_delay_us)
		return UINT64_MAX;
	return frame->vtable->next_wake_delay_us(frame->storage);
}

void landru_task_clear_all(void) {
	while (s_top > 0)
		landru_task_pop();
}
