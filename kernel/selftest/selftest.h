#ifndef KERNEL_SELFTEST_H
#define KERNEL_SELFTEST_H
#include "vanta_boot_info.h"
/* Regression entry points; implementations exist only in self-test builds. */
void kernel_boot_info_selftest(const vanta_boot_info_t* normalized);
void kernel_platform_selftest(void);
void storage_framework_selftest(void);
#endif
