#include "My_ST25DV04.h"

#include "BQ25155.h"
#include "LP5817.h"
#include "RTC_ST.h"
#include "StimScheduler.h"
#include "lib_NDEF_config.h"
#include "nfc_cmd_proto.h"

#include <string.h>

#define NFC_ST_READY_TIMEOUT_MS          250U
#define NFC_ST_INIT_TIMEOUT_MS           1000U
#define NFC_ST_FORMAT_TIMEOUT_MS         1000U
#define NFC_ST_RFWRITE_DONE_TIMEOUT_MS   5000U
#define NFC_ST_RFWRITE_SETTLE_DELAY_MS   100U
#define NFC_ST_RFWRITE_POLL_DELAY_MS     25U
#define NFC_ST_REPAIR_TEXT               "ERR01"

extern volatile uint8_t nfc_gpo_flag;
extern volatile uint8_t rf_processing;
extern BQ25155_Handle_t BQ;
extern I2C_HandleTypeDef hi2c3;
extern RTC_HandleTypeDef hrtc;

volatile uint16_t nfc_st_last_init_status = NDEF_ERROR;
volatile uint16_t nfc_st_last_ndef_status = NDEF_ERROR;
volatile uint8_t nfc_st_last_it_status = 0U;
volatile uint32_t nfc_st_rfwrite_count = 0U;
volatile uint32_t nfc_st_malformed_count = 0U;
volatile uint32_t nfc_st_repair_count = 0U;
volatile uint32_t nfc_st_wait_timeout_count = 0U;
volatile uint32_t nfc_st_ready_timeout_count = 0U;

static char nfc_last_text[NDEF_TEXT_MAX_LENGTH];

static const ST25DVxxKC_PASSWD_t nfc_i2c_password =
{
    .MsbPasswd = 0x00000000,
    .LsbPasswd = 0x00000000
};

static void NFC_ST_ProcessCommandText(const char *text);
static uint16_t NFC_ST_WaitReady(uint32_t timeout_ms);
static uint16_t NFC_ST_WaitInit(uint32_t timeout_ms);
static uint16_t NFC_ST_WaitFormat(uint32_t timeout_ms);
static uint16_t NFC_ST_ReadItStatus(uint8_t *it_status);
static uint16_t NFC_ST_WaitRfWriteDone(uint8_t *last_it_status);
static uint16_t NFC_ST_ValidateFirstRecord(const uint8_t *ndef, uint16_t ndef_size);
static uint16_t NFC_ST_ReadTextValidated(char *text, size_t text_size);
static uint16_t NFC_ST_RepairNdefText(void);
static uint16_t NFC_ST_WriteRepairTextDirect(const char *text);
static uint8_t NFC_ST_IsCcFileValid(uint32_t *ndef_offset, uint32_t *memory_size);

uint16_t NFC_ST_Init(uint8_t allow_format)
{
    uint16_t status;

    status = NFC_ST_WaitInit(NFC_ST_INIT_TIMEOUT_MS);
    if (status != NDEF_OK)
    {
        nfc_st_last_init_status = status;
        return status;
    }

    NfcTag_SelectProtocol(NFCTAG_TYPE5);

    if (!allow_format)
    {
        (void)CUSTOM_GPO_Init();
        nfc_st_last_init_status = NDEF_OK;
        return NDEF_OK;
    }

    (void)NFC_TAG_ResetMBEN_Dyn(NFC_TAG_INSTANCE);

    status = NfcType5_NDEFDetection();
    if (status != NDEF_OK)
    {
        CCFileStruct.MagicNumber = NFCT5_MAGICNUMBER_E1_CCFILE;
        CCFileStruct.Version = NFCT5_VERSION_V1_0;
        CCFileStruct.MemorySize = (ST25DVXXKC_MAX_SIZE / 8) & 0xFF;
        CCFileStruct.TT5Tag = 0x05;

        status = NFC_ST_WaitFormat(NFC_ST_FORMAT_TIMEOUT_MS);
        if (status != NDEF_OK)
        {
            nfc_st_last_init_status = status;
            return status;
        }
        HAL_Delay(5U);
    }

    (void)NFC_TAG_PresentPwd(NFC_TAG_INSTANCE, nfc_i2c_password);
    (void)NFC_TAG_ConfigIT(NFC_TAG_INSTANCE,
                           ST25DVXXKC_GPO1_ENABLE_MASK |
                           ST25DVXXKC_GPO1_RFWRITE_MASK);
    (void)CUSTOM_GPO_Init();

    nfc_st_last_init_status = NDEF_OK;
    return NDEF_OK;
}

