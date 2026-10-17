#include "shell_internal.h"
#include "terminal.h"
#include "vfs.h"
#include "net.h"
#include <stdint.h>
#include <stddef.h>

#define EXTRA_ARG_MAX 64U

static int parse_command(char *input, char **argv, uint32_t capacity,
                         uint32_t *argc_out, int *force_out)
{
    char *read = input, *write = input;
    uint32_t argc = 0;
    int force = 0;
    while (*read) {
        while (*read == ' ' || *read == '\t') ++read;
        if (!*read) break;
        char quote = 0;
        if (*read == '"' || *read == '\'') quote = *read++;
        char *start = write;
        while (*read) {
            if (quote && *read == quote) break;
            if (!quote && (*read == ' ' || *read == '\t')) break;
            if (*read == '\\') {
                if (!read[1]) return 0;
                ++read;
            }
            *write++ = *read++;
        }
        if (quote) {
            if (*read != quote) return 0;
            ++read;
            if (*read && *read != ' ' && *read != '\t') return 0;
        } else if (*read == ' ' || *read == '\t') {
            ++read;
        }
        if (write > start) {
            *write++ = 0;
            if (shell_string_equals(start, "-F")) {
                force = 1;
            } else {
                if (argc >= capacity) return 0;
                argv[argc++] = start;
            }
        } else if (quote) {
            *write++ = 0;
            if (argc >= capacity) return 0;
            argv[argc++] = start;
        }
    }
    if (argc > 0 && shell_string_equals(argv[0], "-F")) {
        force = 1;
        for (uint32_t i = 1; i < argc; ++i) argv[i - 1] = argv[i];
        --argc;
    }
    *argc_out = argc;
    *force_out = force;
    return argc != 0;
}

static int parse_u32(const char *s, uint32_t *out)
{
    if (!s || !*s || !out) return 0;
    uint32_t value = 0;
    for (uint32_t i = 0; s[i]; ++i) {
        if (s[i] < '0' || s[i] > '9') return 0;
        uint32_t digit = (uint32_t)(s[i] - '0');
        if (value > (UINT32_MAX - digit) / 10U) return 0;
        value = value * 10U + digit;
    }
    *out = value;
    return 1;
}

static int parse_ipv4(const char *s, uint32_t *out)
{
    uint32_t octets[4] = {0, 0, 0, 0};
    uint32_t part = 0, value = 0, digits = 0;
    if (!s || !*s || !out) return 0;
    for (uint32_t i = 0;; ++i) {
        char c = s[i];
        if (c >= '0' && c <= '9') {
            if (++digits > 3U) return 0;
            value = value * 10U + (uint32_t)(c - '0');
            if (value > 255U) return 0;
        } else if (c == '.' || c == 0) {
            if (!digits || part >= 4U) return 0;
            octets[part++] = value;
            value = digits = 0;
            if (!c) break;
        } else return 0;
    }
    if (part != 4U) return 0;
    *out = (octets[0] << 24) | (octets[1] << 16) |
           (octets[2] << 8) | octets[3];
    return 1;
}

static void print_ipv4(uint32_t ip)
{
    shell_print_decimal((ip >> 24) & 255U); terminal_putchar('.');
    shell_print_decimal((ip >> 16) & 255U); terminal_putchar('.');
    shell_print_decimal((ip >> 8) & 255U); terminal_putchar('.');
    shell_print_decimal(ip & 255U);
}

static int command_stat(uint32_t argc, char **argv)
{
    if (argc != 2U) {
        terminal_write("\nusage: stat <path>");
        return 1;
    }
    char path[FS_PATH_MAX];
    shell_resolve_path(argv[1], path);
    int id = vfs_lookup(path);
    if (id < 0) {
        terminal_write("\nstat: path not found");
        return 1;
    }
    const fs_node_t *node = vfs_get_node((uint32_t)id);
    if (!node) {
        terminal_write("\nstat: invalid node");
        return 1;
    }
    terminal_write("\nPath: "); terminal_write(path);
    terminal_write("\nType: ");
    terminal_write(node->type == FS_NODE_DIRECTORY ? "directory" :
                   node->type == FS_NODE_FILE ? "file" : "virtual");
    terminal_write("\nNode: "); shell_print_decimal(node->id);
    terminal_write("\nParent: "); shell_print_decimal(node->parent);
    terminal_write("\nSize: "); shell_print_decimal(node->size);
    terminal_write(" bytes\n");
    return 1;
}

