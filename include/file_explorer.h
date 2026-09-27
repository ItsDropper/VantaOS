#ifndef FILE_EXPLORER_H
#define FILE_EXPLORER_H

void file_explorer_initialize(void);
void file_explorer_open_directory(uint32_t id);
void file_explorer_draw(int width, int height);
int file_explorer_click(int x, int y, int width, int height);

#endif
