#pragma once
#include "driver/uart.h"
#include <cstring>

// Overload for printing a constant string without extra parameters.
static void uart_print(uart_port_t uart_num, const char* str) {
    uart_write_bytes(uart_num, str, strlen(str));
}

// Template overload for printing a formatted string with a single value.
template<typename T>
static void uart_print(uart_port_t uart_num, const char* fmt, T value) {
    char buffer[128];
    int len = snprintf(buffer, sizeof(buffer), fmt, value);
    uart_write_bytes(uart_num, buffer, len);
}