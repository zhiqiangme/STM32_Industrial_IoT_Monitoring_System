//******************************************************************************** //
// 		声明：demo为780v2 demo。使用的为stm32的串口2作为通信串口。本程序中也对串口1      **
// 		进行了相关配置。若想使用串口1，则在主函数中将串口1配置放出即可。串口1的中断      **
//		程序已经编写完成。demo功能：对780v2进行进入配置状态操作和重启操作                                                                   
//                                                                                 **
//*********************************************************************************//                     


#include <string.h> 
#include "stm32f10x.h"
#include "delay.h"
#include "led.h"
#include "sys.h" 
#include "key.h" 


/*-----------------------------变量定义------------------------------*/
char received_house[20]; //数据接收数组
int house_num = 0; //数据接收数组计数
int config_flag = 0;
int order_num_max; //动作判断
int received_success_flag=0; //接收成功标志
char compare[20]; //比较数组
int  compare_order_num_max; //校验步骤判断
char strat_num[20]; //开机语接收数组


/*-----------------------------函数声明------------------------------*/
void Usart_SendByte( USART_TypeDef * pUSARTx, uint8_t ch ); 
void led_circle(); //led闪烁函数
void clear_received_house(); //清空接收缓存
void start_handle(); //开机语处理
   

/*-----------------------------函数实现------------------------------*/
//函数1 串口1初始化
//说明：my_usart1_init()函数与USART1_IRQHandler()函数配套使用
void my_usart1_init()
{
	GPIO_InitTypeDef GPIO_Initstrue1; //定义GPIO结构体
	USART_InitTypeDef USART_Initstrue1; //定义串口1结构体
	NVIC_InitTypeDef NVIC_Initstrue1; //定义中断参数
	
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE); //使能A口时钟
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1, ENABLE); //使能串口1时钟
	
	GPIO_Initstrue1.GPIO_Pin = GPIO_Pin_9; //配置串口1的tx（A口9脚）
	GPIO_Initstrue1.GPIO_Mode = GPIO_Mode_AF_PP; //为复用推挽	
	GPIO_Initstrue1.GPIO_Speed = GPIO_Speed_50MHz; //时钟50mhz
	GPIO_Init(GPIOA,&GPIO_Initstrue1);  //写入
	  
	GPIO_Initstrue1.GPIO_Pin = GPIO_Pin_10; //配置串口1的rx（A口10脚）
	GPIO_Initstrue1.GPIO_Mode = GPIO_Mode_IN_FLOATING;	//为浮空输入
	GPIO_Initstrue1.GPIO_Speed = GPIO_Speed_50MHz; //时钟50mhz
	GPIO_Init(GPIOA,&GPIO_Initstrue1); //写入
	
	USART_Initstrue1.USART_BaudRate = 115200; //波特率115200
	USART_Initstrue1.USART_HardwareFlowControl = USART_HardwareFlowControl_None; //硬件流控无
	USART_Initstrue1.USART_Mode = USART_Mode_Rx|USART_Mode_Tx; //发送接收使能
	USART_Initstrue1.USART_Parity = USART_Parity_No; //无奇偶校验
	USART_Initstrue1.USART_StopBits = USART_StopBits_1; //停止位1
	USART_Initstrue1.USART_WordLength = USART_WordLength_8b; //有效数据八位 
	USART_Init(USART1,&USART_Initstrue1); //串口初始化
	
	USART_Cmd(USART1,ENABLE);  //串口1使能
	USART_ITConfig(USART1,USART_IT_RXNE,ENABLE); //使能接收非空中断
	
	NVIC_Initstrue1.NVIC_IRQChannel = USART1_IRQn; //中断通道设置为串口1
	NVIC_Initstrue1.NVIC_IRQChannelCmd = ENABLE; //开启中断通道
	NVIC_Initstrue1.NVIC_IRQChannelPreemptionPriority = 1; //设置中断优先级为1
	NVIC_Initstrue1.NVIC_IRQChannelSubPriority = 1; //设置子优先级为1
	NVIC_Init(&NVIC_Initstrue1);
}