void NFC_ST_Process(void)
{
    uint8_t it_status = 0U;

    if (!nfc_gpo_flag)
    {
        return;
    }

    rf_processing = 1U;
    nfc_gpo_flag = 0U;

    if (NFC_ST_ReadItStatus(&it_status) != NDEF_OK)
    {
        rf_processing = 0U;
        return;
    }

    if (it_status & ST25DVXXKC_ITSTS_DYN_RFWRITE_MASK)
    {
        nfc_st_rfwrite_count++;
        if (NFC_ST_WaitRfWriteDone(&it_status) == NDEF_OK)
        {
            uint16_t read_status;

            read_status = NFC_ST_ReadTextValidated(nfc_last_text, sizeof(nfc_last_text));
            nfc_st_last_ndef_status = read_status;
            if (read_status == NDEF_OK)
            {
                NFC_ST_ProcessCommandText(nfc_last_text);
            }
            else if ((read_status == NDEF_ERROR_NOT_FORMATED) ||
                     (read_status == NDEF_ERROR_MEMORY_INTERNAL))
            {
                nfc_st_malformed_count++;
                (void)NFC_ST_RepairNdefText();
            }
        }
    }

    rf_processing = 0U;
}

uint16_t NFC_ST_ReadText(char *text, size_t text_size)
{
    return NFC_ST_ReadTextValidated(text, text_size);
}

uint16_t NFC_ST_WriteText(const char *text)
{
    if (text == NULL)
    {
        return NDEF_ERROR;
    }

    return NDEF_WriteText((char *)text);
}

const char *NFC_ST_GetLastText(void)
{
    return nfc_last_text;
}

static uint16_t NFC_ST_WaitReady(uint32_t timeout_ms)
{
    uint32_t start = HAL_GetTick();

    do
    {
        if (CUSTOM_NFCTAG_IsDeviceReady(CUSTOM_NFCTAG_INSTANCE, 1U) == NFCTAG_OK)
        {
            return NDEF_OK;
        }

        HAL_Delay(1U);
    } while ((HAL_GetTick() - start) < timeout_ms);

    nfc_st_ready_timeout_count++;
    nfc_st_last_ndef_status = NDEF_ERROR;
    return NDEF_ERROR;
}

static uint16_t NFC_ST_WaitInit(uint32_t timeout_ms)
{
    uint32_t start = HAL_GetTick();

    do
    {
        if (NFC_TAG_Init(NFC_TAG_INSTANCE) == NFCTAG_OK)
        {
            return NDEF_OK;
        }

        HAL_Delay(1U);
    } while ((HAL_GetTick() - start) < timeout_ms);

    nfc_st_wait_timeout_count++;
    return NDEF_ERROR;
}

static uint16_t NFC_ST_WaitFormat(uint32_t timeout_ms)
{
    uint32_t start = HAL_GetTick();

    do
    {
        if (NfcType5_TT5Init() == NDEF_OK)
        {
            return NDEF_OK;
        }

        HAL_Delay(1U);
    } while ((HAL_GetTick() - start) < timeout_ms);

    nfc_st_wait_timeout_count++;
    return NDEF_ERROR_NOT_FORMATED;
}

static uint16_t NFC_ST_ReadItStatus(uint8_t *it_status)
{
    if (it_status == NULL)
    {
        return NDEF_ERROR;
    }

    if (NFC_ST_WaitReady(NFC_ST_READY_TIMEOUT_MS) != NDEF_OK)
    {
        return NDEF_ERROR;
    }

    if (NFC_TAG_ReadITST(NFC_TAG_INSTANCE, it_status) != NFCTAG_OK)
    {
        nfc_st_last_ndef_status = NDEF_ERROR;
        return NDEF_ERROR;
    }

    nfc_st_last_it_status = *it_status;
    return NDEF_OK;
}

