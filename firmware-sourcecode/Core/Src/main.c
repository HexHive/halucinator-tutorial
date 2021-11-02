/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * <h2><center>&copy; Copyright (c) 2021 STMicroelectronics.
  * All rights reserved.</center></h2>
  *
  * This software component is licensed by ST under BSD 3-Clause license,
  * the "License"; You may not use this file except in compliance with the
  * License. You may obtain a copy of the License at:
  *                        opensource.org/licenses/BSD-3-Clause
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "asinine/dsl.h"
#include "asinine/x509.h"

#include "internal/macros.h"
#include "internal/utils.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
TIM_HandleTypeDef htim1;

UART_HandleTypeDef huart2;

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_USART2_UART_Init(void);
static void MX_TIM1_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* Variables */

char uart_data_buffer[1] = {0};
volatile bool data_waiting = false;

typedef enum device_mode_e {
    DM_DEFAULT = 0,
    DM_COMMAND = 1,
} device_mode;

typedef enum commands_e {
    CMD_INVALID = -1,
    CMD_NONE = 0,
    CMD_VERSION = 1,
    CMD_LOGIN = 2,
    CMD_LOGOUT = 3,
    CMD_UPLOAD_CERT = 4,
    CMD_STATUS = 5,
    CMD_PARSECERT = 6,
} command;

device_mode devmode = DM_DEFAULT;
const char* default_prompt = "cmd> ";
char* prompt = NULL;

command cmd_inflight;

uint8_t cmdlogin_mode = 0;
bool cmdcert_transmit = false;
#define USERNAME_MAX 10
#define PASSWORD_MAX 30
char password[PASSWORD_MAX+1] = {0};
char username[USERNAME_MAX+1] = {0};

char internal_username[] = "admin";
char internal_password[] = "123456";
bool internal_cert_set = false;
char internal_cert[2000] = {0};
off_t internal_cert_len = 0;
bool logged_in = false;

/* Forward Declarations */
void uart_tx_message(const char* message);

static command command_find(const char * command) {
    if (strcmp(command, "version")==0) {
        return CMD_VERSION;
    }
    else if (strcmp(command, "login") == 0) {
        return CMD_LOGIN;
    }
    else if (strcmp(command, "logout") == 0) {
        return CMD_LOGOUT;
    }
    else if (strcmp(command, "cert") == 0) {
        return CMD_UPLOAD_CERT;
    }
    else if (strcmp(command, "parsecert") == 0) {
        return CMD_PARSECERT;
    }
    else if (strcmp(command, "status") == 0) {
        return CMD_STATUS;
    }
    return CMD_INVALID;
}

static const signed char decoding_table[] = {62,-1,-1,-1,63,52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-2,-1,-1,-1,0,1,2,3,4,
5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,-1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,
41,42,43,44,45,46,47,48,49,50,51};

bool base64_decode(char* decoded_data,
                   const char *input_data,
                   size_t input_length,
                   size_t *output_length) {

    if (input_length % 4 != 0) return false;

    *output_length = input_length / 4 * 3;
    if (input_data[input_length - 1] == '=') (*output_length)--;
    if (input_data[input_length - 2] == '=') (*output_length)--;

    for (int i = 0, j = 0; i < input_length;) {

        uint32_t sextet_a = input_data[i] == '=' ? 0 & i++ : decoding_table[input_data[i++]-43];
        uint32_t sextet_b = input_data[i] == '=' ? 0 & i++ : decoding_table[input_data[i++]-43];
        uint32_t sextet_c = input_data[i] == '=' ? 0 & i++ : decoding_table[input_data[i++]-43];
        uint32_t sextet_d = input_data[i] == '=' ? 0 & i++ : decoding_table[input_data[i++]-43];

        uint32_t triple = (sextet_a << 3 * 6)
        + (sextet_b << 2 * 6)
        + (sextet_c << 1 * 6)
        + (sextet_d << 0 * 6);

        if (j < *output_length) decoded_data[j++] = (triple >> 2 * 8) & 0xFF;
        if (j < *output_length) decoded_data[j++] = (triple >> 1 * 8) & 0xFF;
        if (j < *output_length) decoded_data[j++] = (triple >> 0 * 8) & 0xFF;
    }

    return true;
}

static void
dump_name(const x509_name_t *name) {
	char buf[10] = {0};

	for (size_t i = 0; i < name->num; i++) {
		const x509_rdn_t *rdn = &name->rdns[i];

		snprintf(buf, sizeof(buf), "%s:", x509_rdn_type_string(rdn->type));
        uart_tx_message(buf); memset(buf, 0, sizeof buf);

		asinine_err_t err = asn1_string(&rdn->value, buf, sizeof(buf));
        /** DELIBERATE BUG: FORMAT STRING VULN. **/
		if (err.errno != ASININE_OK) {
			sprintf(buf, asinine_strerror(err));
		} else {
			sprintf(buf, buf);
		}
        uart_tx_message(buf); memset(buf, 0, sizeof buf);
        uart_tx_message("\r\n");
	}
}

