# Terminal shell commands

The ring-3 terminal shell uses a bounded command buffer and argument parser. Quoted arguments and backslash-escaped characters are supported by the extended commands below. Commands operate only on the VantaOS VFS namespace; they do not expose host files.

## File commands

- `ls [path]`, `cd [path]`, `pwd`, and `cat <path>` navigate and inspect files.
- `mkdir [-F] <path>` and `touch [-F] <path>` create directories and empty files. With `-F`, an existing object of the requested type is accepted as a no-op.
- `write [-F] <path> <text...>` writes text to a file. It creates a missing file; without `-F`, it refuses to overwrite a non-empty file. With `-F`, the existing contents are truncated first.
- `stat <path>` prints cached VFS metadata.
- `cp [-F] <source> <destination>` copies a regular file in bounded chunks. Existing destination files require `-F`.
- `mv [-F] <source> <destination>` renames/moves a path. Existing destination files require `-F`; directories are not replaced.
- `rm [-F] <path>` deletes a path. Directory deletion requires `-F`; the filesystem layer still rejects directories that cannot safely be deleted (for example, non-empty directories).

`-F` may appear after the command (for example, `rm -F /tmp/example`) or before it (`-F rm /tmp/example`). It only affects commands that document support for it; it is not a universal permission bypass. A failed copy can leave a partial destination, so inspect the result before retrying.

## Process commands

- `ps` lists process IDs, parents, states, and names.
- `exec <elf-file> [args...]` loads a static ELF64 executable from the VFS.
- `kill <pid>` terminates a user process. `kill -F <pid>` also permits requesting termination of a kernel process. PID 0 and the current shell process are always protected. The command requests the kernel process-state transition; it does not guarantee that an already-running process has been synchronously reaped.

## Network commands

- `net connect [index]` (alias `net dhcp [index]`) requests a DHCPv4 lease on a link-up interface and configures its IPv4 address, netmask, gateway, and DNS server. It can take up to 30 seconds.
- `net` / `net status` lists interfaces, link state, MTU, MAC, IPv4 configuration, DNS, and RX/TX counters.
- `net dns <hostname>` resolves an IPv4 A record using the configured DNS server.
- `net ping <IPv4-address>` sends an ICMP echo request and waits up to three seconds; firewalls may block ICMP.
- `net http <http://hostname[:port]/path>` (alias `net get <URL>`) resolves the host if needed, opens a TCP connection, sends one HTTP/1.0 GET, and prints up to 4096 response bytes.
- `net poll` runs one bounded network polling cycle.
- `net config <index> <ip> <netmask> <gateway>` configures a validated static IPv4 address, mask, and gateway.

**Limitations:** HTTP is plaintext and intended for connectivity testing. HTTPS is rejected because TLS and certificate validation are not implemented. TCP supports only one synchronous client connection and is not yet a general socket API. DHCP lease renewal/expiry, IPv4 fragmentation, outbound VLAN tagging, and broad physical NIC compatibility remain unfinished. DHCP success alone does not prove Internet reachability.