static int command_mkdir(uint32_t argc, char **argv, int force)
{
    if (argc != 2U) {
        terminal_write("\nusage: mkdir [-F] <path>");
        return 1;
    }
    char path[FS_PATH_MAX];
    shell_resolve_path(argv[1], path);
    int existing = vfs_lookup(path);
    if (existing >= 0) {
        const fs_node_t *node = vfs_get_node((uint32_t)existing);
        if (force && node && node->type == FS_NODE_DIRECTORY) {
            terminal_write("\ndirectory already exists: "); terminal_write(path);
        } else {
            terminal_write("\nmkdir: path already exists");
        }
        return 1;
    }
    if (vfs_create_directory(path) < 0) {
        terminal_write("\nmkdir: create failed");
        return 1;
    }
    terminal_write("\ncreated directory "); terminal_write(path);
    return 1;
}

static int command_touch(uint32_t argc, char **argv, int force)
{
    if (argc != 2U) {
        terminal_write("\nusage: touch [-F] <path>");
        return 1;
    }
    char path[FS_PATH_MAX];
    shell_resolve_path(argv[1], path);
    int existing = vfs_lookup(path);
    if (existing >= 0) {
        const fs_node_t *node = vfs_get_node((uint32_t)existing);
        if (force && node && node->type == FS_NODE_FILE) {
            terminal_write("\nfile already exists: "); terminal_write(path);
        } else {
            terminal_write("\ntouch: path already exists");
        }
        return 1;
    }
    if (vfs_create_file(path) < 0) {
        terminal_write("\ntouch: create failed");
        return 1;
    }
    terminal_write("\ncreated file "); terminal_write(path);
    return 1;
}

static int command_write(uint32_t argc, char **argv, int force)
{
    if (argc < 3U) {
        terminal_write("\nusage: write [-F] <file> <text...>");
        return 1;
    }
    char path[FS_PATH_MAX];
    shell_resolve_path(argv[1], path);
    char data[SHELL_BUFFER_SIZE];
    uint32_t length = 0;
    for (uint32_t i = 2U; i < argc; ++i) {
        for (uint32_t j = 0; argv[i][j]; ++j) {
            if (length + 1U >= sizeof(data)) {
                terminal_write("\nwrite: text exceeds command buffer");
                return 1;
            }
            data[length++] = argv[i][j];
        }
        if (i + 1U < argc) {
            if (length + 1U >= sizeof(data)) {
                terminal_write("\nwrite: text exceeds command buffer");
                return 1;
            }
            data[length++] = ' ';
        }
    }
    data[length] = 0;
    int id = vfs_lookup(path);
    if (id < 0) {
        if (vfs_create_file(path) < 0) {
            terminal_write("\nwrite: cannot create file");
            return 1;
        }
        id = vfs_lookup(path);
    }
    const fs_node_t *node = id < 0 ? 0 : vfs_get_node((uint32_t)id);
    if (!node || node->type != FS_NODE_FILE) {
        terminal_write("\nwrite: destination is not a regular file");
        return 1;
    }
    if (node->size != 0U && !force) {
        terminal_write("\nwrite: refusing to overwrite non-empty file without -F");
        return 1;
    }
    int handle = vfs_open(path, VFS_OPEN_WRITE |
                          (force ? VFS_OPEN_TRUNCATE : 0U));
    if (handle < 0) {
        terminal_write("\nwrite: open failed");
        return 1;
    }
    int written = vfs_write(handle, data, length);
    int closed = vfs_close(handle);
    if (written != (int)length || closed < 0) {
        terminal_write("\nwrite: incomplete write");
        return 1;
    }
    terminal_write("\nwrote "); shell_print_decimal(length);
    terminal_write(" bytes to "); terminal_write(path);
    return 1;
}

static int command_rm(uint32_t argc, char **argv, int force)
{
    if (argc != 2U) {
        terminal_write("\nusage: rm [-F] <path>");
        return 1;
    }
    char path[FS_PATH_MAX];
    shell_resolve_path(argv[1], path);
    int id = vfs_lookup(path);
    if (id < 0) {
        terminal_write("\nrm: path not found");
        return 1;
    }
    const fs_node_t *node = vfs_get_node((uint32_t)id);
    if (!node) {
        terminal_write("\nrm: invalid filesystem node");
        return 1;
    }
    if (node->type == FS_NODE_DIRECTORY && !force) {
        terminal_write("\nrm: refusing to remove a directory without -F");
        return 1;
    }
    if (vfs_delete_path(path) < 0) {
        terminal_write("\nrm: deletion failed (directory may not be empty)");
        return 1;
    }
    terminal_write("\nremoved "); terminal_write(path);
    return 1;
}