void dump_certificate(const x509_cert_t *cert) {
	char buf[256];

    uart_tx_message("---\r\n");
	sprintf(buf, "Version: %d, Algo: %d\r\n", cert->version, cert->signature.algorithm);
    uart_tx_message(buf); memset(buf, 0, sizeof buf);

    //assert(asn1_time_to_string(buf, sizeof(buf), &cert->valid_from) < sizeof(buf));
	sprintf(buf, "Valid from: %s", buf);
    uart_tx_message(buf); memset(buf, 0, sizeof buf);

	//assert(asn1_time_to_string(buf, sizeof(buf), &cert->valid_to) < sizeof(buf));
	sprintf(buf, ", to: %s\r\n", buf);
    uart_tx_message(buf); memset(buf, 0, sizeof buf);

	sprintf(buf, "Issuer:");
	dump_name(&cert->issuer);
    uart_tx_message(buf); memset(buf, 0, sizeof buf);

	sprintf(buf, "Subject:");
	dump_name(&cert->subject);
    uart_tx_message(buf); memset(buf, 0, sizeof buf);

	sprintf(buf, "Public key: %d\r\n", cert->pubkey.algorithm);
    uart_tx_message(buf); memset(buf, 0, sizeof buf);
}

void echo_certificates(const uint8_t *contents, size_t length) {
	x509_cert_t cert;

	asn1_parser_t parser;
	asn1_init(&parser, contents, length);

	asinine_err_t res = ERROR(ASININE_OK, NULL);
	while (!asn1_end(&parser)) {
		asinine_err_t err = x509_parse_cert(&parser, &cert);
		if (err.errno != ASININE_OK) {
			fprintf(stderr, "Invalid certificate: %s\n", asinine_strerror(err));
			if (res.errno == ASININE_OK) {
				res = err;
			}

			RETURN_ON_ERROR(asn1_abort(&parser));
		} else {
			dump_certificate(&cert);
		}
	}
}

void command_exec(char recvbuffer[2001], bool* echo_on) {
    switch (cmd_inflight) {
    case CMD_VERSION:
        uart_tx_message("Version is 1.2.3.4\r\n");
        goto reset;
        break;
    case CMD_LOGIN:
        switch (cmdlogin_mode) {
        case 0:
            devmode = DM_COMMAND;
            prompt = "Username: ";
            cmdlogin_mode++;
            break;
        case 1:
            devmode = DM_COMMAND;
            strcpy(username, recvbuffer);
            prompt = "Password: ";
            cmdlogin_mode++;
            *echo_on = false;
            break;
        case 2:
            strcpy(password, recvbuffer);
            cmdlogin_mode = 0;
            uart_tx_message("Login Requested: \r\n");
            uart_tx_message(username);
            uart_tx_message("\r\n");

            if ( strcmp(username, internal_username) == 0 && 
                 strcmp(password, internal_password) == 0 ) 
            {
                logged_in = true;
                uart_tx_message("Logged in!\r\n");
            } else {
                uart_tx_message("Login failed!\r\n");
            }
            goto reset;
            break;
        }
        break;
    case CMD_LOGOUT:
        logged_in = false;
        uart_tx_message("Logged out!\r\n");
        goto reset;
        break;
    case CMD_PARSECERT:
        if (!cmdcert_transmit) {
            devmode = DM_COMMAND;
            cmdcert_transmit = true;
            prompt = "DER (Base64): ";
        } else {
            size_t output_length; 
            char certbuffer[2000] = {0};
            uart_tx_message("Certificate Uploaded\r\n");
            base64_decode(certbuffer, recvbuffer, strlen(recvbuffer), &output_length);
            echo_certificates(certbuffer, output_length);
            cmdcert_transmit = false;
            goto reset;
        }
        break;
    case CMD_UPLOAD_CERT:
        if (logged_in == false) {
            uart_tx_message("Please login first with 'login' to update the certificate.\r\n");
        } else {
            if (!cmdcert_transmit) {
                devmode = DM_COMMAND;
                cmdcert_transmit = true;
                prompt = "DER (Base64): ";
            } else {
                size_t output_length; 
                char certbuffer[2000] = {0};
                uart_tx_message("Certificate Uploaded\r\n");
                base64_decode(certbuffer, recvbuffer, strlen(recvbuffer), &output_length);
                echo_certificates(certbuffer, output_length);
                memset(internal_cert, 0, sizeof internal_cert);
                memcpy(internal_cert, certbuffer, sizeof internal_cert);
                internal_cert_len = output_length;
                internal_cert_set = true;
                cmdcert_transmit = false;
                goto reset;
            }
        }
        break;
    case CMD_STATUS:
        if (logged_in) {
            uart_tx_message("Admin Mode Logged In\r\n");
        } else {
            uart_tx_message("Not Logged In\r\n");
        }
        uart_tx_message("Certificate Loaded:\r\n");
        if (internal_cert_set) {
            uart_tx_message("Certificate Loaded:\r\n");
            echo_certificates(internal_cert, internal_cert_len);
        }
        else {
            uart_tx_message("No certificate loaded.\r\n");
        }
        break;
    case CMD_NONE:
        break;
    case CMD_INVALID:
        uart_tx_message("INVALID!\r\n");
        break;
    }

    return;
reset:
    prompt = (char*)default_prompt;
    *echo_on = true;
    devmode = DM_DEFAULT;
    cmd_inflight = CMD_NONE;
}

