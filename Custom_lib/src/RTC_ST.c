#include "RTC_ST.h"

static uint8_t rtc_bcd_to_u8(uint8_t bcd)
{
    return (uint8_t)(((bcd >> 4) * 10U) + (bcd & 0x0FU));
}

// Checking if the number is a valid BCD //
static uint8_t rtc_is_bcd(uint8_t bcd)
{
    return (((bcd >> 4) <= 9U) && ((bcd & 0x0FU) <= 9U)) ? 1U : 0U;
}

static uint8_t rtc_days_in_month(uint8_t month)
{
    static const uint8_t days[12] = {31U, 28U, 31U, 30U, 31U, 30U,
                                     31U, 31U, 30U, 31U, 30U, 31U};

    if ((month == 0U) || (month > 12U))
        return 0U;

    return days[month - 1U];
}

static HAL_StatusTypeDef rtc_validate_time(uint8_t hh, uint8_t min, uint8_t ss)
{
    if (!rtc_is_bcd(hh) || !rtc_is_bcd(min) || !rtc_is_bcd(ss))
        return HAL_ERROR;

    if ((rtc_bcd_to_u8(hh) > 23U) || (rtc_bcd_to_u8(min) > 59U) || (rtc_bcd_to_u8(ss) > 59U))
        return HAL_ERROR;

    return HAL_OK;
}

static HAL_StatusTypeDef rtc_validate_date(uint8_t month, uint8_t day)
{
    uint8_t month_bin;
    uint8_t day_bin;

    if (!rtc_is_bcd(month) || !rtc_is_bcd(day))
        return HAL_ERROR;

    month_bin = rtc_bcd_to_u8(month);
    day_bin = rtc_bcd_to_u8(day);

    if ((day_bin == 0U) || (day_bin > rtc_days_in_month(month_bin)))
        return HAL_ERROR;

    return HAL_OK;
}

HAL_StatusTypeDef RTC_ST_ValidateDateTime(uint8_t month, uint8_t day,
                                           uint8_t hh, uint8_t min, uint8_t ss)
{
    if (rtc_validate_date(month, day) != HAL_OK)
        return HAL_ERROR;

    return rtc_validate_time(hh, min, ss);
}

HAL_StatusTypeDef RTC_ST_Setup(RTC_HandleTypeDef *hrtc)
{
    if (hrtc == NULL)
        return HAL_ERROR;

    /* first-time default: 2025/Aug/01 11:00:00, Tuesday */
    if (HAL_RTCEx_BKUPRead(hrtc, RTC_ST_BKP_REG) != RTC_ST_BKP_MAGIC)
    {
        if (RTC_ST_SetDateTime(hrtc, RTC_MONTH_AUGUST, 0x01, 0x11, 0x00, 0x00) != HAL_OK)
            return HAL_ERROR;

        HAL_RTCEx_BKUPWrite(hrtc, RTC_ST_BKP_REG, RTC_ST_BKP_MAGIC);
    }

    return HAL_OK;
}

HAL_StatusTypeDef RTC_ST_SetTime(RTC_HandleTypeDef *hrtc,
                                 uint8_t hh, uint8_t min, uint8_t ss)
{
    RTC_TimeTypeDef sTime = {0};

    if (hrtc == NULL)
        return HAL_ERROR;

    if (rtc_validate_time(hh, min, ss) != HAL_OK)
        return HAL_ERROR;

    sTime.Hours = hh;
    sTime.Minutes = min;
    sTime.Seconds = ss;
    sTime.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
    sTime.StoreOperation = RTC_STOREOPERATION_RESET;

    return HAL_RTC_SetTime(hrtc, &sTime, RTC_FORMAT_BCD);
}

HAL_StatusTypeDef RTC_ST_GetTime(RTC_HandleTypeDef *hrtc,
                                 uint8_t *hh, uint8_t *min, uint8_t *ss)
{
    RTC_TimeTypeDef sTime = {0};
    RTC_DateTypeDef sDate = {0};

    if (!hrtc || !hh || !min || !ss)
        return HAL_ERROR;

    if (HAL_RTC_GetTime(hrtc, &sTime, RTC_FORMAT_BCD) != HAL_OK)
        return HAL_ERROR;

    /* mandatory after GetTime */
    if (HAL_RTC_GetDate(hrtc, &sDate, RTC_FORMAT_BCD) != HAL_OK)
        return HAL_ERROR;

    *hh  = sTime.Hours;
    *min = sTime.Minutes;
    *ss  = sTime.Seconds;

    return HAL_OK;
}

