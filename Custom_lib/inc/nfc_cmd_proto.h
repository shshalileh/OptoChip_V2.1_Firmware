#pragma once

#include "stm32u0xx_hal.h"
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NFC_PROTO_MAX_FRAME_LEN      64U
#define NFC_PROTO_MAX_TEXT_LEN       160U
#define NFC_SCHED_STATUS_FORMAT_WITH_TIMES  0x01U
#define NFC_SCHED_STATUS_EXT_HEADER_LEN     6U
#define NFC_SCHED_STATUS_ENTRY_LEN          7U

typedef enum
{
    NFC_PROTO_OK = 0,
    NFC_PROTO_ERR_NULL,
    NFC_PROTO_ERR_FORMAT,
    NFC_PROTO_ERR_LENGTH,
    NFC_PROTO_ERR_CMD,
    NFC_PROTO_ERR_OVERFLOW
} NFC_ProtoStatus_t;

typedef enum
{
    NFC_CMD_SET_CHARGER_CURRENT = 0x01,
    NFC_CMD_SET_LED_CURRENT     = 0x02,
    NFC_CMD_SET_LED_PULSE       = 0x03,
    NFC_CMD_SET_LED_FULL_SETUP  = 0x04,

    NFC_CMD_READ_BATTERY_VOLT   = 0x10,
    NFC_CMD_READ_LED_STATUS     = 0x11,
    NFC_CMD_READ_MCU_TIME       = 0x20,
    NFC_CMD_SET_MCU_TIME        = 0x21,

    NFC_CMD_SET_LED_SESSION     = 0x30,
    NFC_CMD_CLEAR_SESSION       = 0x31,
    NFC_CMD_READ_SCHED_STATUS   = 0x32,
    NFC_CMD_STOP_ACTIVE_LED     = 0x33,
    NFC_CMD_CLEAR_ALL_SESSIONS  = 0x34
} NFC_CommandCode_t;

typedef enum
{
    NFC_RSP_BATTERY_VOLT = 0x90,
    NFC_RSP_LED_STATUS   = 0x91,
    NFC_RSP_RTC_TIME     = 0xA0,
    NFC_RSP_SCHED_STATUS = 0xB0,
    NFC_RSP_ACK          = 0xF0,
    NFC_RSP_ERROR        = 0xF1
} NFC_ResponseCode_t;

typedef struct
{
    uint8_t cmd;
    uint8_t payload[NFC_PROTO_MAX_FRAME_LEN];
    uint8_t payload_len;
} NFC_CommandFrame_t;

typedef struct
{
    uint8_t rsp;
    uint8_t payload[NFC_PROTO_MAX_FRAME_LEN];
    uint8_t payload_len;
} NFC_ResponseFrame_t;

/* Command payload views */
typedef struct
{
    uint8_t current_mA;
} NFC_CmdSetChargerCurrent_t;

typedef struct
{
    uint8_t led_id;
    uint8_t current_mA;
} NFC_CmdSetLedCurrent_t;

typedef struct
{
    uint8_t led_id;
    uint8_t freq;
    uint8_t duty;
} NFC_CmdSetLedPulse_t;

typedef struct
{
    uint8_t led_id;
    uint8_t current;
    uint8_t freq;
    uint8_t duty;
    uint8_t duration;
} NFC_CmdSetLedFullSetup_t;

typedef struct
{
    uint8_t mm;
    uint8_t dd;
    uint8_t hh;
    uint8_t min;
    uint8_t ss;
} NFC_CmdSetTime_t;

typedef struct
{
    uint8_t slot;
    uint8_t led_id;
    uint8_t mm;
    uint8_t dd;
    uint8_t hh;
    uint8_t min;
    uint8_t ss;
    uint8_t current_mA;
    uint8_t frequency_hz;
    uint8_t duty_percent;
    uint16_t duration_value;
    uint8_t duration_unit;
} NFC_CmdSetLedSession_t;

/* Main API */
NFC_ProtoStatus_t NFC_Proto_ParseTextHex(const uint8_t *text,
                                         NFC_CommandFrame_t *frame);

NFC_ProtoStatus_t NFC_Proto_ValidateCommand(const NFC_CommandFrame_t *frame);

NFC_ProtoStatus_t NFC_Proto_EncodeResponseTextHex(const NFC_ResponseFrame_t *rsp,
                                                  uint8_t *out_text,
                                                  size_t out_text_size);

/* Optional unpack helpers */
NFC_ProtoStatus_t NFC_Proto_GetSetChargerCurrent(const NFC_CommandFrame_t *frame,
                                                 NFC_CmdSetChargerCurrent_t *out);

NFC_ProtoStatus_t NFC_Proto_GetSetLedCurrent(const NFC_CommandFrame_t *frame,
                                             NFC_CmdSetLedCurrent_t *out);

NFC_ProtoStatus_t NFC_Proto_GetSetLedPulse(const NFC_CommandFrame_t *frame,
                                           NFC_CmdSetLedPulse_t *out);

NFC_ProtoStatus_t NFC_Proto_GetSetLedFullSetup(const NFC_CommandFrame_t *frame,
                                               NFC_CmdSetLedFullSetup_t *out);

NFC_ProtoStatus_t NFC_Proto_GetSetTime(const NFC_CommandFrame_t *frame,
                                       NFC_CmdSetTime_t *out);

NFC_ProtoStatus_t NFC_Proto_GetSetLedSession(const NFC_CommandFrame_t *frame,
                                             NFC_CmdSetLedSession_t *out);

/* Quick response builders */
void NFC_Proto_BuildAck(NFC_ResponseFrame_t *rsp);
void NFC_Proto_BuildError(NFC_ResponseFrame_t *rsp, uint8_t err_code);
void NFC_Proto_BuildBatteryVoltage(NFC_ResponseFrame_t *rsp, uint16_t batt_mV);
void NFC_Proto_BuildLedStatus(NFC_ResponseFrame_t *rsp, const uint8_t *data, uint8_t len);
void NFC_Proto_BuildRtcTime(NFC_ResponseFrame_t *rsp, uint8_t mm, uint8_t dd,
							 uint8_t hh, uint8_t min, uint8_t ss);
void NFC_Proto_BuildScheduleStatus(NFC_ResponseFrame_t *rsp, const uint8_t *data, uint8_t len);

#ifdef __cplusplus
}
#endif
