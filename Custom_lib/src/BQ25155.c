#include "bq25155.h"

extern I2C_HandleTypeDef hi2c3;

// BQ25155 Global Handle
BQ25155_Handle_t BQ = {
  .hi2c  = &hi2c3,
  .addr7 = BQ25155_I2C_ADDR_7BIT
};

// STM32 HAL expects 8-bit address (7-bit << 1)
static inline uint16_t bq_addr8(const BQ25155_Handle_t *h) {
    return (uint16_t)(h->addr7 << 1);
}

static HAL_StatusTypeDef BQ25155_WriteReg(BQ25155_Handle_t *h, uint8_t reg, uint8_t val)
{
    if (!h || !h->hi2c) return HAL_ERROR;
    return HAL_I2C_Mem_Write(h->hi2c, bq_addr8(h), reg,
                            I2C_MEMADD_SIZE_8BIT, &val, 1, 100);
}

HAL_StatusTypeDef BQ25155_ReadReg(BQ25155_Handle_t *h, uint8_t reg, uint8_t *val)
{
	if (h == NULL || h->hi2c == NULL || val == NULL) return HAL_ERROR;
    return HAL_I2C_Mem_Read(h->hi2c, bq_addr8(h), reg, I2C_MEMADD_SIZE_8BIT, val, 1, 100);
}

HAL_StatusTypeDef BQ25155_SetBatteryVoltage(BQ25155_Handle_t *h, uint16_t voltage_mV)
{
    if (voltage_mV < 3600) voltage_mV = 3600;
    if (voltage_mV > 4600) voltage_mV = 4600;

    uint8_t code = (voltage_mV - 3600) / 10;  // 10mV per LSB

    return BQ25155_WriteReg(h, BQ25155_VBAT_CTRL, code);
}

HAL_StatusTypeDef BQ25155_SetFastChargeCurrent(BQ25155_Handle_t *h, uint32_t current_mA)
{
	// Step is 1.25 mA
	// float step = 1.25f;
    // uint32_t code = current_mA / step;

	uint32_t code = (current_mA * 4) / 5;

    if (code < 1) code = 1;
    if (code > 255) code = 255;

    return BQ25155_WriteReg(h, BQ25155_ICHG_CTRL, (uint8_t)code);
}

HAL_StatusTypeDef BQ25155_TriggerADC(BQ25155_Handle_t *h)
{
    uint8_t reg;

    if (BQ25155_ReadReg(h, BQ25155_ADCCTRL0, &reg) != HAL_OK)
        return HAL_ERROR;

    reg |= (1 << 5);   // ADC_CONV_START

    return BQ25155_WriteReg(h, BQ25155_ADCCTRL0, reg);
}

HAL_StatusTypeDef BQ25155_IsADCReady(BQ25155_Handle_t *h, uint8_t *ready)
{
    uint8_t reg;
    HAL_StatusTypeDef status;

    status = BQ25155_ReadReg(h, BQ25155_FLAG2, &reg);
    if (status != HAL_OK)
        return status;

    *ready = (reg >> 7) & 0x01;   // ADC_READY bit

    return HAL_OK;
}

/**
  * For reading the VBAT you need to refresh the registers by initiating
  * the ADC reading. An example code:
  *
  * BQ25155_TriggerADC();
  *	HAL_Delay(30);
  * (Definitely check the ADC complete flag before performing any reading action by
  *  BQ25155_IsADCReady function)
  *	uint16_t vbat;
  *	BQ25155_ReadVBAT(&vbat);
  *
  */
HAL_StatusTypeDef BQ25155_ReadVBAT(BQ25155_Handle_t *h, uint16_t *voltage_mV)
{
    uint8_t msb, lsb;
    uint16_t code;
    HAL_StatusTypeDef status;

    BQ25155_TriggerADC(h);
    HAL_Delay(30);

//    (Optionally) check the ADC complete flag before performing any reading action by
//	   BQ25155_IsADCReady//

    status = BQ25155_ReadReg(h, BQ25155_ADCDATA_VBAT_M, &msb);
    if (status != HAL_OK) return status;

    status = BQ25155_ReadReg(h, BQ25155_ADCDATA_VBAT_L, &lsb);
    if (status != HAL_OK) return status;

    code = ((uint16_t)msb << 8) | lsb;

    uint32_t mv = (uint32_t)code * 6000;
    mv = mv / 65536;

    *voltage_mV = (uint16_t)mv;

    return HAL_OK;
}



