#include "StimScheduler.h"
#include "LP5817.h"
#include "RTC_ST.h"
#include <string.h>

#define STIM_FLASH_MAGIC        0x5354494DU
#define STIM_FLASH_VERSION      1U
#define STIM_FLASH_PAGE_INDEX   (FLASH_PAGE_NB - 1U)
#define STIM_FLASH_BASE_ADDR    (FLASH_BASE + (STIM_FLASH_PAGE_INDEX * FLASH_PAGE_SIZE))

typedef struct
{
    uint8_t active;
    uint8_t slot;
    uint8_t led_id;
    uint8_t current_mA;
    uint8_t duty_percent;
    uint8_t state_on;
    uint32_t elapsed_ms;
    uint32_t next_edge_ms;
    uint32_t period_ms;
    uint32_t on_ms;
    uint32_t off_ms;
    uint32_t stop_at_ms;
} StimActiveLed_t;

// This is the layout for the Stim table stored in the flash (last page)//
typedef struct
{
    uint32_t magic;
    uint16_t version;
    uint16_t crc;
    StimSession_t slots[STIM_SCHED_SLOT_COUNT];
} StimFlashTable_t;

static RTC_HandleTypeDef *sched_rtc;
static I2C_HandleTypeDef *sched_i2c;
static StimSession_t sched_slots[STIM_SCHED_SLOT_COUNT];
static StimActiveLed_t active_leds[STIM_LED_COUNT];
static volatile uint8_t stim_tick_pending;
static uint8_t table_valid;

extern LPTIM_HandleTypeDef hlptim1;

// Computes CRC for the saved flash table //
static uint16_t crc16_ccitt(const uint8_t *data, uint32_t len)
{
    uint16_t crc = 0xFFFFU;

    while (len--)
    {
        crc ^= (uint16_t)(*data++) << 8;
        for (uint8_t i = 0; i < 8U; i++)
        {
            crc = (crc & 0x8000U) ? (uint16_t)((crc << 1) ^ 0x1021U) : (uint16_t)(crc << 1);
        }
    }

    return crc;
}

static uint32_t bcd_to_u8(uint8_t bcd)
{
    return ((uint32_t)(bcd >> 4) * 10UL) + (uint32_t)(bcd & 0x0FU);
}

static uint32_t date_seconds(uint8_t month, uint8_t day, uint8_t hour, uint8_t minute, uint8_t second)
{
    static const uint16_t month_offsets[12] = {0U, 31U, 59U, 90U, 120U, 151U,
                                               181U, 212U, 243U, 273U, 304U, 334U};
    uint32_t month_bin = bcd_to_u8(month);
    uint32_t day_bin = bcd_to_u8(day);

    if ((month_bin == 0UL) || (month_bin > 12UL) || (day_bin == 0UL))
        return 0xFFFFFFFFUL;

    return ((month_offsets[month_bin - 1UL] + (day_bin - 1UL)) * 86400UL) +
           (bcd_to_u8(hour) * 3600UL) +
           (bcd_to_u8(minute) * 60UL) +
           bcd_to_u8(second);
}

static uint32_t session_seconds(const StimSession_t *s)
{
    return date_seconds(s->month, s->day, s->hour, s->minute, s->second);
}

static HAL_StatusTypeDef get_session_seconds(const StimSession_t *s, uint32_t *target)
{
    if ((s == NULL) || (target == NULL))
        return HAL_ERROR;

    if (RTC_ST_ValidateDateTime(s->month, s->day, s->hour, s->minute, s->second) != HAL_OK)
        return HAL_ERROR;

    *target = session_seconds(s);
    return HAL_OK;
}

static HAL_StatusTypeDef get_now_seconds(uint32_t *now)
{
    RTC_ST_DateTime_t dt;

    if ((sched_rtc == NULL) || (now == NULL))
        return HAL_ERROR;

    if (RTC_ST_GetDateTime(sched_rtc, &dt) != HAL_OK)
        return HAL_ERROR;

    if (RTC_ST_ValidateDateTime(dt.month, dt.day, dt.hh, dt.min, dt.ss) != HAL_OK)
        return HAL_ERROR;

    *now = date_seconds(dt.month, dt.day, dt.hh, dt.min, dt.ss);
    return HAL_OK;
}

