#include "main.h"
#include "custom_nfc07a1_nfctag.h"
#include "ota_bootloader_protocol.h"
#include "ota_shared.h"

#include <string.h>

#define BOOTLOADER_VERSION              0x00010000UL
#define MAILBOX_POLL_DELAY_MS           5U
#define MAILBOX_INIT_TIMEOUT_MS         1000U
#define MAILBOX_WAIT_TIMEOUT_MS         30000U

static uint8_t rx_buf[OTA_MAILBOX_MAX_PAYLOAD];
static uint8_t tx_buf[OTA_MAILBOX_MAX_PAYLOAD];
static OTA_Metadata_t ota_meta;
static uint32_t running_crc = 0xFFFFFFFFUL;

static void SystemClock_Config(void);
static void GPIO_MinimalInit(void);
static void jump_to_app(void);
static uint8_t should_enter_ota(void);
static HAL_StatusTypeDef mailbox_init(void);
static HAL_StatusTypeDef mailbox_static_enable(void);
static HAL_StatusTypeDef mailbox_read(uint8_t *buf, uint16_t *len);
static HAL_StatusTypeDef mailbox_write(const uint8_t *buf, uint16_t len);
static uint8_t parse_frame(const uint8_t *buf, uint16_t len, OTA_FrameView_t *frame);
static uint16_t build_response(uint8_t cmd, uint16_t seq, uint32_t offset,
                               uint32_t status, const uint8_t *payload, uint16_t payload_len);
static void process_frame(const OTA_FrameView_t *frame);
static HAL_StatusTypeDef erase_app_slot(uint32_t image_size);
static HAL_StatusTypeDef program_app_data(uint32_t offset, const uint8_t *data, uint16_t len);
static uint8_t flash_range_matches(uint32_t offset, const uint8_t *data, uint16_t len);
static void metadata_set_error(uint32_t err);
static void restore_running_crc(void);
static uint32_t rd32(const uint8_t *p);
static uint16_t rd16(const uint8_t *p);
static void wr32(uint8_t *p, uint32_t v);
static void wr16(uint8_t *p, uint16_t v);

int main(void)
{
    HAL_Init();
    SystemClock_Config();

    memcpy(&ota_meta, (const void *)OTA_METADATA_BASE_ADDR, sizeof(ota_meta));
    if (!should_enter_ota())
    {
        jump_to_app();
    }

    GPIO_MinimalInit();

    if (mailbox_init() != HAL_OK)
    {
        while (1)
        {
        }
    }

    if (!OTA_MetadataIsSane(&ota_meta))
    {
        memset(&ota_meta, 0, sizeof(ota_meta));
        ota_meta.state = OTA_STATE_REQUESTED;
        ota_meta.error = OTA_ERR_NONE;
        (void)OTA_MetadataWrite(&ota_meta);
    }
    else if ((ota_meta.state == OTA_STATE_RECEIVING) && (ota_meta.received_size > 0UL))
    {
        restore_running_crc();
    }

    while (1)
    {
        uint16_t len = 0U;
        OTA_FrameView_t frame;

        if (mailbox_read(rx_buf, &len) != HAL_OK)
        {
            continue;
        }

        if (parse_frame(rx_buf, len, &frame))
        {
            process_frame(&frame);
        }
        else
        {
            uint16_t rsp_len = build_response(0U, 0U, ota_meta.received_size,
                                              OTA_ERR_PROTOCOL, NULL, 0U);
            (void)mailbox_write(tx_buf, rsp_len);
        }
    }
}

static uint8_t should_enter_ota(void)
{
    if (OTA_MetadataIsSane(&ota_meta))
    {
        if ((ota_meta.state == OTA_STATE_REQUESTED) ||
            (ota_meta.state == OTA_STATE_RECEIVING) ||
            (ota_meta.state == OTA_STATE_ERROR))
        {
            return 1U;
        }
    }

    return OTA_AppVectorIsValid(OTA_APP_BASE_ADDR, OTA_APP_MAX_SIZE) ? 0U : 1U;
}

