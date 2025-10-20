#include "stm32f10x.h" // Device header
#include "Delay.h"
#include "OLED.h"
#include <string.h>
#include <stdio.h> // 添加 sscanf 和 sprintf 支持

// 通勤人数计数器
uint16_t attendanceCount = 0;

// 时间变量 - 声明为 volatile
volatile uint8_t hours = 0, minutes = 0, seconds = 0; // 初始时间设为0，等待同步

// 日期变量 - 声明为 volatile
volatile uint16_t year = 0;
volatile uint8_t month = 0, day = 0;
volatile uint8_t dateSynced = 0; // 日期同步标志

// 显示更新标志位 - 声明为 volatile
volatile uint8_t updateDisplayFlag = 0;

// 函数声明
void USART_Config(void);
void TIM_Config(void);
void parseCommand(char *cmd);
void updateTime(void);
void processCheckIn(char *employeeID);					 // 新函数名
void USART_SendString(USART_TypeDef *USARTx, char *str); // 声明发送函数

// 串口接收缓冲区
#define RX_BUFFER_SIZE 128
char rxBuffer[RX_BUFFER_SIZE];
uint8_t rxIndex = 0;

int main(void)
{
	// 系统模块初始化
	OLED_Init();	// OLED初始化
	USART_Config(); // 串口初始化
	TIM_Config();	// 定时器初始化(用于时间递增)

	// 初始显示 - 恢复 4 行布局
	OLED_Clear();
	OLED_ShowString(1, 1, "Attendance Sys"); // 恢复较长标题
	OLED_ShowString(2, 1, "Count:");
	OLED_ShowNum(2, 7, attendanceCount, 3);
	OLED_ShowString(3, 1, "Date: --.-- --"); // 日期占位符在第 3 行
	OLED_ShowString(4, 1, "Time: --:--:--"); // 时间占位符在第 4 行

	USART_SendString(USART1, "System Initialized (No Servo)\r\n"); // 更新调试信息

	while (1)
	{
		// 检查是否需要更新显示
		if (updateDisplayFlag)
		{
			updateDisplayFlag = 0; // 清除标志

			// 更新计数显示 (第 2 行)
			OLED_ShowString(2, 1, "Count:");
			OLED_ShowNum(2, 7, attendanceCount, 3);
			OLED_ShowString(2, 10, "      "); // 清除第 2 行剩余部分

			// 更新日期显示 (第 3 行, 如果已同步)
			if (dateSynced)
			{
				OLED_ShowString(3, 1, "Date:"); // 确保标签在
				OLED_ShowNum(3, 6, month, 2);	// MM
				OLED_ShowChar(3, 8, '-');
				OLED_ShowNum(3, 9, day, 2);			// DD
				OLED_ShowChar(3, 11, ' ');			// Spacing
				OLED_ShowNum(3, 12, year % 100, 2); // YY
				OLED_ShowString(3, 14, "  ");		// 清除第 3 行剩余部分
			}
			else
			{
				OLED_ShowString(3, 1, "Date: --.-- --");
			}

			// 更新时间显示 (第 4 行)
			OLED_ShowString(4, 1, "Time:");
			OLED_ShowNum(4, 6, hours, 2); // HH
			OLED_ShowChar(4, 8, ':');
			OLED_ShowNum(4, 9, minutes, 2); // MM
			OLED_ShowChar(4, 11, ':');
			OLED_ShowNum(4, 12, seconds, 2); // SS
			OLED_ShowString(4, 14, "  ");	 // 清除第 4 行剩余部分
		}
		// 主循环等待中断
	}
}

// USART 发送字符串函数
void USART_SendString(USART_TypeDef *USARTx, char *str)
{
	while (*str)
	{
		// 检查 USARTx 是否有效 (可选，增加健壮性)
		// if(USARTx == NULL) return;

		USART_SendData(USARTx, *str++);
		// 等待发送完成
		while (USART_GetFlagStatus(USARTx, USART_FLAG_TXE) == RESET)
			;
	}
}

