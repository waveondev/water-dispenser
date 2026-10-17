#ifndef __APP_BUTTON_H__
#define __APP_BUTTON_H__

bool get_factory_flag(void);
bool button_task_init(void);


void bf_LongPress10SecAction(void);
void bf_LongPress5SecAction(void) ;
void bf_LongPress3SecAction(void) ;
void bf_DoubleClickAction(void) ;
void bf_SingleClickAction(void);
int button_press_state(void);
#endif