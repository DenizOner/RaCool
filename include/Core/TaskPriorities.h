#ifndef RACOOL_TASK_PRIORITIES_H
#define RACOOL_TASK_PRIORITIES_H

/* Sensor acquisition has a bounded bus wait and feeds thermal safety decisions.
 * It must preempt visual work when a shared bus mutex is released. Otherwise a
 * higher-priority renderer can reacquire that mutex ahead of a waiting sensor.
 * Both workers block between operations; these priorities do not replace locks. */
#define RACOOL_TASK_PRIORITY_THERMAL_SENSOR 6U
#define RACOOL_TASK_PRIORITY_DISPLAY 5U
#define RACOOL_TASK_PRIORITY_UI 5U

#endif