static void jump_to_app(void)
{
    uint32_t app_stack = *(const uint32_t *)OTA_APP_BASE_ADDR;
    uint32_t app_reset = *(const uint32_t *)(OTA_APP_BASE_ADDR + 4UL);
    void (*app_entry)(void) = (void (*)(void))app_reset;

    __disable_irq();
    SysTick->CTRL = 0UL;
    SysTick->LOAD = 0UL;
    SysTick->VAL = 0UL;
    SCB->VTOR = OTA_APP_BASE_ADDR;
    __set_MSP(app_stack);
    app_entry();
}

static HAL_StatusTypeDef mailbox_init(void)
{
    uint32_t start = HAL_GetTick();

    do
    {
        if (CUSTOM_NFCTAG_Init(CUSTOM_NFCTAG_INSTANCE) == NFCTAG_OK)
        {
            break;
        }
        HAL_Delay(1U);
    } while ((HAL_GetTick() - start) < MAILBOX_INIT_TIMEOUT_MS);

    if ((HAL_GetTick() - start) >= MAILBOX_INIT_TIMEOUT_MS)
    {
        return HAL_ERROR;
    }

    if (mailbox_static_enable() != HAL_OK)
    {
        return HAL_ERROR;
    }

    (void)CUSTOM_NFCTAG_ResetMBEN_Dyn(CUSTOM_NFCTAG_INSTANCE);
    if (CUSTOM_NFCTAG_SetMBEN_Dyn(CUSTOM_NFCTAG_INSTANCE) != NFCTAG_OK)
    {
        return HAL_ERROR;
    }

    return HAL_OK;
}

static HAL_StatusTypeDef mailbox_static_enable(void)
{
    ST25DVxxKC_EN_STATUS_E mb_mode;
    ST25DVxxKC_I2CSSO_STATUS_E i2c_session;
    ST25DVxxKC_PASSWD_t passwd = {0};

    if (CUSTOM_NFCTAG_ReadMBMode(CUSTOM_NFCTAG_INSTANCE, &mb_mode) != NFCTAG_OK)
    {
        return HAL_ERROR;
    }

    if (mb_mode == ST25DVXXKC_ENABLE)
    {
        return HAL_OK;
    }

    if (CUSTOM_NFCTAG_ReadI2CSecuritySession_Dyn(CUSTOM_NFCTAG_INSTANCE, &i2c_session) != NFCTAG_OK)
    {
        return HAL_ERROR;
    }

    if (i2c_session == ST25DVXXKC_SESSION_CLOSED)
    {
        if (CUSTOM_NFCTAG_PresentI2CPassword(CUSTOM_NFCTAG_INSTANCE, passwd) != NFCTAG_OK)
        {
            return HAL_ERROR;
        }
    }

    if (CUSTOM_NFCTAG_WriteMBMode(CUSTOM_NFCTAG_INSTANCE, ST25DVXXKC_ENABLE) != NFCTAG_OK)
    {
        return HAL_ERROR;
    }

    passwd.MsbPasswd = 123UL;
    passwd.LsbPasswd = 123UL;
    (void)CUSTOM_NFCTAG_PresentI2CPassword(CUSTOM_NFCTAG_INSTANCE, passwd);
    return HAL_OK;
}

