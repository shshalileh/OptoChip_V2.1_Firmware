#pragma once

#include <stdint.h>

#define OTA_FRAME_MAGIC                 0x4F544146UL /* "OTAF" */
#define OTA_FRAME_HEADER_LEN            24U
#define OTA_FRAME_VERSION               1U

#define OTA_CMD_HELLO                   0x01U
#define OTA_CMD_BEGIN                   0x02U
#define OTA_CMD_DATA                    0x03U
#define OTA_CMD_COMMIT                  0x04U
#define OTA_CMD_STATUS                  0x05U
#define OTA_CMD_ABORT                   0x06U
#define OTA_RSP_FLAG                    0x80U

#define OTA_STATUS_OK                   0x00U
#define OTA_STATUS_ERROR                0x01U
#define OTA_STATUS_DUPLICATE            0x02U

typedef struct
{
    uint8_t cmd;
    uint8_t version;
    uint16_t seq;
    uint32_t offset;
    uint16_t len;
    uint16_t flags;
    uint32_t arg0;
    const uint8_t *payload;
} OTA_FrameView_t;
