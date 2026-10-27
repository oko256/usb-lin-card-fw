/* USER CODE BEGIN Header */
/**
 ******************************************************************************
 * @file           : usbd_cdc_if.c
 * @version        : v2.0_Cube
 * @brief          : Usb device for Virtual Com Port.
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
#include "usbd_cdc_if.h"

/* USER CODE BEGIN INCLUDE */
#include "fast_ring_buffer.h"
#include "slcan.h"
/* USER CODE END INCLUDE */

/* Private typedef -----------------------------------------------------------*/
/* Private define ------------------------------------------------------------*/
/* Private macro -------------------------------------------------------------*/

/* USER CODE BEGIN PV */
/* Private variables ---------------------------------------------------------*/

/* USER CODE END PV */

/** @addtogroup STM32_USB_OTG_DEVICE_LIBRARY
 * @brief Usb device library.
 * @{
 */

/** @addtogroup USBD_CDC_IF
 * @{
 */

/** @defgroup USBD_CDC_IF_Private_TypesDefinitions USBD_CDC_IF_Private_TypesDefinitions
 * @brief Private types.
 * @{
 */

/* USER CODE BEGIN PRIVATE_TYPES */
/* USER CODE END PRIVATE_TYPES */

/**
 * @}
 */

/** @defgroup USBD_CDC_IF_Private_Defines USBD_CDC_IF_Private_Defines
 * @brief Private defines.
 * @{
 */

/* USER CODE BEGIN PRIVATE_DEFINES */
/* USER CODE END PRIVATE_DEFINES */

/**
 * @}
 */

/** @defgroup USBD_CDC_IF_Private_Macros USBD_CDC_IF_Private_Macros
 * @brief Private macros.
 * @{
 */

/* USER CODE BEGIN PRIVATE_MACRO */

/* USER CODE END PRIVATE_MACRO */

/**
 * @}
 */

/** @defgroup USBD_CDC_IF_Private_Variables USBD_CDC_IF_Private_Variables
 * @brief Private variables.
 * @{
 */
/* Create buffer for reception and transmission           */
/* It's up to user to redefine and/or remove those define */
/** Received data over USB are stored in this buffer      */
uint8_t UserRxBufferFS[APP_RX_DATA_SIZE];

/** Data to send over USB CDC are stored in this buffer   */
uint8_t UserTxBufferFS[APP_TX_DATA_SIZE];

/* USER CODE BEGIN PRIVATE_VARIABLES */
static uint8_t          cdc_tx_ring_storage[CDC_USB_TX_RING_SIZE];
static fast_ring_buffer cdc_tx_ring;
/// Received packet that didn't fit into slcan input buffer. USB reception is not re-armed while a
/// packet is held, so the host gets NAKs (flow control) instead of data being lost.
static volatile bool    cdc_rx_held = false;
static uint8_t*         cdc_rx_held_buf;
static uint32_t         cdc_rx_held_len;
/* USER CODE END PRIVATE_VARIABLES */

/**
 * @}
 */

/** @defgroup USBD_CDC_IF_Exported_Variables USBD_CDC_IF_Exported_Variables
 * @brief Public variables.
 * @{
 */

extern USBD_HandleTypeDef hUsbDeviceFS;

/* USER CODE BEGIN EXPORTED_VARIABLES */

/* USER CODE END EXPORTED_VARIABLES */

/**
 * @}
 */

/** @defgroup USBD_CDC_IF_Private_FunctionPrototypes USBD_CDC_IF_Private_FunctionPrototypes
 * @brief Private functions declaration.
 * @{
 */

static int8_t CDC_Init_FS(void);
static int8_t CDC_DeInit_FS(void);
static int8_t CDC_Control_FS(uint8_t cmd, uint8_t* pbuf, uint16_t length);
static int8_t CDC_Receive_FS(uint8_t* pbuf, uint32_t* Len);

/* USER CODE BEGIN PRIVATE_FUNCTIONS_DECLARATION */