/* Helper function that can deal with constant strings 
 * without allocating temporary buffers */
void uart_tx_message(const char* message) {
    size_t messagelen = strlen(message);

    HAL_UART_Transmit(&huart2, (uint8_t*)message, messagelen, 100);    
}

void run()
{

    static char recvbuffer[2001] = {0};
    static uint32_t pchars = 0;
    static bool echo_on = true;

    HAL_StatusTypeDef result = HAL_UART_Receive(&huart2, (uint8_t *)uart_data_buffer, 1, 1000);
    if ( result != HAL_OK ) {
        HAL_Delay(1);
        return;
    }
    
    if (uart_data_buffer[0] == '\r') 
    {
        uart_tx_message("\r\n");
        if (devmode == DM_DEFAULT) {
            cmd_inflight = command_find(recvbuffer);    
        }
        command_exec(recvbuffer, &echo_on); 

        pchars = 0;
        memset(recvbuffer, 0, sizeof recvbuffer);
        uart_tx_message(prompt);
    }
    else if ( pchars >= 2000 ) {
        uart_tx_message("\r\nERROR: Input Too long!\r\n"); 
        uart_tx_message(prompt);
    }
    else 
    {
        recvbuffer[pchars] = uart_data_buffer[0];
        pchars++;
        if ( echo_on == true ) {
            uart_tx_message(uart_data_buffer); 
        } else {
            uart_tx_message("*");
        }
    }
}
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{
  /* USER CODE BEGIN 1 */
  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */
  prompt = (char*) &default_prompt[0];
  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_USART2_UART_Init();
  MX_TIM1_Init();
  /* USER CODE BEGIN 2 */

  char message[100]={0};

  HAL_Delay(250);
  uart_tx_message("\r\n\r\n");
  uart_tx_message("*******************************\r\n");
  uart_tx_message("* Early Init!\r\n");
  sprintf(message, "* UART2 Handle: %p\r\n", &huart2);
  uart_tx_message(message); uart_tx_message("\r\n");
  uart_tx_message("*******************************\r\n\r\n");
  uart_tx_message(prompt);
  //HAL_UART_Receive_IT(&huart2, (uint8_t *)uart_data_buffer, 1);
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
      run();
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
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);
  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = 8;
  RCC_OscInitStruct.PLL.PLLN = 100;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 4;
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

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_3) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief TIM1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM1_Init(void)
{

  /* USER CODE BEGIN TIM1_Init 0 */

  /* USER CODE END TIM1_Init 0 */

  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_SlaveConfigTypeDef sSlaveConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};
  TIM_IC_InitTypeDef sConfigIC = {0};

  /* USER CODE BEGIN TIM1_Init 1 */

  /* USER CODE END TIM1_Init 1 */
  htim1.Instance = TIM1;
  htim1.Init.Prescaler = 0;
  htim1.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim1.Init.Period = 65535;
  htim1.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim1.Init.RepetitionCounter = 0;
  htim1.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim1) != HAL_OK)
  {
    Error_Handler();
  }
  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim1, &sClockSourceConfig) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_IC_Init(&htim1) != HAL_OK)
  {
    Error_Handler();
  }
  sSlaveConfig.SlaveMode = TIM_SLAVEMODE_RESET;
  sSlaveConfig.InputTrigger = TIM_TS_ITR0;
  if (HAL_TIM_SlaveConfigSynchro(&htim1, &sSlaveConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim1, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sConfigIC.ICPolarity = TIM_INPUTCHANNELPOLARITY_RISING;
  sConfigIC.ICSelection = TIM_ICSELECTION_DIRECTTI;
  sConfigIC.ICPrescaler = TIM_ICPSC_DIV1;
  sConfigIC.ICFilter = 0;
  if (HAL_TIM_IC_ConfigChannel(&htim1, &sConfigIC, TIM_CHANNEL_1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM1_Init 2 */

  /* USER CODE END TIM1_Init 2 */

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

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin : B1_Pin */
  GPIO_InitStruct.Pin = B1_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(B1_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : PB8 PB9 */
  GPIO_InitStruct.Pin = GPIO_PIN_8|GPIO_PIN_9;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_OD;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  GPIO_InitStruct.Alternate = GPIO_AF4_I2C1;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

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

#ifdef  USE_FULL_ASSERT
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

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
