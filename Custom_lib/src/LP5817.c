#include "LP5817.h"
#include "StimScheduler.h"

// external variables
extern LPTIM_HandleTypeDef hlptim1;

// variables
__IO uint32_t counter_lptim = 0;
__IO uint8_t  pulse_active = 0;
__IO uint8_t  led_state = 0;
__IO uint32_t pulses_remaining = 0;

static uint32_t on_ticks = 0;
static uint32_t off_ticks = 0;

static uint8_t pulse_channel;
static I2C_HandleTypeDef *pulse_i2c;

// Converts requested current in mA to the LP5817 DC register value //
uint8_t LP5817_CurrentToDc(uint8_t current_mA)
{
    uint32_t dc = ((uint32_t)current_mA * 255UL + 25UL) / 51UL;

    if (dc > 255UL)
        dc = 255UL;

    return (uint8_t)dc;
}


/* Write register */
static HAL_StatusTypeDef LP5817_WriteReg(I2C_HandleTypeDef *hi2c,
                                         uint8_t reg,
                                         uint8_t value)
{
    return HAL_I2C_Mem_Write(hi2c,
                             LP5817_I2C_ADDR,
                             reg,
                             I2C_MEMADD_SIZE_8BIT,
                             &value,
                             1,
                             HAL_MAX_DELAY);
}
/* Read register */
static HAL_StatusTypeDef LP5817_ReadReg(I2C_HandleTypeDef *hi2c,
                                        uint8_t reg,
                                        uint8_t *value)
{
    return HAL_I2C_Mem_Read(hi2c,
                            LP5817_I2C_ADDR,
                            reg,
                            I2C_MEMADD_SIZE_8BIT,
                            value,
                            1,
                            HAL_MAX_DELAY);
}
/* Read-modify-write helper */
HAL_StatusTypeDef LP5817_UpdateBits(I2C_HandleTypeDef *hi2c,
                                           uint8_t reg,
                                           uint8_t mask,
                                           uint8_t value)
{
    HAL_StatusTypeDef status;
    uint8_t reg_val;

    status = LP5817_ReadReg(hi2c, reg, &reg_val);
    if (status != HAL_OK) return status;

    reg_val &= ~mask;
    reg_val |= (value & mask);

    return LP5817_WriteReg(hi2c, reg, reg_val);
}
/* Clear flags (POR / TSD) */
HAL_StatusTypeDef LP5817_ClearFlags(I2C_HandleTypeDef *hi2c,
                                    uint8_t flags)
{
    return LP5817_WriteReg(hi2c,
                           LP5817_REG_FLAG_CLR,
                           flags);
}
/* Set DC and PWM */
// Very Important: The DC register is 51*(DC/255 mA)
HAL_StatusTypeDef LP5817_SetChannelCurrent_AndPWM(
    I2C_HandleTypeDef *hi2c,
    uint8_t channel,
    uint8_t dc_val,
    uint8_t pwm_val)
{
    HAL_StatusTypeDef status;
    uint8_t dc_reg;
    uint8_t pwm_reg;

    switch (channel)
    {
        case 0:
            dc_reg  = LP5817_REG_OUT0_DC;
            pwm_reg = LP5817_REG_OUT0_MANUAL_PWM;
            break;

        case 1:
            dc_reg  = LP5817_REG_OUT1_DC;
            pwm_reg = LP5817_REG_OUT1_MANUAL_PWM;
            break;

        default:
            return HAL_ERROR;
    }

    status = LP5817_WriteReg(hi2c, dc_reg, dc_val);
    if (status != HAL_OK) return status;

    status = LP5817_WriteReg(hi2c, pwm_reg, pwm_val);
    if (status != HAL_OK) return status;

    return HAL_OK;
}



/* Set DC */
// Very Important: The DC register is 51*(DC/255 mA)
HAL_StatusTypeDef LP5817_SetChannelCurrent(I2C_HandleTypeDef *hi2c, const uint8_t channel, const uint8_t dc_val)
{
    HAL_StatusTypeDef status;
    uint8_t dc_reg;

    switch (channel)
    {
        case 0:
            dc_reg  = LP5817_REG_OUT0_DC;
            break;

        case 1:
            dc_reg  = LP5817_REG_OUT1_DC;
            break;

        default:
            return HAL_ERROR;
    }

    status = LP5817_WriteReg(hi2c, dc_reg, dc_val);
    if (status != HAL_OK) return status;

    return HAL_OK;
}

HAL_StatusTypeDef LP5817_SetChannelCurrent_mA(I2C_HandleTypeDef *hi2c, uint8_t channel, uint8_t current_mA)
{
    if (current_mA > LP5817_MAX_CURRENT_MA)
        return HAL_ERROR;

    return LP5817_SetChannelCurrent(hi2c, channel, LP5817_CurrentToDc(current_mA));
}


