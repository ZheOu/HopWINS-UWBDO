/**
  ******************************************************************************
  * @file           : console_protocol.h
  * @brief          : Fixed-buffer UART diagnostic message builder
  ******************************************************************************
  */

#ifndef HOPWINS_CONSOLE_PROTOCOL_H
#define HOPWINS_CONSOLE_PROTOCOL_H

#ifdef __cplusplus
extern "C" {
#endif

#include "board.h"

typedef struct {
  uint8_t *data;
  size_t capacity;
  size_t len;
  bool valid;
} console_protocol_message_t;

void console_protocol_message_init(
    console_protocol_message_t *message,
    uint8_t *storage,
    size_t capacity);
bool console_protocol_append_text(
    console_protocol_message_t *message,
    const char *text);
bool console_protocol_append_hex(
    console_protocol_message_t *message,
    uint32_t value,
    uint32_t digits);
bool console_protocol_append_hex64(
    console_protocol_message_t *message,
    uint64_t value,
    uint32_t digits);
bool console_protocol_append_i32(
    console_protocol_message_t *message,
    int32_t value);
bool console_protocol_append_bool(
    console_protocol_message_t *message,
    bool value);
board_status_t console_protocol_send_with_crc(
    console_protocol_message_t *message);
board_status_t console_protocol_send_with_crc_blocking(
    console_protocol_message_t *message);
board_status_t console_protocol_write(const char *text);
board_status_t console_protocol_write_blocking(const char *text);

#ifdef __cplusplus
}
#endif

#endif /* HOPWINS_CONSOLE_PROTOCOL_H */
