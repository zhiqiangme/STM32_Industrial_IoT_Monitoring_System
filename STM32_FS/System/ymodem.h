/**
  ******************************************************************************
  * @file    ymodem.h
  * @brief   Project-local YMODEM transport helpers.
  ******************************************************************************
  */

#ifndef __YMODEM_H_
#define __YMODEM_H_

#include <stdint.h>

typedef enum
{
  COM_OK      = 0x00,
  COM_ERROR   = 0x01,
  COM_ABORT   = 0x02,
  COM_TIMEOUT = 0x03,
  COM_DATA    = 0x04,
  COM_LIMIT   = 0x05
} COM_StatusTypeDef;

#define PACKET_HEADER_SIZE      ((uint32_t)3)
#define PACKET_DATA_INDEX       ((uint32_t)4)
#define PACKET_START_INDEX      ((uint32_t)1)
#define PACKET_NUMBER_INDEX     ((uint32_t)2)
#define PACKET_CNUMBER_INDEX    ((uint32_t)3)
#define PACKET_TRAILER_SIZE     ((uint32_t)2)
#define PACKET_OVERHEAD_SIZE    (PACKET_HEADER_SIZE + PACKET_TRAILER_SIZE - 1)
#define PACKET_SIZE             ((uint32_t)128)
#define PACKET_1K_SIZE          ((uint32_t)1024)

/* Buffer layout keeps byte 0 unused for alignment compatibility with ST IAP. */
#define YMODEM_PACKET_BUFFER_SIZE (PACKET_1K_SIZE + PACKET_DATA_INDEX + PACKET_TRAILER_SIZE)

#define FILE_NAME_LENGTH        ((uint32_t)64)
#define FILE_INFO_LENGTH        ((uint32_t)160)
#define YMODEM_IMAGE_SHA256_SIZE ((uint32_t)32)

#define SOH                     ((uint8_t)0x01)
#define STX                     ((uint8_t)0x02)
#define EOT                     ((uint8_t)0x04)
#define ACK                     ((uint8_t)0x06)
#define NAK                     ((uint8_t)0x15)
#define CA                      ((uint8_t)0x18)
#define CRC16                   ((uint8_t)0x43)
#define NEGATIVE_BYTE           ((uint8_t)0xFF)

#define ABORT1                  ((uint8_t)0x41)
#define ABORT2                  ((uint8_t)0x61)

#define DOWNLOAD_TIMEOUT        ((uint32_t)1000)
#define MAX_ERRORS              ((uint32_t)5)

typedef uint8_t (*YmodemReadByteCallback)(uint8_t *byte, uint32_t timeout_ms, void *context);
typedef void (*YmodemWriteBytesCallback)(const uint8_t *data, uint16_t len, void *context);
typedef COM_StatusTypeDef (*YmodemStartCallback)(const char *file_name,
                                                 uint32_t file_size,
                                                 uint32_t image_crc32,
                                                 uint32_t target_fw_version,
                                                 const uint8_t *image_sha256,
                                                 void *context);
typedef COM_StatusTypeDef (*YmodemDataCallback)(uint32_t offset,
                                                const uint8_t *data,
                                                uint32_t len,
                                                void *context);

typedef struct
{
  YmodemReadByteCallback read_byte;
  YmodemWriteBytesCallback write_bytes;
  void *io_context;
  YmodemStartCallback on_start;
  YmodemDataCallback on_data;
  void *user_context;
} YmodemReceiveConfig;

typedef struct
{
  char file_name[FILE_NAME_LENGTH + 1u];
  uint32_t file_size;
  uint32_t image_crc32;
  uint32_t target_fw_version;
  uint8_t image_sha256[YMODEM_IMAGE_SHA256_SIZE];
  uint32_t bytes_received;
} YmodemReceiveResult;

COM_StatusTypeDef Ymodem_Receive(const YmodemReceiveConfig *config, YmodemReceiveResult *result);
uint16_t Ymodem_CalculateCrc16(const uint8_t *data, uint32_t size);

#endif