static uint16_t NFC_ST_WaitRfWriteDone(uint8_t *last_it_status)
{
    uint32_t start = HAL_GetTick();
    uint8_t it_status = 0U;

    HAL_Delay(NFC_ST_RFWRITE_SETTLE_DELAY_MS);

    do
    {
        if (NFC_ST_ReadItStatus(&it_status) != NDEF_OK)
        {
            return NDEF_ERROR;
        }

        if ((it_status & ST25DVXXKC_ITSTS_DYN_RFWRITE_MASK) == 0U)
        {
            if (last_it_status != NULL)
            {
                *last_it_status = it_status;
            }
            return NFC_ST_WaitReady(NFC_ST_READY_TIMEOUT_MS);
        }

        HAL_Delay(NFC_ST_RFWRITE_POLL_DELAY_MS);
    } while ((HAL_GetTick() - start) < NFC_ST_RFWRITE_DONE_TIMEOUT_MS);

    nfc_st_wait_timeout_count++;
    nfc_st_last_it_status = it_status;
    if (last_it_status != NULL)
    {
        *last_it_status = it_status;
    }

    return NDEF_ERROR;
}

static uint16_t NFC_ST_ValidateFirstRecord(const uint8_t *ndef, uint16_t ndef_size)
{
    uint32_t payload_len;
    uint16_t offset;
    uint8_t type_len;
    uint8_t id_len = 0U;

    if ((ndef == NULL) || (ndef_size < 3U))
    {
        return NDEF_ERROR_NOT_FORMATED;
    }

    type_len = ndef[1];
    if (ndef[0] & SR_Mask)
    {
        payload_len = ndef[2];
        offset = 3U;
    }
    else
    {
        if (ndef_size < 6U)
        {
            return NDEF_ERROR_NOT_FORMATED;
        }
        payload_len = (((uint32_t)ndef[2]) << 24) |
                      (((uint32_t)ndef[3]) << 16) |
                      (((uint32_t)ndef[4]) << 8) |
                      ((uint32_t)ndef[5]);
        offset = 6U;
    }

    if (ndef[0] & IL_Mask)
    {
        if (offset >= ndef_size)
        {
            return NDEF_ERROR_NOT_FORMATED;
        }
        id_len = ndef[offset];
        offset++;
    }

    if (((uint32_t)type_len + (uint32_t)id_len + payload_len) >
        ((uint32_t)ndef_size - (uint32_t)offset))
    {
        return NDEF_ERROR_NOT_FORMATED;
    }

    return NDEF_OK;
}

static uint16_t NFC_ST_ReadTextValidated(char *text, size_t text_size)
{
    sRecordInfo_t record;
    NDEF_Text_info_t text_info;
    uint16_t status;
    uint16_t ndef_size = 0U;

    memset(&record, 0, sizeof(record));
    memset(&text_info, 0, sizeof(text_info));

    status = NfcTag_GetLength(&ndef_size);
    if (status != NDEF_OK)
    {
        nfc_st_last_ndef_status = status;
        return status;
    }

    if (ndef_size == 0U)
    {
        nfc_st_last_ndef_status = NDEF_ERROR;
        return NDEF_ERROR;
    }

    if (ndef_size > NDEF_MAX_SIZE)
    {
        nfc_st_last_ndef_status = NDEF_ERROR_MEMORY_INTERNAL;
        return NDEF_ERROR_MEMORY_INTERNAL;
    }

    status = NfcTag_ReadNDEF(NDEF_Buffer);
    if (status != NDEF_OK)
    {
        nfc_st_last_ndef_status = status;
        return status;
    }

    status = NFC_ST_ValidateFirstRecord(NDEF_Buffer, ndef_size);
    if (status != NDEF_OK)
    {
        nfc_st_last_ndef_status = status;
        return status;
    }

    status = NDEF_IdentifyBuffer(&record, NDEF_Buffer);
    if (status != NDEF_OK)
    {
        nfc_st_last_ndef_status = status;
        return status;
    }

    if (record.NDEF_Type != TEXT_TYPE)
    {
        nfc_st_last_ndef_status = NDEF_ERROR;
        return NDEF_ERROR;
    }

    status = NDEF_ReadText(&record, &text_info);
    if (status != NDEF_OK)
    {
        nfc_st_last_ndef_status = status;
        return status;
    }

    if ((text != NULL) && (text_size > 0U))
    {
        (void)strncpy(text, text_info.text, text_size - 1U);
        text[text_size - 1U] = '\0';
    }

    nfc_st_last_ndef_status = NDEF_OK;
    return NDEF_OK;
}

static uint16_t NFC_ST_RepairNdefText(void)
{
    uint16_t status;

    status = NFC_ST_WriteRepairTextDirect(NFC_ST_REPAIR_TEXT);
    nfc_st_last_ndef_status = status;
    if (status == NDEF_OK)
    {
        nfc_st_repair_count++;
    }

    return status;
}

