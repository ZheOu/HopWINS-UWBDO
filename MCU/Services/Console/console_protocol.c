/**
  ******************************************************************************
  * @file           : console_protocol.c
  * @brief          : Fixed-buffer UART diagnostic message builder
  ******************************************************************************
  */

#include "console_protocol.h"

#include <string.h>

static bool console_protocol_append_bytes(
    console_protocol_message_t *message,
    const uint8_t *data,
    size_t len);

void console_protocol_message_init(
    console_protocol_message_t *message,
    uint8_t *storage,
    size_t capacity)
{
  if (message == NULL) {
    return;
  }

  message->data = storage;
  message->capacity = capacity;
  message->len = 0U;
  message->valid = (storage != NULL) && (capacity > 0U);
}

bool console_protocol_append_text(
    console_protocol_message_t *message,
    const char *text)
{
  if (text == NULL) {
    return false;
  }

  return console_protocol_append_bytes(
      message,
      (const uint8_t *)text,
      strlen(text));
}

bool console_protocol_append_hex(
    console_protocol_message_t *message,
    uint32_t value,
    uint32_t digits)
{
  return console_protocol_append_hex64(message, value, digits);
}

bool console_protocol_append_hex64(
    console_protocol_message_t *message,
    uint64_t value,
    uint32_t digits)
{
  static const uint8_t hex[] = "0123456789ABCDEF";
  uint8_t encoded[16];

  if ((digits == 0U) || (digits > sizeof(encoded))) {
    if (message != NULL) {
      message->valid = false;
    }
    return false;
  }

  for (uint32_t i = 0U; i < digits; i++) {
    uint32_t shift = (digits - 1U - i) * 4U;
    encoded[i] = hex[(uint32_t)(value >> shift) & 0x0FU];
  }

  return console_protocol_append_bytes(message, encoded, digits);
}

bool console_protocol_append_bool(
    console_protocol_message_t *message,
    bool value)
{
  const uint8_t encoded = value ? (uint8_t)'1' : (uint8_t)'0';
  return console_protocol_append_bytes(message, &encoded, 1U);
}

board_status_t console_protocol_send_with_crc(
    console_protocol_message_t *message)
{
  uint32_t crc;
  board_status_t status;

  if ((message == NULL) || !message->valid || (message->len == 0U)) {
    return BOARD_BAD_ARG;
  }

  status = board_crc32_calculate(message->data, message->len, &crc);
  if (status != BOARD_OK) {
    return status;
  }

  if (!console_protocol_append_text(message, ", CRC32=0x") ||
      !console_protocol_append_hex(message, crc, 8U) ||
      !console_protocol_append_text(message, "\r\n")) {
    return BOARD_BAD_ARG;
  }

  return board_pc_transmit(message->data, message->len);
}

board_status_t console_protocol_write(const char *text)
{
  if (text == NULL) {
    return BOARD_BAD_ARG;
  }

  return board_pc_transmit((const uint8_t *)text, strlen(text));
}

static bool console_protocol_append_bytes(
    console_protocol_message_t *message,
    const uint8_t *data,
    size_t len)
{
  if ((message == NULL) || !message->valid ||
      ((data == NULL) && (len != 0U)) ||
      (message->len > message->capacity) ||
      (len > (message->capacity - message->len))) {
    if (message != NULL) {
      message->valid = false;
    }
    return false;
  }

  memcpy(&message->data[message->len], data, len);
  message->len += len;
  return true;
}
