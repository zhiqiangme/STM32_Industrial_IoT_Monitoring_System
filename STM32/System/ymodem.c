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

/* 头包元数据目前仍坚持“ASCII 文本字段”而不是二进制结构体。
 * 这样升级脚本和 Bootloader 都更容易排查，也方便后续继续向后追加字段。 */
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

static int32_t Ymodem_ParseHexNibble(char value)
{
  if ((value >= '0') && (value <= '9'))
  {
    return (int32_t)(value - '0');
  }
  if ((value >= 'a') && (value <= 'f'))
  {
    return (int32_t)(value - 'a' + 10);
  }
  if ((value >= 'A') && (value <= 'F'))
  {
    return (int32_t)(value - 'A' + 10);
  }

  return -1;
}

static uint8_t Ymodem_ParseHexBytes(const char *text,
                                    uint8_t *bytes,
                                    uint32_t byte_count,
                                    const char **endptr)
{
  uint32_t i;

  if ((text == NULL) || (bytes == NULL))
  {
    return 0u;
  }

  /* 把类似 "e1f601..." 的 64 字符十六进制串还原成 32 字节摘要。 */
  for (i = 0u; i < byte_count; i++)
  {
    int32_t hi = Ymodem_ParseHexNibble(text[i * 2u]);
    int32_t lo = Ymodem_ParseHexNibble(text[i * 2u + 1u]);

    if ((hi < 0) || (lo < 0))
    {
      return 0u;
    }

    bytes[i] = (uint8_t)((hi << 4) | lo);
  }

  if (endptr != NULL)
  {
    *endptr = text + byte_count * 2u;
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
         ((PACKET_DATA_INDEX + file_name_len) < YMODEM_PACKET_BUFFER_SIZE) &&
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

  /* 文件信息区格式当前约定为：
   * "<size> 0x<crc32> <target_fw_version> <sha256_hex>"
   * 仍然保持“字段追加”的兼容风格，不重做整个 YMODEM 头包协议。
   * 解析路径会回写 g_packet_data，所以读侧索引也要显式拦截在缓冲区内，
   * 避免上层若改大 FILE_NAME_LENGTH/FILE_INFO_LENGTH 时悄悄越界。 */
  {
    uint32_t info_base = PACKET_DATA_INDEX + file_name_len + 1u;

    while ((file_info_len < FILE_INFO_LENGTH) &&
           ((info_base + file_info_len) < YMODEM_PACKET_BUFFER_SIZE) &&
           (file_info_len < (YMODEM_PACKET_BUFFER_SIZE - 1u)) &&
           (packet[info_base + file_info_len] != '\0'))
    {
      g_packet_data[file_info_len] = packet[info_base + file_info_len];
      file_info_len++;
    }
  }
  g_packet_data[file_info_len] = '\0';

  cursor = (const char *)g_packet_data;
  /* 现在 SHA-256 已经是本地升级链路里的标准字段，所以这里按必填解析。
   * 如果后面头包里没有哈希，会直接判成头包非法。 */
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

  if (Ymodem_SkipSpaces(&cursor) == 0u)
  {
    return 0u;
  }
  if (Ymodem_ParseHexBytes(cursor, result->image_sha256, YMODEM_IMAGE_SHA256_SIZE, &cursor) == 0u)
  {
    return 0u;
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

  /* 这里只负责还原“一个 YMODEM 包”的原始边界和 CRC16。
   * 真正的业务含义，例如头包/数据包/结束包，要留给上层状态机判断。 */
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

  /* 这个状态机保留了标准 YMODEM 的整体节奏：
   * - packet#0: 头包
   * - packet#1..N: 数据包
   * - EOT: 文件结束
   * - 空头包: 会话结束 */
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

                  /* 头包一旦解析成功，就把 size/crc32/version/sha256 一次性交给上层。
                   * 上层可在这里决定是否接受这次镜像、擦除 App 区并初始化状态页。 */
                  status = config->on_start(result->file_name,
                                            result->file_size,
                                            result->image_crc32,
                                            result->target_fw_version,
                                            result->image_sha256,
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

                /* 数据包只把“实际有效长度”交给上层。
                 * 末包即使是 1K 包，也只写入 file_size 对应的有效部分。 */
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