static uint16_t NFC_ST_WriteRepairTextDirect(const char *text)
{
    uint8_t repair_buffer[NDEF_TEXT_MAX_LENGTH + 8U];
    uint32_t ndef_offset = 0U;
    uint32_t memory_size = 0U;
    uint32_t offset = 0U;
    size_t text_len;
    uint8_t ndef_len;
    uint8_t terminator = NFCT5_TERMINATOR_TLV;

    if ((text == NULL) ||
        (NFC_ST_IsCcFileValid(&ndef_offset, &memory_size) == 0U) ||
        (NFC_ST_WaitReady(NFC_ST_READY_TIMEOUT_MS) != NDEF_OK))
    {
        return NDEF_ERROR_NOT_FORMATED;
    }

    text_len = strlen(text);
    if (text_len > (NDEF_TEXT_MAX_LENGTH - 1U))
    {
        return NDEF_ERROR_MEMORY_INTERNAL;
    }

    ndef_len = (uint8_t)(3U + text_len + 4U);
    if ((ndef_offset + 2U + ndef_len + 1U) > memory_size)
    {
        return NDEF_ERROR_MEMORY_TAG;
    }

    repair_buffer[offset++] = NFCT5_NDEF_MSG_TLV;
    repair_buffer[offset++] = ndef_len;
    repair_buffer[offset++] = 0xD1U;
    repair_buffer[offset++] = TEXT_TYPE_STRING_LENGTH;
    repair_buffer[offset++] = (uint8_t)(3U + text_len);
    repair_buffer[offset++] = TEXT_TYPE_STRING[0];
    repair_buffer[offset++] = ISO_ENGLISH_CODE_STRING_LENGTH;
    repair_buffer[offset++] = ISO_ENGLISH_CODE_STRING[0];
    repair_buffer[offset++] = ISO_ENGLISH_CODE_STRING[1];
    memcpy(&repair_buffer[offset], text, text_len);
    offset += (uint32_t)text_len;

    if (NDEF_Wrapper_WriteData(repair_buffer, ndef_offset, offset) != NDEF_OK)
    {
        return NDEF_ERROR;
    }

    if (NDEF_Wrapper_WriteData(&terminator, ndef_offset + offset, sizeof(terminator)) != NDEF_OK)
    {
        return NDEF_ERROR;
    }

    return NDEF_OK;
}

static uint8_t NFC_ST_IsCcFileValid(uint32_t *ndef_offset, uint32_t *memory_size)
{
    uint8_t cc_file[8] = {0U};

    if (NfcType5_ReadCCFile(cc_file) != NDEF_OK)
    {
        return 0U;
    }

    if ((cc_file[0] != NFCT5_MAGICNUMBER_E1_CCFILE) &&
        (cc_file[0] != NFCT5_MAGICNUMBER_E2_CCFILE))
    {
        return 0U;
    }

    if (((cc_file[1] & 0xFCU) != 0x40U) &&
        ((cc_file[1] & 0xFCU) != 0x10U))
    {
        return 0U;
    }

    if (cc_file[2] == NFCT5_EXTENDED_CCFILE)
    {
        if (ndef_offset != NULL)
        {
            *ndef_offset = ccFileOffset + 8U;
        }
        if (memory_size != NULL)
        {
            *memory_size = (uint32_t)(((uint16_t)cc_file[6] << 8) | cc_file[7]) * 8U;
        }
    }
    else
    {
        if (ndef_offset != NULL)
        {
            *ndef_offset = ccFileOffset + 4U;
        }
        if (memory_size != NULL)
        {
            *memory_size = (uint32_t)cc_file[2] * 8U;
        }
    }

    return 1U;
}