HAL_StatusTypeDef BQ25155_InitAll(BQ25155_Handle_t *h)
{

    HAL_StatusTypeDef st;

    st = BQ25155_WriteReg(h, BQ25155_MASK0, 0x1F); if (st) return st;
    st = BQ25155_WriteReg(h, BQ25155_MASK1, 0xBE); if (st) return st;
    st = BQ25155_WriteReg(h, BQ25155_MASK2, 0x71); if (st) return st;
    st = BQ25155_WriteReg(h, BQ25155_MASK3, 0x77); if (st) return st;

    st = BQ25155_WriteReg(h, BQ25155_VBAT_CTRL, 0x3C); if (st) return st;
    st = BQ25155_WriteReg(h, BQ25155_ICHG_CTRL, 0x0C); if (st) return st;
    st = BQ25155_WriteReg(h, BQ25155_PCHRGCTRL, 0x02); if (st) return st;
    st = BQ25155_WriteReg(h, BQ25155_TERMCTRL, 0x3C); if (st) return st;

    st = BQ25155_WriteReg(h, BQ25155_BUVLO, 0x23); if (st) return st;
    st = BQ25155_WriteReg(h, BQ25155_CHARGERCTRL0, 0x52); if (st) return st;
    st = BQ25155_WriteReg(h, BQ25155_CHARGERCTRL1, 0x40); if (st) return st;
    st = BQ25155_WriteReg(h, BQ25155_ILIMCTRL, 0x02); if (st) return st;

    st = BQ25155_WriteReg(h, BQ25155_LDOCTRL, 0x30); if (st) return st;
    st = BQ25155_WriteReg(h, BQ25155_MRCTRL, 0x2A); if (st) return st;
    st = BQ25155_WriteReg(h, BQ25155_ICCTRL0, 0x10); if (st) return st;
    st = BQ25155_WriteReg(h, BQ25155_ICCTRL1, 0x0C); if (st) return st;
    st = BQ25155_WriteReg(h, BQ25155_ICCTRL2, 0x50); if (st) return st;

    st = BQ25155_WriteReg(h, BQ25155_ADCCTRL0, 0x00); if (st) return st;
    st = BQ25155_WriteReg(h, BQ25155_ADCCTRL1, 0x00); if (st) return st;

    st = BQ25155_WriteReg(h, BQ25155_ADCALARM_COMP1_M, 0x00); if (st) return st;
    st = BQ25155_WriteReg(h, BQ25155_ADCALARM_COMP1_L, 0x00); if (st) return st;
    st = BQ25155_WriteReg(h, BQ25155_ADCALARM_COMP2_M, 0x00); if (st) return st;
    st = BQ25155_WriteReg(h, BQ25155_ADCALARM_COMP2_L, 0x00); if (st) return st;
    st = BQ25155_WriteReg(h, BQ25155_ADCALARM_COMP3_M, 0x00); if (st) return st;
    st = BQ25155_WriteReg(h, BQ25155_ADCALARM_COMP3_L, 0x00); if (st) return st;

    st = BQ25155_WriteReg(h, BQ25155_ADC_READ_EN, 0x68); if (st) return st;
    st = BQ25155_WriteReg(h, BQ25155_TS_FASTCHGCTRL, 0x32); if (st) return st;

    st = BQ25155_WriteReg(h, BQ25155_TS_COLD, 0x7E); if (st) return st;
    st = BQ25155_WriteReg(h, BQ25155_TS_COOL, 0x6F); if (st) return st;
    st = BQ25155_WriteReg(h, BQ25155_TS_WARM, 0x39); if (st) return st;
    st = BQ25155_WriteReg(h, BQ25155_TS_HOT, 0x29);  if (st) return st;

    return HAL_OK;
}
