#ifndef __WEIGHT_H
#define __WEIGHT_H

#include <stdint.h>

#define WEIGHT_SLAVE_ID           0x03
#define WEIGHT_FUNC_READ          0x03
#define WEIGHT_FUNC_WRITE         0x06

#define WEIGHT_REG_CH1_VALUE      0x0000
#define WEIGHT_REG_CH2_VALUE      0x0002
#define WEIGHT_REG_CH3_VALUE      0x0004
#define WEIGHT_REG_CH4_VALUE      0x0006

#define WEIGHT_REG_TARE           0x0034
#define WEIGHT_REG_ZERO_CAL       0x0035

#define WEIGHT_REG_TRACK_ZERO_CH3 0x0038
#define WEIGHT_REG_DIVISION_CH3   0x0040
#define WEIGHT_REG_SAMPLE_RATE_CH3 0x0045
#define WEIGHT_REG_COEFF_CH3      0x0068

#define WEIGHT_CH3_ACTION         0x0003

uint8_t Weight_ReadRegister(uint16_t reg_addr, uint16_t *value);
uint8_t Weight_WriteRegister(uint16_t reg_addr, uint16_t value);
uint8_t Weight_ReadChannel(uint8_t channel, int32_t *weight_g);
uint8_t Weight_Read(int32_t *weight_g);
void Weight_ScanAllChannels(void);
void Weight_ReadCh3Config(void);
void Weight_WriteCh3Defaults(void);
void Weight_WriteCh3Coefficient(float coeff);
uint8_t Weight_TareCh3(void);
uint8_t Weight_ZeroCalibrateCh3(void);

/* 兼容旧宏名 */
#define ZSG4_Read_Register(addr, val)        Weight_ReadRegister((addr), (val))
#define ZSG4_Write_Register(addr, val)       Weight_WriteRegister((addr), (val))
#define ZSG4_Read_Weight_Channel(ch, val)    Weight_ReadChannel((ch), (val))
#define ZSG4_Read_Weight(val)                Weight_Read((val))
#define ZSG4_Scan_All_Channels()             Weight_ScanAllChannels()
#define ZSG4_Read_CH3_Config()               Weight_ReadCh3Config()
#define ZSG4_Write_CH3_Defaults()            Weight_WriteCh3Defaults()
#define ZSG4_Write_CH3_Coefficient(c)        Weight_WriteCh3Coefficient((c))
#define ZSG4_Tare_CH3()                      Weight_TareCh3()
#define ZSG4_Zero_Cal_CH3()                  Weight_ZeroCalibrateCh3()

#endif
