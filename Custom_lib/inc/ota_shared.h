#pragma once

#include "stm32u0xx_hal.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OTA_BOOTLOADER_BASE_ADDR        0x08000000UL
#define OTA_BOOTLOADER_SIZE             0x00003000UL
#define OTA_APP_BASE_ADDR               0x08003000UL
#define OTA_APP_MAX_SIZE                0x0000C000UL
#define OTA_METADATA_BASE_ADDR          0x0800F000UL
#define OTA_METADATA_PAGE_INDEX         30UL
#define OTA_SCHEDULER_BASE_ADDR         0x0800F800UL
#define OTA_SCHEDULER_PAGE_INDEX        31UL

#define OTA_FLASH_PAGE_SIZE             0x800UL
#define OTA_FLASH_DOUBLEWORD_SIZE       8UL
#define OTA_MAILBOX_MAX_PAYLOAD         256U
#define OTA_DATA_MAX_PAYLOAD            224U

#define OTA_METADATA_MAGIC              0x4F54414DUL /* "OTAM" */
#define OTA_METADATA_VERSION            1U
#define OTA_UNLOCK_TOKEN                0x4E46434FUL /* "NFCO" */

typedef enum
{
    OTA_STATE_IDLE = 0,
    OTA_STATE_REQUESTED = 1,
    OTA_STATE_RECEIVING = 2,
    OTA_STATE_READY = 3,
    OTA_STATE_VALID = 4,
    OTA_STATE_ERROR = 5
} OTA_State_t;

typedef enum
{
    OTA_ERR_NONE = 0,
    OTA_ERR_UNLOCK = 1,
    OTA_ERR_SIZE = 2,
    OTA_ERR_OFFSET = 3,
    OTA_ERR_FRAME_CRC = 4,
    OTA_ERR_IMAGE_CRC = 5,
    OTA_ERR_VECTOR = 6,
    OTA_ERR_FLASH = 7,
    OTA_ERR_PROTOCOL = 8
} OTA_Error_t;

typedef struct
{
    uint32_t magic;
    uint16_t version;
    uint16_t header_size;
    uint32_t state;
    uint32_t app_base;
    uint32_t app_max_size;
    uint32_t image_size;
    uint32_t expected_crc32;
    uint32_t received_size;
    uint32_t sequence;
    uint32_t error;
    uint32_t flags;
    uint32_t metadata_crc32;
} OTA_Metadata_t;

uint32_t OTA_Crc32Update(uint32_t crc, const uint8_t *data, uint32_t len);
uint32_t OTA_Crc32Finish(uint32_t crc);
uint32_t OTA_MetadataCrc32(const OTA_Metadata_t *metadata);
uint8_t OTA_MetadataIsSane(const OTA_Metadata_t *metadata);
uint8_t OTA_AppVectorIsValid(uint32_t app_base, uint32_t app_size);
HAL_StatusTypeDef OTA_MetadataWrite(const OTA_Metadata_t *metadata);
HAL_StatusTypeDef OTA_MetadataClear(void);
HAL_StatusTypeDef OTA_RequestBootloader(void);

#ifdef __cplusplus
}
#endif