/* USER CODE END PRIVATE_FUNCTIONS_DECLARATION */

/**
 * @}
 */

USBD_CDC_ItfTypeDef USBD_Interface_fops_FS = {CDC_Init_FS, CDC_DeInit_FS, CDC_Control_FS, CDC_Receive_FS};

/* Private functions ---------------------------------------------------------*/
/**
 * @brief  Initializes the CDC media low layer over the FS USB IP
 * @retval USBD_OK if all operations are OK else USBD_FAIL
 */
static int8_t CDC_Init_FS(void)
{
    /* USER CODE BEGIN 3 */
    fast_ring_buffer_init(&cdc_tx_ring, cdc_tx_ring_storage, CDC_USB_TX_RING_SIZE);
    cdc_rx_held = false;
    /* Set Application Buffers */
    USBD_CDC_SetTxBuffer(&hUsbDeviceFS, UserTxBufferFS, 0);
    USBD_CDC_SetRxBuffer(&hUsbDeviceFS, UserRxBufferFS);
    return (USBD_OK);
    /* USER CODE END 3 */
}

/**
 * @brief  DeInitializes the CDC media low layer
 * @retval USBD_OK if all operations are OK else USBD_FAIL
 */
static int8_t CDC_DeInit_FS(void)
{
    /* USER CODE BEGIN 4 */
    return (USBD_OK);
    /* USER CODE END 4 */
}

/**
 * @brief  Manage the CDC class requests
 * @param  cmd: Command code
 * @param  pbuf: Buffer containing command data (request parameters)
 * @param  length: Number of data to be sent (in bytes)
 * @retval Result of the operation: USBD_OK if all operations are OK else USBD_FAIL
 */
static int8_t CDC_Control_FS(uint8_t cmd, uint8_t* pbuf, uint16_t length)
{
    /* USER CODE BEGIN 5 */
    (void)pbuf; // UNUSED
    (void)length; // UNUSED
    switch (cmd) {
    case CDC_SEND_ENCAPSULATED_COMMAND:

        break;

    case CDC_GET_ENCAPSULATED_RESPONSE:

        break;

    case CDC_SET_COMM_FEATURE:

        break;

    case CDC_GET_COMM_FEATURE:

        break;

    case CDC_CLEAR_COMM_FEATURE:

        break;

        /*******************************************************************************/
        /* Line Coding Structure */
        /*-----------------------------------------------------------------------------*/
        /* Offset | Field       | Size | Value  | Description */
        /* 0      | dwDTERate   |   4  | Number |Data terminal rate, in bits per
         * second*/
        /* 4      | bCharFormat |   1  | Number | Stop bits */
        /*                                        0 - 1 Stop bit */
        /*                                        1 - 1.5 Stop bits */
        /*                                        2 - 2 Stop bits */
        /* 5      | bParityType |  1   | Number | Parity */
        /*                                        0 - None */
        /*                                        1 - Odd */
        /*                                        2 - Even */
        /*                                        3 - Mark */
        /*                                        4 - Space */
        /* 6      | bDataBits  |   1   | Number Data bits (5, 6, 7, 8 or 16). */
        /*******************************************************************************/
    case CDC_SET_LINE_CODING:

        break;

    case CDC_GET_LINE_CODING:

        break;

    case CDC_SET_CONTROL_LINE_STATE:

        break;

    case CDC_SEND_BREAK:

        break;

    default:
        break;
    }

    return (USBD_OK);
    /* USER CODE END 5 */
}

/**
 * @brief  Data received over USB OUT endpoint are sent over CDC interface
 *         through this function.
 *
 *         @note
 *         This function will issue a NAK packet on any OUT packet received on
 *         USB endpoint until exiting this function. If you exit this function
 *         before transfer is complete on CDC interface (ie. using DMA controller)
 *         it will result in receiving more data while previous ones are still
 *         not sent.
 *
 * @param  Buf: Buffer of data to be received
 * @param  Len: Number of data received (in bytes)
 * @retval Result of the operation: USBD_OK if all operations are OK else USBD_FAIL
 */
