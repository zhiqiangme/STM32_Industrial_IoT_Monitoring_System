#ifndef __BSP_ZSG4_H
#define __BSP_ZSG4_H

#include <stdint.h>

#define ZSG4_SLAVE_ID       0x02
#define ZSG4_FUNC_READ      0x03   /* Read Holding Registers */
#define ZSG4_FUNC_WRITE     0x06   /* Write Single Holding Register */

/* Channel Weight Value Registers (each channel uses 2 regs for int32) */
#define ZSG4_REG_CH1_WEIGHT 0x0000 /* Protocol addr 0x0000~0x0001 */
#define ZSG4_REG_CH2_WEIGHT 0x0002 /* Protocol addr 0x0002~0x0003 */
#define ZSG4_REG_CH3_WEIGHT 0x0004 /* Protocol addr 0x0004~0x0005 */
#define ZSG4_REG_CH4_WEIGHT 0x0006 /* Protocol addr 0x0006~0x0007 */

/* Action Registers */
#define ZSG4_REG_TARE       0x0034 /* Tare trigger */
#define ZSG4_REG_ZERO_CAL   0x0035 /* Zero calibration trigger */

/* Channel action values */
#define ZSG4_CH1_ACTION     0x0001
#define ZSG4_CH2_ACTION     0x0002
#define ZSG4_CH3_ACTION     0x0003
#define ZSG4_CH4_ACTION     0x0004

/* Read specific channel weight */
uint8_t ZSG4_Read_Weight_Channel(uint8_t channel, int32_t *weight_g);

/* Convenience function for Channel 3 */
uint8_t ZSG4_Read_Weight(int32_t *weight_g);

/* Scan all channels for diagnostics */
void ZSG4_Scan_All_Channels(void);

/* Tare Channel 3 (set current weight as zero) */
uint8_t ZSG4_Tare_CH3(void);

/* Zero calibration for Channel 3 */
uint8_t ZSG4_Zero_Cal_CH3(void);

#endif