// 配置串口
void USART_Config(void)
{
	// 使能时钟
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1 | RCC_APB2Periph_GPIOA, ENABLE);

	// 配置GPIO
	GPIO_InitTypeDef GPIO_InitStructure;

	// 配置USART1 Tx (PA9)
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_9;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(GPIOA, &GPIO_InitStructure);

	// 配置USART1 Rx (PA10)
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_10;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING; // 浮空输入
	GPIO_Init(GPIOA, &GPIO_InitStructure);

	// 配置USART参数
	USART_InitTypeDef USART_InitStructure;
	USART_InitStructure.USART_BaudRate = 115200;
	USART_InitStructure.USART_WordLength = USART_WordLength_8b;
	USART_InitStructure.USART_StopBits = USART_StopBits_1;
	USART_InitStructure.USART_Parity = USART_Parity_No;
	USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
	USART_InitStructure.USART_Mode = USART_Mode_Rx | USART_Mode_Tx; // 使能接收和发送

	USART_Init(USART1, &USART_InitStructure);

	// 使能接收中断 (RXNE中断)
	USART_ITConfig(USART1, USART_IT_RXNE, ENABLE);

	// 配置NVIC (嵌套向量中断控制器)
	NVIC_InitTypeDef NVIC_InitStructure;
	NVIC_InitStructure.NVIC_IRQChannel = USART1_IRQn;		  // 中断通道
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0; // 抢占优先级
	NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;		  // 子优先级
	NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;			  // 使能中断通道
	NVIC_Init(&NVIC_InitStructure);

	// 使能USART1
	USART_Cmd(USART1, ENABLE);
}

// 配置定时器 (TIM3 用于秒递增)
void TIM_Config(void)
{
	// 使能TIM3时钟
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM3, ENABLE);

	// 配置TIM3
	TIM_TimeBaseInitTypeDef TIM_TimeBaseStructure;
	// 定时器产生1秒中断: 72MHz / 7200 / 10000 = 1Hz = 1s
	TIM_TimeBaseStructure.TIM_Period = 9999;					// 自动重装载值 (ARR)
	TIM_TimeBaseStructure.TIM_Prescaler = 7199;					// 预分频器 (PSC) (7200-1 -> 7199 for 0-based)
	TIM_TimeBaseStructure.TIM_ClockDivision = 0;				// 时钟分频因子
	TIM_TimeBaseStructure.TIM_CounterMode = TIM_CounterMode_Up; // 向上计数模式
	TIM_TimeBaseInit(TIM3, &TIM_TimeBaseStructure);

	// 清除更新中断标志位 (防止一开始就触发中断)
	TIM_ClearITPendingBit(TIM3, TIM_IT_Update);
	// 使能TIM3更新中断
	TIM_ITConfig(TIM3, TIM_IT_Update, ENABLE);

	// 配置NVIC
	NVIC_InitTypeDef NVIC_InitStructure;
	NVIC_InitStructure.NVIC_IRQChannel = TIM3_IRQn;
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1; // 优先级低于串口
	NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
	NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
	NVIC_Init(&NVIC_InitStructure);

	// 使能TIM3计数器
	TIM_Cmd(TIM3, ENABLE);
}

// 更新时间 (由 TIM3 中断调用)
void updateTime(void)
{
	seconds++;
	if (seconds >= 60)
	{
		seconds = 0;
		minutes++;
		if (minutes >= 60)
		{
			minutes = 0;
			hours++;
			if (hours >= 24)
			{
				hours = 0;
			}
		}
	}
}

// 处理打卡成功函数
void processCheckIn(char *employeeID)
{
	OLED_Clear();
	// 显示 ID
	OLED_ShowString(1, 1, "ID:");
	char displayID[11] = {0};
	strncpy(displayID, employeeID, 10);
	OLED_ShowString(1, 4, displayID);

	// 显示打卡成功
	OLED_ShowString(2, 1, "Check-in OK!");

	// 模拟"开门"动作并发送反馈
	USART_SendString(USART1, "Simulating Door Open...\r\n"); // 调试信息
	// 此处可以添加其他模拟动作，例如点亮一个LED指示灯
	USART_SendString(USART1, "OK: Door Open\r\n"); // 发送开门反馈给主机

	// 更新通勤人数
	attendanceCount++;

	// 短暂显示打卡信息
	Delay_ms(2000);

	// 恢复正常显示 (4 行布局)
	OLED_Clear();
	OLED_ShowString(1, 1, "Attendance Sys");
	OLED_ShowString(2, 1, "Count: ---");
	OLED_ShowString(3, 1, "Date: --.-- --");
	OLED_ShowString(4, 1, "Time: --:--:--");
	updateDisplayFlag = 1; // 触发主循环更新显示
}