//函数2 串口1接收中断
//说明：my_usart1_init()函数与USART1_IRQHandler()函数配套使用
void USART1_IRQHandler()
{
	u8 res;
	if(USART_GetITStatus(USART1,USART_IT_RXNE))
	{
		 res = USART_ReceiveData(USART1);
		 USART_SendData(USART1,res);
	}
}

//函数3 串口2初始化函数
//说明：my_usart2_init()函数与USART2_IRQHandler()配套使用
void my_usart2_init()
{
	GPIO_InitTypeDef GPIO_Initstrue2; //定义GPIO结构体
	USART_InitTypeDef USART_Initstrue2; //定义串口2结构体                                                                                                                                    
	NVIC_InitTypeDef NVIC_Initstrue2; //定义中断参数
	
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE); //使能A口时钟
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART2, ENABLE);  //使能串口2时钟
  
	GPIO_Initstrue2.GPIO_Pin = GPIO_Pin_2; //配置串口2的tx（A口2脚）
	GPIO_Initstrue2.GPIO_Mode = GPIO_Mode_AF_PP; //为复用推挽	
	GPIO_Initstrue2.GPIO_Speed = GPIO_Speed_50MHz; //时钟50mhz
	GPIO_Init(GPIOA,&GPIO_Initstrue2); //写入
	
	GPIO_Initstrue2.GPIO_Pin = GPIO_Pin_3; //配置串口2的rx（A口3脚）
	GPIO_Initstrue2.GPIO_Mode = GPIO_Mode_IN_FLOATING; //为浮空输入
	GPIO_Initstrue2.GPIO_Speed = GPIO_Speed_50MHz; //时钟50mhz
	GPIO_Init(GPIOA,&GPIO_Initstrue2); //写入
	
	USART_Initstrue2.USART_BaudRate = 115200; //波特率115200
	USART_Initstrue2.USART_HardwareFlowControl = USART_HardwareFlowControl_None; //硬件流控无
	USART_Initstrue2.USART_Mode = USART_Mode_Rx|USART_Mode_Tx; //发送接收使能
	USART_Initstrue2.USART_Parity = USART_Parity_No; //无奇偶校验
	USART_Initstrue2.USART_StopBits = USART_StopBits_1; //停止位1
	USART_Initstrue2.USART_WordLength = USART_WordLength_8b; //有效数据八位
	USART_Init(USART2,&USART_Initstrue2); //串口初始化
	USART_Cmd(USART2,ENABLE); //串口2使能
	
	USART_ITConfig(USART2,USART_IT_RXNE,ENABLE); //使能接收非空中断
	
	NVIC_Initstrue2.NVIC_IRQChannel = USART2_IRQn; //中断通道设置为串口2
	NVIC_Initstrue2.NVIC_IRQChannelCmd =  ENABLE; //开启中断通道
	NVIC_Initstrue2.NVIC_IRQChannelPreemptionPriority = 1;//设置中断优先级为1
	NVIC_Initstrue2.NVIC_IRQChannelSubPriority = 1;  //设置子优先级为1
	NVIC_Init(&NVIC_Initstrue2);
}

//函数3 串口2接收中断函数
//说明：my_usart2_init()函数与USART2_IRQHandler()配套使用
void USART2_IRQHandler()
{
	if(USART_GetITStatus(USART2,USART_IT_RXNE))
	{
		received_house[house_num] = USART_ReceiveData(USART2);
		if(received_success_flag==0) //未接收完成自加
		{	
		house_num++;
		}
	}
}

