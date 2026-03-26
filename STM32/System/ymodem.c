/**
  ******************************************************************************
  * @file    ymodem.c
  * @brief   Project-local YMODEM receiver adapted from the STM32 IAP example.
  ******************************************************************************
  */

#include "ymodem.h"

#include <stdlib.h>
#include <string.h>

typedef enum
{
  YMODEM_PACKET_OK = 0,
  YMODEM_PACKET_ERROR,
  YMODEM_PACKET_TIMEOUT,
  YMODEM_PACKET_ABORT
} YmodemPacketStatus;

/* Keep the packet buffer 32-bit aligned like the ST example. */
static uint8_t g_packet_data[YMODEM_PACKET_BUFFER_SIZE];

static void Ymodem_SendByte(const YmodemReceiveConfig *config, uint8_t value)
{
  if ((config != NULL) && (config->write_bytes != NULL))
  {
    config->write_bytes(&value, 1u, config->io_context);
  }
}

static void Ymodem_SendAbort(const YmodemReceiveConfig *config)
{
  Ymodem_SendByte(config, CA);
  Ymodem_SendByte(config, CA);
}

static uint8_t Ymodem_ReadBytes(const YmodemReceiveConfig *config,
                                uint8_t *buffer,
                                uint32_t len,
                                uint32_t timeout_ms)
{
  uint32_t i;

  if ((config == NULL) || (config->read_byte == NULL) || (buffer == NULL))
  {
    return 0u;
  }

  for (i = 0u; i < len; i++)
  {
    if (config->read_byte(&buffer[i], timeout_ms, config->io_context) == 0u)
    {
      return 0u;
    }
  }

  return 1u;
}

static uint8_t Ymodem_SkipSpaces(const char **text)
{
  if ((text == NULL) || (*text == NULL))
  {
    return 0u;
  }

  while ((**text == ' ') || (**text == '\t'))
  {
    (*text)++;
  }

  return (**text != '\0') ? 1u : 0u;
}

static uint8_t Ymodem_ParseU32(const char *text, uint32_t base, uint32_t *value, const char **endptr)
{
  char *end = NULL;
  unsigned long parsed;

  if ((text == NULL) || (value == NULL))
  {
    return 0u;
  }

  parsed = strtoul(text, &end, (int)base);
  if (end == text)
  {
    return 0u;
  }

  *value = (uint32_t)parsed;
  if (endptr != NULL)
  {
    *endptr = end;
  }

  return 1u;
}

static uint8_t Ymodem_ParseHeader(YmodemReceiveResult *result, const uint8_t *packet)
{
  uint32_t file_name_len = 0u;
  uint32_t file_info_len = 0u;
  const char *cursor;
  uint32_t value;

  if ((result == NULL) || (packet == NULL))
  {
    return 0u;
  }

  memset(result, 0, sizeof(*result));

  while ((file_name_len < FILE_NAME_LENGTH) &&
         (packet[PACKET_DATA_INDEX + file_name_len] != '\0'))
  {
    result->file_name[file_name_len] = (char)packet[PACKET_DATA_INDEX + file_name_len];
    file_name_len++;
  }
  result->file_name[file_name_len] = '\0';

  if (result->file_name[0] == '\0')
  {
    return 1u;
  }

  while ((file_info_len < FILE_INFO_LENGTH) &&
         (packet[PACKET_DATA_INDEX + file_name_len + 1u + file_info_len] != '\0'))
  {
    g_packet_data[file_info_len] = packet[PACKET_DATA_INDEX + file_name_len + 1u + file_info_len];
    file_info_len++;
  }
  g_packet_data[file_info_len] = '\0';

  cursor = (const char *)g_packet_data;
  if (Ymodem_SkipSpaces(&cursor) == 0u)
  {
    return 0u;
  }
  if (Ymodem_ParseU32(cursor, 10u, &value, &cursor) == 0u)
  {
    return 0u;
  }
  result->file_size = value;

  if (Ymodem_SkipSpaces(&cursor) != 0u)
  {
    if (Ymodem_ParseU32(cursor, 0u, &value, &cursor) == 0u)
    {
      return 0u;
    }
    result->image_crc32 = value;
  }

  if (Ymodem_SkipSpaces(&cursor) != 0u)
  {
    if (Ymodem_ParseU32(cursor, 0u, &value, &cursor) == 0u)
    {
      return 0u;
    }
    result->target_fw_version = value;
  }

  return 1u;
}

