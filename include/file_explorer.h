#ifndef FILE_EXPLORER_H
#define FILE_EXPLORER_H

#include <stdint.h>

void file_explorer_initialize(void);
void file_explorer_open_directory(uint32_t id);
void file_explorer_draw(int width, int height);
void file_explorer_window_geometry(int width, int height, int* x, int* y, int* w, int* h);
int file_explorer_click(int x, int y, int width, int height);

#endif
