#pragma once

#include <stddef.h>
#include "task.h"

void scheduler_init();
void scheduler_start();
void scheduler_tick();
void add_task(task_t* task);
void switch_task(task_t* old_task, task_t* new_task);
