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
check(re.search(r"if \(\(pt\[k\] & PAGE_PRESENT\) && \(pt\[k\] & PAGE_SOFT_DEMAND\)\)\s*\{\s*pmm_free_block", reclaim),
      "reclaim must free only PAGE_SOFT_DEMAND leaves")

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
check(range_release.find("paging_space_lock_release") >
      range_release.find("pmm_free_block((void*)(uintptr_t)frame)"),
      "demand frame release must remain serialized until shootdown and reclaim")

# 6. Security checks stay in place.
check("VM_PROT_WRITE | VM_PROT_EXEC" in region, "W^X check missing from region validation")
check("VM_PF_RESERVED" in region and "VM_PF_USER" in region, "reserved-bit/user-bit checks missing")
check("VM_FAULT_NOT_DEMAND" in region, "populated regions must not be demand-filled")
check("region.type == VM_REGION_GUARD" in region, "guard regions must be rejected")
check("pmm_alloc_block_below" in fault, "demand frames must come from the identity-mapped range")
check("for (size_t i = 0U; i < PAGE_SIZE / sizeof(uint64_t); i++)" in fault and
      fault.find("words[i] = 0U") < fault.find("paging_map_user_page_demand(\n        space"),
      "frames must be zeroed before they are mapped")

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
