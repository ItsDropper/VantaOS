# Vanta Launcher compatibility plan

Copyright (c) 2026 ItsDropper

VantaOS aims to run the Vanta Launcher as a normal userspace application. The target is the existing JavaFX launcher in `ItsDropper/Vanta`, not a kernel-specific launcher API. The OS should provide stable generic interfaces and the launcher should consume them.

## Current state: not runnable yet

The current Vanta Launcher repository builds a Java 21 / JavaFX 21 desktop application and packages a platform-specific application image. VantaOS currently has a custom ring-3 ELF process model and a GUI, but it does not provide a JVM, Java standard-library runtime, JavaFX runtime, POSIX compatibility layer, or HTTPS/TLS. The Windows package cannot be copied to VantaOS and expected to run.

The new `curl` command is a small network bring-up tool, not the launcher installer itself:

```sh
net status
net connect
curl http://<reachable-host>/<path>
curl -o /HOME/USER/file.bin http://<reachable-host>/<path>
```

Only plain HTTP over IPv4 is supported today. The client rejects HTTPS, redirects, non-200 responses, and any `Transfer-Encoding` header; downloads are capped at 8 MiB. A reusable host-testable response parser validates HTTP/1.0 and HTTP/1.1 status lines, header syntax, control bytes, and `Content-Length` bounds. It rejects duplicate `Content-Length` fields (including identical duplicates), malformed lengths, and unsupported transfer coding. The command refuses to overwrite an existing output path, checks declared body length as bytes arrive, requires an orderly peer close before accepting the response, and removes output files on several transfer failures. The parser has deterministic CI tests; the complete downloader still lacks an end-to-end network/VFS regression test. The current client is not a general-purpose `curl.exe` replacement. Do not use plain HTTP for credentials, private data, or unverified executable downloads. The actual Vanta Launcher release endpoints use HTTPS, so these commands cannot yet fetch the launcher from its normal distribution source.

## Compatibility milestones

The following are gates, not claims that these features are complete.

1. **Reliable network bootstrap.** Validate TCP/DNS behavior with deterministic local test servers; add robust transfer framing, bounded retries, and a deterministic downloader suite covering exact bytes, malformed framing, truncated responses, size limits, and filesystem failures. Verify cleanup through the real VFS before treating it as a guarantee. Implement TLS only after the kernel has suitable entropy and trusted time plus reviewed cryptographic and certificate-validation code. HTTPS must fail closed on certificate or hostname errors.
2. **Native userspace contract.** Stabilize ELF loading, process lifecycle/waiting, file descriptors, directory operations, memory mapping, clocks, threads/synchronization, and error reporting. Publish a small C runtime ABI so applications do not bind directly to internal kernel functions.
3. **Java runtime feasibility.** Port or bring up a compatible JVM only after the required virtual-memory, thread, signal/exception, dynamic-linking, filesystem, clock, and synchronization interfaces exist. A JVM is not a single ELF file that can simply be downloaded and launched.
4. **Windowing and JavaFX.** Provide the native graphics/input/event facilities and compatible runtime libraries required by JavaFX. The existing Vanta Launcher UI depends on JavaFX controls; the current VantaOS GUI API is not JavaFX-compatible.
5. **Launcher portability.** Separate launcher services from Windows-specific integration. Replace Windows credential-store/JNA assumptions with a platform service, audit native extraction and process launching, and make paths/configuration follow a VantaOS userspace layout. Preserve Microsoft account security; do not store access or refresh tokens in plaintext.
6. **Minecraft execution.** Provide a supported Java runtime, TLS downloads, native library loading, graphics APIs/driver, audio/input, and the process/resource behavior required by Minecraft. Verify a minimal Java program before attempting the launcher, then verify JavaFX, then a minimal Minecraft client.

## First end-to-end test target

The first useful milestone is **not** “launch the full Minecraft UI.” It is:

- establish DHCP/DNS/TCP connectivity in QEMU using a deterministic test network;
- download a small known fixture to `/HOME/USER` and verify exact bytes and failure behavior;
- provide authenticated HTTPS with certificate and hostname validation;
- execute a tiny ring-3 C program from the filesystem and verify process exit/reaping;
- only then begin a JVM port/runtime experiment.

## Architecture rule

Keep launcher-specific behavior out of the kernel. VantaOS should expose general filesystem, process, networking/TLS, clock, input, graphics, and audio services. Vanta Launcher should call portable application services and use a platform adapter only where behavior is inherently OS-specific.