static int command_mv(uint32_t argc, char **argv, int force)
{
    if (argc != 3U) {
        terminal_write("\nusage: mv [-F] <source> <destination>");
        return 1;
    }
    char source[FS_PATH_MAX], destination[FS_PATH_MAX];
    shell_resolve_path(argv[1], source);
    shell_resolve_path(argv[2], destination);
    if (shell_string_equals(source, destination)) {
        terminal_write("\nmv: source and destination are identical");
        return 1;
    }
    if (vfs_lookup(source) < 0) {
        terminal_write("\nmv: source path not found");
        return 1;
    }
    int target = vfs_lookup(destination);
    if (target >= 0) {
        const fs_node_t *node = vfs_get_node((uint32_t)target);
        if (!force) {
            terminal_write("\nmv: destination exists; use -F to replace it");
            return 1;
        }
        if (!node || node->type == FS_NODE_DIRECTORY ||
            vfs_delete_path(destination) < 0) {
            terminal_write("\nmv: cannot replace destination");
            return 1;
        }
    }
    if (vfs_rename_path(source, destination) < 0) {
        terminal_write("\nmv: rename failed");
        return 1;
    }
    terminal_write("\nmoved "); terminal_write(source);
    terminal_write(" -> "); terminal_write(destination);
    return 1;
}

static int command_cp(uint32_t argc, char **argv, int force)
{
    if (argc != 3U) {
        terminal_write("\nusage: cp [-F] <source> <destination>");
        return 1;
    }
    char source[FS_PATH_MAX], destination[FS_PATH_MAX];
    shell_resolve_path(argv[1], source);
    shell_resolve_path(argv[2], destination);
    if (shell_string_equals(source, destination)) {
        terminal_write("\ncp: source and destination are identical");
        return 1;
    }
    int src = vfs_open(source, VFS_OPEN_READ);
    if (src < 0) {
        terminal_write("\ncp: cannot open source");
        return 1;
    }
    int target = vfs_lookup(destination);
    if (target >= 0) {
        const fs_node_t *node = vfs_get_node((uint32_t)target);
        if (!force || !node || node->type != FS_NODE_FILE) {
            vfs_close(src);
            terminal_write(force ? "\ncp: destination is not a file" :
                           "\ncp: destination exists; use -F to overwrite");
            return 1;
        }
    } else if (vfs_create_file(destination) < 0) {
        vfs_close(src);
        terminal_write("\ncp: cannot create destination");
        return 1;
    }
    int dst = vfs_open(destination, VFS_OPEN_WRITE | VFS_OPEN_TRUNCATE);
    if (dst < 0) {
        vfs_close(src);
        terminal_write("\ncp: cannot open destination");
        return 1;
    }
    char buffer[128];
    int failed = 0;
    for (;;) {
        int n = vfs_read(src, buffer, sizeof(buffer));
        if (n < 0) { failed = 1; break; }
        if (n == 0) break;
        int written = vfs_write(dst, buffer, (unsigned int)n);
        if (written != n) { failed = 1; break; }
    }
    if (vfs_close(src) < 0) failed = 1;
    if (vfs_close(dst) < 0) failed = 1;
    if (failed) {
        terminal_write("\ncp: copy failed; destination may be partial");
        return 1;
    }
    terminal_write("\ncopied "); terminal_write(source);
    terminal_write(" -> "); terminal_write(destination);
    return 1;
}

static int command_kill(uint32_t argc, char **argv, int force)
{
    uint32_t pid;
    if (argc != 2U || !parse_u32(argv[1], &pid)) {
        terminal_write("\nusage: kill [-F] <pid>");
        return 1;
    }
    if (pid == 0U || pid == process_current_pid()) {
        terminal_write("\nkill: refusing to terminate the idle or current process");
        return 1;
    }
    process_t process;
    if (!process_snapshot(pid, &process)) {
        terminal_write("\nkill: no such process");
        return 1;
    }
    if (!process.is_user && !force) {
        terminal_write("\nkill: refusing to terminate a kernel process without -F");
        return 1;
    }
    if (!process_terminate(pid)) {
        terminal_write("\nkill: process refused termination or is already exiting");
        return 1;
    }
    terminal_write("\nterminated PID "); shell_print_decimal(pid);
    return 1;
}

