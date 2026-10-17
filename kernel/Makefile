# Core kernel initialization and architecture-independent services.
VANTAOS_KERNEL_C := kernel/kernel.c kernel/smp.c kernel/cpu_local.c kernel/serial.c kernel/panic.c kernel/fault_trace.c kernel/syscall.c kernel/crypto_rng.c kernel/memory.c
include kernel/selftest/Makefile
include kernel/boot/Makefile
include kernel/proc/Makefile
include kernel/sched/Makefile
include kernel/time/Makefile
include kernel/graphics/Makefile