static uint32_t duration_to_seconds(uint16_t value, uint8_t unit)
{
    uint32_t seconds = value;

    if (unit == STIM_DURATION_MINUTES)
        seconds *= 60UL;

    return seconds;
}

static HAL_StatusTypeDef validate_command(const StimSessionCommand_t *cmd)
{
    uint32_t duration_s;

    if (cmd == NULL)
        return HAL_ERROR;

    if ((cmd->slot >= STIM_SCHED_SLOT_COUNT) || (cmd->led_id >= STIM_LED_COUNT))
        return HAL_ERROR;

    if (cmd->current_mA > STIM_MAX_CURRENT_MA)
        return HAL_ERROR;

    if (RTC_ST_ValidateDateTime(cmd->month, cmd->day, cmd->hour, cmd->minute, cmd->second) != HAL_OK)
        return HAL_ERROR;

    if ((cmd->frequency_hz < STIM_MIN_FREQ_HZ) || (cmd->frequency_hz > STIM_MAX_FREQ_HZ))
        return HAL_ERROR;

    if ((cmd->duty_percent < STIM_MIN_DUTY_PERCENT) || (cmd->duty_percent > STIM_MAX_DUTY_PERCENT))
        return HAL_ERROR;

    if ((cmd->duration_unit != STIM_DURATION_SECONDS) && (cmd->duration_unit != STIM_DURATION_MINUTES))
        return HAL_ERROR;

    duration_s = duration_to_seconds(cmd->duration_value, cmd->duration_unit);
    if ((duration_s == 0UL) || (duration_s > STIM_MAX_DURATION_SECONDS))
        return HAL_ERROR;

    return HAL_OK;
}

// This function reset the table stored in RAM //
static void table_reset(void)
{
    memset(sched_slots, 0, sizeof(sched_slots));
    for (uint8_t i = 0; i < STIM_SCHED_SLOT_COUNT; i++)
    {
        sched_slots[i].slot = i;
        sched_slots[i].state = STIM_SESSION_EMPTY;
    }
}

// Stores the table in RAM in the Flash to retain the data before going to Standby //
static HAL_StatusTypeDef table_save(void)
{
    StimFlashTable_t table;
    FLASH_EraseInitTypeDef erase = {0};
    uint32_t page_error = 0;
    uint32_t addr = STIM_FLASH_BASE_ADDR;
    const uint64_t *src;
    uint32_t words;

    memset(&table, 0xFF, sizeof(table));
    table.magic = STIM_FLASH_MAGIC;
    table.version = STIM_FLASH_VERSION;
    memcpy(table.slots, sched_slots, sizeof(sched_slots));
    table.crc = 0U;
    table.crc = crc16_ccitt((const uint8_t *)&table.version, sizeof(table) - sizeof(table.magic));

    HAL_FLASH_Unlock();

    erase.TypeErase = FLASH_TYPEERASE_PAGES;
    erase.Banks = FLASH_BANK_1;
    erase.Page = STIM_FLASH_PAGE_INDEX;
    erase.NbPages = 1;

    if (HAL_FLASHEx_Erase(&erase, &page_error) != HAL_OK)
    {
        HAL_FLASH_Lock();
        return HAL_ERROR;
    }

    // TODO: analyze this part + memset and memcpy?
    src = (const uint64_t *)&table;
    words = (uint32_t)((sizeof(table) + 7U) / 8U);

    for (uint32_t i = 0; i < words; i++)
    {
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, addr, src[i]) != HAL_OK)
        {
            HAL_FLASH_Lock();
            return HAL_ERROR;
        }
        addr += 8U;
    }

    HAL_FLASH_Lock();
    return HAL_OK;
}

