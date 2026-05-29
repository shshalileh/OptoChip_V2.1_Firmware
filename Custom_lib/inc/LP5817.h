#pragma once

#include "stm32u0xx_hal.h"
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* I2C address (7-bit = 0x2D, shifted for HAL) */
#define LP5817_I2C_ADDR              (0x2D << 1)

/* Registers */
#define LP5817_REG_CHIP_EN           0x00
#define LP5817_REG_DEV_CONFIG0       0x01
#define LP5817_REG_DEV_CONFIG1       0x02
#define LP5817_REG_DEV_CONFIG2       0x03
#define LP5817_REG_DEV_CONFIG3       0x04

#define LP5817_REG_SHUTDOWN_CMD      0x0D
#define LP5817_REG_RESET_CMD         0x0E
#define LP5817_REG_UPDATE_CMD        0x0F

#define LP5817_REG_FLAG_CLR          0x13

#define LP5817_REG_OUT0_DC           0x14
#define LP5817_REG_OUT1_DC           0x15
#define LP5817_REG_OUT2_DC           0x16

#define LP5817_REG_OUT0_MANUAL_PWM   0x18
#define LP5817_REG_OUT1_MANUAL_PWM   0x19
#define LP5817_REG_OUT2_MANUAL_PWM   0x1A

#define LP5817_REG_FLAG              0x40

/* Bit masks */

/* CHIP_EN register */
#define LP5817_CHIP_EN_BIT           (1U << 0)

/* DEV_CONFIG0 */
#define LP5817_MAX_CURRENT_BIT       (1U << 0)

/* DEV_CONFIG1 */
#define LP5817_OUT0_EN_BIT           (1U << 0)
#define LP5817_OUT1_EN_BIT           (1U << 1)
#define LP5817_OUT2_EN_BIT           (1U << 2)

/* FLAG_CLR */
#define LP5817_FLAG_CLR_POR          (1U << 0)
#define LP5817_FLAG_CLR_TSD          (1U << 1)

/* Commands */
#define LP5817_CMD_SHUTDOWN          0x33
#define LP5817_CMD_RESET             0xCC
#define LP5817_CMD_UPDATE            0x55

/* Current values */
#define LP5817_DC_20mA               0x64
#define LP5817_MAX_CURRENT_MA        30U

/* PWM values */
#define LP5817_PWM_100               0xFF
#define LP5817_PWM_0                 0x00

// These functions are private (static in the .C file)
// HAL_StatusTypeDef LP5817_WriteReg(I2C_HandleTypeDef *hi2c, uint8_t reg, uint8_t value);
// HAL_StatusTypeDef LP5817_ReadReg(I2C_HandleTypeDef *hi2c, uint8_t reg, uint8_t *value);
HAL_StatusTypeDef LP5817_UpdateBits(I2C_HandleTypeDef *hi2c, uint8_t reg, uint8_t mask, uint8_t value);
HAL_StatusTypeDef custom_delay(uint32_t delay_ms);

HAL_StatusTypeDef LP5817_Init_2CH_20mA(I2C_HandleTypeDef *hi2c);
uint8_t LP5817_CurrentToDc(uint8_t current_mA);
HAL_StatusTypeDef LP5817_SetChannelCurrent_AndPWM(I2C_HandleTypeDef *hi2c, uint8_t channel, uint8_t dc_val, uint8_t pwm_val);
HAL_StatusTypeDef LP5817_SetChannel_PWM(I2C_HandleTypeDef *hi2c, uint8_t channel, uint8_t pwm_val);
HAL_StatusTypeDef LP5817_SetChannelCurrent(I2C_HandleTypeDef *hi2c, const uint8_t channel, const uint8_t dc_val);
HAL_StatusTypeDef LP5817_SetChannelCurrent_mA(I2C_HandleTypeDef *hi2c, uint8_t channel, uint8_t current_mA);
HAL_StatusTypeDef LP5817_GetLED01Settings(I2C_HandleTypeDef *hi2c, uint8_t *led0_current, uint8_t *led1_current,
                                          uint8_t *led0_enabled, uint8_t *led1_enabled);

HAL_StatusTypeDef LP5817_ClearFlags(I2C_HandleTypeDef *hi2c, uint8_t flags);
HAL_StatusTypeDef LP5817_DumpRegisters(I2C_HandleTypeDef *hi2c);

//HAL_StatusTypeDef LP5817_PulseLED_Blocking(I2C_HandleTypeDef *hi2c, uint8_t channel,
//								  float frequency_hz, float duty_cycle, uint32_t duration_s);

HAL_StatusTypeDef LP5817_PulseLED_NonBlocking(I2C_HandleTypeDef *hi2c, uint8_t channel,
                                  float frequency_hz, float duty_cycle,uint32_t duration_s);

void LP5817_StopPulse(void);

#ifdef __cplusplus
}
#endif

