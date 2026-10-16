/*
 * Test-only kernel regression probes. This translation unit is empty in
 * ordinary builds; VANTAOS_SELF_TEST enables the test implementation.
 */
#include "desktop_process.h"
#include "filesystem.h"
#include "fat32.h"
#include "heap.h"
#include "graphics.h"
#include "keyboard.h"
#include "mouse.h"
#include "process.h"
#include "cpu_local.h"
#include "smp.h"
#include "panic.h"
#include "spinlock.h"
#include "pmm.h"
#include "serial.h"
#include "scheduler.h"
#include "shell.h"
#include "terminal.h"
#include "terminal_process.h"
#include "vfs.h"

#ifdef VANTAOS_SELF_TEST
#include "selftest.h"

void filesystem_selftest_initialize(void)
{
    int root_id = vfs_lookup("/");
    const fs_node_t* root = root_id >= 0 ?
        vfs_get_node((uint32_t)root_id) : NULL;

    /*
     * When the CI disk is mounted, prove that case variants resolve to
     * the same on-disk nodes and that the FAT32 entries remain reachable.
     * This catches duplicate virtual directories shadowing real directories.
     */
    int fat32_namespace_ok = 1;
    if (fat32_is_mounted())
    {
        int system_lower = filesystem_lookup("/system");
        int system_upper = filesystem_lookup("/SYSTEM");
        int drivers_lower = filesystem_lookup("/system/drivers");
        int drivers_upper = filesystem_lookup("/SYSTEM/DRIVERS");
        int ata_lower = filesystem_lookup("/system/drivers/ata.sys");
        int ata_upper = filesystem_lookup("/SYSTEM/DRIVERS/ATA.SYS");
        int keyboard_lfn = filesystem_lookup("/system/devices/keyboard0.dev");
        int config_lfn = filesystem_lookup("/etc/system.conf");
        const fs_node_t* keyboard_node = keyboard_lfn >= 0 ?
            filesystem_get_node((uint32_t)keyboard_lfn) : NULL;
        const fs_node_t* config_node = config_lfn >= 0 ?
            filesystem_get_node((uint32_t)config_lfn) : NULL;

        fat32_namespace_ok =
            system_lower >= 0 &&
            system_lower == system_upper &&
            drivers_lower >= 0 &&
            drivers_lower == drivers_upper &&
            ata_lower >= 0 &&
            ata_lower == ata_upper &&
            keyboard_node &&
            keyboard_node->size > 0 &&
            config_node &&
            config_node->size > 0;
    }

    if (!filesystem_is_initialized() ||
        !vfs_is_initialized() ||
        !root ||
        root->type != FS_NODE_DIRECTORY ||
        vfs_lookup("/system") < 0 ||
        vfs_lookup("/system/drivers") < 0 ||
        vfs_lookup("/home/user") < 0 ||
        vfs_lookup("/etc") < 0 ||
        vfs_lookup("/__vantaos_ci_missing_entry__") != -1 ||
        vfs_list((uint32_t)root_id, NULL, 0) < 0 ||
        !fat32_namespace_ok)
    {
        serial_write("VANTAOS_FILESYSTEM_NAMESPACE_FAIL\n");
        return;
    }

    char byte = 'x';
    if (vfs_read(-1, &byte, 1) != -1 ||
        vfs_write(-1, "x", 1) != -1 ||
        vfs_close(-1) != -1)
    {
        serial_write("VANTAOS_VFS_HANDLE_GUARDS_FAIL\n");
        return;
    }

    /*
     * This image is a disposable CI fixture. Exercise the real ATA -> FAT32
     * allocation/directory-entry -> VFS read path with a bounded 8.3 name.
     * A failure is a hard regression marker; no success marker is printed
     * until bytes written to the disk can be read back through the VFS.
     */
    static const char write_probe[] = "VANTAOS_FAT32_WRITE_OK";
    char write_back[64];

    /* On later boots, verify bytes committed by the previous boot first. */
    int prior_probe = filesystem_lookup("/VNTTEST.TXT");
    if (prior_probe >= 0 &&
        filesystem_read((uint32_t)prior_probe, write_back, sizeof(write_back)) ==
            (int)(sizeof(write_probe) - 1U))
    {
        unsigned int i;
        for (i = 0; i < sizeof(write_probe) - 1U; i++)
            if (write_back[i] != write_probe[i]) break;
        if (i == sizeof(write_probe) - 1U)
            serial_write("VANTAOS_FAT32_REBOOT_PERSIST_OK\n");
    }
    if (filesystem_write_file("/VNTTEST.TXT", write_probe) !=
            (int)(sizeof(write_probe) - 1U))
    {
        serial_write("VANTAOS_FAT32_WRITE_FAIL\n");
        return;
    }
    int probe_id = filesystem_lookup("/VNTTEST.TXT");
    if (probe_id < 0 ||
        filesystem_read((uint32_t)probe_id, write_back, sizeof(write_back)) !=
            (int)(sizeof(write_probe) - 1U))
    {
        serial_write("VANTAOS_FAT32_WRITE_READBACK_FAIL\n");
        return;
    }
    for (unsigned int i = 0; i < sizeof(write_probe); i++)
    {
        if (write_back[i] != write_probe[i])
        {
            serial_write("VANTAOS_FAT32_WRITE_CONTENT_FAIL\n");
            return;
        }
    }
    serial_write("VANTAOS_FAT32_WRITE_READBACK_OK\n");

    serial_write("VANTAOS_FILESYSTEM_NAMESPACE_OK\n");
    serial_write("VANTAOS_VFS_HANDLE_GUARDS_OK\n");
}


#endif
