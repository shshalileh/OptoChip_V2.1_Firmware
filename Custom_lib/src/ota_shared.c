#include "ota_shared.h"

#include <string.h>

uint32_t OTA_Crc32Update(uint32_t crc, const uint8_t *data, uint32_t len)
{
    if (data == NULL)
    {
        return crc;
    }

    while (len-- > 0UL)
    {
        crc ^= (uint32_t)(*data++);
        for (uint8_t i = 0U; i < 8U; i++)
        {
            crc = (crc >> 1) ^ (0xEDB88320UL & (uint32_t)(0UL - (crc & 1UL)));
        }
    }

    return crc;
}

uint32_t OTA_Crc32Finish(uint32_t crc)
{
    return ~crc;
}

uint32_t OTA_MetadataCrc32(const OTA_Metadata_t *metadata)
{
    OTA_Metadata_t temp;

    if (metadata == NULL)
    {
        return 0UL;
    }

    memcpy(&temp, metadata, sizeof(temp));
    temp.metadata_crc32 = 0UL;
    return OTA_Crc32Finish(OTA_Crc32Update(0xFFFFFFFFUL, (const uint8_t *)&temp, sizeof(temp)));
}

uint8_t OTA_MetadataIsSane(const OTA_Metadata_t *metadata)
{
    if (metadata == NULL)
    {
        return 0U;
    }

    if ((metadata->magic != OTA_METADATA_MAGIC) ||
        (metadata->version != OTA_METADATA_VERSION) ||
        (metadata->header_size != sizeof(OTA_Metadata_t)) ||
        (metadata->app_base != OTA_APP_BASE_ADDR) ||
        (metadata->app_max_size != OTA_APP_MAX_SIZE))
    {
        return 0U;
    }

    return (metadata->metadata_crc32 == OTA_MetadataCrc32(metadata)) ? 1U : 0U;
}

uint8_t OTA_AppVectorIsValid(uint32_t app_base, uint32_t app_size)
{
    uint32_t stack = *(const uint32_t *)app_base;
    uint32_t reset = *(const uint32_t *)(app_base + 4UL);
    uint32_t app_end = app_base + app_size;

    if ((stack < 0x20000000UL) || (stack > 0x20003000UL))
    {
        return 0U;
    }

    if ((reset < app_base) || (reset >= app_end) || ((reset & 1UL) == 0UL))
    {
        return 0U;
    }

    return 1U;
}

static HAL_StatusTypeDef metadata_erase(void)
{
    FLASH_EraseInitTypeDef erase = {0};
    uint32_t page_error = 0UL;

    erase.TypeErase = FLASH_TYPEERASE_PAGES;
    erase.Banks = FLASH_BANK_1;
    erase.Page = OTA_METADATA_PAGE_INDEX;
    erase.NbPages = 1UL;

    return HAL_FLASHEx_Erase(&erase, &page_error);
}

HAL_StatusTypeDef OTA_MetadataWrite(const OTA_Metadata_t *metadata)
{
    uint64_t words_buf[(sizeof(OTA_Metadata_t) + 7U) / 8U];
    OTA_Metadata_t *temp = (OTA_Metadata_t *)words_buf;
    uint32_t addr = OTA_METADATA_BASE_ADDR;
    uint32_t words;
    HAL_StatusTypeDef status;

    if (metadata == NULL)
    {
        return HAL_ERROR;
    }

    memset(words_buf, 0xFF, sizeof(words_buf));
    memcpy(temp, metadata, sizeof(*temp));
    temp->magic = OTA_METADATA_MAGIC;
    temp->version = OTA_METADATA_VERSION;
    temp->header_size = sizeof(OTA_Metadata_t);
    temp->app_base = OTA_APP_BASE_ADDR;
    temp->app_max_size = OTA_APP_MAX_SIZE;
    temp->metadata_crc32 = OTA_MetadataCrc32(temp);

    status = HAL_FLASH_Unlock();
    if (status != HAL_OK)
    {
        return status;
    }

    status = metadata_erase();
    if (status == HAL_OK)
    {
        words = (uint32_t)(sizeof(words_buf) / sizeof(words_buf[0]));
        for (uint32_t i = 0UL; i < words; i++)
        {
            status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, addr, words_buf[i]);
            if (status != HAL_OK)
            {
                break;
            }
            addr += OTA_FLASH_DOUBLEWORD_SIZE;
        }
    }

    (void)HAL_FLASH_Lock();
    return status;
}

HAL_StatusTypeDef OTA_MetadataClear(void)
{
    HAL_StatusTypeDef status;

    status = HAL_FLASH_Unlock();
    if (status == HAL_OK)
    {
        status = metadata_erase();
    }
    (void)HAL_FLASH_Lock();
    return status;
}

HAL_StatusTypeDef OTA_RequestBootloader(void)
{
    OTA_Metadata_t metadata;

    memset(&metadata, 0, sizeof(metadata));
    metadata.state = OTA_STATE_REQUESTED;
    metadata.error = OTA_ERR_NONE;

    return OTA_MetadataWrite(&metadata);
}