static HAL_StatusTypeDef mailbox_read(uint8_t *buf, uint16_t *len)
{
    uint32_t start = HAL_GetTick();
    ST25DVxxKC_MB_CTRL_DYN_STATUS_t ctrl;
    uint8_t mb_len = 0U;
    uint16_t msg_len;

    if ((buf == NULL) || (len == NULL))
    {
        return HAL_ERROR;
    }

    do
    {
        if (CUSTOM_NFCTAG_ReadMBCtrl_Dyn(CUSTOM_NFCTAG_INSTANCE, &ctrl) == NFCTAG_OK)
        {
            if (ctrl.RfPutMsg != 0U)
            {
                if (CUSTOM_NFCTAG_ReadMBLength_Dyn(CUSTOM_NFCTAG_INSTANCE, &mb_len) != NFCTAG_OK)
                {
                    return HAL_ERROR;
                }

                msg_len = (uint16_t)mb_len + 1U;
                if (msg_len > OTA_MAILBOX_MAX_PAYLOAD)
                {
                    return HAL_ERROR;
                }
                if (CUSTOM_NFCTAG_ReadMailboxData(CUSTOM_NFCTAG_INSTANCE, buf, 0U, msg_len) != NFCTAG_OK)
                {
                    return HAL_ERROR;
                }

                *len = msg_len;
                (void)CUSTOM_NFCTAG_ResetMBEN_Dyn(CUSTOM_NFCTAG_INSTANCE);
                (void)CUSTOM_NFCTAG_SetMBEN_Dyn(CUSTOM_NFCTAG_INSTANCE);
                return HAL_OK;
            }
        }
        HAL_Delay(MAILBOX_POLL_DELAY_MS);
    } while ((HAL_GetTick() - start) < MAILBOX_WAIT_TIMEOUT_MS);

    return HAL_ERROR;
}

static HAL_StatusTypeDef mailbox_write(const uint8_t *buf, uint16_t len)
{
    uint32_t start = HAL_GetTick();
    ST25DVxxKC_MB_CTRL_DYN_STATUS_t ctrl;

    if ((buf == NULL) || (len == 0U) || (len > OTA_MAILBOX_MAX_PAYLOAD))
    {
        return HAL_ERROR;
    }

    do
    {
        if (CUSTOM_NFCTAG_ReadMBCtrl_Dyn(CUSTOM_NFCTAG_INSTANCE, &ctrl) == NFCTAG_OK)
        {
            if ((ctrl.RfPutMsg == 0U) && (ctrl.HostPutMsg == 0U))
            {
                return (CUSTOM_NFCTAG_WriteMailboxData(CUSTOM_NFCTAG_INSTANCE, buf, len) == NFCTAG_OK) ? HAL_OK : HAL_ERROR;
            }
        }
        HAL_Delay(MAILBOX_POLL_DELAY_MS);
    } while ((HAL_GetTick() - start) < MAILBOX_WAIT_TIMEOUT_MS);

    return HAL_ERROR;
}

static uint8_t parse_frame(const uint8_t *buf, uint16_t len, OTA_FrameView_t *frame)
{
    uint32_t crc;
    uint32_t expected_crc;

    if ((buf == NULL) || (frame == NULL) || (len < OTA_FRAME_HEADER_LEN))
    {
        return 0U;
    }

    if ((rd32(&buf[0]) != OTA_FRAME_MAGIC) ||
        (buf[5] != OTA_FRAME_VERSION))
    {
        return 0U;
    }

    frame->cmd = buf[4];
    frame->version = buf[5];
    frame->seq = rd16(&buf[6]);
    frame->offset = rd32(&buf[8]);
    frame->len = rd16(&buf[12]);
    frame->flags = rd16(&buf[14]);
    frame->arg0 = rd32(&buf[16]);
    expected_crc = rd32(&buf[20]);

    if ((uint32_t)OTA_FRAME_HEADER_LEN + frame->len > len)
    {
        return 0U;
    }

    crc = OTA_Crc32Update(0xFFFFFFFFUL, buf, 20U);
    crc = OTA_Crc32Update(crc, &buf[OTA_FRAME_HEADER_LEN], frame->len);
    crc = OTA_Crc32Finish(crc);
    if (crc != expected_crc)
    {
        return 0U;
    }

    frame->payload = &buf[OTA_FRAME_HEADER_LEN];
    return 1U;
}