HAL_StatusTypeDef RTC_ST_SetDate(RTC_HandleTypeDef *hrtc,
                                 uint8_t month, uint8_t day)
{
    RTC_DateTypeDef sDate = {0};

    if (hrtc == NULL)
        return HAL_ERROR;

    if (rtc_validate_date(month, day) != HAL_OK)
        return HAL_ERROR;

    sDate.Year = 0x25;                    /* fixed 2025 */
    sDate.Month = month;
    sDate.Date = day;
    sDate.WeekDay = RTC_WEEKDAY_TUESDAY;  /* fixed */

    return HAL_RTC_SetDate(hrtc, &sDate, RTC_FORMAT_BCD);
}

HAL_StatusTypeDef RTC_ST_GetDate(RTC_HandleTypeDef *hrtc,
                                 uint8_t *month, uint8_t *day)
{
    RTC_TimeTypeDef sTime = {0};
    RTC_DateTypeDef sDate = {0};

    if (!hrtc || !month || !day)
        return HAL_ERROR;

    if (HAL_RTC_GetTime(hrtc, &sTime, RTC_FORMAT_BCD) != HAL_OK)
        return HAL_ERROR;

    if (HAL_RTC_GetDate(hrtc, &sDate, RTC_FORMAT_BCD) != HAL_OK)
        return HAL_ERROR;

    *month = sDate.Month;
    *day   = sDate.Date;

    return HAL_OK;
}

HAL_StatusTypeDef RTC_ST_SetDateTime(RTC_HandleTypeDef *hrtc,
                                     uint8_t month, uint8_t day,
                                     uint8_t hh, uint8_t min, uint8_t ss)
{
    if (RTC_ST_ValidateDateTime(month, day, hh, min, ss) != HAL_OK)
        return HAL_ERROR;

    if (RTC_ST_SetTime(hrtc, hh, min, ss) != HAL_OK)
        return HAL_ERROR;

    if (RTC_ST_SetDate(hrtc, month, day) != HAL_OK)
        return HAL_ERROR;

    return HAL_OK;
}

HAL_StatusTypeDef RTC_ST_GetDateTime(RTC_HandleTypeDef *hrtc,
                                     RTC_ST_DateTime_t *dt)
{
    RTC_TimeTypeDef sTime = {0};
    RTC_DateTypeDef sDate = {0};

    if (!hrtc || !dt)
        return HAL_ERROR;

    if (HAL_RTC_GetTime(hrtc, &sTime, RTC_FORMAT_BCD) != HAL_OK)
        return HAL_ERROR;

    if (HAL_RTC_GetDate(hrtc, &sDate, RTC_FORMAT_BCD) != HAL_OK)
        return HAL_ERROR;

    dt->month = sDate.Month;
    dt->day   = sDate.Date;
    dt->hh    = sTime.Hours;
    dt->min   = sTime.Minutes;
    dt->ss    = sTime.Seconds;

    return HAL_OK;
}

HAL_StatusTypeDef RTC_ST_SetAlarm(RTC_HandleTypeDef *hrtc,
                                       uint8_t dd, uint8_t hh, uint8_t min, uint8_t ss)
{
    RTC_AlarmTypeDef sAlarm = {0};

    if (hrtc == NULL)
        return HAL_ERROR;

    if ((rtc_validate_time(hh, min, ss) != HAL_OK) || !rtc_is_bcd(dd) ||
        (rtc_bcd_to_u8(dd) == 0U) || (rtc_bcd_to_u8(dd) > 31U))
        return HAL_ERROR;

    (void)HAL_RTC_DeactivateAlarm(hrtc, RTC_ALARM_A);

    sAlarm.AlarmTime.Hours = hh;
    sAlarm.AlarmTime.Minutes = min;
    sAlarm.AlarmTime.Seconds = ss;
    sAlarm.AlarmTime.SubSeconds = 0x0;
    sAlarm.AlarmTime.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
    sAlarm.AlarmTime.StoreOperation = RTC_STOREOPERATION_RESET;

    sAlarm.AlarmMask = RTC_ALARMMASK_NONE;
    sAlarm.AlarmSubSecondMask = RTC_ALARMSUBSECONDMASK_ALL;
    sAlarm.AlarmDateWeekDaySel = RTC_ALARMDATEWEEKDAYSEL_DATE;
    sAlarm.AlarmDateWeekDay = dd;
    sAlarm.Alarm = RTC_ALARM_A;

    return HAL_RTC_SetAlarm_IT(hrtc, &sAlarm, RTC_FORMAT_BCD);
}


