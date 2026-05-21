#pragma once

#include "stm32u0xx_hal.h"

#include <stddef.h>
#include <stdint.h>

#include "custom_nfc07a1.h"
#include "custom_nfc07a1_nfctag.h"
#include "lib_NDEF.h"
#include "lib_NDEF_Text.h"
#include "lib_wrapper.h"
#include "tagtype5_wrapper.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NFC_TAG_INSTANCE      CUSTOM_NFCTAG_INSTANCE
#define NFC_TAG_Init          CUSTOM_NFCTAG_Init
#define NFC_TAG_ResetMBEN_Dyn CUSTOM_NFCTAG_ResetMBEN_Dyn
#define NFC_TAG_PresentPwd    CUSTOM_NFCTAG_PresentI2CPassword
#define NFC_TAG_ConfigIT      CUSTOM_NFCTAG_ConfigIT
#define NFC_TAG_ReadITST      CUSTOM_NFCTAG_ReadITSTStatus_Dyn

void NFC_ST_Init(uint8_t allow_format);
void NFC_ST_Process(void);
uint16_t NFC_ST_ReadText(char *text, size_t text_size);
uint16_t NFC_ST_WriteText(const char *text);
const char *NFC_ST_GetLastText(void);

#ifdef __cplusplus
}
#endif