// 解析接收到的命令
void parseCommand(char *cmd)
{
	char employeeID[20] = {0};
	int h = -1, m = -1;
	int yr = -1, mn = -1, dy = -1; // 用于解析日期

	// 格式: "Hello STM32,id:123,time:14:35,date:2024-07-26\n"
	if (strstr(cmd, "Hello STM32"))
	{
		USART_SendString(USART1, "CMD RX: ");
		USART_SendString(USART1, cmd);

		// 解析 ID (逻辑不变)
		char *idStart = strstr(cmd, "id:");
		if (idStart)
		{
			idStart += 3;
			char *idEnd = strstr(idStart, ",");
			if (idEnd)
			{
				int idLen = idEnd - idStart;
				if (idLen > 0 && idLen < sizeof(employeeID) - 1)
				{
					strncpy(employeeID, idStart, idLen);
					employeeID[idLen] = '\0';
				}
				else
				{
					USART_SendString(USART1, "Invalid ID length.\r\n");
					employeeID[0] = '\0';
				}
			}
			else
			{
				USART_SendString(USART1, "ID delimiter ',' not found.\r\n");
				employeeID[0] = '\0';
			}
		}
		else
		{
			USART_SendString(USART1, "'id:' tag not found.\r\n");
		}

		// 解析时间 (逻辑不变)
		char *timeStart = strstr(cmd, "time:");
		char *timeTagEnd = NULL; // 指向 time: 标签后的逗号或结束符
		if (timeStart)
		{
			timeTagEnd = strstr(timeStart, ","); // 查找时间后的逗号
			timeStart += 5;
			char *timeEnd = NULL;
			if (timeTagEnd)
			{ // 如果找到逗号，说明后面还有内容
				timeEnd = timeTagEnd;
			}
			else
			{ // 否则时间是最后一个字段
				timeEnd = strpbrk(timeStart, "\r\n");
			}

			if (timeEnd)
				*timeEnd = '\0'; // 截断

			if (sscanf(timeStart, "%d:%d", &h, &m) == 2)
			{
				if (h >= 0 && h <= 23 && m >= 0 && m <= 59)
				{
					hours = (uint8_t)h;
					minutes = (uint8_t)m;
					seconds = 0;
					// updateDisplayFlag = 1; // 时间更新单独触发显示，避免覆盖日期检查逻辑
					char timeBuffer[20];
					sprintf(timeBuffer, "Time Sync: %02d:%02d\r\n", hours, minutes);
					USART_SendString(USART1, timeBuffer);
				}
				else
				{
					USART_SendString(USART1, "Invalid time value parsed.\r\n");
				}
			}
			else
			{
				USART_SendString(USART1, "Time format parsing failed.\r\n");
			}
			// 恢复截断的字符，以便解析日期
			if (timeEnd)
				*timeEnd = (timeTagEnd != NULL) ? ',' : '\n';
		}
		else
		{
			USART_SendString(USART1, "'time:' tag not found.\r\n");
		}

		// 解析日期
		char *dateStart = strstr(cmd, "date:");
		if (dateStart)
		{
			dateStart += 5;								// 跳过 "date:"
			char *dateEnd = strpbrk(dateStart, "\r\n"); // 日期通常是最后一个字段
			if (dateEnd)
				*dateEnd = '\0'; // 截断

			// 使用 sscanf 解析 YYYY-MM-DD 格式
			if (sscanf(dateStart, "%d-%d-%d", &yr, &mn, &dy) == 3)
			{
				// 基本有效性检查 (简单)
				if (yr > 2000 && mn >= 1 && mn <= 12 && dy >= 1 && dy <= 31)
				{
					char dateBuffer[30];
					sprintf(dateBuffer, "Date Parsed: %d-%02d-%02d\r\n", yr, mn, dy);
					USART_SendString(USART1, dateBuffer);

					// 检查日期是否变化（新的一天）
					if (dateSynced == 0 || (uint16_t)yr != year || (uint8_t)mn != month || (uint8_t)dy != day)
					{
						if (dateSynced == 1)
						{ // 只有在之前同步过，且日期确实变了，才重置计数
							USART_SendString(USART1, "New day detected! Resetting count.\r\n");
							attendanceCount = 0; // 重置考勤计数
						}
						else
						{
							USART_SendString(USART1, "First date sync.\r\n");
						}
						// 更新存储的日期
						year = (uint16_t)yr;
						month = (uint8_t)mn;
						day = (uint8_t)dy;
						dateSynced = 1;		   // 标记日期已同步
						updateDisplayFlag = 1; // 触发显示更新（包括新日期和可能重置的计数）
					}
					else
					{
						// 日期未变
						USART_SendString(USART1, "Date is the same.\r\n");
						// 如果时间也更新了，确保显示标志被设置
						if (h != -1)
							updateDisplayFlag = 1;
					}
				}
				else
				{
					USART_SendString(USART1, "Invalid date value parsed.\r\n");
				}
			}
			else
			{
				USART_SendString(USART1, "Date format parsing failed.\r\n");
			}
			// if(dateEnd) *dateEnd = '\n'; // 恢复换行符
		}
		else
		{
			USART_SendString(USART1, "'date:' tag not found.\r\n");
			// 如果连日期都没有，但有ID和时间，可能还是需要更新时间显示
			if (h != -1)
				updateDisplayFlag = 1;
		}

		// 如果成功解析到 ID 且日期和时间至少尝试解析过，则处理打卡
		if (employeeID[0] != '\0')
		{
			processCheckIn(employeeID); // 调用打卡处理函数
										// processCheckIn 内部会设置 updateDisplayFlag
		}
		else
		{
			USART_SendString(USART1, "No valid ID found. Check-in aborted.\r\n");
			// 如果只有时间/日期更新，没有有效ID，确保显示更新
			if (h != -1 || yr != -1)
				updateDisplayFlag = 1;
		}
	}
	else
	{
		USART_SendString(USART1, "Not 'Hello STM32' command.\r\n");
	}
}

