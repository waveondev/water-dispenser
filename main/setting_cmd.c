#include "setting_cmd.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "app_config_flash.h"
#include "app_HX711.h"
#include "app_adc.h"
#include "debug_cli.h"
#include "app_led.h"
#include "opmode_task.h"
#include "gpio_util.h"
static const char *TAG = __FILE__;
#if 0
typedef enum
{
  HX711_Case_cal    = 0,
  HX711_Scale_cal   = 1,
  HX711_GetValue    = 2,
}Setting_cmd_e;
#endif

static Motion_Packet_t Motion_Packet_buf;
void Setting_callback(Motion_Packet_t* Motion_Packet)
{
    app_config_t* app_config = get_app_config();
    memcpy(&Motion_Packet_buf,Motion_Packet,sizeof(Motion_Packet_t));
    switch(Motion_Packet->setting_req.cmd_type)
    {
        case HX711_Case_cal:
            HX711_cal_init(Motion_Packet->setting_req.hx711_case_cal.status? 1 : 0);
        break;
        case HX711_Scale_cal:
            HX711_cal_init(Motion_Packet->setting_req.hx711_weight_cal.cal_weight);    
        break;
        case HX711_GetValue:
            Motion_Packet_buf.event_code = SETTING_RESPONSE;
            Motion_Packet_buf.setting_res.cmd_type = HX711_GetValue;
            Motion_Packet_buf.setting_res.hx711_getvalue.current_weight = (uint32_t)(loadcell_data_get() * 100.0f);   
            ble_send_data_to_queue(NULL,(uint8_t*)&Motion_Packet_buf,sizeof(Motion_Packet_t));        
        break;        
        case IR_GetValue:
            Motion_Packet_buf.event_code = SETTING_RESPONSE;
            Motion_Packet_buf.setting_res.cmd_type = IR_GetValue;
            Motion_Packet_buf.setting_res.IR_getvalue.Getmotor_adc = GetMotor_adc();
            Motion_Packet_buf.setting_res.IR_getvalue.current_ir_left = GetIR_LEFT();
            Motion_Packet_buf.setting_res.IR_getvalue.current_ir_right = GetIR_RIGHT();
            Motion_Packet_buf.setting_res.IR_getvalue.center_ir_left = app_config->tof_sense_threshold_l;
            Motion_Packet_buf.setting_res.IR_getvalue.center_ir_right =  app_config->tof_sense_threshold_r;                                
            //sprintf((char*)Motion_Packet_buf.setting_res.data,"%04d %04d %04d",GetMotor_adc(), GetIR_LEFT(),GetIR_RIGHT());
            ble_send_data_to_queue(NULL,(uint8_t*)&Motion_Packet_buf,sizeof(Motion_Packet_t));
        break;        
        case LED_SetValue:
            Motion_Packet_buf.event_code = SETTING_RESPONSE;
            Motion_Packet_buf.setting_res.cmd_type = LED_SetValue;
            DBG_Resister_t *DBG_Resister = Debug_Get();
            DBG_Resister->led = 1;

            // 0번 비트부터 7번 비트까지 오른쪽에서 왼쪽 방향으로 순회
            for (int i = 0; i < 8; i++) {
                // i번째 비트가 1인지 체크
                if ((Motion_Packet->setting_req.led_setvalue.index_bit >> i) & 1) {
                    printf("%d번째 비트는 1입니다.\n", i);
                    set_rgb_led_for_number(i
                                    ,Motion_Packet->setting_req.led_setvalue.red
                                    ,Motion_Packet->setting_req.led_setvalue.green
                                    ,Motion_Packet->setting_req.led_setvalue.blue
                                    ,Motion_Packet->setting_req.led_setvalue.white);
                } else {
                    printf("%d번째 비트는 0입니다.\n", i);
                }
            }            


            ble_send_data_to_queue(NULL,(uint8_t*)&Motion_Packet_buf,sizeof(Motion_Packet_t));        
        break;              
        case MOTOR_SetValue:
            Motion_Packet_buf.event_code = SETTING_RESPONSE;
            Motion_Packet_buf.setting_res.cmd_type = MOTOR_SetValue;

            esp_err_t TPL0401_SetValue(int value);
            TPL0401_SetValue(Motion_Packet->setting_req.motor_set_value.value);
            //set_motor_speed(&i);

            ble_send_data_to_queue(NULL,(uint8_t*)&Motion_Packet_buf,sizeof(Motion_Packet_t));        
        break;    
        case IR_SetValue:
            Motion_Packet_buf.event_code = SETTING_RESPONSE;
            Motion_Packet_buf.setting_res.cmd_type = IR_SetValue;
            app_config->tof_sense_threshold_l = Motion_Packet->setting_req.IR_set_value.IR_Left;
            app_config->tof_sense_threshold_r = Motion_Packet->setting_req.IR_set_value.IR_Right;         

            ble_send_data_to_queue(NULL,(uint8_t*)&Motion_Packet_buf,sizeof(Motion_Packet_t));        
        break;      
        case Beacon_GetValue:
            Motion_Packet_buf.event_code = SETTING_RESPONSE;
            Motion_Packet_buf.setting_res.cmd_type = Beacon_GetValue;
            Motion_Packet_buf.setting_res.beacon_get_value.beacon_rssi = app_config->gate_way_rssi_th;
            ble_send_data_to_queue(NULL,(uint8_t*)&Motion_Packet_buf,sizeof(Motion_Packet_t));        
        break;      
        case Beacon_SetValue:
            Motion_Packet_buf.event_code = SETTING_RESPONSE;
            Motion_Packet_buf.setting_res.cmd_type = Beacon_SetValue;
            app_config->gate_way_rssi_th = Motion_Packet->setting_req.beacon_set_value.beacon_rssi;
            ble_send_data_to_queue(NULL,(uint8_t*)&Motion_Packet_buf,sizeof(Motion_Packet_t));        
        break;              
    }
}


void Setting_Enable(Setting_cmd_e cmd)
{
    Motion_Packet_t Motion_Packet;

    memcpy(&Motion_Packet,&Motion_Packet_buf,sizeof(Motion_Packet_t));
    Motion_Packet.event_code = SETTING_RESPONSE;
    Motion_Packet.setting_res.cmd_type = cmd;
 
    ble_send_data_to_queue(NULL,(uint8_t*)&Motion_Packet,sizeof(Motion_Packet_t));   

}



