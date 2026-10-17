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

- `net connect [index]` (alias `net dhcp [index]`) runs a bounded DHCPv4 exchange on the selected link-up interface (or the first link-up interface), then configures its IPv4 address, netmask, gateway, and DNS server. It times out after 30 seconds and reports the result. It does not yet renew/expire leases or prove end-to-end Internet reachability.
- `net` / `net status` lists registered network devices, link state, MTU, MAC, configured IPv4 address, and RX/TX counters.
- `net poll` runs one bounded network poll cycle.
- `net config <index> <ip> <netmask> <gateway>` configures a registered interface with a validated static IPv4 configuration.

These commands provide diagnostics and DHCP/static configuration, but are not yet a general Internet client. The current stack includes E1000 polling, Ethernet/VLAN demultiplexing, ARP replies, IPv4 ICMP echo replies, and validated UDP receive callbacks. DHCP lease acquisition is implemented; DNS query resolution, UDP transmit for general clients, TCP, routing, sockets, and a general-purpose Internet login/connect flow are not implemented. A configured IP address is not evidence that a gateway or Internet route works.