// USART1中断处理函数
void USART1_IRQHandler(void)
{
	// 检查是否是接收中断
	if (USART_GetITStatus(USART1, USART_IT_RXNE) != RESET)
	{
		// 读取接收到的数据
		char ch = USART_ReceiveData(USART1);

		// 存储到缓冲区，防止溢出
		if (rxIndex < RX_BUFFER_SIZE - 1)
		{
			// 只存储有效字符，忽略回车符本身，但用它来判断结束
			if (ch != '\r')
			{
				rxBuffer[rxIndex++] = ch;
			}
		}
		else
		{
			// 缓冲区溢出，重置索引
			rxIndex = 0;
			USART_SendString(USART1, "RX Buffer Overflow!\r\n");
		}

		// 如果收到行结束符 '\n' (或 '\r' 也可以作为结束标志)
		if (ch == '\n')
		{
			rxBuffer[rxIndex] = '\0'; // 添加字符串结束符
			if (rxIndex > 0)
			{							// 确保不是空行
				parseCommand(rxBuffer); // 处理命令
			}
			rxIndex = 0;						 // 重置缓冲区索引，准备接收下一条命令
			memset(rxBuffer, 0, RX_BUFFER_SIZE); // 清空缓冲区 (可选，更安全)
		}
		// 清除RXNE中断标志位 （重要！）
		// USART_ClearITPendingBit(USART1, USART_IT_RXNE); // 标准库通常在读取数据后自动清除，但显式清除更保险
	}
	// 可以添加对其他中断标志的处理，例如发送完成、错误等
}

// TIM3中断处理函数 - 用于更新实时时间
void TIM3_IRQHandler(void)
{
	// 检查是否是更新中断
	if (TIM_GetITStatus(TIM3, TIM_IT_Update) != RESET)
	{
		updateTime();		   // 更新时间变量 (秒++)
		updateDisplayFlag = 1; // 设置显示更新标志

		// 清除TIM3更新中断标志位 （非常重要！）
		TIM_ClearITPendingBit(TIM3, TIM_IT_Update);
	}
}
