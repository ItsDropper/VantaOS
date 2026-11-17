#!/usr/bin/env python3
"""Structural checks for the demand-paging wiring.

Hardware behavior (CR2, iretq restart, TLB) is covered by the ring-3 QEMU
suite. These checks pin down source-level invariants that a refactor could
silently break and that a QEMU run would not necessarily notice.
"""
import difflib, pathlib, re, subprocess, sys, tempfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
failures = []

def read(path):
    return (ROOT / path).read_text()

def body(src, signature):
    """Return the brace-matched body of the function starting at signature."""
    start = src.find(signature)
    if start < 0:
        failures.append("function not found: " + signature)
        return ""
    open_at = src.find("{", start)
    depth = 0
    for i in range(open_at, len(src)):
        if src[i] == "{":
            depth += 1
        elif src[i] == "}":
            depth -= 1
            if depth == 0:
                return src[open_at:i + 1]
    failures.append("unbalanced braces: " + signature)
    return ""

def check(condition, message):
    if not condition:
        failures.append(message)

panic = read("kernel/panic.c")
asm = read("arch/x86_64/isr.asm")
mapping = read("mm/user_mapping.c")
paging = read("mm/paging.c")
fault = read("mm/vm_fault.c")
cache = read("mm/vm_page_cache.c")
region = read("mm/vm_region.c")
elf = read("kernel/proc/process_elf.c")

# 1. CR2 is latched before any handler that could fault or be preempted.
cr2_at = panic.find("mov %%cr2")
selftest_at = panic.find("user_mode_selftest_handle_exception(exception_number")
check(0 <= cr2_at < selftest_at, "CR2 must be captured before other exception handling")

# 2. Demand paging only ever runs for faults that came from ring 3.
user_block = re.search(r"if \(!fatal_machine_exception && exception_number != 2U &&\s*frame && \(frame->cs & 3U\) == 3U\)\s*\{(.*?)\n    uintptr_t fault_address", panic, re.S)
check(user_block is not None, "user fault block not found")
if user_block:
    check("vm_handle_user_fault" in user_block.group(1), "vm_handle_user_fault must live in the CPL3 block")
    check(re.search(r"if \(resolved == VM_FAULT_RESOLVED\)\s*return 1;", user_block.group(1)),
          "a resolved fault must return 1 (restart) and nothing else may")
    check(user_block.group(1).count("return 1;") == 1, "only the resolved-fault path may return 1")
check(panic.count("vm_handle_user_fault") == 1, "vm_handle_user_fault must have exactly one call site")
check("kernel_panic(" in panic[panic.rfind("uintptr_t fault_address"):], "kernel-mode faults must still reach kernel_panic")

# 3. Only a resolved demand fault may skip the reschedule path.
check(len(re.findall(r"cmp eax, 1\s*\n\s*je %%resume", asm)) == 2, "both exception stub macros need the resume branch")
check(len(re.findall(r"^%%resume:", asm, re.M)) == 2, "resume label missing")
check("exception_handler" in asm and "return 1;" in panic, "exception_handler must report resolved faults")

# 4. Demand pages are never executable and are tagged for reclaim.
dm = body(mapping, "paging_demand_result_t paging_map_user_page_demand(")
leaf = re.search(r"\*entry = (.*?);", dm, re.S)
check(leaf is not None and all(f in leaf.group(1) for f in ("PAGE_PRESENT", "PAGE_USER", "PAGE_NX", "PAGE_SOFT_DEMAND")),
      "demand leaf must be PRESENT|USER|NX|SOFT_DEMAND")
check("IDENTITY_MAP_LIMIT" in dm, "demand frames must be below the identity-map ceiling")
check("paging_tlb_shootdown" not in dm, "demand install must not take the shootdown path (not-present entries are never cached)")
reclaim = body(mapping, "uint32_t paging_free_detached_user_tables(")
check("PAGE_SOFT_DEMAND" in reclaim and "PAGE_SOFT_SHARED" in reclaim and
      "vm_page_cache_release(frame)" in reclaim and "pmm_free_block((void*)frame)" in reclaim,
      "reclaim must release private frames and shared-cache references by leaf ownership")

# 5. Destruction detaches tables under the paging lock and frees them after.
destroy = body(paging, "int paging_destroy_address_space_counted(")
check(destroy.find("paging_detach_user_page_tables") < destroy.find("paging_lock_release(saved_flags);\n\n    uint32_t freed"),
      "page tables must be detached before the paging lock is dropped")
check(destroy.find("paging_free_detached_user_tables") > destroy.rfind("paging_lock_release"),
      "frame reclaim must run after the paging lock is released")
check("active_cpu_mask" in destroy and "pending_shootdowns" in destroy,
      "destroy must keep refusing live/shootdown-pending address spaces")

# 5a. User page-table mutation is isolated per address space. The global lock
# only pins lifetime; unrelated address spaces must not hold it across walks.
space_lock = body(paging, "int paging_space_lock_acquire(")
space_unlock = body(paging, "void paging_space_lock_release(")
check("active_operations" in space_lock and "paging_mutation_lock" not in space_lock,
      "address-space pinning must be a short lifetime operation")
check("page_table_lock" in space_lock and "active_operations" in space_unlock,
      "per-space lock must remain pinned through unlock")
check("active_operations" in destroy,
      "destroy must refuse while an address-space operation is active")
check("paging_lock_acquire" not in mapping and
      mapping.count("paging_space_lock_acquire(address_space") >= 5,
      "user mapping paths must use per-address-space synchronization")
