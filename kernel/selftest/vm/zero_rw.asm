%include "common.inc"
; Touch `count` anonymous pages. Even pages read first (read-fault path must
; map a zero page), odd pages write first (write-fault path). Every page must
; read zero at both ends before any value is stored, keep its own pattern, and
; never alias another page or another process.
    mov r15d, 0                  ; patched: per-process pattern (offset 2)
    mov r14d, 0                  ; patched: page count (offset 8)
    mov rbx, ANON
    xor ecx, ecx
.touch:
    cmp ecx, r14d
    jae .verify_start
    mov rdx, rcx
    shl rdx, 12
    mov eax, ecx
    xor eax, r15d
    test ecx, 1
    jnz .write_first
    cmp qword [rbx+rdx+8], 0
    jne .fail_nonzero
.write_first:
    test ecx, 1
    jz .store
    mov [rbx+rdx+8], rax         ; first touch is a write fault
.store:
    cmp qword [rbx+rdx+4088], 0
    jne .fail_nonzero
    mov [rbx+rdx+8], rax
    mov [rbx+rdx+4088], rax
    inc ecx
    jmp .touch
.verify_start:
    xor ecx, ecx
.verify:
    cmp ecx, r14d
    jae .ok
    mov rdx, rcx
    shl rdx, 12
    mov eax, ecx
    xor eax, r15d
    cmp [rbx+rdx+8], rax
    jne .fail_pattern
    cmp [rbx+rdx+4088], rax
    jne .fail_pattern
    inc ecx
    jmp .verify
.ok:
    EXIT 0
.fail_nonzero:
    EXIT 1
.fail_pattern:
    EXIT 2
