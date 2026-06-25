#ifndef __OPMODE_TASK_H__
#define __OPMODE_TASK_H__


typedef enum {
    OP_MODE_NORMAL = 0,
    OP_MODE_NIGHT,
    OP_MODE_SMART,
    OP_MODE_SLEEP,
    OP_MODE_TEST
} op_mode_e;


void opmode_task_init(void);
void Opmode_Set(void);
void Opmode_test_mode(void);


#endif