static uint16_t build_response(uint8_t cmd, uint16_t seq, uint32_t offset,
                               uint32_t status, const uint8_t *payload, uint16_t payload_len)
{
    uint32_t crc;

    if (payload_len > (OTA_MAILBOX_MAX_PAYLOAD - OTA_FRAME_HEADER_LEN))
    {
        payload_len = OTA_MAILBOX_MAX_PAYLOAD - OTA_FRAME_HEADER_LEN;
    }

    wr32(&tx_buf[0], OTA_FRAME_MAGIC);
    tx_buf[4] = (uint8_t)(cmd | OTA_RSP_FLAG);
    tx_buf[5] = OTA_FRAME_VERSION;
    wr16(&tx_buf[6], seq);
    wr32(&tx_buf[8], offset);
    wr16(&tx_buf[12], payload_len);
    wr16(&tx_buf[14], 0U);
    wr32(&tx_buf[16], status);
    wr32(&tx_buf[20], 0UL);
    if ((payload != NULL) && (payload_len > 0U))
    {
        memcpy(&tx_buf[OTA_FRAME_HEADER_LEN], payload, payload_len);
    }

    crc = OTA_Crc32Update(0xFFFFFFFFUL, tx_buf, 20U);
    crc = OTA_Crc32Update(crc, &tx_buf[OTA_FRAME_HEADER_LEN], payload_len);
    crc = OTA_Crc32Finish(crc);
    wr32(&tx_buf[20], crc);
    return (uint16_t)(OTA_FRAME_HEADER_LEN + payload_len);
}

