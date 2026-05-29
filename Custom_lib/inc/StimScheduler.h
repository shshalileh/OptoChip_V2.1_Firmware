#pragma once

#include "stm32u0xx_hal.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define STIM_SCHED_SLOT_COUNT          8U
#define STIM_LED_COUNT                 2U

#define STIM_MAX_CURRENT_MA            30U
#define STIM_MIN_FREQ_HZ               1U
#define STIM_MAX_FREQ_HZ               100U
#define STIM_MIN_DUTY_PERCENT          1U
#define STIM_MAX_DUTY_PERCENT          99U
#define STIM_MAX_DURATION_SECONDS      86400UL
#define STIM_MISSED_GRACE_SECONDS      60UL

typedef enum
{
    STIM_DURATION_SECONDS = 0,
    STIM_DURATION_MINUTES = 1
} StimDurationUnit_t;

typedef enum
{
    STIM_SESSION_EMPTY = 0,
    STIM_SESSION_SCHEDULED,
    STIM_SESSION_RUNNING,
    STIM_SESSION_COMPLETED,
    STIM_SESSION_CANCELLED,
    STIM_SESSION_ERROR
} StimSessionState_t;

typedef struct
{
    uint8_t slot;
    uint8_t led_id;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    uint8_t current_mA;
    uint8_t frequency_hz;
    uint8_t duty_percent;
    uint16_t duration_value;
    uint8_t duration_unit;
} StimSessionCommand_t;

// This is structure for saving the session into flash
typedef struct
{
    uint8_t slot;
    uint8_t led_id;
    uint8_t state;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    uint8_t current_mA;
    uint8_t frequency_hz;
    uint8_t duty_percent;
    uint16_t duration_value;
    uint8_t duration_unit;
    uint32_t duration_seconds;
} StimSession_t;

typedef struct
{
    uint8_t active_mask;
    uint8_t next_slot;
    uint8_t next_valid;
    uint8_t table_valid;
} StimSchedulerStatus_t;

HAL_StatusTypeDef StimScheduler_Init(RTC_HandleTypeDef *hrtc, I2C_HandleTypeDef *hi2c);
HAL_StatusTypeDef StimScheduler_SetSession(const StimSessionCommand_t *cmd, uint8_t *started_now);
HAL_StatusTypeDef StimScheduler_ClearSession(uint8_t slot);
HAL_StatusTypeDef StimScheduler_ClearAll(void);
HAL_StatusTypeDef StimScheduler_GetSession(uint8_t slot, StimSession_t *out);
HAL_StatusTypeDef StimScheduler_GetStatus(StimSchedulerStatus_t *out);
HAL_StatusTypeDef StimScheduler_RunDueSessions(void);
HAL_StatusTypeDef StimScheduler_StopActiveLed(uint8_t led_id);
HAL_StatusTypeDef StimScheduler_StopAllActive(void);
HAL_StatusTypeDef StimScheduler_ArmNextAlarm(void);
uint8_t StimScheduler_IsAnyLedActive(void);
void StimScheduler_Tick1ms(void);
void StimScheduler_Service(void);

#ifdef __cplusplus
}
#endif
