#include "qc_cli.h"
#include "FreeRTOS_CLI.h"
#include "esp_log.h"
#include "app_led.h"
#include "app_adc.h"
#include "app_HX711.h"
#include "app_TOF.h"
#include "app_config_flash.h"

void ir_calibration_enable(int dest);
void QC_mode(char cRxedChar)
{
    static uint8_t len_color[4] = {0};

    int left; 
    int right;
    app_config_t* app_config = get_app_config();


    switch(cRxedChar)
    {
        case '1':
        case '2':       
        case '3':
        case '4':
        {
            // '1'은 아스키코드 0x31과 동일하므로 cRxedChar - '1'을 통해 0~3 인덱스를 구합니다.
            uint8_t target_index = cRxedChar - '1'; 
            
            for (int i = 0; i < 4; i++) 
            {
                if (i == target_index) 
                {
					switch(len_color[i])
					{
						case 0 : 
                            APP_String_printf("LED %c r\r\n",cRxedChar);
                            set_rgb_led_for_number(i,255,0,0,0);
						break;
						case 1 : 
                            APP_String_printf("LED %c g\r\n",cRxedChar);
                            set_rgb_led_for_number(i,0,255,0,0);
						break;
						case 2 : 
                            APP_String_printf("LED %c b\r\n",cRxedChar);
                            set_rgb_led_for_number(i,0,0,255,0);
						break;
						case 3 : 
                            APP_String_printf("LED %c w\r\n",cRxedChar);
                            set_rgb_led_for_number(i,0,0,0,255);
						break;		
						case 4 : 
                            APP_String_printf("LED %c off\r\n",cRxedChar);
                            set_rgb_led_for_number(i,0,0,0,0);
						break;		                        																
					}
                    len_color[i]++; // 타겟 위치는 1 증가	
                    if(len_color[i] > 4)
                        len_color[i] = 0;			
                } 
                else 
                {
                    len_color[i] = 0; // 나머지 위치는 0으로 초기화
                    set_rgb_led_for_number(i,0,0,0,0);
                }
				
            }
            

            break;
        }
        case '5':
        {
            set_rgb_len_no_Breathing(0,0,0,0);
            currunt_ir_value(&left,&right);

            if (left > app_config->tof_sense_threshold_l && right > app_config->tof_sense_threshold_r) {
                set_rgb_len_no_Breathing(0,0,0,255);
            }
            else if (left > app_config->tof_sense_threshold_l) {
                set_rgb_len_no_Breathing(0,255,0,0);
            }
            else if( right > app_config->tof_sense_threshold_r)
            {
                set_rgb_len_no_Breathing(0,0,255,0);
            }
            else {
                set_rgb_len_no_Breathing(255,0,0,0);
            }                

                
            APP_String_printf("LEFT = %d RIGHT = %d \r\n",left, right);
            break;
        }
        case '6':
        {

            APP_String_printf("load cell %.2fg\r\n",loadcell_data_get());
            break;
        }   
        case '7':
        {
            APP_String_printf("motor adc %d\r\n",GetMotor_adc());
            break;
        }

        case 'z':
        {
            HX711_cal_init(1);
            APP_String_printf("load cell zero setting start\r\n");
            break;
        }   
        case 'x':
        {
            HX711_cal_init(2480);
            APP_String_printf("IR CAL LEFT start 2480g\r\n");
            break;
        }  
        case 'a':
        {
            ir_calibration_enable(0x03);
            APP_String_printf("IR CAL ALL setting start\r\n");
            break;
        }            
        case 's':
        {
            ir_calibration_enable(0x02);
            APP_String_printf("IR CAL LEFT setting start\r\n");
            break;
        }      
        case 'd':
        {
            ir_calibration_enable(0x01);
            APP_String_printf("IR CAL RIGHT setting start\r\n");
            break;
        }                                                    
    }
}



