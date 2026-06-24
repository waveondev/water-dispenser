#ifndef VL53L0X_H
#define VL53L0X_H

#include <stdbool.h>
#include <stdint.h>
#include "driver/i2c.h"

// 필요한 레지스터 주소 정의 (생략된 주소는 기존 프로젝트 헤더에서 참조)
#define ADDRESS_DEFAULT                          0x29
#define IDENTIFICATION_MODEL_ID                  0xC0
#define VHV_CONFIG_PAD_SCL_SDA__EXTSUP_HV        0x89
#define MSRC_CONFIG_CONTROL                      0x16
#define SYSTEM_SEQUENCE_CONFIG                   0x01
#define GLOBAL_CONFIG_SPAD_ENABLES_REF_0         0xB0
#define DYNAMIC_SPAD_REF_EN_START_OFFSET         0x4E
#define DYNAMIC_SPAD_NUM_REQUESTED_REF_SPAD     0x4E
#define GLOBAL_CONFIG_REF_EN_START_SELECT        0xB4
#define SYSTEM_INTERRUPT_CONFIG_GPIO             0x0A
#define GPIO_HV_MUX_ACTIVE_HIGH                  0x84
#define SYSTEM_INTERRUPT_CLEAR                   0x0B
#define RESULT_INTERRUPT_STATUS                  0x13
#define RESULT_RANGE_STATUS                      0x14
#define SYSRANGE_START                           0x00
#define FINAL_RANGE_CONFIG_MIN_COUNT_RATE_RTN_LIMIT 0x44
#define FINAL_RANGE_CONFIG_TIMEOUT_MACROP_HI     0x46
#define PRE_RANGE_CONFIG_TIMEOUT_MACROP_HI       0x51
#define MSRC_CONFIG_TIMEOUT_MACROP               0x46
#define PRE_RANGE_CONFIG_VALID_PHASE_HIGH        0x56
#define PRE_RANGE_CONFIG_VALID_PHASE_LOW         0x57
#define PRE_RANGE_CONFIG_VCSEL_PERIOD            0x50
#define FINAL_RANGE_CONFIG_VALID_PHASE_HIGH      0x48
#define FINAL_RANGE_CONFIG_VALID_PHASE_LOW       0x47
#define GLOBAL_CONFIG_VCSEL_WIDTH                0x32
#define ALGO_PHASECAL_CONFIG_TIMEOUT             0x30
#define ALGO_PHASECAL_LIM                        0x30
#define FINAL_RANGE_CONFIG_VCSEL_PERIOD          0x44
#define OSC_CALIBRATE_VAL                        0xF8
#define SYSTEM_INTERMEASUREMENT_PERIOD           0x04

typedef enum {
    VcselPeriodPreRange,
    VcselPeriodFinalRange
} vcselPeriodType;

typedef struct {
    bool tcc, dss, msrc, pre_range, final_range;
} SequenceStepEnables;

typedef struct {
    uint16_t pre_range_vcsel_period_pclks, final_range_vcsel_period_pclks;
    uint16_t msrc_dss_tcc_mclks, pre_range_mclks, final_range_mclks;
    uint32_t msrc_dss_tcc_us, pre_range_us, final_range_us;
} SequenceStepTimeouts;

// VL53L0X 장치 인스턴스 구조체 (C++ 멤버 변수 대체)
typedef struct {
    i2c_port_t i2c_port;
    uint8_t address;
    uint16_t io_timeout;
    uint32_t timeout_start_ms;
    bool did_timeout;
    uint8_t stop_variable;
    uint32_t measurement_timing_budget_us;
    esp_err_t last_status;
} VL53L0X_t;

// 함수 선언
void VL53L0X_setBus(VL53L0X_t *dev, i2c_port_t port, uint8_t addr);
void VL53L0X_setAddress(VL53L0X_t *dev, uint8_t new_addr);
bool VL53L0X_init(VL53L0X_t *dev, bool io_2v8);
void VL53L0X_startContinuous(VL53L0X_t *dev, uint32_t period_ms);
void VL53L0X_stopContinuous(VL53L0X_t *dev);
uint16_t VL53L0X_readRangeContinuousMillimeters(VL53L0X_t *dev);
uint16_t VL53L0X_readRangeSingleMillimeters(VL53L0X_t *dev);
bool VL53L0X_timeoutOccurred(VL53L0X_t *dev);
bool VL53L0X_setMeasurementTimingBudget(VL53L0X_t *dev, uint32_t budget_us);
uint32_t VL53L0X_getMeasurementTimingBudget(VL53L0X_t *dev);
bool VL53L0X_setVcselPulsePeriod(VL53L0X_t *dev, vcselPeriodType type, uint8_t period_pclks);
uint8_t VL53L0X_getVcselPulsePeriod(VL53L0X_t *dev, vcselPeriodType type);
bool VL53L0X_setSignalRateLimit(VL53L0X_t *dev, float limit_Mcps);
float VL53L0X_getSignalRateLimit(VL53L0X_t *dev);

#endif // VL53L0X_H