static void process_frame(const OTA_FrameView_t *frame)
{
    uint8_t payload[24];
    uint32_t status = OTA_STATUS_OK;
    uint8_t rsp_cmd = frame->cmd;

    switch (frame->cmd)
    {
    case OTA_CMD_HELLO:
    case OTA_CMD_STATUS:
        wr32(&payload[0], BOOTLOADER_VERSION);
        wr32(&payload[4], OTA_FLASH_PAGE_SIZE);
        wr32(&payload[8], OTA_APP_MAX_SIZE);
        wr32(&payload[12], OTA_DATA_MAX_PAYLOAD);
        wr32(&payload[16], ota_meta.state);
        wr32(&payload[20], ota_meta.error);
        (void)mailbox_write(tx_buf, build_response(rsp_cmd, frame->seq, ota_meta.received_size,
                                                   OTA_STATUS_OK, payload, sizeof(payload)));
        return;

    case OTA_CMD_BEGIN:
        if ((frame->len != 4U) || (frame->arg0 != OTA_UNLOCK_TOKEN) ||
            (frame->offset == 0UL) || (frame->offset > OTA_APP_MAX_SIZE))
        {
            metadata_set_error((frame->arg0 != OTA_UNLOCK_TOKEN) ? OTA_ERR_UNLOCK : OTA_ERR_SIZE);
            status = OTA_STATUS_ERROR;
            break;
        }

        memset(&ota_meta, 0, sizeof(ota_meta));
        ota_meta.state = OTA_STATE_RECEIVING;
        ota_meta.image_size = frame->offset;
        ota_meta.expected_crc32 = rd32(frame->payload);
        ota_meta.received_size = 0UL;
        ota_meta.sequence = 0UL;
        ota_meta.error = OTA_ERR_NONE;
        running_crc = 0xFFFFFFFFUL;

        if ((OTA_MetadataWrite(&ota_meta) != HAL_OK) || (erase_app_slot(ota_meta.image_size) != HAL_OK))
        {
            metadata_set_error(OTA_ERR_FLASH);
            status = OTA_STATUS_ERROR;
        }
        break;

    case OTA_CMD_DATA:
        if ((ota_meta.state != OTA_STATE_RECEIVING) ||
            (frame->offset > ota_meta.image_size) ||
            ((uint32_t)frame->offset + frame->len > ota_meta.image_size))
        {
            metadata_set_error(OTA_ERR_OFFSET);
            status = OTA_STATUS_ERROR;
            break;
        }

        if (frame->offset < ota_meta.received_size)
        {
            status = flash_range_matches(frame->offset, frame->payload, frame->len) ? OTA_STATUS_DUPLICATE : OTA_STATUS_ERROR;
            break;
        }

        if ((frame->offset != ota_meta.received_size) ||
            ((frame->offset & (OTA_FLASH_DOUBLEWORD_SIZE - 1UL)) != 0UL) ||
            (((uint32_t)frame->offset + frame->len < ota_meta.image_size) &&
             ((frame->len & (OTA_FLASH_DOUBLEWORD_SIZE - 1UL)) != 0U)))
        {
            metadata_set_error(OTA_ERR_OFFSET);
            status = OTA_STATUS_ERROR;
            break;
        }

        if (program_app_data(frame->offset, frame->payload, frame->len) != HAL_OK)
        {
            metadata_set_error(OTA_ERR_FLASH);
            status = OTA_STATUS_ERROR;
            break;
        }

        running_crc = OTA_Crc32Update(running_crc, frame->payload, frame->len);
        ota_meta.received_size += frame->len;
        ota_meta.sequence++;
        if (OTA_MetadataWrite(&ota_meta) != HAL_OK)
        {
            metadata_set_error(OTA_ERR_FLASH);
            status = OTA_STATUS_ERROR;
        }
        break;

    case OTA_CMD_COMMIT:
        if ((ota_meta.state != OTA_STATE_RECEIVING) ||
            (ota_meta.received_size != ota_meta.image_size) ||
            (OTA_Crc32Finish(running_crc) != ota_meta.expected_crc32))
        {
            metadata_set_error(OTA_ERR_IMAGE_CRC);
            status = OTA_STATUS_ERROR;
            break;
        }

        if (!OTA_AppVectorIsValid(OTA_APP_BASE_ADDR, ota_meta.image_size))
        {
            metadata_set_error(OTA_ERR_VECTOR);
            status = OTA_STATUS_ERROR;
            break;
        }

        ota_meta.state = OTA_STATE_VALID;
        ota_meta.error = OTA_ERR_NONE;
        if (OTA_MetadataWrite(&ota_meta) != HAL_OK)
        {
            metadata_set_error(OTA_ERR_FLASH);
            status = OTA_STATUS_ERROR;
            break;
        }

        (void)mailbox_write(tx_buf, build_response(rsp_cmd, frame->seq, ota_meta.received_size,
                                                   OTA_STATUS_OK, NULL, 0U));
        HAL_Delay(50U);
        NVIC_SystemReset();
        return;

    case OTA_CMD_ABORT:
        (void)OTA_MetadataClear();
        memset(&ota_meta, 0, sizeof(ota_meta));
        ota_meta.state = OTA_STATE_IDLE;
        break;

    default:
        metadata_set_error(OTA_ERR_PROTOCOL);
        status = OTA_STATUS_ERROR;
        break;
    }

    (void)mailbox_write(tx_buf, build_response(rsp_cmd, frame->seq, ota_meta.received_size,
                                               status, NULL, 0U));
}

static HAL_StatusTypeDef erase_app_slot(uint32_t image_size)
{
    FLASH_EraseInitTypeDef erase = {0};
    uint32_t page_error = 0UL;
    uint32_t pages = (image_size + OTA_FLASH_PAGE_SIZE - 1UL) / OTA_FLASH_PAGE_SIZE;
    HAL_StatusTypeDef status;

    if ((pages == 0UL) || (pages > (OTA_APP_MAX_SIZE / OTA_FLASH_PAGE_SIZE)))
    {
        return HAL_ERROR;
    }

    status = HAL_FLASH_Unlock();
    if (status != HAL_OK)
    {
        return status;
    }

    erase.TypeErase = FLASH_TYPEERASE_PAGES;
    erase.Banks = FLASH_BANK_1;
    erase.Page = OTA_APP_BASE_ADDR / OTA_FLASH_PAGE_SIZE - FLASH_BASE / OTA_FLASH_PAGE_SIZE;
    erase.NbPages = pages;
    status = HAL_FLASHEx_Erase(&erase, &page_error);
    (void)HAL_FLASH_Lock();
    return status;
}

