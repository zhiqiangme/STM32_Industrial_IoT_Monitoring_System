/**
  ******************************************************************************
  * @file    system_stm32f1xx.c
  * @author  MCD Application Team
  * @brief   CMSIS Cortex-M3 Device Peripheral Access Layer System Source File.
  *
  * Bootloader 专用版本：
  * - 向量表固定在 0x08000000
  * - 不能与 App 共享同一份 VECT_TAB_OFFSET 配置
  ******************************************************************************
  */

#include "stm32f1xx.h"

#if !defined(HSE_VALUE)
#define HSE_VALUE    ((uint32_t)8000000)
#endif

#if !defined(HSI_VALUE)
#define HSI_VALUE    ((uint32_t)8000000)
#endif

/* #define VECT_TAB_SRAM */
#define VECT_TAB_OFFSET  0x0

uint32_t SystemCoreClock = 8000000;
const uint8_t AHBPrescTable[16] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 3, 4, 6, 7, 8, 9};
const uint8_t APBPrescTable[8] =  {0, 0, 0, 0, 1, 2, 3, 4};

void SystemInit(void)
{
  RCC->CR |= (uint32_t)0x00000001;

#if !defined(STM32F105xC) && !defined(STM32F107xC)
  RCC->CFGR &= (uint32_t)0xF8FF0000;
#else
  RCC->CFGR &= (uint32_t)0xF0FF0000;
#endif

  RCC->CR &= (uint32_t)0xFEF6FFFF;
  RCC->CR &= (uint32_t)0xFFFBFFFF;
  RCC->CFGR &= (uint32_t)0xFF80FFFF;

#if defined(STM32F105xC) || defined(STM32F107xC)
  RCC->CR &= (uint32_t)0xEBFFFFFF;
  RCC->CIR = 0x00FF0000;
  RCC->CFGR2 = 0x00000000;
#elif defined(STM32F100xB) || defined(STM32F100xE)
  RCC->CIR = 0x009F0000;
  RCC->CFGR2 = 0x00000000;
#else
  RCC->CIR = 0x009F0000;
#endif

#ifdef VECT_TAB_SRAM
  SCB->VTOR = SRAM_BASE | VECT_TAB_OFFSET;
#else
  SCB->VTOR = FLASH_BASE | VECT_TAB_OFFSET;
#endif
}

void SystemCoreClockUpdate(void)
{
  uint32_t tmp = 0, pllmull = 0, pllsource = 0;

#if defined(STM32F105xC) || defined(STM32F107xC)
  uint32_t prediv1source = 0, prediv1factor = 0, prediv2factor = 0, pll2mull = 0;
#endif

#if defined(STM32F100xB) || defined(STM32F100xE)
  uint32_t prediv1factor = 0;
#endif

  tmp = RCC->CFGR & RCC_CFGR_SWS;

  switch (tmp)
  {
    case 0x00:
      SystemCoreClock = HSI_VALUE;
      break;
    case 0x04:
      SystemCoreClock = HSE_VALUE;
      break;
    case 0x08:
      pllmull = RCC->CFGR & RCC_CFGR_PLLMULL;
      pllsource = RCC->CFGR & RCC_CFGR_PLLSRC;

#if !defined(STM32F105xC) && !defined(STM32F107xC)
      pllmull = (pllmull >> 18) + 2;

      if (pllsource == 0x00)
      {
        SystemCoreClock = (HSI_VALUE >> 1) * pllmull;
      }
      else
      {
#if defined(STM32F100xB) || defined(STM32F100xE)
        prediv1factor = (RCC->CFGR2 & RCC_CFGR2_PREDIV1) + 1;
        SystemCoreClock = (HSE_VALUE / prediv1factor) * pllmull;
#else
        if ((RCC->CFGR & RCC_CFGR_PLLXTPRE) != (uint32_t)RESET)
        {
          SystemCoreClock = (HSE_VALUE >> 1) * pllmull;
        }
        else
        {
          SystemCoreClock = HSE_VALUE * pllmull;
        }
#endif
      }
#else
      pllmull = pllmull >> 18;

      if (pllmull != 0x0D)
      {
        pllmull += 2;
      }
      else
      {
        pllmull = 13 / 2;
      }

      if (pllsource == 0x00)
      {
        SystemCoreClock = (HSI_VALUE >> 1) * pllmull;
      }
      else
      {
        prediv1source = RCC->CFGR2 & RCC_CFGR2_PREDIV1SRC;
        prediv1factor = (RCC->CFGR2 & RCC_CFGR2_PREDIV1) + 1;

        if (prediv1source == 0)
        {
          SystemCoreClock = (HSE_VALUE / prediv1factor) * pllmull;
        }
        else
        {
          prediv2factor = ((RCC->CFGR2 & RCC_CFGR2_PREDIV2) >> 4) + 1;
          pll2mull = ((RCC->CFGR2 & RCC_CFGR2_PLL2MUL) >> 8) + 2;
          SystemCoreClock = (((HSE_VALUE / prediv2factor) * pll2mull) / prediv1factor) * pllmull;
        }
      }
#endif
      break;

    default:
      SystemCoreClock = HSI_VALUE;
      break;
  }

  tmp = AHBPrescTable[((RCC->CFGR & RCC_CFGR_HPRE) >> 4)];
  SystemCoreClock >>= tmp;
}
