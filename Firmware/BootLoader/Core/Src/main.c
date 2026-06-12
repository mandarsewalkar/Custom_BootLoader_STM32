/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
FLASH_EraseInitTypeDef EraseInitStruct;
typedef struct {
	uint32_t active_slot;
	uint32_t image_size;
	uint32_t image_crc;
} boot_cfg_t;

typedef enum {
		PROG_MODE,
		EXE_MODE
} curr_mode_t;
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define Bootloader_cfg 0x08004000
#define APP_A_ADDR 0x08020000
#define APP_B_ADDR 0x08040000

#define PACKET_SIZE 4096
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
CRC_HandleTypeDef hcrc;

UART_HandleTypeDef huart2;

/* USER CODE BEGIN PV */
uint32_t app_add;
curr_mode_t curr_mode = EXE_MODE;
uint8_t eoc_packet_flag;

uint32_t SectorError;

uint8_t rx_buffer[PACKET_SIZE + 13];
uint8_t ch;
uint16_t index = 0;

uint32_t data[4] =
{
    0x12346678,
    0xAAAAAAAA,
    0x55555555,
    0x87654321
};

uint8_t flag = 0;
uint16_t expected_size = 0;
uint8_t parsing_done = 0;
uint8_t already_erased;

uint8_t mem_unlocked = 0;
uint32_t watchdog_tick = 0;

uint32_t address;
uint16_t length;
uint32_t payload[PACKET_SIZE / 4];
uint32_t crc;

uint32_t image_crc;
uint32_t image_size;

char ack[19] = "PACKET RECEIVED\r\n";
char crc_err[16] = "CRC MISMATCH\r\n";
char erasing[20] = "ERASING MEMORY\r\n";
char prog_done[20] = "PROGRAMMING DONE\r\n";

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_USART2_UART_Init(void);
static void MX_CRC_Init(void);
/* USER CODE BEGIN PFP */

void eraseMem(uint32_t address){
	int sector;

	if(address >= 0x08060000)
		sector = 7;
	else if(address >= 0x08040000)
		sector = 6;
	else if(address >= 0x08020000)
		sector = 5;
	else if(address >= 0x08010000)
		sector = 4;
	else if(address >= 0x0800C000)
		sector = 3;
	else if(address >= 0x08008000)
		sector = 2;
	else if(address >= 0x08004000)
		sector = 1;
	else if(address >= 0x08000000)
		sector = 0;


//	HAL_FLASH_Unlock();

	uint32_t temp;

	switch(sector)
	{
			case 0:
				temp = FLASH_SECTOR_0;
				break;
			case 1:
				temp = FLASH_SECTOR_1;
				break;
			case 2:
				temp = FLASH_SECTOR_2;
				break;
			case 3:
				temp = FLASH_SECTOR_3;
				break;
			case 4:
				temp = FLASH_SECTOR_4;
				break;
			case 5:
				temp = FLASH_SECTOR_5;
				break;
			case 6:
				temp = FLASH_SECTOR_6;
				break;
			case 7:
				temp = FLASH_SECTOR_7;
				break;

	}

	EraseInitStruct.TypeErase = FLASH_TYPEERASE_SECTORS;
	EraseInitStruct.VoltageRange = VOLTAGE_RANGE_3;
	EraseInitStruct.Sector = temp;
    EraseInitStruct.NbSectors = 1;

	HAL_FLASHEx_Erase(&EraseInitStruct, &SectorError);

//	HAL_FLASH_Lock();
}

void progMem(uint32_t address, uint32_t instruction){


//	  HAL_FLASH_Unlock();

	  HAL_FLASH_Program((uint32_t)FLASH_TYPEPROGRAM_WORD, address, instruction);

//	  HAL_FLASH_Lock();
}

void unlock(void){
	HAL_FLASH_Unlock();
	mem_unlocked = 1;
}

void lock(void){
	HAL_FLASH_Lock();
	mem_unlocked = 0;
}

void prog(void){
	uint32_t temp = 0;

	temp = HAL_CRC_Calculate(&hcrc, payload, length / 4);

	if((crc == temp) && parsing_done){

		if(!mem_unlocked){
	        unlock();
		}

	HAL_UART_Transmit(&huart2, ack, sizeof(ack), HAL_MAX_DELAY);

    if(!already_erased){
	    eraseMem(address);
	    already_erased = 1;
    }

    HAL_UART_Transmit(&huart2, erasing, sizeof(erasing), HAL_MAX_DELAY);

    for(uint16_t i = 0; i < length; i += 4)
    {
    	uint32_t word =
    	      ((uint32_t)rx_buffer[i + 8])
    	    | ((uint32_t)rx_buffer[i + 9]  << 8)
    	    | ((uint32_t)rx_buffer[i + 10] << 16)
    	    | ((uint32_t)rx_buffer[i + 11] << 24);

        progMem(address + i, word);
    }


    HAL_UART_Transmit(&huart2, prog_done, sizeof(prog_done), HAL_MAX_DELAY);

    parsing_done = 0;
	} else {
	    HAL_UART_Transmit(&huart2, crc_err, sizeof(crc_err), HAL_MAX_DELAY);
	}
}