static int command_net(uint32_t argc, char **argv)
{
    if (argc == 1U || (argc == 2U && shell_string_equals(argv[1], "status"))) {
        uint32_t count = net_device_count();
        terminal_write("\nNetwork devices: "); shell_print_decimal(count);
        terminal_write("\n");
        for (uint32_t i = 0; i < count; ++i) {
            net_device_t *d = net_device_get(i);
            if (!d) continue;
            terminal_write("["); shell_print_decimal(i); terminal_write("] ");
            terminal_write(d->name);
            terminal_write(" link="); terminal_write(d->link_up ? "up" : "down");
            terminal_write(" mtu="); shell_print_decimal(d->mtu);
            terminal_write(" mac=");
            for (uint32_t m = 0; m < 6U; ++m) {
                const char hex[] = "0123456789ABCDEF";
                if (m) terminal_putchar(':');
                terminal_putchar(hex[d->mac[m] >> 4]);
                terminal_putchar(hex[d->mac[m] & 15U]);
            }
            uint32_t ip = 0, mask = 0, gateway = 0;
            if (net_device_get_ipv4(d, &ip, &mask, &gateway)) {
                terminal_write("\n    IPv4 "); print_ipv4(ip);
                terminal_write(" / "); print_ipv4(mask);
                terminal_write(" gateway "); print_ipv4(gateway);
            } else terminal_write("\n    IPv4 unconfigured");
            net_device_stats_t stats;
            if (net_device_get_stats(d, &stats)) {
                terminal_write("\n    RX "); shell_print_decimal((uint32_t)stats.rx_frames);
                terminal_write(" frames, "); shell_print_decimal((uint32_t)stats.rx_dropped);
                terminal_write(" dropped; TX "); shell_print_decimal((uint32_t)stats.tx_frames);
                terminal_write(" frames, "); shell_print_decimal((uint32_t)stats.tx_errors);
                terminal_write(" errors");
            }
            terminal_putchar('\n');
        }
        terminal_write("Note: device visibility does not imply Internet access; DHCP/DNS/TCP are not implemented.\n");
        return 1;
    }
    if (argc == 2U && shell_string_equals(argv[1], "poll")) {
        size_t packet_work = net_poll(16U);
        terminal_write("\npolled receive packets="); shell_print_decimal((uint32_t)packet_work);
        return 1;
    }
    if (argc == 6U && shell_string_equals(argv[1], "config")) {
        uint32_t index, ip, mask, gateway;
        if (!parse_u32(argv[2], &index) ||
            !parse_ipv4(argv[3], &ip) ||
            !parse_ipv4(argv[4], &mask) ||
            !parse_ipv4(argv[5], &gateway)) {
            terminal_write("\nusage: net config <index> <ip> <netmask> <gateway>");
            return 1;
        }
        net_device_t *d = net_device_get(index);
        if (!d || !net_device_configure_ipv4(d, ip, mask, gateway)) {
            terminal_write("\nnet: invalid device or IPv4 configuration");
            return 1;
        }
        terminal_write("\nconfigured "); terminal_write(d->name); terminal_write(" as ");
        print_ipv4(ip);
        terminal_write(" (static; no DHCP lease or Internet test performed)");
        return 1;
    }
    terminal_write("\nusage: net [status|poll] | net config <index> <ip> <netmask> <gateway>");
    return 1;
}

int shell_execute_extended(const char *command_line)
{
    if (!command_line) return 0;
    char storage[SHELL_BUFFER_SIZE];
    uint32_t length = 0;
    while (command_line[length]) {
        if (length + 1U >= sizeof(storage)) return 0;
        storage[length] = command_line[length];
        ++length;
    }
    storage[length] = 0;
    char *argv[EXTRA_ARG_MAX];
    uint32_t argc = 0;
    int force = 0;
    if (!parse_command(storage, argv, EXTRA_ARG_MAX, &argc, &force)) return 0;
    if (shell_string_equals(argv[0], "rm")) return command_rm(argc, argv, force);
    if (shell_string_equals(argv[0], "mv")) return command_mv(argc, argv, force);
    if (shell_string_equals(argv[0], "cp")) return command_cp(argc, argv, force);
    if (shell_string_equals(argv[0], "kill")) return command_kill(argc, argv, force);
    if (shell_string_equals(argv[0], "stat")) return command_stat(argc, argv);
    if (shell_string_equals(argv[0], "mkdir")) return command_mkdir(argc, argv, force);
    if (shell_string_equals(argv[0], "touch")) return command_touch(argc, argv, force);
    if (shell_string_equals(argv[0], "write")) return command_write(argc, argv, force);
    if (shell_string_equals(argv[0], "net")) return command_net(argc, argv);
    return 0;
}
