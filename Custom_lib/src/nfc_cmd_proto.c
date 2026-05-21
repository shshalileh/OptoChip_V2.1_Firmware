#include "nfc_cmd_proto.h"
#include <string.h>
#include <ctype.h>

static int hex_nibble(char c)
{
    if ((c >= '0') && (c <= '9')) return (c - '0');
    if ((c >= 'A') && (c <= 'F')) return (c - 'A' + 10);
    if ((c >= 'a') && (c <= 'f')) return (c - 'a' + 10);
    return -1;
}

static uint8_t expected_payload_len(uint8_t cmd, uint8_t *known)
{
    *known = 1U;

    switch (cmd)
    {
    case NFC_CMD_SET_CHARGER_CURRENT: return 1U;
    case NFC_CMD_SET_LED_CURRENT:     return 2U;
    case NFC_CMD_SET_LED_PULSE:       return 3U;
    case NFC_CMD_SET_LED_FULL_SETUP:  return 5U;
    case NFC_CMD_READ_BATTERY_VOLT:   return 0U;
    case NFC_CMD_READ_LED_STATUS:     return 0U;
    case NFC_CMD_READ_MCU_TIME:       return 0U;
    case NFC_CMD_SET_MCU_TIME:        return 5U;
    case NFC_CMD_SET_LED_SESSION:     return 13U;
    case NFC_CMD_CLEAR_SESSION:       return 1U;
    case NFC_CMD_READ_SCHED_STATUS:   return 0U;
    case NFC_CMD_STOP_ACTIVE_LED:     return 1U;
    case NFC_CMD_CLEAR_ALL_SESSIONS:  return 0U;
    default:
        *known = 0U;
        return 0U;
    }
}

NFC_ProtoStatus_t NFC_Proto_ParseTextHex(const uint8_t *text,
                                         NFC_CommandFrame_t *frame)
{
    uint8_t raw[NFC_PROTO_MAX_FRAME_LEN + 1U];
    uint8_t raw_len = 0U;
    int hi = -1;

    if ((text == NULL) || (frame == NULL))
        return NFC_PROTO_ERR_NULL;

    memset(frame, 0, sizeof(*frame));

    while (*text != '\0')
    {
        if ((*text == ' ') || (*text == '\r') || (*text == '\n') || (*text == '\t') || (*text == '-') || (*text == ':'))
        {
            text++;
            continue;
        }

        int v = hex_nibble((char)*text);
        if (v < 0)
            return NFC_PROTO_ERR_FORMAT;

        if (hi < 0)
        {
            hi = v;
        }
        else
        {
            if (raw_len >= sizeof(raw))
                return NFC_PROTO_ERR_OVERFLOW;

            raw[raw_len++] = (uint8_t)((hi << 4) | v);
            hi = -1;
        }

        text++;
    }

    if (hi >= 0)
        return NFC_PROTO_ERR_FORMAT;

    if (raw_len < 1U)
        return NFC_PROTO_ERR_LENGTH;

    frame->cmd = raw[0];
    frame->payload_len = (uint8_t)(raw_len - 1U);

    if (frame->payload_len > 0U)
    {
        memcpy(frame->payload, &raw[1], frame->payload_len);
    }

    return NFC_PROTO_OK;
}

NFC_ProtoStatus_t NFC_Proto_ValidateCommand(const NFC_CommandFrame_t *frame)
{
    uint8_t known = 0U;
    uint8_t len_expected;

    if (frame == NULL)
        return NFC_PROTO_ERR_NULL;

    len_expected = expected_payload_len(frame->cmd, &known);

    if (!known)
        return NFC_PROTO_ERR_CMD;

    if (frame->payload_len != len_expected)
        return NFC_PROTO_ERR_LENGTH;

    return NFC_PROTO_OK;
}

NFC_ProtoStatus_t NFC_Proto_EncodeResponseTextHex(const NFC_ResponseFrame_t *rsp,
                                                  uint8_t *out_text,
                                                  size_t out_text_size)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t needed;
    size_t i;
    size_t p = 0U;

    if ((rsp == NULL) || (out_text == NULL))
        return NFC_PROTO_ERR_NULL;

    needed = (size_t)(rsp->payload_len + 1U) * 2U + 1U;

    if (out_text_size < needed)
        return NFC_PROTO_ERR_OVERFLOW;

    out_text[p++] = (uint8_t)hex[(rsp->rsp >> 4) & 0x0F];
    out_text[p++] = (uint8_t)hex[rsp->rsp & 0x0F];

    for (i = 0; i < rsp->payload_len; i++)
    {
        out_text[p++] = (uint8_t)hex[(rsp->payload[i] >> 4) & 0x0F];
        out_text[p++] = (uint8_t)hex[rsp->payload[i] & 0x0F];
    }

    out_text[p] = '\0';
    return NFC_PROTO_OK;
}

NFC_ProtoStatus_t NFC_Proto_GetSetChargerCurrent(const NFC_CommandFrame_t *frame,
                                                 NFC_CmdSetChargerCurrent_t *out)
{
    if ((frame == NULL) || (out == NULL)) return NFC_PROTO_ERR_NULL;
    if ((frame->cmd != NFC_CMD_SET_CHARGER_CURRENT) || (frame->payload_len != 1U)) return NFC_PROTO_ERR_LENGTH;

    out->current_mA = frame->payload[0];
    return NFC_PROTO_OK;
}

NFC_ProtoStatus_t NFC_Proto_GetSetLedCurrent(const NFC_CommandFrame_t *frame,
                                             NFC_CmdSetLedCurrent_t *out)
{
    if ((frame == NULL) || (out == NULL)) return NFC_PROTO_ERR_NULL;
    if ((frame->cmd != NFC_CMD_SET_LED_CURRENT) || (frame->payload_len != 2U)) return NFC_PROTO_ERR_LENGTH;

    out->led_id     = frame->payload[0];
    out->current_mA = frame->payload[1];
    return NFC_PROTO_OK;
}