static int8_t CDC_Receive_FS(uint8_t* Buf, uint32_t* Len)
{
    /* USER CODE BEGIN 6 */
    if (slcan_append_input(Buf, *Len)) {
        USBD_CDC_SetRxBuffer(&hUsbDeviceFS, &Buf[0]);
        USBD_CDC_ReceivePacket(&hUsbDeviceFS);
    } else {
        // Hold the packet until CDC_Service_Rx() manages to pass it on.
        cdc_rx_held_buf = Buf;
        cdc_rx_held_len = *Len;
        cdc_rx_held     = true;
    }
    return (USBD_OK);
    /* USER CODE END 6 */
}

/**
 * @brief  CDC_Transmit_FS
 *         Data to send over USB IN endpoint are sent over CDC interface
 *         through this function.
 *         @note
 *
 *
 * @param  Buf: Buffer of data to be sent
 * @param  Len: Number of data to be sent (in bytes)
 * @retval USBD_OK if all operations are OK else USBD_FAIL or USBD_BUSY
 */
uint8_t CDC_Transmit_FS(uint8_t* Buf, uint16_t Len)
{
    uint8_t                 result = USBD_OK;
    /* USER CODE BEGIN 7 */
    USBD_CDC_HandleTypeDef* hcdc   = (USBD_CDC_HandleTypeDef*)hUsbDeviceFS.pClassData;
    if (hcdc->TxState != 0) {
        return USBD_BUSY;
    }
    USBD_CDC_SetTxBuffer(&hUsbDeviceFS, Buf, Len);
    result = USBD_CDC_TransmitPacket(&hUsbDeviceFS);
    /* USER CODE END 7 */
    return result;
}

/* USER CODE BEGIN PRIVATE_FUNCTIONS_IMPLEMENTATION */
void CDC_Flush_Tx_Ring(void)
{
    USBD_CDC_HandleTypeDef* hcdc = (USBD_CDC_HandleTypeDef*)hUsbDeviceFS.pClassData;

    // Do not touch HW if USB peripheral isn't ready or is already busy.
    if (hcdc == NULL || hcdc->TxState != 0) {
        return;
    }

    uint32_t bytes_to_send = fast_ring_buffer_pop(&cdc_tx_ring, UserTxBufferFS, APP_TX_DATA_SIZE);
    if (bytes_to_send == 0) {
        // Nothing to send; don't send empty packets.
        return;
    }

    // Hand the transmit buffer to ST USB stack.
    USBD_CDC_SetTxBuffer(&hUsbDeviceFS, UserTxBufferFS, (uint16_t)bytes_to_send);
    USBD_CDC_TransmitPacket(&hUsbDeviceFS);
}

bool CDC_Transmit_NonBlocking(const uint8_t* buf, uint32_t len)
{
    if (!fast_ring_buffer_push(&cdc_tx_ring, buf, len)) {
        // Ring buffer couldn't fit the data.
        return false;
    }
    // Trigger the transmission if HW is idle.
    CDC_Flush_Tx_Ring();
    return true;
}

void CDC_Service_Rx(void)
{
    if (!cdc_rx_held || !slcan_append_input(cdc_rx_held_buf, cdc_rx_held_len)) {
        return;
    }
    // Re-arm reception with USB interrupt disabled, as this is not called from the USB interrupt.
    HAL_NVIC_DisableIRQ(USB_IRQn);
    cdc_rx_held = false;
    USBD_CDC_SetRxBuffer(&hUsbDeviceFS, cdc_rx_held_buf);
    USBD_CDC_ReceivePacket(&hUsbDeviceFS);
    HAL_NVIC_EnableIRQ(USB_IRQn);
}
/* USER CODE END PRIVATE_FUNCTIONS_IMPLEMENTATION */

/**
 * @}
 */

/**
 * @}
 */