HAL_StatusTypeDef LP5817_GetLED01Settings(I2C_HandleTypeDef *hi2c,
                                          uint8_t *led0_current, uint8_t *led1_current,
                                          uint8_t *led0_enabled, uint8_t *led1_enabled)
{
    uint8_t reg_cfg1;
    HAL_StatusTypeDef status;

    if (!hi2c || !led0_current || !led1_current || !led0_enabled || !led1_enabled)
        return HAL_ERROR;

    status = LP5817_ReadReg(hi2c, LP5817_REG_OUT0_DC, led0_current);
    if (status != HAL_OK) return status;

    status = LP5817_ReadReg(hi2c, LP5817_REG_OUT1_DC, led1_current);
    if (status != HAL_OK) return status;

    status = LP5817_ReadReg(hi2c, LP5817_REG_DEV_CONFIG1, &reg_cfg1);
    if (status != HAL_OK) return status;

    *led0_enabled = (reg_cfg1 & LP5817_OUT0_EN_BIT) ? 1 : 0;
    *led1_enabled = (reg_cfg1 & LP5817_OUT1_EN_BIT) ? 1 : 0;

    return HAL_OK;
}

/* Fast PWM Toggling */
HAL_StatusTypeDef LP5817_SetChannel_PWM(I2C_HandleTypeDef *hi2c, uint8_t channel, uint8_t pwm_val)
{
    HAL_StatusTypeDef status;
    uint8_t pwm_reg;

    switch (channel)
    {
        case 0:
            pwm_reg = LP5817_REG_OUT0_MANUAL_PWM;
            break;

        case 1:
            pwm_reg = LP5817_REG_OUT1_MANUAL_PWM;
            break;

        default:
            return HAL_ERROR;
    }

    status = LP5817_WriteReg(hi2c, pwm_reg, pwm_val);
    if (status != HAL_OK) return status;

    return HAL_OK;
}


/* Initialization */
HAL_StatusTypeDef LP5817_Init_2CH_20mA(I2C_HandleTypeDef *hi2c)
{
    HAL_StatusTypeDef status;

    HAL_Delay(1);

    /* Enable chip */
    status = LP5817_UpdateBits(
                hi2c,
                LP5817_REG_CHIP_EN,
                LP5817_CHIP_EN_BIT,
                LP5817_CHIP_EN_BIT);
    if (status != HAL_OK) return status;

    /* Set max current = 51mA */
    status = LP5817_UpdateBits(
                hi2c,
                LP5817_REG_DEV_CONFIG0,
                LP5817_MAX_CURRENT_BIT,
                LP5817_MAX_CURRENT_BIT);
    if (status != HAL_OK) return status;

    /* Enable OUT0 and OUT1 for independent stimulation channels. */
    status = LP5817_UpdateBits(
                hi2c,
                LP5817_REG_DEV_CONFIG1,
                LP5817_OUT0_EN_BIT |
                LP5817_OUT1_EN_BIT |
                LP5817_OUT2_EN_BIT,
                LP5817_OUT0_EN_BIT |
                LP5817_OUT1_EN_BIT);
    if (status != HAL_OK) return status;

    /* Disable fade */
    status = LP5817_WriteReg(hi2c,
                             LP5817_REG_DEV_CONFIG2,
                             0x00);
    if (status != HAL_OK) return status;

    /* Linear PWM */
    status = LP5817_WriteReg(hi2c,
                             LP5817_REG_DEV_CONFIG3,
                             0x00);
    if (status != HAL_OK) return status;

    /* Clear POR flag */
    status = LP5817_ClearFlags(
                hi2c,
                LP5817_FLAG_CLR_POR);
    if (status != HAL_OK) return status;

    /* Latch configuration registers */
    status = LP5817_WriteReg(hi2c,
                             LP5817_REG_UPDATE_CMD,
                             LP5817_CMD_UPDATE);
    if (status != HAL_OK) return status;

    /* OUT0 -> 20mA OFF */
    status = LP5817_SetChannelCurrent_AndPWM(
                hi2c,
                0,
                LP5817_DC_20mA,
                LP5817_PWM_0);
    if (status != HAL_OK) return status;

    /* OUT1 -> 20mA OFF */
    status = LP5817_SetChannelCurrent_AndPWM(
                hi2c,
                1,
                LP5817_DC_20mA,
                LP5817_PWM_0);
    if (status != HAL_OK) return status;

    /* Ensure OUT2 disabled */
    status = LP5817_WriteReg(hi2c, LP5817_REG_OUT2_DC, 0x00);
    if (status != HAL_OK) return status;

    status = LP5817_WriteReg(hi2c, LP5817_REG_OUT2_MANUAL_PWM, 0x00);
    if (status != HAL_OK) return status;

    return HAL_OK;
}
/* Dumping all the register */
HAL_StatusTypeDef LP5817_DumpRegisters(
    I2C_HandleTypeDef *hi2c)
{
    HAL_StatusTypeDef status;
    uint8_t val;
    for (uint8_t reg = 0x00; reg <= 0x40; reg++)
    {
        status = LP5817_ReadReg(hi2c, reg, &val);
        if (status != HAL_OK)
            return status;
    }

    return HAL_OK;
}