range_release = body(mapping, "int paging_release_user_demand_range(")
check(range_release.find("paging_space_lock_acquire") <
      range_release.find("leaf_present(address_space, va, &entry)"),
      "demand range validation must hold the address-space lock")
check(range_release.find("spinlock_unlock_irqrestore") <
      range_release.find("paging_tlb_shootdown") <
      range_release.find("spinlock_lock_irqsave"),
      "PTE lock must be dropped during TLB wait and reacquired afterward")
check(range_release.rfind("paging_space_lock_release") >
      range_release.find("pmm_free_block((void*)(uintptr_t)frame)"),
      "address-space lifetime pin must cover shootdown and frame reclaim")

# 6. Security checks stay in place.
check("VM_PROT_WRITE | VM_PROT_EXEC" in region, "W^X check missing from region validation")
check("VM_PF_RESERVED" in region and "VM_PF_USER" in region, "reserved-bit/user-bit checks missing")
check("VM_FAULT_NOT_DEMAND" in region, "populated regions must not be demand-filled")
check("VM_BACKING_FILE" in region and "vm_space_add_file_region" in region,
      "file-backed regions must validate and retain bounded image ranges")
check("VM_BACKING_FILE" in fault and "vm_page_cache_acquire_file_page" in fault and
      "paging_map_user_page_shared" in fault,
      "read-only file-backed faults must use shared cache mappings")
check("references" in cache and "cache_frame_matches_view" in cache and
      "VM_PAGE_CACHE_MAX_ENTRIES" in cache and "vm_page_cache_reclaim" in cache,
      "page cache must deduplicate full pages, reference-count mappings and stay bounded")
check(cache.find("cache_find_locked(hash, &view)") < cache.find("pmm_alloc_block_below"),
      "a cache hit must not require a spare physical frame")
check("(*link)->references == 0U" in cache and "entry->references++" in cache,
      "LRU eviction must skip active mappings and cache hits must acquire references")
shared_map = body(mapping, "paging_demand_result_t paging_map_user_page_shared(")
shared_leaf = re.search(r"\*entry = (.*?);", shared_map, re.S)
check(shared_leaf is not None and "PAGE_SOFT_SHARED" in shared_leaf.group(1) and
      "PAGE_WRITE" not in shared_leaf.group(1),
      "shared cache PTEs must be software-owned, read-only leaves")
check("PAGE_SOFT_SHARED" in mapping and "vm_page_cache_release" in mapping,
      "address-space teardown must release shared-cache mapping references")
unmap_user = body(mapping, "int paging_unmap_user_page(")
check("PAGE_SOFT_DEMAND | PAGE_SOFT_SHARED" in unmap_user,
      "generic unmap must reject page-table-owned and shared-cache leaves")
check("vm_space_add_file_region" in elf and "retained_image" in elf,
      "non-executable ELF data segments must use retained lazy backing")
check("const void* image_backing = process->address_space->vm.image_backing" in read("kernel/proc/process_user.c") and
      "kfree((void*)image_backing)" in read("kernel/proc/process_user.c"),
      "retained ELF backing must be freed only after address-space teardown")
check("region.type == VM_REGION_GUARD" in region, "guard regions must be rejected")
check("pmm_alloc_block_below" in fault, "demand frames must come from the identity-mapped range")
check("vm_page_cache_reclaim(VM_PAGE_CACHE_MAX_ENTRIES)" in fault,
      "demand OOM must reclaim idle file-cache pages before failing")
check("for (size_t i = 0U; i < PAGE_SIZE / sizeof(uint64_t); i++)" in fault and
      fault.find("words[i] = 0U") < fault.find("paging_demand_result_t mapped"),
      "private frames must be zeroed before they are mapped")
check("cache_zero_page(candidate)" in cache and
      cache.find("cache_zero_page(candidate)") < cache.find("vm_region_copy_file_page"),
      "shared file-cache candidates must be zeroed before source bytes are copied")
cache_view = body(cache, "static uint8_t cache_view_byte(")
check("destination_offset" in cache_view and "copy_bytes" in cache_view and
      "return 0U" in cache_view and "region->file_prefix" in cache,
      "page-cache identity must include zero-filled prefix and file tail bytes")

# 7. The ELF loader reserves a guard below the stack and rejects segments overlapping it.
check("VM_REGION_GUARD" in elf and "page_end > guard_base" in elf, "ELF loader must reserve and respect the stack guard")

# 8. Generated ring-3 program header is current.
result = subprocess.run([sys.executable, str(ROOT / "scripts/gen_vm_selftest_programs.py"),
                         "--check", str(ROOT / "os/selftest/vm_selftest_programs.h")])
if result.returncode != 0:
    with tempfile.NamedTemporaryFile(suffix=".h") as generated:
        subprocess.run([sys.executable, str(ROOT / "scripts/gen_vm_selftest_programs.py"),
                        generated.name], check=True)
        expected = (ROOT / "os/selftest/vm_selftest_programs.h").read_text().splitlines(True)
        actual = pathlib.Path(generated.name).read_text().splitlines(True)
        sys.stdout.writelines(difflib.unified_diff(
            expected, actual, fromfile="committed header", tofile="NASM output"))
    failures.append("os/selftest/vm_selftest_programs.h is stale")

for failure in failures:
    print("FAIL:", failure)
if failures:
    sys.exit(1)
print("demand paging structure checks passed")