NFC_ProtoStatus_t NFC_Proto_GetSetLedPulse(const NFC_CommandFrame_t *frame,
                                           NFC_CmdSetLedPulse_t *out)
{
    if ((frame == NULL) || (out == NULL)) return NFC_PROTO_ERR_NULL;
    if ((frame->cmd != NFC_CMD_SET_LED_PULSE) || (frame->payload_len != 3U)) return NFC_PROTO_ERR_LENGTH;

    out->led_id = frame->payload[0];
    out->freq   = frame->payload[1];
    out->duty   = frame->payload[2];
    return NFC_PROTO_OK;
}

NFC_ProtoStatus_t NFC_Proto_GetSetLedFullSetup(const NFC_CommandFrame_t *frame,
                                               NFC_CmdSetLedFullSetup_t *out)
{
    if ((frame == NULL) || (out == NULL)) return NFC_PROTO_ERR_NULL;
    if ((frame->cmd != NFC_CMD_SET_LED_FULL_SETUP) || (frame->payload_len != 5U)) return NFC_PROTO_ERR_LENGTH;

    out->led_id  = frame->payload[0];
    out->current = frame->payload[1];
    out->freq    = frame->payload[2];
    out->duty    = frame->payload[3];
    out->duration= frame->payload[4];
    return NFC_PROTO_OK;
}

NFC_ProtoStatus_t NFC_Proto_GetSetTime(const NFC_CommandFrame_t *frame,
                                       NFC_CmdSetTime_t *out)
{
    if ((frame == NULL) || (out == NULL)) return NFC_PROTO_ERR_NULL;
    if ((frame->cmd != NFC_CMD_SET_MCU_TIME) || (frame->payload_len != 5U)) return NFC_PROTO_ERR_LENGTH;

    out->mm  = frame->payload[0];
    out->dd  = frame->payload[1];
    out->hh  = frame->payload[2];
    out->min = frame->payload[3];
    out->ss  = frame->payload[4];
    return NFC_PROTO_OK;
}

NFC_ProtoStatus_t NFC_Proto_GetSetLedSession(const NFC_CommandFrame_t *frame,
                                             NFC_CmdSetLedSession_t *out)
{
    if ((frame == NULL) || (out == NULL)) return NFC_PROTO_ERR_NULL;
    if ((frame->cmd != NFC_CMD_SET_LED_SESSION) || (frame->payload_len != 13U)) return NFC_PROTO_ERR_LENGTH;

    out->slot           = frame->payload[0];
    out->led_id         = frame->payload[1];
    out->mm             = frame->payload[2];
    out->dd             = frame->payload[3];
    out->hh             = frame->payload[4];
    out->min            = frame->payload[5];
    out->ss             = frame->payload[6];
    out->current_mA     = frame->payload[7];
    out->frequency_hz   = frame->payload[8];
    out->duty_percent   = frame->payload[9];
    out->duration_value = (uint16_t)(((uint16_t)frame->payload[10] << 8) | frame->payload[11]);
    out->duration_unit  = frame->payload[12];

    return NFC_PROTO_OK;
}

void NFC_Proto_BuildAck(NFC_ResponseFrame_t *rsp)
{
    if (rsp == NULL) return;
    rsp->rsp = NFC_RSP_ACK;
    rsp->payload_len = 0U;
}

void NFC_Proto_BuildError(NFC_ResponseFrame_t *rsp, uint8_t err_code)
{
    if (rsp == NULL) return;
    rsp->rsp = NFC_RSP_ERROR;
    rsp->payload[0] = err_code;
    rsp->payload_len = 1U;
}

void NFC_Proto_BuildBatteryVoltage(NFC_ResponseFrame_t *rsp, uint16_t batt_mV)
{
    if (rsp == NULL) return;
    rsp->rsp = NFC_RSP_BATTERY_VOLT;
    rsp->payload[0] = (uint8_t)((batt_mV >> 8) & 0xFF);
    rsp->payload[1] = (uint8_t)(batt_mV & 0xFF);
    rsp->payload_len = 2U;
}

void NFC_Proto_BuildLedStatus(NFC_ResponseFrame_t *rsp, const uint8_t *data, uint8_t len)
{
    if ((rsp == NULL) || (data == NULL) || (len > NFC_PROTO_MAX_FRAME_LEN)) return;
    rsp->rsp = NFC_RSP_LED_STATUS;
    memcpy(rsp->payload, data, len);
    rsp->payload_len = len;
}

void NFC_Proto_BuildRtcTime(NFC_ResponseFrame_t *rsp,
                            uint8_t mm, uint8_t dd,
                            uint8_t hh, uint8_t min, uint8_t ss)
{
    if (rsp == NULL) return;
    rsp->rsp = NFC_RSP_RTC_TIME;
    rsp->payload[0] = mm;
    rsp->payload[1] = dd;
    rsp->payload[2] = hh;
    rsp->payload[3] = min;
    rsp->payload[4] = ss;
    rsp->payload_len = 5U;
}

void NFC_Proto_BuildScheduleStatus(NFC_ResponseFrame_t *rsp, const uint8_t *data, uint8_t len)
{
    if ((rsp == NULL) || (data == NULL) || (len > NFC_PROTO_MAX_FRAME_LEN)) return;
    rsp->rsp = NFC_RSP_SCHED_STATUS;
    memcpy(rsp->payload, data, len);
    rsp->payload_len = len;
}