//////////////////////////////////////
////// LED Pulsating Functions ///////
HAL_StatusTypeDef LP5817_PulseLED_NonBlocking(I2C_HandleTypeDef *hi2c, uint8_t channel,
                                  float frequency_hz, float duty_cycle,uint32_t duration_s)
{
    if ((hi2c == NULL) || (frequency_hz <= 0.0f) || (duty_cycle <= 0.0f) ||
        (duty_cycle >= 100.0f) || (duration_s == 0))
    {
        return HAL_ERROR;
    }

    uint32_t period_ms = (uint32_t)(1000.0f / frequency_hz);

    on_ticks  = (uint32_t)(period_ms * (duty_cycle / 100.0f));
    off_ticks = period_ms - on_ticks;
    pulses_remaining = (uint32_t)(frequency_hz * duration_s);

    pulse_i2c = hi2c;
    pulse_channel = channel;

    pulse_active = 1;
    led_state = 0;

    /* Start LPTIM only when pulsing starts */
    return HAL_LPTIM_Counter_Start_IT(&hlptim1);
}

void LP5817_StopPulse(void)
{
    pulse_active = 0;
    pulses_remaining = 0;

    LP5817_SetChannelCurrent_AndPWM(pulse_i2c, pulse_channel, LP5817_DC_20mA, LP5817_PWM_0);
}

void HAL_LPTIM_AutoReloadMatchCallback(LPTIM_HandleTypeDef *hlptim)
{
    if (hlptim->Instance != LPTIM1)
        return;

    StimScheduler_Tick1ms();
}

/////////////////////////////////////////////////////////////////////////////
///////////////////////////BLocking Pulsating////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
//
//HAL_StatusTypeDef LP5817_PulseLED_Blocking(I2C_HandleTypeDef *hi2c, uint8_t channel,
//    float frequency_hz, float duty_cycle, uint32_t duration_s)
//{
//    if (frequency_hz <= 0 || duty_cycle <= 0 || duty_cycle >= 100)
//        return HAL_ERROR;
//
//    uint32_t period_ms = (uint32_t)(1000.0f / frequency_hz);
//    uint32_t on_time_ms = (uint32_t)(period_ms * (duty_cycle / 100.0f));
//    uint32_t off_time_ms = period_ms - on_time_ms;
//
//    uint32_t total_cycles = (uint32_t)(frequency_hz * duration_s);
//
//    for (uint32_t i = 0; i < total_cycles; i++)
//    {
//        /* LED ON */
//        LP5817_SetChannelCurrent_AndPWM(hi2c, channel, LP5817_DC_20mA,LP5817_PWM_100);
//
//        custom_delay(on_time_ms);
//
//        /* LED OFF */
//        LP5817_SetChannelCurrent_AndPWM(hi2c, channel, LP5817_DC_20mA, LP5817_PWM_0);
//
//        custom_delay(off_time_ms);
//    }
//    return HAL_OK;
//}
//
////  Care must be taken to have the auto-reload register set before using the function:
////	__HAL_LPTIM_AUTORELOAD_SET(hlptim, hlptim->Init.Period);
//HAL_StatusTypeDef custom_delay(uint32_t delay_ms)
//{
//	HAL_StatusTypeDef status;
//    counter_lptim = delay_ms;
//
//    /* Start LPTIM */
//    status = HAL_LPTIM_Counter_Start_IT(&hlptim1);
//    if (status != HAL_OK) return status;
//
//    while (counter_lptim)
//    {
//        // __WFI();   // wait for interrupt, more efficient when CPU is waiting
//    }
//
//    HAL_LPTIM_Counter_Stop_IT(&hlptim1);
//    return HAL_OK;
//}

// Call back for the blocking function
//void HAL_LPTIM_AutoReloadMatchCallback(LPTIM_HandleTypeDef *hlptim)
//{
//    if (hlptim->Instance == LPTIM1)
//    {
//        if (counter_lptim)
//        {
//            counter_lptim--;
//        }
//    }
//}
