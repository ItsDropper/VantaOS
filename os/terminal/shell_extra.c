#include "shell_internal.h"
#include "terminal.h"
#include "vfs.h"
#include "net.h"
#include "net_dns.h"
#include "net_ipv4.h"
#include "net_tcp.h"
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
            if (shell_string_equals(start, "-F") && argc <= 1U) {
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
    char backup[FS_PATH_MAX];
    backup[0] = 0;
    int backed_up = 0;
    if (target >= 0) {
        const fs_node_t *node = vfs_get_node((uint32_t)target);
        if (!force) {
            terminal_write("\nmv: destination exists; use -F to replace it");
            return 1;
        }
        if (!node || node->type != FS_NODE_FILE) {
            terminal_write("\nmv: only an existing file can be replaced");
            return 1;
        }
        unsigned int n = 0;
        while (destination[n] && n < FS_PATH_MAX - 1U) ++n;
        static const char suffix[] = ".vanta-mv-backup";
        unsigned int suffix_len = (unsigned int)(sizeof(suffix) - 1U);
        if (n + suffix_len >= FS_PATH_MAX) {
            terminal_write("\nmv: destination path too long for safe replacement");
            return 1;
        }
        for (unsigned int i = 0; i < n; ++i) backup[i] = destination[i];
        for (unsigned int i = 0; i <= suffix_len; ++i) backup[n + i] = suffix[i];
        if (vfs_lookup(backup) >= 0) {
            terminal_write("\nmv: safe replacement blocked; backup path already exists");
            return 1;
        }
        if (vfs_rename_path(destination, backup) < 0) {
            terminal_write("\nmv: could not stage existing destination");
            return 1;
        }
        backed_up = 1;
    }
    if (vfs_rename_path(source, destination) < 0) {
        if (!backed_up) {
            terminal_write("\nmv: rename failed; destination was not changed");
        } else if (vfs_rename_path(backup, destination) < 0) {
            terminal_write("\nmv: rename failed; previous destination remains at ");
            terminal_write(backup);
        } else {
            terminal_write("\nmv: rename failed; previous destination restored");
        }
        return 1;
    }
    if (backed_up && vfs_delete_path(backup) < 0) {
        terminal_write("\nmoved, but old destination backup remains at ");
        terminal_write(backup);
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


static int append_text(char *out, size_t capacity, size_t *used,
                       const char *text)
{
    if (!out || !used || !text) return 0;
    while (*text) {
        if (*used + 1U >= capacity) return 0;
        out[(*used)++] = *text++;
    }
    out[*used] = 0;
    return 1;
}

static int command_http_url(const char *url)
{
    char host[96], path[96], request[512], output[257];
    size_t host_length = 0, path_length = 0, used = 0;
    uint32_t port = 80U, address = 0;
    const char *authority, *cursor, *path_start, *port_start = 0;
    net_device_t *device = 0;

    if (!url) return 1;
    if (url[0] == 'h' && url[1] == 't' && url[2] == 't' &&
        url[3] == 'p' && url[4] == 's' && url[5] == ':' &&
        url[6] == '/' && url[7] == '/') {
        terminal_write("\nnet http: HTTPS requires TLS, which is not implemented; refusing insecure downgrade.");
        return 1;
    }
    if (!(url[0] == 'h' && url[1] == 't' && url[2] == 't' &&
          url[3] == 'p' && url[4] == ':' && url[5] == '/' && url[6] == '/')) {
        terminal_write("\nusage: net http <http://hostname[:port]/path>");
        return 1;
    }

    authority = url + 7U;
    cursor = authority;
    while (*cursor && *cursor != '/' && *cursor != '?' && *cursor != '#') {
        if (*cursor == ':') { port_start = cursor + 1U; break; }
        ++cursor;
    }
    path_start = cursor;
    if (port_start) {
        path_start = port_start;
        while (*path_start && *path_start != '/' &&
               *path_start != '?' && *path_start != '#') ++path_start;
        char port_text[6];
        size_t digits = (size_t)(path_start - port_start);
        if (!digits || digits >= sizeof(port_text)) {
            terminal_write("\nnet http: invalid TCP port");
            return 1;
        }
        for (size_t i = 0; i < digits; ++i) port_text[i] = port_start[i];
        port_text[digits] = 0;
        if (!parse_u32(port_text, &port) || !port || port > 65535U) {
            terminal_write("\nnet http: invalid TCP port");
            return 1;
        }
        host_length = (size_t)((port_start - 1U) - authority);
    } else {
        host_length = (size_t)(cursor - authority);
    }
    if (!host_length || host_length >= sizeof(host)) {
        terminal_write("\nnet http: invalid or oversized hostname");
        return 1;
    }
    for (size_t i = 0; i < host_length; ++i) host[i] = authority[i];
    host[host_length] = 0;

    if (*path_start == '#') {
        terminal_write("\nnet http: URL fragments are not sent to the server");
        path_start = "/";
    }
    if (*path_start == '?' || *path_start == 0) {
        path[path_length++] = '/';
        if (*path_start == '?') {
            size_t q = 0;
            while (path_start[q] && path_start[q] != '#' &&
                   path_length < sizeof(path) - 1U)
                path[path_length++] = path_start[q++];
        }
    } else {
        while (path_start[path_length] && path_start[path_length] != '#' &&
               path_length < sizeof(path) - 1U) {
            path[path_length] = path_start[path_length];
            ++path_length;
        }
    }
    path[path_length] = 0;
    if (!path_length) { path[0] = '/'; path[1] = 0; }

    for (uint32_t i = 0; i < net_device_count(); ++i) {
        net_device_t *candidate = net_device_get(i);
        if (candidate && candidate->link_up && candidate->ipv4_configured) {
            device = candidate;
            break;
        }
    }
    if (!device) {
        terminal_write("\nnet http: no configured link-up interface; run 'net connect' first");
        return 1;
    }

    if (!parse_ipv4(host, &address)) {
        if (!device->ipv4_dns) {
            terminal_write("\nnet http: no DNS server configured; run 'net connect' first");
            return 1;
        }
        terminal_write("\nResolving "); terminal_write(host); terminal_write("...");
        if (!net_dns_resolve(device, host, &address, 8000U)) {
            terminal_write("\nnet http: DNS lookup failed or timed out");
            return 1;
        }
        terminal_write(" -> "); print_ipv4(address);
    }

    request[0] = 0;
    if (!append_text(request, sizeof(request), &used, "GET ") ||
        !append_text(request, sizeof(request), &used, path) ||
        !append_text(request, sizeof(request), &used, " HTTP/1.0\r\nHost: ") ||
        !append_text(request, sizeof(request), &used, host)) {
        terminal_write("\nnet http: request is too large");
        return 1;
    }
    if (port != 80U) {
        char port_text[6], digits[6];
        size_t n = 0;
        uint32_t value = port;
        do { digits[n++] = (char)('0' + value % 10U); value /= 10U; }
        while (value && n < sizeof(digits));
        for (size_t i = 0; i < n; ++i) port_text[i] = digits[n - i - 1U];
        port_text[n] = 0;
        if (!append_text(request, sizeof(request), &used, ":") ||
            !append_text(request, sizeof(request), &used, port_text)) {
            terminal_write("\nnet http: request is too large");
            return 1;
        }
    }
    if (!append_text(request, sizeof(request), &used,
                     "\r\nUser-Agent: VantaOS/0.1\r\nConnection: close\r\nAccept: */*\r\n\r\n")) {
        terminal_write("\nnet http: request is too large");
        return 1;
    }

    terminal_write("\nConnecting to "); print_ipv4(address);
    terminal_write(":"); shell_print_decimal(port); terminal_write("...");
    if (!net_tcp_connect(device, address, (uint16_t)port, 15000U)) {
        terminal_write("\nnet http: TCP connection failed or timed out");
        return 1;
    }
    if (!net_tcp_send(device, request, used, 10000U)) {
        terminal_write("\nnet http: request transmission failed");
        net_tcp_close(device);
        return 1;
    }

    terminal_write("\n--- HTTP response (limited to 4096 bytes) ---\n");
    size_t total = 0;
    for (uint32_t chunks = 0; chunks < 16U; ++chunks) {
        int received = net_tcp_receive(device, output, sizeof(output) - 1U, 10000U);
        if (received == 0) break;
        if (received < 0) {
            terminal_write("\nnet http: response timed out or connection failed");
            break;
        }
        output[received] = 0;
        terminal_write(output);
        total += (size_t)received;
        if (total >= 4096U) {
            terminal_write("\n[response display limit reached]");
            break;
        }
    }
    net_tcp_close(device);
    if (!total) terminal_write("\nnet http: server returned no data");
    terminal_write("\n--- end response ---");
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
                if (d->ipv4_dns) {
                    terminal_write("\n    DNS "); print_ipv4(d->ipv4_dns);
                }
                terminal_write("\n    RX "); shell_print_decimal((uint32_t)stats.rx_frames);
                terminal_write(" frames, "); shell_print_decimal((uint32_t)stats.rx_dropped);
                terminal_write(" dropped; TX "); shell_print_decimal((uint32_t)stats.tx_frames);
                terminal_write(" frames, "); shell_print_decimal((uint32_t)stats.tx_errors);
                terminal_write(" errors");
            }
            terminal_putchar('\n');
        }
        terminal_write("Use 'net connect' for DHCP, 'net ping' to test reachability, and 'net dns' to resolve names. TCP/HTTP clients are not implemented yet.\n");
        return 1;
    }
    if ((argc == 2U || argc == 3U) &&
        (shell_string_equals(argv[1], "dhcp") ||
         shell_string_equals(argv[1], "connect"))) {
        uint32_t index = UINT32_MAX;
        if (argc == 3U && !parse_u32(argv[2], &index)) {
            terminal_write("\nusage: net connect [interface-index]");
            return 1;
        }
        net_device_t *d = 0;
        if (index != UINT32_MAX) {
            d = net_device_get(index);
        } else {
            for (uint32_t i = 0; i < net_device_count(); ++i) {
                net_device_t *candidate = net_device_get(i);
                if (candidate && candidate->link_up) {
                    d = candidate;
                    index = i;
                    break;
                }
            }
        }
        if (!d || !d->link_up) {
            terminal_write("\nnet connect: no link-up network interface; check cable or virtual NIC");
            return 1;
        }
        terminal_write("\nDHCP: requesting IPv4 configuration on ");
        terminal_write(d->name);
        terminal_write(" (up to 30 seconds)...");
        uint32_t dns = 0, lease = 0;
        if (!net_dhcp_configure(d, 30000U, &dns, &lease)) {
            terminal_write("\nDHCP failed or timed out. Check link, DHCP server, VLAN, and driver RX.");
            return 1;
        }
        d->ipv4_dns = dns;
        terminal_write("\nDHCP lease acquired: ");
        print_ipv4(d->ipv4_address);
        terminal_write(" / ");
        print_ipv4(d->ipv4_netmask);
        terminal_write("\nGateway: ");
        if (d->ipv4_gateway) print_ipv4(d->ipv4_gateway);
        else terminal_write("none");
        terminal_write("\nDNS: ");
        if (d->ipv4_dns) print_ipv4(d->ipv4_dns);
        else terminal_write("not supplied");
        terminal_write("\nLease seconds: ");
        shell_print_decimal(lease);
        terminal_write("\nInterface configured. General outbound Internet traffic still requires IPv4/ARP transmit and DNS/TCP client support.");
        return 1;
    }
    if (argc == 3U && (shell_string_equals(argv[1], "http") ||
                       shell_string_equals(argv[1], "get"))) {
        return command_http_url(argv[2]);
    }
    if (argc == 3U && shell_string_equals(argv[1], "ping")) {
        uint32_t target = 0;
        if (!parse_ipv4(argv[2], &target)) {
            terminal_write("\nusage: net ping <IPv4-address>");
            return 1;
        }
        net_device_t *d = 0;
        for (uint32_t i = 0; i < net_device_count(); ++i) {
            net_device_t *candidate = net_device_get(i);
            if (candidate && candidate->link_up && candidate->ipv4_configured) {
                d = candidate;
                break;
            }
        }
        if (!d) {
            terminal_write("\nnet ping: no configured link-up interface; run net connect first");
            return 1;
        }
        uint32_t elapsed = 0;
        terminal_write("\nPING "); print_ipv4(target); terminal_write(" ...");
        if (!net_ipv4_ping(d, target, 3000U, &elapsed)) {
            terminal_write("\nno ICMP echo reply (check gateway, firewall, and Internet route)");
            return 1;
        }
        terminal_write("\nreply from "); print_ipv4(target);
        terminal_write(": time="); shell_print_decimal(elapsed);
        terminal_write(" ms");
        return 1;
    }
    if (argc == 3U && shell_string_equals(argv[1], "dns")) {
        net_device_t *d = 0;
        for (uint32_t i = 0; i < net_device_count(); ++i) {
            net_device_t *candidate = net_device_get(i);
            if (candidate && candidate->link_up && candidate->ipv4_configured &&
                candidate->ipv4_dns) {
                d = candidate;
                break;
            }
        }
        if (!d) {
            terminal_write("\nnet dns: no DHCP DNS server configured; run net connect first");
            return 1;
        }
        uint32_t address = 0;
        terminal_write("\nResolving "); terminal_write(argv[2]); terminal_write("...");
        if (!net_dns_resolve(d, argv[2], &address, 5000U)) {
            terminal_write("\nDNS lookup failed or timed out");
            return 1;
        }
        terminal_write("\n"); terminal_write(argv[2]); terminal_write(" -> ");
        print_ipv4(address);
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
    terminal_write("\nusage: net [status|poll|connect [index]|dhcp [index]|dns <hostname>] | net config <index> <ip> <netmask> <gateway>");
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
