#include "interrupts.h"
#include "idt.h"
#include "pic.h"
#include "timer.h"
#include "keyboard.h"
#include "mouse.h"
#include "apic.h"
#include "cpu_local.h"
#include "process.h"
#include "scheduler.h"
#include "smp.h"

extern void irq0_stub(void);
extern void irq1_stub(void);
extern void irq12_stub(void);
extern void apic_spurious_stub(void);
extern void smp_wakeup_stub(void);
extern void lapic_timer_stub(void);
extern void reschedule_ipi_stub(void);
extern void tlb_shootdown_ipi_stub(void);
extern void user_mode_syscall_stub(void);

#define DECLARE_EXCEPTION(n) extern void exception##n##_stub(void);
DECLARE_EXCEPTION(0) DECLARE_EXCEPTION(1) DECLARE_EXCEPTION(2)
DECLARE_EXCEPTION(3) DECLARE_EXCEPTION(4) DECLARE_EXCEPTION(5)
DECLARE_EXCEPTION(6) DECLARE_EXCEPTION(7) DECLARE_EXCEPTION(8)
DECLARE_EXCEPTION(9) DECLARE_EXCEPTION(10) DECLARE_EXCEPTION(11)
DECLARE_EXCEPTION(12) DECLARE_EXCEPTION(13) DECLARE_EXCEPTION(14)
DECLARE_EXCEPTION(15) DECLARE_EXCEPTION(16) DECLARE_EXCEPTION(17)
DECLARE_EXCEPTION(18) DECLARE_EXCEPTION(19) DECLARE_EXCEPTION(20)
DECLARE_EXCEPTION(21) DECLARE_EXCEPTION(22) DECLARE_EXCEPTION(23)
DECLARE_EXCEPTION(24) DECLARE_EXCEPTION(25) DECLARE_EXCEPTION(26)
DECLARE_EXCEPTION(27) DECLARE_EXCEPTION(28) DECLARE_EXCEPTION(29)
DECLARE_EXCEPTION(30) DECLARE_EXCEPTION(31)

#define IRQ0_VECTOR 32
#define IRQ1_VECTOR 33
#define IRQ12_VECTOR 44

static void install_exceptions(void)
{
    void (*stubs[32])(void) = {
        exception0_stub, exception1_stub, exception2_stub, exception3_stub,
        exception4_stub, exception5_stub, exception6_stub, exception7_stub,
        exception8_stub, exception9_stub, exception10_stub, exception11_stub,
        exception12_stub, exception13_stub, exception14_stub, exception15_stub,
        exception16_stub, exception17_stub, exception18_stub, exception19_stub,
        exception20_stub, exception21_stub, exception22_stub, exception23_stub,
        exception24_stub, exception25_stub, exception26_stub, exception27_stub,
        exception28_stub, exception29_stub, exception30_stub, exception31_stub
    };

    for (int i = 0; i < 32; i++)
        idt_set_gate(i, (uint64_t)(uintptr_t)stubs[i], 0x08, 0x8E);
}

void interrupts_initialize(void)
{
    idt_initialize();
    install_exceptions();

    idt_set_gate(IRQ0_VECTOR, (uint64_t)(uintptr_t)irq0_stub, 0x08, 0x8E);
    idt_set_gate(IRQ1_VECTOR, (uint64_t)(uintptr_t)irq1_stub, 0x08, 0x8E);
    idt_set_gate(IRQ12_VECTOR, (uint64_t)(uintptr_t)irq12_stub, 0x08, 0x8E);
    idt_set_gate(0xFF, (uint64_t)(uintptr_t)apic_spurious_stub, 0x08, 0x8E);
    idt_set_gate(0xF1, (uint64_t)(uintptr_t)smp_wakeup_stub, 0x08, 0x8E);
    idt_set_gate(0xF2, (uint64_t)(uintptr_t)lapic_timer_stub, 0x08, 0x8E);
    /* Reschedule IPIs are hardware-delivery only; user waits use the timer. */
    idt_set_gate(0xF3, (uint64_t)(uintptr_t)reschedule_ipi_stub, 0x08, 0x8E);
    idt_set_gate(0xF4, (uint64_t)(uintptr_t)tlb_shootdown_ipi_stub, 0x08, 0x8E);
    idt_set_gate(0x80, (uint64_t)(uintptr_t)user_mode_syscall_stub, 0x08, 0xEE);

    pic_remap();
    timer_initialize(100);
    idt_load();
}

uint64_t interrupt_handler(uint64_t interrupt_number, uint64_t saved_stack)
{
    switch (interrupt_number)
    {
        case IRQ1_VECTOR:
            keyboard_handle_interrupt();
            pic_send_eoi(1);
            break;
        case IRQ12_VECTOR:
            mouse_handle_interrupt();
            pic_send_eoi(12);
            break;
        default:
            if (interrupt_number >= 32 && interrupt_number <= 47)
                pic_send_eoi((unsigned int)(interrupt_number - 32));
            break;
    }

    return saved_stack;
}

uint64_t lapic_timer_handle_interrupt(uint64_t saved_stack)
{
    cpu_local_t* local = cpu_local_current();
    if (!local || local->self != local || local->online == 0U)
    {
        apic_send_eoi();
        return saved_stack;
    }

    __atomic_add_fetch(&local->lapic_timer_ticks, 1U, __ATOMIC_RELAXED);
    apic_send_eoi();

    if (__atomic_load_n(&local->scheduler_online, __ATOMIC_ACQUIRE) == 0U ||
        !scheduler_is_initialized() || !process_is_initialized())
        return saved_stack;

    if (process_current_pid() == 0U)
        return scheduler_start_cpu(saved_stack);

    /* The BSP still schedules from the legacy timer; APs use their LAPIC. */
    if (smp_cpu_count() > 1U && !smp_cpu_is_bsp(local->logical_id))
        return scheduler_tick(saved_stack);

    return saved_stack;
}

uint64_t reschedule_ipi_handle_interrupt(uint64_t saved_stack)
{
    cpu_local_t* local = cpu_local_current();
    int valid_local = local && local->self == local &&
                      local->online != 0U;
    /*
     * Publish the deferred request before publishing delivery to the BSP.
     * The BSP's test waits on reschedule_ipi_ticks, so reversing these stores
     * would let it observe delivery before the pending bit becomes visible.
     */
    if (!valid_local || !scheduler_is_initialized() ||
        !process_is_initialized() || process_current_pid() == 0U)
    {
        if (valid_local)
        {
            __atomic_store_n(&local->reschedule_pending, 1U,
                             __ATOMIC_RELEASE);
            __atomic_add_fetch(&local->reschedule_ipi_ticks, 1U,
                               __ATOMIC_RELEASE);
        }
        apic_send_eoi();
        return saved_stack;
    }

    __atomic_store_n(&local->reschedule_pending, 0U, __ATOMIC_RELEASE);
    __atomic_add_fetch(&local->reschedule_ipi_ticks, 1U, __ATOMIC_RELEASE);
    apic_send_eoi();
    return scheduler_reschedule(saved_stack);
}

unsigned int interrupts_get_ticks(void)
{
    return timer_get_ticks();
}
