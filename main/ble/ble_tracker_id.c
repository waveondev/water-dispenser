#include "ble_tracker_id.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "string.h"


typedef struct{
    char Device_ID[32];
    uint32_t Device_Time;
}Tracker_Device_t;

#define TRACKER_DEVICE_MAX 20
Tracker_Device_t* Tracker_Device[TRACKER_DEVICE_MAX];



void Tracker_In_New_ID(char* Tracker_ID)
{
    int i=0;
    for(i = 0; i < TRACKER_DEVICE_MAX; i++)
    {
        if(Tracker_Device[i] == NULL)
            break;
    }
    Tracker_Device[i] = malloc(sizeof(Tracker_Device_t));
    
    if(Tracker_Device[i] == NULL)
        return;
    memcpy(Tracker_Device[i]->Device_ID,Tracker_ID,strlen(Tracker_ID));
    

}










