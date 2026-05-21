#pragma once

#include "stm32u0xx_hal.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
Why BCD here:
- CubeMX RTC init already uses RTC_FORMAT_BCD
- easier to stay consistent with STM32 RTC registers/examples
- BIN is also fine, but BCD avoids mixing formats in your project

BKP_MAGIC usage:
- stored in RTC backup register
- lets firmware know RTC was already initialized once
- prevents resetting time/date on every MCU reset
*/

#define RTC_ST_BKP_MAGIC   0xA5A5U
#define RTC_ST_BKP_REG     RTC_BKP_DR0

typedef struct
{
    uint8_t month;
    uint8_t day;
    uint8_t hh;
    uint8_t min;
    uint8_t ss;
} RTC_ST_DateTime_t;

HAL_StatusTypeDef RTC_ST_Setup(RTC_HandleTypeDef *hrtc);

HAL_StatusTypeDef RTC_ST_ValidateDateTime(uint8_t month, uint8_t day,
                                           uint8_t hh, uint8_t min, uint8_t ss);

HAL_StatusTypeDef RTC_ST_SetTime(RTC_HandleTypeDef *hrtc,
                                 uint8_t hh, uint8_t min, uint8_t ss);

HAL_StatusTypeDef RTC_ST_GetTime(RTC_HandleTypeDef *hrtc,
                                 uint8_t *hh, uint8_t *min, uint8_t *ss);

HAL_StatusTypeDef RTC_ST_SetDate(RTC_HandleTypeDef *hrtc,
                                 uint8_t month, uint8_t day);

HAL_StatusTypeDef RTC_ST_GetDate(RTC_HandleTypeDef *hrtc,
                                 uint8_t *month, uint8_t *day);

HAL_StatusTypeDef RTC_ST_SetDateTime(RTC_HandleTypeDef *hrtc,
                                     uint8_t month, uint8_t day,
                                     uint8_t hh, uint8_t min, uint8_t ss);

HAL_StatusTypeDef RTC_ST_GetDateTime(RTC_HandleTypeDef *hrtc,
                                     RTC_ST_DateTime_t *dt);

HAL_StatusTypeDef RTC_ST_SetAlarm(RTC_HandleTypeDef *hrtc,
                                       uint8_t dd, uint8_t hh, uint8_t min, uint8_t ss);

#ifdef __cplusplus
}
#endif