static YmodemPacketStatus Ymodem_ReceivePacket(const YmodemReceiveConfig *config,
                                               uint8_t *packet,
                                               uint32_t *packet_length,
                                               uint32_t timeout_ms)
{
  uint8_t first_byte;
  uint32_t data_size = 0u;
  uint16_t received_crc;

  if ((config == NULL) || (packet == NULL) || (packet_length == NULL))
  {
    return YMODEM_PACKET_ERROR;
  }

  *packet_length = 0u;

  if (config->read_byte(&first_byte, timeout_ms, config->io_context) == 0u)
  {
    return YMODEM_PACKET_TIMEOUT;
  }

  switch (first_byte)
  {
    case SOH:
      data_size = PACKET_SIZE;
      break;

    case STX:
      data_size = PACKET_1K_SIZE;
      break;

    case EOT:
      packet[PACKET_START_INDEX] = EOT;
      return YMODEM_PACKET_OK;

    case CA:
      if ((config->read_byte(&packet[PACKET_START_INDEX], timeout_ms, config->io_context) != 0u) &&
          (packet[PACKET_START_INDEX] == CA))
      {
        *packet_length = 2u;
        return YMODEM_PACKET_ABORT;
      }
      return YMODEM_PACKET_ERROR;

    case ABORT1:
    case ABORT2:
      return YMODEM_PACKET_ABORT;

    default:
      return YMODEM_PACKET_ERROR;
  }

  packet[PACKET_START_INDEX] = first_byte;
  if (Ymodem_ReadBytes(config, &packet[PACKET_NUMBER_INDEX],
                       data_size + PACKET_OVERHEAD_SIZE, timeout_ms) == 0u)
  {
    return YMODEM_PACKET_TIMEOUT;
  }

  if (packet[PACKET_NUMBER_INDEX] != (uint8_t)(packet[PACKET_CNUMBER_INDEX] ^ NEGATIVE_BYTE))
  {
    return YMODEM_PACKET_ERROR;
  }

  received_crc = (uint16_t)(((uint16_t)packet[data_size + PACKET_DATA_INDEX] << 8) |
                            packet[data_size + PACKET_DATA_INDEX + 1u]);
  if (Ymodem_CalculateCrc16(&packet[PACKET_DATA_INDEX], data_size) != received_crc)
  {
    return YMODEM_PACKET_ERROR;
  }

  *packet_length = data_size;
  return YMODEM_PACKET_OK;
}

uint16_t Ymodem_CalculateCrc16(const uint8_t *data, uint32_t size)
{
  uint32_t crc = 0u;
  uint32_t i;
  uint32_t j;

  if (data == NULL)
  {
    return 0u;
  }

  for (i = 0u; i < size; i++)
  {
    crc ^= (uint32_t)data[i] << 8;
    for (j = 0u; j < 8u; j++)
    {
      if ((crc & 0x8000u) != 0u)
      {
        crc = (crc << 1) ^ 0x1021u;
      }
      else
      {
        crc <<= 1;
      }
    }
  }

  return (uint16_t)(crc & 0xFFFFu);
}

COM_StatusTypeDef Ymodem_Receive(const YmodemReceiveConfig *config, YmodemReceiveResult *result)
{
  uint8_t expected_packet = 0u;
  uint32_t packet_length;
  uint32_t session_done = 0u;
  uint32_t file_done = 0u;
  uint32_t errors = 0u;
  uint8_t session_begin = 0u;
  COM_StatusTypeDef status = COM_OK;

  if ((config == NULL) || (result == NULL) ||
      (config->read_byte == NULL) || (config->write_bytes == NULL) ||
      (config->on_start == NULL) || (config->on_data == NULL))
  {
    return COM_ERROR;
  }

  memset(result, 0, sizeof(*result));

  while ((session_done == 0u) && (status == COM_OK))
  {
    expected_packet = 0u;
    file_done = 0u;

    while ((file_done == 0u) && (status == COM_OK))
    {
      switch (Ymodem_ReceivePacket(config, g_packet_data, &packet_length, DOWNLOAD_TIMEOUT))
      {
        case YMODEM_PACKET_OK:
          errors = 0u;
          switch (packet_length)
          {
            case 0u:
              Ymodem_SendByte(config, ACK);
              file_done = 1u;
              break;

            default:
              if (g_packet_data[PACKET_NUMBER_INDEX] != expected_packet)
              {
                Ymodem_SendByte(config, NAK);
                break;
              }

              if (expected_packet == 0u)
              {
                if (g_packet_data[PACKET_DATA_INDEX] != 0u)
                {
                  if (Ymodem_ParseHeader(result, g_packet_data) == 0u)
                  {
                    Ymodem_SendAbort(config);
                    status = COM_ERROR;
                    break;
                  }

                  status = config->on_start(result->file_name,
                                            result->file_size,
                                            result->image_crc32,
                                            result->target_fw_version,
                                            config->user_context);
                  if (status != COM_OK)
                  {
                    Ymodem_SendAbort(config);
                    break;
                  }

                  Ymodem_SendByte(config, ACK);
                  Ymodem_SendByte(config, CRC16);
                }
                else
                {
                  Ymodem_SendByte(config, ACK);
                  file_done = 1u;
                  session_done = 1u;
                }
              }
              else
              {
                uint32_t remaining;
                uint32_t write_len;

                if (result->bytes_received >= result->file_size)
                {
                  Ymodem_SendAbort(config);
                  status = COM_DATA;
                  break;
                }

                remaining = result->file_size - result->bytes_received;
                write_len = (packet_length < remaining) ? packet_length : remaining;

                status = config->on_data(result->bytes_received,
                                         &g_packet_data[PACKET_DATA_INDEX],
                                         write_len,
                                         config->user_context);
                if (status != COM_OK)
                {
                  Ymodem_SendAbort(config);
                  break;
                }

                result->bytes_received += write_len;
                Ymodem_SendByte(config, ACK);
              }

              expected_packet++;
              session_begin = 1u;
              break;
          }
          break;

        case YMODEM_PACKET_ABORT:
          Ymodem_SendAbort(config);
          status = COM_ABORT;
          break;

        case YMODEM_PACKET_TIMEOUT:
        case YMODEM_PACKET_ERROR:
        default:
          if (session_begin != 0u)
          {
            errors++;
          }

          if (errors > MAX_ERRORS)
          {
            Ymodem_SendAbort(config);
            status = COM_ERROR;
          }
          else
          {
            Ymodem_SendByte(config, CRC16);
          }
          break;
      }
    }
  }

  return status;
}