//数据接收完成之后的处理函数
void success_received_handle()
{
	if(received_success_flag==1) //接收标志完成
	{
		if(compare_order_num_max==1)
			strcpy(compare,"a+ok\r\n"); //进入配置状态指令发送之后的模块正常返回值
		else if(compare_order_num_max>=2)
			strcpy(compare,"AT+Z\r\n\r\nOK\r\n\r\n"); //重启指令发送后的模块正确返回值
		if(strcmp(compare,received_house)==0)//判断接收正常
		{
			led_circle();
			received_success_flag = 0;
			//判断完成之后客户可根据自己的需求放置判断标志位用于成功或者失败之后下一步操作的指示
		}
		//else
		//{
		//     判断完成之后客户可根据自己的需求放置判断标志位用于成功或者失败之后下一步操作的指示
		//}
		clear_received_house();
		house_num = 0;
		received_success_flag = 0;
	}
}
//注：数据处理函数目前只写了配置状态和重启指令发送之后的返回值校验，其他指令校验客户可以模块返回值特性自行编写



//函数4 字节发送函数，发送一个字节数据
void Usart_SendByte( USART_TypeDef * pUSARTx, uint8_t ch )
{
	USART_SendData(pUSARTx,ch); //发送一个字节数据到USART1 
	while (USART_GetFlagStatus(pUSARTx, USART_FLAG_TXE) == RESET); //等待发送完毕 
}

//函数5 字符串发送函数
void Usart_SendString( USART_TypeDef * pUSARTx, uint8_t *str)
{
	unsigned int k=0;
    do 
    {
        Usart_SendByte( pUSARTx, *(str + k) );
        k++;
    } while(*(str + k)!='\0');
}

//led闪烁函数
void led_circle()
{
	LED1=0;
	delay_ms(100);
	LED1=1;
}

//接收数组清空函数
void clear_received_house()
{
	int i;
	for(i=0;i<20;i++)
	{
		received_house[i]=0;
	}
}

//模块开机信息接收和处理函数
void start_handle()
{
	strcpy(strat_num,"\r\n[USR-7S4 V2]");
	if(strcmp(strat_num,received_house)==0)
	{
		clear_received_house();
		house_num = 0;
	}
}

int main(void)
{	
	NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2); //抢占和响应优先级为2
//my_usart1_init(); //若用到串口1则将注释去掉，并将串口1中断放出即可
	KEY_Init(); //按键初始化
	my_usart2_init(); //串口2初始化
	LED_Init(); //led初始化
	delay_init(72);	 //延时初始化
	while(1)
	{
		if(KEY0==0) //按键0
		{
			delay_ms(30);
			if(KEY0==0)
			{
				while(KEY0==0);
				order_num_max = compare_order_num_max = 1;
				config_flag = 0;
				LED0=1;
				
			}
		}
		
		if(KEY1==0) //按键1
		{
			delay_ms(30);
			if(KEY1==0)
			{
				while(KEY1==0);
				order_num_max = compare_order_num_max = 2;
				config_flag = 0;
				LED0=1;
			}
		}
		
		switch(order_num_max)
		{
			case 1: //进入配置的动作
			Usart_SendString(USART2,"+++");
			delay_ms(200);
			Usart_SendString(USART2,"a");
			order_num_max = 0;
			delay_ms(20000); //毫秒延时并不准确，因此这个延时是并不能说明等待时间为20秒钟，用户真实延时大概1秒钟即可
			received_success_flag = 1;
			//由于接收数据中结尾的数据在整个过程中也出现，因此无法使用结尾字符当作判断接收完成的标志
			//此处使用延时等待来判断。用户在代码执行过程中也可以使用定时器来进行计时等待，以增强代码执行的实时性
			house_num = 0;
			break;
			
			case 2: //重启动作，由于重启返回指令回复时间较长，此时延时等待时间加长
			Usart_SendString(USART2,"AT+Z\r\n");
			order_num_max = 0;
			delay_ms(40000);//毫秒延时并不准确，因此这个延时是并不能说明等待时间为40秒钟，用户真实延时大概2秒钟即可
			received_success_flag = 1;
			house_num = 0;
			break;
			
			default:	//缺省动作时，做清空接收区动作，防止干扰数据造成接收错误
			clear_received_house();
			house_num = 0;
		}
		success_received_handle(); //接收完成处理
		start_handle(); //有开机信息则处理开机信息
	}					 
}