static void NFC_ST_ProcessCommandText(const char *text)
{
    NFC_CommandFrame_t cmd;
    NFC_ResponseFrame_t rsp;
    NFC_ProtoStatus_t proto_st;
    uint8_t rsp_text[NFC_PROTO_MAX_TEXT_LEN];

    proto_st = NFC_Proto_ParseTextHex((const uint8_t *)text, &cmd);
    if (proto_st != NFC_PROTO_OK)
    {
        NFC_Proto_BuildError(&rsp, 0x01U);
    }
    else
    {
        proto_st = NFC_Proto_ValidateCommand(&cmd);
        if (proto_st != NFC_PROTO_OK)
        {
            NFC_Proto_BuildError(&rsp, 0x02U);
        }
        else
        {
            switch (cmd.cmd)
            {
            case NFC_CMD_SET_CHARGER_CURRENT:
            {
                NFC_CmdSetChargerCurrent_t p;
                (void)NFC_Proto_GetSetChargerCurrent(&cmd, &p);

                if (BQ25155_SetFastChargeCurrent(&BQ, p.current_mA) == HAL_OK)
                {
                    NFC_Proto_BuildAck(&rsp);
                }
                else
                {
                    NFC_Proto_BuildError(&rsp, 0x01U);
                }
                break;
            }

            case NFC_CMD_SET_LED_CURRENT:
            {
                NFC_CmdSetLedCurrent_t p;
                (void)NFC_Proto_GetSetLedCurrent(&cmd, &p);

                if (LP5817_SetChannelCurrent_mA(&hi2c3, p.led_id, p.current_mA) == HAL_OK)
                {
                    NFC_Proto_BuildAck(&rsp);
                }
                else
                {
                    NFC_Proto_BuildError(&rsp, 0x02U);
                }
                break;
            }

            case NFC_CMD_SET_LED_PULSE:
            {
                NFC_CmdSetLedPulse_t p;
                (void)NFC_Proto_GetSetLedPulse(&cmd, &p);

                if (p.led_id < STIM_LED_COUNT)
                {
                    NFC_Proto_BuildAck(&rsp);
                }
                else
                {
                    NFC_Proto_BuildError(&rsp, 0x03U);
                }
                break;
            }

            case NFC_CMD_SET_LED_FULL_SETUP:
            {
                NFC_CmdSetLedFullSetup_t p;
                RTC_ST_DateTime_t now;
                StimSessionCommand_t session;
                uint8_t started_now = 0U;

                (void)NFC_Proto_GetSetLedFullSetup(&cmd, &p);

                if ((p.led_id >= STIM_LED_COUNT) ||
                    (RTC_ST_GetDateTime(&hrtc, &now) != HAL_OK))
                {
                    NFC_Proto_BuildError(&rsp, 0x04U);
                    break;
                }

                memset(&session, 0, sizeof(session));
                session.slot = p.led_id;
                session.led_id = p.led_id;
                session.month = now.month;
                session.day = now.day;
                session.hour = now.hh;
                session.minute = now.min;
                session.second = now.ss;
                session.current_mA = p.current;
                session.frequency_hz = p.freq;
                session.duty_percent = p.duty;
                session.duration_value = p.duration;
                session.duration_unit = STIM_DURATION_SECONDS;

                if (StimScheduler_SetSession(&session, &started_now) == HAL_OK)
                {
                    NFC_Proto_BuildAck(&rsp);
                }
                else
                {
                    NFC_Proto_BuildError(&rsp, 0x04U);
                }
                break;
            }

            case NFC_CMD_READ_BATTERY_VOLT:
            {
                uint16_t batt_mV;

                if (BQ25155_ReadVBAT(&BQ, &batt_mV) == HAL_OK)
                {
                    NFC_Proto_BuildBatteryVoltage(&rsp, batt_mV);
                }
                else
                {
                    NFC_Proto_BuildError(&rsp, 0x10U);
                }
                break;
            }

            case NFC_CMD_READ_LED_STATUS:
            {
                uint8_t led0_dc;
                uint8_t led1_dc;
                uint8_t led0_en;
                uint8_t led1_en;
                uint8_t payload[4];

                if (LP5817_GetLED01Settings(&hi2c3, &led0_dc, &led1_dc, &led0_en, &led1_en) == HAL_OK)
                {
                    payload[0] = led0_dc;
                    payload[1] = led0_en;
                    payload[2] = led1_dc;
                    payload[3] = led1_en;
                    NFC_Proto_BuildLedStatus(&rsp, payload, sizeof(payload));
                }
                else
                {
                    NFC_Proto_BuildError(&rsp, 0x11U);
                }
                break;
            }

            case NFC_CMD_READ_MCU_TIME:
            {
                RTC_ST_DateTime_t dt;

                if (RTC_ST_GetDateTime(&hrtc, &dt) == HAL_OK)
                {
                    NFC_Proto_BuildRtcTime(&rsp, dt.month, dt.day, dt.hh, dt.min, dt.ss);
                }
                else
                {
                    NFC_Proto_BuildError(&rsp, 0x20U);
                }
                break;
            }

            case NFC_CMD_SET_MCU_TIME:
            {
                NFC_CmdSetTime_t p;
                (void)NFC_Proto_GetSetTime(&cmd, &p);

                if (RTC_ST_SetDateTime(&hrtc, p.mm, p.dd, p.hh, p.min, p.ss) == HAL_OK)
                {
                    NFC_Proto_BuildAck(&rsp);
                }
                else
                {
                    NFC_Proto_BuildError(&rsp, 0x21U);
                }
                break;
            }

            case NFC_CMD_SET_LED_SESSION:
            {
                NFC_CmdSetLedSession_t p;
                StimSessionCommand_t session;
                uint8_t started_now = 0U;

                (void)NFC_Proto_GetSetLedSession(&cmd, &p);

                memset(&session, 0, sizeof(session));
                session.slot = p.slot;
                session.led_id = p.led_id;
                session.month = p.mm;
                session.day = p.dd;
                session.hour = p.hh;
                session.minute = p.min;
                session.second = p.ss;
                session.current_mA = p.current_mA;
                session.frequency_hz = p.frequency_hz;
                session.duty_percent = p.duty_percent;
                session.duration_value = p.duration_value;
                session.duration_unit = p.duration_unit;

                if (StimScheduler_SetSession(&session, &started_now) == HAL_OK)
                {
                    NFC_Proto_BuildAck(&rsp);
                }
                else
                {
                    NFC_Proto_BuildError(&rsp, 0x30U);
                }
                break;
            }

            case NFC_CMD_CLEAR_SESSION:
                if (StimScheduler_ClearSession(cmd.payload[0]) == HAL_OK)
                {
                    NFC_Proto_BuildAck(&rsp);
                }
                else
                {
                    NFC_Proto_BuildError(&rsp, 0x31U);
                }
                break;

            case NFC_CMD_READ_SCHED_STATUS:
            {
                StimSchedulerStatus_t st;
                uint8_t payload[NFC_SCHED_STATUS_EXT_HEADER_LEN +
                                (STIM_SCHED_SLOT_COUNT * NFC_SCHED_STATUS_ENTRY_LEN)];
                uint8_t payload_len = NFC_SCHED_STATUS_EXT_HEADER_LEN;
                uint8_t status_ok = 1U;

                if (StimScheduler_GetStatus(&st) == HAL_OK)
                {
                    payload[0] = st.active_mask;
                    payload[1] = st.next_valid;
                    payload[2] = st.next_slot;
                    payload[3] = st.table_valid;
                    payload[4] = NFC_SCHED_STATUS_FORMAT_WITH_TIMES;
                    payload[5] = 0U;

                    for (uint8_t i = 0U; i < STIM_SCHED_SLOT_COUNT; i++)
                    {
                        StimSession_t s;
                        if (StimScheduler_GetSession(i, &s) != HAL_OK)
                        {
                            status_ok = 0U;
                            break;
                        }

                        if (s.state == STIM_SESSION_EMPTY)
                        {
                            continue;
                        }

                        payload[payload_len++] = i;
                        payload[payload_len++] = s.state;
                        payload[payload_len++] = s.month;
                        payload[payload_len++] = s.day;
                        payload[payload_len++] = s.hour;
                        payload[payload_len++] = s.minute;
                        payload[payload_len++] = s.second;
                        payload[5]++;
                    }

                    if (status_ok)
                    {
                        NFC_Proto_BuildScheduleStatus(&rsp, payload, payload_len);
                    }
                    else
                    {
                        NFC_Proto_BuildError(&rsp, 0x32U);
                    }
                }
                else
                {
                    NFC_Proto_BuildError(&rsp, 0x32U);
                }
                break;
            }

            case NFC_CMD_STOP_ACTIVE_LED:
                if (StimScheduler_StopActiveLed(cmd.payload[0]) == HAL_OK)
                {
                    NFC_Proto_BuildAck(&rsp);
                }
                else
                {
                    NFC_Proto_BuildError(&rsp, 0x33U);
                }
                break;

            case NFC_CMD_CLEAR_ALL_SESSIONS:
                if (StimScheduler_ClearAll() == HAL_OK)
                {
                    NFC_Proto_BuildAck(&rsp);
                }
                else
                {
                    NFC_Proto_BuildError(&rsp, 0x34U);
                }
                break;

            default:
                NFC_Proto_BuildError(&rsp, 0x03U);
                break;
            }
        }
    }

    if (NFC_Proto_EncodeResponseTextHex(&rsp, rsp_text, sizeof(rsp_text)) == NFC_PROTO_OK)
    {
        (void)NFC_ST_WriteText((const char *)rsp_text);
    }
}
