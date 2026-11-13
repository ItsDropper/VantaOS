/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "nvx_survey.h"
#include "nvx_survey_table.h"

static void emit(const nvx_bringup_io_t *io, const char *s)
{
    if (io && io->text) io->text(s, io->context);
}

static size_t put_hex(char *p, uint32_t v, unsigned digits)
{
    static const char d[] = "0123456789ABCDEF";
    p[0] = '0'; p[1] = 'x';
    for (unsigned i = 0; i < digits; i++) p[2 + i] = d[(v >> (4U * (digits - 1U - i))) & 0xFU];
    return 2U + digits;
}

static size_t put_str(char *p, const char *s)
{
    size_t n = 0U;
    while (s[n]) { p[n] = s[n]; n++; }
    return n;
}

static size_t put_dec(char *p, unsigned v)
{
    char t[10]; size_t n = 0U, i;
    do { t[n++] = (char)('0' + v % 10U); v /= 10U; } while (v);
    for (i = 0U; i < n; i++) p[i] = t[n - 1U - i];
    return n;
}

static size_t put_hex4_lower(char *p, uint16_t v)
{
    static const char d[] = "0123456789abcdef";
    for (unsigned i = 0; i < 4U; i++) p[i] = d[(v >> (12U - 4U * i)) & 0xFU];
    return 4U;
}

int nvx_survey_run(const nvx_mmio_region_t *bar0, uint16_t vendor_id, uint16_t device_id,
                   const nvx_bringup_io_t *io)
{
    char line[96];
    size_t n;
    unsigned count = 0U;

    if (!bar0 || !bar0->base) return -1;

    n = put_str(line, "NVXSURVEY begin pci=");
    n += put_hex4_lower(line + n, vendor_id);
    line[n++] = ':';
    n += put_hex4_lower(line + n, device_id);
    line[n++] = '\n'; line[n] = '\0';
    emit(io, line);

    for (size_t i = 0; i < NVX_SURVEY_TABLE_COUNT; i++) {
        uint32_t value = 0U;
        const nvx_survey_entry_t *e = &NVX_SURVEY_TABLE[i];
        int ok = nvx_mmio_read32(bar0, e->offset, &value) == NVX_MMIO_OK;

        n = put_str(line, "NVXOBS id=");
        n += put_str(line + n, e->fact_id);
        n += put_str(line + n, " off=");
        n += put_hex(line + n, e->offset, 8U);
        if (ok) {
            n += put_str(line + n, " val=");
            n += put_hex(line + n, value, 8U);
        } else {
            n += put_str(line + n, " error=read-failed");
        }
        line[n++] = '\n'; line[n] = '\0';
        emit(io, line);
        count++;
    }

    n = put_str(line, "NVXSURVEY end count=");
    n += put_dec(line + n, count);
    line[n++] = '\n'; line[n] = '\0';
    emit(io, line);
    return (int)count;
}