//AT指令集
/*
>[Rx<-][11:24:01][asc]
T: test command
AT+H: command help information
AT+E: enable or disable echo
AT+ENTM: back to throughput mode
AT+VER: firmware version
AT+Z: restart module
AT+REBOOT: restart module
AT+BUILD: firmware Build
AT+WKMOD: query or set woke mode
AT+CMDPW: query or set command password
AT+SN: SN information
AT+RSTIM: set restart time
AT+APN: query or set APN
AT+SOCKIND: enable or disable indication of socket
AT+SDPEN: enable or disable Socket Distribution Protocol
AT+KEEPALIVEA: query or set socket A keepalive
AT+KEEPALIVEB: query or set socket B keepalive
AT+SOCKA: query or set socket A parameters
AT+SOCKAEN: enable or disable socket A
AT+SOCKALK: query the connection status of socket A
AT+SOCKASL: query or set long or short connection of socket A
AT+SHORATO: query or set the timeout of short socket A
AT+SOCKATO: query or set the timeout of socket A
AT+SOCKB: query or set socket B parameters
AT+SOCKBEN: enable or disable socket B
AT+SOCKBSL: query or set long or short connection of socket B

>[Rx<-][11:24:01][asc]

AT+SOCKBLK: query the connection status of socket B
AT+SHORBTO: query or set the timeout of short socket B
AT+SOCKBTO: query or set the timeout of socket B
AT+SOCKRSTIM: query or set the timeout of all sockets to restart
AT+CSQ: query current RSSI
AT+IMEI: IMEI information for 402tf in china
AT+ICCID: ICCID information for 402tf in china
AT+IMSI: IMSI information for 402tf in china
AT+STMSG: start message
AT+UART: query or set uart parameters
AT+UARTFT: query or set uart data pack period
AT+UARTFL: query or set uart data pack length
AT+CFGTF: save current parameters as user factory parameters
AT+RELD: reload user factory parameters
AT+CLEAR: reload USR factory parameters
AT+SYSINFO: System networking information display
AT+SYSCONFIG: System networking information display
AT+REGEN: enable or disable register package function
AT+REGDT: query or set user defined register data
AT+REGTP: query or set the type of register
AT+REGSND: query or set when to send register pack
AT+CLOUD: query or se
>[Rx<-][11:24:01][asc]
t passthrough cloud ID and password
AT+SMSEND: send SMS, same to CISMSSEND
AT+CISMSSEND: send SMS, same to SMSEND
AT+SMSREN: query or set if filter SMS recv
AT+HEARTEN: enable or disable heart beat function
AT+HEARTDT: query or set user defined heart data
AT+HEARTSND: query or set the dirction to send
AT+HEARTTM: query or set heart sending period
AT+HTPTP: query or set HTTP request type
AT+HTPURL: query or set HTTP request URL
AT+HTPSV: query or set HTTP server address and port
AT+HTPHD: query or set HTTP request head
AT+HTPTO: query or set HTTP request time out
AT+HTPFLT: query or set if filter HTTP head
AT+LOCIP: get local ip
AT+ID: query or set UDC ID
AT+LBS: get LBS
AT+SHELL: query or set shell cmd
*/

/*
具体指令集的应用与操作参看780v2软件设计手册：http://www.usr.cn/Down/USR-G780-V2_software_V1.0.3.pdf
*/
//程序执行过程描述
//按下按键1，780进入配置状态，若返回的指令回复判断正常，则led闪烁，反之不闪烁
//按下按键2，780重启，若返回的指令回复判断正常，则led闪烁，反之不闪烁
