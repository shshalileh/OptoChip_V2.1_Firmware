#include "My_ST25DV04.h"

#include <string.h>

extern volatile uint8_t nfc_gpo_flag;
extern volatile uint8_t rf_processing;

static char nfc_last_text[NDEF_TEXT_MAX_LENGTH];

static const ST25DVxxKC_PASSWD_t nfc_i2c_password =
{
    .MsbPasswd = 0x00000000,
    .LsbPasswd = 0x00000000
};

void NFC_ST_Init(uint8_t allow_format)
{
    while (NFC_TAG_Init(NFC_TAG_INSTANCE) != NFCTAG_OK) {}

    (void)CUSTOM_GPO_Init();
    (void)NFC_TAG_PresentPwd(NFC_TAG_INSTANCE, nfc_i2c_password);
    (void)NFC_TAG_ConfigIT(NFC_TAG_INSTANCE,
                           ST25DVXXKC_GPO1_ENABLE_MASK |
                           ST25DVXXKC_GPO1_FIELDCHANGE_MASK);
    (void)NFC_TAG_ResetMBEN_Dyn(NFC_TAG_INSTANCE);

    NfcTag_SelectProtocol(NFCTAG_TYPE5);

    if (!allow_format)
    {
        return;
    }

    if (NfcType5_NDEFDetection() != NDEF_OK)
    {
        CCFileStruct.MagicNumber = NFCT5_MAGICNUMBER_E1_CCFILE;
        CCFileStruct.Version = NFCT5_VERSION_V1_0;
        CCFileStruct.MemorySize = (ST25DVXXKC_MAX_SIZE / 8) & 0xFF;
        CCFileStruct.TT5Tag = 0x05;

        while (NfcType5_TT5Init() != NFCTAG_OK) {}
        HAL_Delay(5U);
    }
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

    if (CUSTOM_NFCTAG_IsDeviceReady(CUSTOM_NFCTAG_INSTANCE, 1U) != NFCTAG_OK)
    {
        rf_processing = 0U;
        return;
    }

    if (CUSTOM_NFCTAG_ReadITSTStatus_Dyn(CUSTOM_NFCTAG_INSTANCE, &it_status) != NFCTAG_OK)
    {
        rf_processing = 0U;
        return;
    }

    if (it_status & ST25DVXXKC_ITSTS_DYN_RFWRITE_MASK)
    {
        HAL_Delay(100U);

        while (1)
        {
            if (CUSTOM_NFCTAG_IsDeviceReady(CUSTOM_NFCTAG_INSTANCE, 1U) != NFCTAG_OK)
            {
                rf_processing = 0U;
                return;
            }

            if (NFC_TAG_ReadITST(NFC_TAG_INSTANCE, &it_status) != NFCTAG_OK)
            {
                rf_processing = 0U;
                return;
            }

            if (it_status & ST25DVXXKC_ITSTS_DYN_RFWRITE_MASK)
            {
                HAL_Delay(100U);
            }
            else
            {
                break;
            }
        }

        (void)NFC_ST_ReadText(nfc_last_text, sizeof(nfc_last_text));
    }

    rf_processing = 0U;
}

uint16_t NFC_ST_ReadText(char *text, size_t text_size)
{
    sRecordInfo_t record;
    NDEF_Text_info_t text_info;
    uint16_t status;

    memset(&record, 0, sizeof(record));
    memset(&text_info, 0, sizeof(text_info));

    status = NDEF_IdentifyNDEF(&record, NDEF_Buffer);
    if (status != NDEF_OK)
    {
        return status;
    }

    if (record.NDEF_Type != TEXT_TYPE)
    {
        return NDEF_ERROR;
    }

    status = NDEF_ReadText(&record, &text_info);
    if (status != NDEF_OK)
    {
        return status;
    }

    if ((text != NULL) && (text_size > 0U))
    {
        (void)strncpy(text, text_info.text, text_size - 1U);
        text[text_size - 1U] = '\0';
    }

    return NDEF_OK;
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