// Reload the table into RAM space //
static void table_load(void)
{
    const StimFlashTable_t *table = (const StimFlashTable_t *)STIM_FLASH_BASE_ADDR;
    StimFlashTable_t temp;
    uint16_t crc;

    table_reset();
    table_valid = 0U;

    memcpy(&temp, table, sizeof(temp));
    if ((temp.magic != STIM_FLASH_MAGIC) || (temp.version != STIM_FLASH_VERSION))
        return;

    crc = temp.crc;
    temp.crc = 0U;
    if (crc != crc16_ccitt((const uint8_t *)&temp.version, sizeof(temp) - sizeof(temp.magic)))
        return;

    memcpy(sched_slots, temp.slots, sizeof(sched_slots));
    table_valid = 1U;
}

static HAL_StatusTypeDef set_led_off(uint8_t led_id)
{
    return LP5817_SetChannel_PWM(sched_i2c, led_id, LP5817_PWM_0);
}

static HAL_StatusTypeDef start_session_led(StimSession_t *s)
{
    StimActiveLed_t *a;
    uint32_t period_ms;
    uint32_t on_ms;

    if ((s == NULL) || (s->led_id >= STIM_LED_COUNT))
        return HAL_ERROR;

    a = &active_leds[s->led_id];

    if (a->active)
    {
        (void)set_led_off(s->led_id);
        if (a->slot < STIM_SCHED_SLOT_COUNT)
        {
            sched_slots[a->slot].state = STIM_SESSION_CANCELLED;
        }
    }

    period_ms = 1000UL / (uint32_t)s->frequency_hz;
    if (period_ms == 0UL)
        period_ms = 1UL;

    on_ms = (period_ms * (uint32_t)s->duty_percent) / 100UL;
    if (on_ms == 0UL)
        on_ms = 1UL;
    if (on_ms >= period_ms)
        on_ms = period_ms - 1UL;

    memset(a, 0, sizeof(*a));
    a->active = 1U;
    a->slot = s->slot;
    a->led_id = s->led_id;
    a->current_mA = s->current_mA;
    a->duty_percent = s->duty_percent;
    a->period_ms = period_ms;
    a->on_ms = on_ms;
    a->off_ms = period_ms - on_ms;
    a->stop_at_ms = s->duration_seconds * 1000UL;
    a->next_edge_ms = 0UL;

    if (LP5817_SetChannelCurrent_AndPWM(sched_i2c, s->led_id, LP5817_CurrentToDc(s->current_mA), LP5817_PWM_0) != HAL_OK)
        return HAL_ERROR;

    s->state = STIM_SESSION_RUNNING;
    if (HAL_LPTIM_Counter_Start_IT(&hlptim1) != HAL_OK)
        return HAL_ERROR;

    return table_save();
}

static void complete_active_led(StimActiveLed_t *a, uint8_t state)
{
    if ((a == NULL) || (!a->active))
        return;

    (void)set_led_off(a->led_id);

    if (a->slot < STIM_SCHED_SLOT_COUNT)
    {
        sched_slots[a->slot].state = state;
        (void)table_save();
    }

    memset(a, 0, sizeof(*a));

    if (!StimScheduler_IsAnyLedActive())
    {
        (void)HAL_LPTIM_Counter_Stop_IT(&hlptim1);
    }
}

///////////////////////////////////////////////////////////////////
/////////////////// Public Functions definitions///////////////////
///////////////////////////////////////////////////////////////////

HAL_StatusTypeDef StimScheduler_Init(RTC_HandleTypeDef *hrtc, I2C_HandleTypeDef *hi2c)
{
    sched_rtc = hrtc;
    sched_i2c = hi2c;
    memset(active_leds, 0, sizeof(active_leds));
    stim_tick_pending = 0U;
    table_load();
    return HAL_OK;
}

