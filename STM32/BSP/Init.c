#include "Init.h"

//STM32F103 HAL库程序初始化

void STM32_Init(void)
{	
    HAL_Init();

    #ifdef __SYS_H
    sys_stm32_clock_init(RCC_PLL_MUL9);     /* 设置时钟, 72Mhz */
    #endif\

	//初始化DWT
    #ifdef __DWT_H
    DWT_Init();
    #endif
	
    #ifdef __DELAY_H
    delay_init(72);
	delay_ms(100); //上电延时
    #endif
	
	#ifndef __DELAY_H
	Init_Delay(); //上电延时
    #endif
	
    #ifdef __USART_H
    usart_init(115200);
    #endif
	
    #ifdef __UART_H
    uart_init(115200);
    #endif
	
    #ifdef __LED_H
    LED_Init();
    #endif

    #ifdef __KEY_H
    Key_Init();
    #endif
	
    #ifdef __OLED_H
    OLED_Init();
    #endif
	
	

    #ifdef __TIM2_H
    tim2_init();
    #endif
	//自定义模块初始化
	
	
	
	
	
	//初始化DHT11
    #ifdef __DHT11_H
    DHT11_Init();
    #endif

    //初始化蜂鸣器
    #ifdef __BEEP_H
    beep_init();
    #endif



    //初始化SPI
    #ifdef __SPI_H
    spi_init();
    #endif

    //初始化W25Q128
    #ifdef __W25Q128_H
    w25q128_init();
    #endif

    //初始化SDRAM
    #ifdef __SDRAM_H
    sram_init();
    #endif

    //初始化DMA
    #ifdef __DMA_H
    dma_init();
    #endif

    //初始化ADC
    #ifdef __ADC_H
    adc_init();
    #endif

    //初始化DAC
    #ifdef __DAC_H
    dac_init();
    #endif

    //初始化IIC
    #ifdef __IIC_H
    iic_init();
    #endif

    //初始化SPI
    #ifdef __SPI_H
    spi_init();
    #endif

    //初始化TFTLCD
    #ifdef __TFTLCD_H
    tftlcd_init();
    #endif

    //初始化触摸屏
    #ifdef __TOUCH_H
    touch_init();
    #endif

    //初始化SD
    #ifdef __SD_H
    sd_init();
    #endif
    
    //初始化FSMC
    #ifdef __FSMC_H
    fsmc_init();
    #endif

    //初始化摄像头
    #ifdef __OV2640_H
    ov2640_init();
    #endif

    //初始化WIFI
    #ifdef __WIFI_H
    wifi_init();
    #endif
	
	#ifdef __DELAY_H
	delay_ms(1000); //运行前延时
    #endif
	
	
	#ifndef __DELAY_H
	for (uint8_t k = 0; k < 20; k++)  //调用20次 ≈ 1秒
		Init_Delay(); //每次≈50ms    //运行前延时
    #endif
}

void Init_Delay(void)
{
	for (uint16_t i = 0; i < 1000; i ++)
	{
		for (uint16_t j = 0; j < 1000; j ++);
			__NOP(); 
	}
}
