#pragma once
#include "stm32u0xx_hal.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// 7-bit I2C address from datasheet: 0x6B
#define BQ25155_I2C_ADDR_7BIT   (0x6B)

#define BQ25155_STAT0	(0x00)
#define BQ25155_STAT1	(0x01)
#define BQ25155_STAT2	(0x02)

#define BQ25155_FLAG0	(0x03)
#define BQ25155_FLAG1	(0x04)
#define BQ25155_FLAG2	(0x05)
#define BQ25155_FLAG3	(0x06)

#define BQ25155_MASK0	(0x07)
#define BQ25155_MASK1	(0x08)
#define BQ25155_MASK2	(0x09)
#define BQ25155_MASK3	(0x0A)

#define BQ25155_VBAT_CTRL	(0x12)
#define BQ25155_ICHG_CTRL	(0x13)
#define BQ25155_PCHRGCTRL	(0x14)
#define BQ25155_TERMCTRL	(0x15)
#define BQ25155_BUVLO	(0x16)
#define BQ25155_CHARGERCTRL0	(0x17)
#define BQ25155_CHARGERCTRL1	(0x18)
#define BQ25155_ILIMCTRL	(0x19)

#define BQ25155_LDOCTRL	(0x1D)
#define BQ25155_MRCTRL	(0x30)
#define BQ25155_ICCTRL0	(0x35)
#define BQ25155_ICCTRL1	(0x36)
#define BQ25155_ICCTRL2	(0x37)

#define BQ25155_ADCCTRL0	(0x40)
#define BQ25155_ADCCTRL1	(0x41)
#define BQ25155_ADCDATA_VBAT_M	(0x42)
#define BQ25155_ADCDATA_VBAT_L	(0x43)
#define BQ25155_ADCDATA_TS_M	(0x44)
#define BQ25155_ADCDATA_TS_L	(0x45)
#define BQ25155_ADCDATA_ICHG_M	(0x46)
#define BQ25155_ADCDATA_ICHG_L	(0x47)
#define BQ25155_ADCDATA_ADCIN_M	(0x48)
#define BQ25155_ADCDATA_ADCIN_L	(0x49)
#define BQ25155_ADCDATA_VIN_M	(0x4A)
#define BQ25155_ADCDATA_VIN_L	(0x4B)
#define BQ25155_ADCDATA_PMID_M	(0x4C)
#define BQ25155_ADCDATA_PMID_L	(0x4D)
#define BQ25155_ADCDATA_IIN_M	(0x4E)
#define BQ25155_ADCDATA_IIN_L	(0x4F)
#define BQ25155_ADCDATA_TJ_M	(0x50)
#define BQ25155_ADCDATA_TJ_L	(0x51)

#define BQ25155_ADCALARM_COMP1_M	(0x52)
#define BQ25155_ADCALARM_COMP1_L	(0x53)
#define BQ25155_ADCALARM_COMP2_M	(0x54)
#define BQ25155_ADCALARM_COMP2_L	(0x55)
#define BQ25155_ADCALARM_COMP3_M	(0x56)
#define BQ25155_ADCALARM_COMP3_L	(0x57)

#define BQ25155_ADC_READ_EN 		(0x58)

#define BQ25155_TS_FASTCHGCTRL		(0x61)
#define BQ25155_TS_COLD				(0x62)
#define BQ25155_TS_COOL				(0x63)
#define BQ25155_TS_WARM				(0x64)
#define BQ25155_TS_HOT				(0x65)

#define BQ25155_DEVICE_ID			(0x6F)
#define BQ25155_STAT			BIT7 | BIT6

typedef struct {
    I2C_HandleTypeDef *hi2c;
    uint8_t addr7; // HAL need 8-bit address
} BQ25155_Handle_t;

// These functions are private (static in the .C file)
// HAL_StatusTypeDef BQ25155_WriteReg(BQ25155_Handle_t *h, uint8_t reg, uint8_t val);
HAL_StatusTypeDef BQ25155_ReadReg (BQ25155_Handle_t *h, uint8_t reg, uint8_t *val);

HAL_StatusTypeDef BQ25155_SetBatteryVoltage(BQ25155_Handle_t *h, uint16_t voltage_mV);
HAL_StatusTypeDef BQ25155_SetFastChargeCurrent(BQ25155_Handle_t *h, uint32_t current_mA);
HAL_StatusTypeDef BQ25155_TriggerADC(BQ25155_Handle_t *h);
HAL_StatusTypeDef BQ25155_ReadVBAT(BQ25155_Handle_t *h, uint16_t *voltage_mV);
HAL_StatusTypeDef BQ25155_IsADCReady(BQ25155_Handle_t *h, uint8_t *ready);

HAL_StatusTypeDef BQ25155_InitAll(BQ25155_Handle_t *h);

#ifdef __cplusplus
}
#endif