void prog_eoc(void){

//	HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, 1);
//	HAL_Delay(100);
//	HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, 0);
//	HAL_Delay(100);

	uint32_t temp = 0;

	uint32_t change_add;

	uint32_t eoc_pacet_crc_temp[2];

	eoc_pacet_crc_temp[0] = image_size;
	eoc_pacet_crc_temp[1] = image_crc;

	temp = HAL_CRC_Calculate(&hcrc, eoc_pacet_crc_temp, 2);



	if(temp == crc){

		HAL_UART_Transmit(&huart2, ack, sizeof(ack), HAL_MAX_DELAY);

	    if(address > 0x08040000){
		    change_add = 01;
	    }else{
		    change_add = 00;
	    }
	    eraseMem(Bootloader_cfg);
	    progMem(Bootloader_cfg, change_add);
	    progMem((Bootloader_cfg + 4), image_size);
	    progMem((Bootloader_cfg + 8), image_crc);

	    lock();
	    mem_unlocked = 0;

	    HAL_Delay(200);

	NVIC_SystemReset();

	}else{
		HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, 1);
		HAL_Delay(100);
		HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, 0);
		HAL_Delay(100);
		HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, 1);
		HAL_Delay(100);
		HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, 0);
		HAL_Delay(100);
	}

}


void parse_uart(uint8_t *rx, uint16_t size)
{
	if(eoc_packet_flag){

		image_size =
		          ((uint32_t)rx[2])
		        | ((uint32_t)rx[3] << 8)
		        | ((uint32_t)rx[4] << 16)
		        | ((uint32_t)rx[5] << 24);

		image_crc =
	          ((uint32_t)rx[6])
	        | ((uint32_t)rx[7] << 8)
	        | ((uint32_t)rx[8] << 16)
	        | ((uint32_t)rx[9] << 24);

		crc =
		          ((uint32_t)rx[10])
		        | ((uint32_t)rx[11] << 8)
		        | ((uint32_t)rx[12] << 16)
		        | ((uint32_t)rx[13] << 24);

		prog_eoc();

		eoc_packet_flag = 0;

	}else{

    if(size < 12)
        return;

    address =
          ((uint32_t)rx[2])
        | ((uint32_t)rx[3] << 8)
        | ((uint32_t)rx[4] << 16)
        | ((uint32_t)rx[5] << 24);

    crc =
          ((uint32_t)rx[8 + length])
        | ((uint32_t)rx[9 + length] << 8)
        | ((uint32_t)rx[10 + length] << 16)
        | ((uint32_t)rx[11 + length] << 24);

    // bootloader protection
    if(address < APP_A_ADDR)
        return;


    length =
          ((uint16_t)rx[6])
        | ((uint16_t)rx[7] << 8);

    if(size != (2 + 4 + 2 + length + 4))
        return;

    if(length > PACKET_SIZE)
        return;

    if(length % 4 != 0)
        return;

    for(uint16_t i = 0; i < length; i += 4)
    {
    	uint32_t word =
    	      ((uint32_t)rx[i + 8])
    	    | ((uint32_t)rx[i + 9]  << 8)
    	    | ((uint32_t)rx[i + 10] << 16)
    	    | ((uint32_t)rx[i + 11] << 24);

        payload[i / 4] = word;
    }

    crc =
          ((uint32_t)rx[8 + length])
        | ((uint32_t)rx[9 + length] << 8)
        | ((uint32_t)rx[10 + length] << 16)
        | ((uint32_t)rx[11 + length] << 24);

    parsing_done = 1;

	watchdog_tick = HAL_GetTick();

    prog();

	}
}


void checker(uint8_t ch)
{

    switch(flag)
    {
        case 0: // WAIT_AA

            if(ch == 0xAA)
            {
                rx_buffer[0] = ch;
                index = 1;
                flag = 1;
            }

            break;

        case 1: // WAIT_55

            if(ch == 0x55)
            {
                rx_buffer[index++] = ch;
                flag = 2;
            }
            else
            {
                flag = 0;
                index = 0;

            }

            break;

        case 2: // separating type of packets
        	if(ch == 0x11)
        	{
        		eoc_packet_flag = 1;

//        		HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, 1);
//        		HAL_Delay(100);
//        		HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, 0);
//        		HAL_Delay(100);
        	}
            flag = 3;
            break;

        case 3: // COLLECT PACKET

            rx_buffer[index++] = ch;

            if(index >= 8)
            {
                length =
                      ((uint16_t)rx_buffer[6])
                    | ((uint16_t)rx_buffer[7] << 8);

                expected_size =
                      2
                    + 4
                    + 2
                    + length
                    + 4;
            }

            if(eoc_packet_flag){
            	expected_size =
                        2
                      + 4
					  + 4
                      + 4;
            }

            if(expected_size > 0 &&
               index >= expected_size)
            {
                parse_uart(rx_buffer, expected_size);

                flag = 0;
                index = 0;
                expected_size = 0;

            }

            break;
    }
}

