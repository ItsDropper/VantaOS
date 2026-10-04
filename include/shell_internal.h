#ifndef SHELL_INTERNAL_H
#define SHELL_INTERNAL_H

#include "shell.h"
#include "filesystem.h"
#include "process.h"

#define SHELL_BUFFER_SIZE 128
#define SHELL_HISTORY_SIZE 32

extern char shell_buffer[SHELL_BUFFER_SIZE];
extern unsigned int shell_length;
extern unsigned int shell_cursor;
extern char shell_history[SHELL_HISTORY_SIZE][SHELL_BUFFER_SIZE];
extern unsigned int shell_history_count;
extern unsigned int shell_history_position;
extern char shell_history_draft[SHELL_BUFFER_SIZE];
extern unsigned int shell_history_draft_length;
extern multiboot_info_t* multiboot_info;
extern int pmm_initialized;
extern int pci_initialized;
extern char shell_cwd[FS_PATH_MAX];

int shell_string_equals(const char* a,const char* b);
void shell_print_decimal(unsigned int value);
void shell_print_hex64(unsigned long long value);
void shell_copy_string(char* destination,const char* source);
void shell_clear_buffer(void);
void shell_prompt(void);
void shell_load_buffer(const char* text);
void shell_add_history(void);
void shell_show_history(void);
void shell_execute(void);

void shell_copy_path(char* destination,const char* source);
void shell_append_path(char* destination,const char* source);
int shell_resolve_path(const char* argument,char* resolved);
void shell_ls(const char* argument);
void shell_cat(const char* argument);
void shell_pwd(void);
void shell_cd(const char* argument);
void shell_mkdir(const char* argument);
void shell_touch(const char* argument);
void shell_write_file(const char* arguments);

void shell_help(void);
void shell_about(void);
void shell_specs(void);
void shell_boot(void);
void shell_uptime(void);
void shell_mem(void);
void shell_heap(void);
void shell_fault(void);
void shell_reboot(void);
void shell_storage(void);

#endif