static HAL_StatusTypeDef program_app_data(uint32_t offset, const uint8_t *data, uint16_t len)
{
    uint64_t word;
    uint8_t padded[OTA_FLASH_DOUBLEWORD_SIZE];
    uint32_t addr = OTA_APP_BASE_ADDR + offset;
    HAL_StatusTypeDef status;

    status = HAL_FLASH_Unlock();
    if (status != HAL_OK)
    {
        return status;
    }

    while (len > 0U)
    {
        uint16_t n = (len >= OTA_FLASH_DOUBLEWORD_SIZE) ? OTA_FLASH_DOUBLEWORD_SIZE : len;
        memset(padded, 0xFF, sizeof(padded));
        memcpy(padded, data, n);
        memcpy(&word, padded, sizeof(word));

        status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, addr, word);
        if (status != HAL_OK)
        {
            break;
        }
        addr += OTA_FLASH_DOUBLEWORD_SIZE;
        data += n;
        len = (uint16_t)(len - n);
    }

    (void)HAL_FLASH_Lock();
    return status;
}

static uint8_t flash_range_matches(uint32_t offset, const uint8_t *data, uint16_t len)
{
    const uint8_t *flash = (const uint8_t *)(OTA_APP_BASE_ADDR + offset);

    return (memcmp(flash, data, len) == 0) ? 1U : 0U;
}

static void metadata_set_error(uint32_t err)
{
    ota_meta.state = OTA_STATE_ERROR;
    ota_meta.error = err;
    (void)OTA_MetadataWrite(&ota_meta);
}

static void restore_running_crc(void)
{
    const uint8_t *flash = (const uint8_t *)OTA_APP_BASE_ADDR;
    uint32_t remaining = ota_meta.received_size;

    running_crc = 0xFFFFFFFFUL;
    while (remaining > 0UL)
    {
        uint32_t n = (remaining > 256UL) ? 256UL : remaining;
        running_crc = OTA_Crc32Update(running_crc, flash, n);
        flash += n;
        remaining -= n;
    }
}

static void GPIO_MinimalInit(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();

    HAL_GPIO_WritePin(NFC_VCC_GPIO_Port, NFC_VCC_Pin, GPIO_PIN_SET);
    GPIO_InitStruct.Pin = NFC_VCC_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(NFC_VCC_GPIO_Port, &GPIO_InitStruct);

    HAL_Delay(5U);
}

static void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE2);
    HAL_PWR_EnableBkUpAccess();
    __HAL_RCC_LSEDRIVE_CONFIG(RCC_LSEDRIVE_MEDIUMHIGH);

    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI | RCC_OSCILLATORTYPE_LSE;
    RCC_OscInitStruct.LSEState = RCC_LSE_ON;
    RCC_OscInitStruct.HSIState = RCC_HSI_ON;
    RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
    {
        Error_Handler();
    }

    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV4;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;

    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK)
    {
        Error_Handler();
    }
}

void Error_Handler(void)
{
    __disable_irq();
    while (1)
    {
    }
}

static uint32_t rd32(const uint8_t *p)
{
    return ((uint32_t)p[0]) | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0]) | ((uint16_t)p[1] << 8));
}

static void wr32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFFU);
    p[1] = (uint8_t)((v >> 8) & 0xFFU);
    p[2] = (uint8_t)((v >> 16) & 0xFFU);
    p[3] = (uint8_t)((v >> 24) & 0xFFU);
}

static void wr16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFFU);
    p[1] = (uint8_t)((v >> 8) & 0xFFU);
}