HAL_StatusTypeDef StimScheduler_SetSession(const StimSessionCommand_t *cmd, uint8_t *started_now)
{
    StimSession_t *s;
    uint32_t now;
    uint32_t target;
    uint32_t duration_s;

    if (started_now != NULL)
        *started_now = 0U;

    if (validate_command(cmd) != HAL_OK)
        return HAL_ERROR;

    s = &sched_slots[cmd->slot];
    duration_s = duration_to_seconds(cmd->duration_value, cmd->duration_unit);

    s->slot = cmd->slot;
    s->led_id = cmd->led_id;
    s->state = STIM_SESSION_SCHEDULED;
    s->month = cmd->month;
    s->day = cmd->day;
    s->hour = cmd->hour;
    s->minute = cmd->minute;
    s->second = cmd->second;
    s->current_mA = cmd->current_mA;
    s->frequency_hz = cmd->frequency_hz;
    s->duty_percent = cmd->duty_percent;
    s->duration_value = cmd->duration_value;
    s->duration_unit = cmd->duration_unit;
    s->duration_seconds = duration_s;

    if (table_save() != HAL_OK)
        return HAL_ERROR;

    if (get_now_seconds(&now) == HAL_OK)
    {
        target = session_seconds(s);
        if ((target <= now) && ((now - target) <= STIM_MISSED_GRACE_SECONDS))
        {
            if (start_session_led(s) == HAL_OK)
            {
                if (started_now != NULL)
                    *started_now = 1U;
                return HAL_OK;
            }
            return HAL_ERROR;
        }
    }

    return StimScheduler_ArmNextAlarm();
}

HAL_StatusTypeDef StimScheduler_ClearSession(uint8_t slot)
{
    if (slot >= STIM_SCHED_SLOT_COUNT)
        return HAL_ERROR;

    if ((sched_slots[slot].led_id < STIM_LED_COUNT) && active_leds[sched_slots[slot].led_id].active &&
        (active_leds[sched_slots[slot].led_id].slot == slot))
    {
        complete_active_led(&active_leds[sched_slots[slot].led_id], STIM_SESSION_CANCELLED);
    }

    memset(&sched_slots[slot], 0, sizeof(sched_slots[slot]));
    sched_slots[slot].slot = slot;
    sched_slots[slot].state = STIM_SESSION_EMPTY;

    if (table_save() != HAL_OK)
        return HAL_ERROR;

    return StimScheduler_ArmNextAlarm();
}

HAL_StatusTypeDef StimScheduler_ClearAll(void)
{
    StimScheduler_StopAllActive();
    table_reset();
    return table_save();
}

HAL_StatusTypeDef StimScheduler_GetSession(uint8_t slot, StimSession_t *out)
{
    if ((slot >= STIM_SCHED_SLOT_COUNT) || (out == NULL))
        return HAL_ERROR;

    *out = sched_slots[slot];
    return HAL_OK;
}

HAL_StatusTypeDef StimScheduler_GetStatus(StimSchedulerStatus_t *out)
{
    uint32_t now;
    uint32_t best_delta = 0xFFFFFFFFUL;

    if (out == NULL)
        return HAL_ERROR;

    memset(out, 0, sizeof(*out));
    out->table_valid = table_valid;
    out->next_slot = 0xFFU;

    for (uint8_t led = 0; led < STIM_LED_COUNT; led++)
    {
        if (active_leds[led].active)
            out->active_mask |= (uint8_t)(1U << led);
    }

    if (get_now_seconds(&now) == HAL_OK)
    {
        for (uint8_t i = 0; i < STIM_SCHED_SLOT_COUNT; i++)
        {
            if (sched_slots[i].state == STIM_SESSION_SCHEDULED)
            {
                uint32_t target;
                if (get_session_seconds(&sched_slots[i], &target) != HAL_OK)
                    continue;

                if ((target >= now) && ((target - now) < best_delta))
                {
                    best_delta = target - now;
                    out->next_slot = i;
                    out->next_valid = 1U;
                }
            }
        }
    }

    return HAL_OK;
}

