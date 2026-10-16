#ifndef SERIAL_H
#define SERIAL_H

#include <stddef.h>

void serial_initialize(void);
void serial_write(const char* text);
void serial_write_bytes(const char* data, size_t length);

#endif