void boot_loader_startup(void)
{
    if(curr_mode != EXE_MODE)
        return;

    boot_cfg_t cfg;
    boot_cfg_t *cfg_flash =
        (boot_cfg_t *)Bootloader_cfg;

    if(cfg_flash->active_slot == 0x00000000 ||
       cfg_flash->active_slot == 0x00000001)
    {
        cfg.active_slot = cfg_flash->active_slot;
        cfg.image_size  = cfg_flash->image_size;
        cfg.image_crc   = cfg_flash->image_crc;
    }
    else
    {
        cfg.active_slot = 0;
        cfg.image_size  = 0;
        cfg.image_crc   = 0;
    }

    if(cfg.active_slot == 0)
    {
        app_add = APP_A_ADDR;
    }
    else
    {
        app_add = APP_B_ADDR;
    }

    /*
     * Verify application CRC before jump
     */

    uint32_t words =
        (cfg.image_size + 3) / 4;

    uint32_t calc_crc =
        HAL_CRC_Calculate(
            &hcrc,
            (uint32_t *)app_add,
            words
        );

    if(calc_crc != cfg.image_crc)
    {
        /*
         * Invalid firmware.
         * Stay in bootloader.
         */
        curr_mode = PROG_MODE;

        HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, 1);

        return;
    }
}
typedef void (*pfunction)(void);

void jump_to_app(void){
	uint32_t appstack;
	uint32_t app_reset_handler;

	pfunction jump;

	appstack = *(volatile uint32_t*)app_add;

	if(appstack < 0x20000000 || appstack > 0x20020000)
	{
	    while(1);   // invalid app
	}

	app_reset_handler = *(volatile uint32_t*)(app_add + 4);

	if(app_reset_handler == 0xFFFFFFFF)
	{
	    while(1);
	}


	__disable_irq();

//	HAL_RCC_DeInit();
//	HAL_DeInit();

	SCB->VTOR = app_add;

	__set_MSP(appstack);

	jump = (pfunction)app_reset_handler;

	jump();
}


/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  uint32_t button_press_time = 0;
  already_erased = 0;
  uint32_t start;
  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_USART2_UART_Init();
  MX_CRC_Init();
  /* USER CODE BEGIN 2 */

  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, 1);

  start = HAL_GetTick(); // so that start is once defined on reset

  while(HAL_GetTick() - start < 3000){
      if(HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_13) == GPIO_PIN_RESET)
      {
          if(button_press_time == 0)
          {
              button_press_time = HAL_GetTick();
          }

          if((HAL_GetTick() - button_press_time) >= 1000)
          {
              curr_mode = PROG_MODE;
              break;
          }
          }
      else
      {
      button_press_time = 0;
      }
  }


  // PROG / EXE logic

  if(curr_mode == PROG_MODE){

	  // LED indicator
	  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, 0);
	  HAL_Delay(200);
	  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, 1);
	  HAL_Delay(200);
	  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, 0);
	  HAL_Delay(200);
	  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, 1);
	  HAL_Delay(200);
      HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, 0);



      eoc_packet_flag = 0;

	  while(1){
          HAL_UART_Receive(&huart2, &ch, 1, 10);
          checker(ch);

          if(mem_unlocked && HAL_GetTick() - watchdog_tick >= 2500){
        	  lock();
          }

	  }
  }else if (curr_mode == EXE_MODE){
	      boot_loader_startup();

	      jump_to_app();
  }

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while(1)
  {





//      rx_tx();

//      parse_uart(rx_buffer, 512);

  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE3);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = 16;
  RCC_OscInitStruct.PLL.PLLN = 336;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV4;
  RCC_OscInitStruct.PLL.PLLQ = 2;
  RCC_OscInitStruct.PLL.PLLR = 2;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief CRC Initialization Function
  * @param None
  * @retval None
  */
static void MX_CRC_Init(void)
{

  /* USER CODE BEGIN CRC_Init 0 */

  /* USER CODE END CRC_Init 0 */

  /* USER CODE BEGIN CRC_Init 1 */

  /* USER CODE END CRC_Init 1 */
  hcrc.Instance = CRC;
  if (HAL_CRC_Init(&hcrc) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN CRC_Init 2 */

  /* USER CODE END CRC_Init 2 */

}

/**
  * @brief USART2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART2_UART_Init(void)
{

  /* USER CODE BEGIN USART2_Init 0 */

  /* USER CODE END USART2_Init 0 */

  /* USER CODE BEGIN USART2_Init 1 */

  /* USER CODE END USART2_Init 1 */
  huart2.Instance = USART2;
  huart2.Init.BaudRate = 115200;
  huart2.Init.WordLength = UART_WORDLENGTH_8B;
  huart2.Init.StopBits = UART_STOPBITS_1;
  huart2.Init.Parity = UART_PARITY_NONE;
  huart2.Init.Mode = UART_MODE_TX_RX;
  huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart2.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART2_Init 2 */

  /* USER CODE END USART2_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin : B1_Pin */
  GPIO_InitStruct.Pin = B1_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(B1_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : LD2_Pin */
  GPIO_InitStruct.Pin = LD2_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LD2_GPIO_Port, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