// Checks all slots and starts sessions whose scheduled time has arrived //
HAL_StatusTypeDef StimScheduler_RunDueSessions(void)
{
    uint32_t now;
    HAL_StatusTypeDef status = HAL_OK;

    if (get_now_seconds(&now) != HAL_OK)
        return HAL_ERROR;

    for (uint8_t i = 0; i < STIM_SCHED_SLOT_COUNT; i++)
    {
        StimSession_t *s = &sched_slots[i];

        if (s->state != STIM_SESSION_SCHEDULED)
            continue;

        uint32_t target;
        if (get_session_seconds(s, &target) != HAL_OK)
        {
            s->state = STIM_SESSION_ERROR;
            status = HAL_ERROR;
            (void)table_save();
            continue;
        }

        if ((target <= now) && ((now - target) <= STIM_MISSED_GRACE_SECONDS))
        {
            if (start_session_led(s) != HAL_OK)
            {
                s->state = STIM_SESSION_ERROR;
                status = HAL_ERROR;
                (void)table_save();
            }
        }
        else if ((target < now) && ((now - target) > STIM_MISSED_GRACE_SECONDS))
        {
            s->state = STIM_SESSION_CANCELLED;
            (void)table_save();
        }
    }

    (void)StimScheduler_ArmNextAlarm();
    return status;
}

HAL_StatusTypeDef StimScheduler_StopActiveLed(uint8_t led_id)
{
    if (led_id >= STIM_LED_COUNT)
        return HAL_ERROR;

    complete_active_led(&active_leds[led_id], STIM_SESSION_CANCELLED);
    return HAL_OK;
}

HAL_StatusTypeDef StimScheduler_StopAllActive(void)
{
    for (uint8_t led = 0; led < STIM_LED_COUNT; led++)
    {
        complete_active_led(&active_leds[led], STIM_SESSION_CANCELLED);
    }
    return HAL_OK;
}

HAL_StatusTypeDef StimScheduler_ArmNextAlarm(void)
{
    uint32_t now;
    uint32_t best_delta = 0xFFFFFFFFUL;
    uint8_t best = 0xFFU;

    if (get_now_seconds(&now) != HAL_OK)
        return HAL_ERROR;

    for (uint8_t i = 0; i < STIM_SCHED_SLOT_COUNT; i++)
    {
        if (sched_slots[i].state == STIM_SESSION_SCHEDULED)
        {
            uint32_t target;
            if (get_session_seconds(&sched_slots[i], &target) != HAL_OK)
                continue;

            if ((target >= now) && ((target - now) < best_delta))
            {
                best_delta = target - now;
                best = i;
            }
        }
    }

    (void)HAL_RTC_DeactivateAlarm(sched_rtc, RTC_ALARM_A);

    if (best == 0xFFU)
        return HAL_OK;

    return RTC_ST_SetAlarm(sched_rtc,
                           sched_slots[best].day,
                           sched_slots[best].hour,
                           sched_slots[best].minute,
                           sched_slots[best].second);
}

uint8_t StimScheduler_IsAnyLedActive(void)
{
    for (uint8_t led = 0; led < STIM_LED_COUNT; led++)
    {
        if (active_leds[led].active)
            return 1U;
    }

    return 0U;
}

void StimScheduler_Tick1ms(void)
{
    stim_tick_pending = 1U;
}

void StimScheduler_Service(void)
{
    if (!stim_tick_pending)
        return;

    stim_tick_pending = 0U;

    for (uint8_t led = 0; led < STIM_LED_COUNT; led++)
    {
        StimActiveLed_t *a = &active_leds[led];

        if (!a->active)
            continue;

        a->elapsed_ms++;

        if (a->elapsed_ms >= a->stop_at_ms)
        {
            complete_active_led(a, STIM_SESSION_COMPLETED);
            continue;
        }

        if (a->elapsed_ms < a->next_edge_ms)
            continue;

        if (a->state_on)
        {
            (void)LP5817_SetChannel_PWM(sched_i2c, led, LP5817_PWM_0);
            a->state_on = 0U;
            a->next_edge_ms = a->elapsed_ms + a->off_ms;
        }
        else
        {
            (void)LP5817_SetChannel_PWM(sched_i2c, led, LP5817_PWM_100);
            a->state_on = 1U;
            a->next_edge_ms = a->elapsed_ms + a->on_ms;
        }
    }
}
