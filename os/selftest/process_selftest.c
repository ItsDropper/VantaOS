/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

/*
 * Test-only kernel regression probes. This translation unit is empty in
 * ordinary builds; VANTAOS_SELF_TEST enables the test implementation.
 */
#include "desktop_process.h"
#include "filesystem.h"
#include "fat32.h"
#include "heap.h"
#include "graphics.h"
#include "gui_server.h"
#include "keyboard.h"
#include "mouse.h"
#include "process.h"
#include "cpu_local.h"
#include "smp.h"
#include "panic.h"
#include "spinlock.h"
#include "pmm.h"
#include "serial.h"
#include "scheduler.h"
#include "shell.h"
#include "terminal.h"
#include "terminal_process.h"
#include "vfs.h"

#ifdef VANTAOS_SELF_TEST
#include "selftest.h"

static uint32_t scheduler_probe_pid;

static void scheduler_probe_entry(void)
{
    serial_write("VANTAOS_SCHEDULER_PROBE_OK\n");
}

static void scheduler_reap_observer_entry(void)
{
    for (uint32_t attempt = 0; attempt < 1000U; attempt++)
    {
        process_t snapshot;
        if (!process_snapshot(scheduler_probe_pid, &snapshot))
        {
            serial_write("VANTAOS_SCHEDULER_REAP_OK\n");
            return;
        }

        __asm__ volatile ("hlt");
    }

    serial_write("VANTAOS_SCHEDULER_REAP_FAIL\n");
}

static void process_state_transition_probe(void)
{
    int pid = process_create_kernel(
        "state-transition-probe",
        process_current_pid(),
        scheduler_probe_entry
    );

    if (pid < 0)
    {
        serial_write("VANTAOS_PROCESS_STATE_TRANSITIONS_FAIL\n");
        return;
    }

    int ok = 1;
    process_t process;
    ok = process_snapshot((uint32_t)pid, &process) &&
         process.state == PROCESS_READY;

    if (ok)
        ok = process_set_running((uint32_t)pid);
    if (ok)
        ok = process_snapshot((uint32_t)pid, &process) &&
             process.state == PROCESS_RUNNING;

    if (ok)
        ok = process_mark_ready((uint32_t)pid);
    if (ok)
        ok = process_snapshot((uint32_t)pid, &process) &&
             process.state == PROCESS_READY;

    if (ok)
        ok = process_set_running((uint32_t)pid);
    if (ok)
        ok = process_mark_ready((uint32_t)pid);
    if (ok)
    {
        ok = process_snapshot((uint32_t)pid, &process) &&
             process.state == PROCESS_READY &&
             process.owner_cpu != PROCESS_CPU_UNOWNED;
        if (ok)
            ok = !process_claim_for_cpu(
                (uint32_t)pid, process_current_pid(),
                process.owner_cpu + 1U);
    }
    if (ok)
        ok = process_set_running((uint32_t)pid);
    if (ok)
    {
        process_block_current();
        ok = process_snapshot((uint32_t)pid, &process) &&
             process.state == PROCESS_SLEEPING &&
             process.owner_cpu != PROCESS_CPU_UNOWNED;
    }

    if (ok)
        ok = process_wake((uint32_t)pid);
    if (ok)
        ok = process_snapshot((uint32_t)pid, &process) &&
             process.state == PROCESS_READY;

    /* Restore the boot/desktop process before reaping the probe. */
    if (process_current_pid() != 1U)
        ok = process_switch_to(1U) && ok;

    if (process_snapshot((uint32_t)pid, &process))
        ok = process_terminate((uint32_t)pid) && ok;

    if (!ok || process_snapshot((uint32_t)pid, &process))
    {
        serial_write("VANTAOS_PROCESS_STATE_TRANSITIONS_FAIL\n");
        return;
    }

    serial_write("VANTAOS_PROCESS_STATE_TRANSITIONS_OK\n");
}



/*
 * State-machine regression for the scheduler's deferred ownership release.
 * The runtime IRQ/assembly path is separately exercised by QEMU scheduling
 * and reap-observer probes; this probe checks the table transition itself.
 */
static void process_deferred_switch_probe(void)
{
    uint32_t cpu_id = process_current_cpu_id();
    int outgoing_pid = process_create_kernel(
        "deferred-outgoing-probe", process_current_pid(), scheduler_probe_entry);
    int incoming_pid = process_create_kernel(
        "deferred-incoming-probe", process_current_pid(), scheduler_probe_entry);
    int ok = outgoing_pid > 0 && incoming_pid > 0;
    uint32_t failure_stage = ok ? 0U : 1U;

    if (ok)
    {
        ok = process_set_running((uint32_t)outgoing_pid);
        if (!ok) failure_stage = 2U;
    }
    if (ok)
    {
        ok = process_terminate((uint32_t)outgoing_pid);
        if (!ok) failure_stage = 3U;
    }
    uint64_t incoming_stack = 0;
    paging_address_space_t* incoming_address_space = NULL;
    if (ok)
    {
        ok = process_prepare_context_switch((uint32_t)incoming_pid,
                                            (uint32_t)outgoing_pid, cpu_id,
                                            &incoming_stack,
                                            &incoming_address_space) &&
             incoming_stack != 0U && incoming_address_space != NULL;
        if (!ok) failure_stage = 4U;
    }

    process_t incoming;
    if (ok)
    {
        ok = process_snapshot((uint32_t)incoming_pid, &incoming) &&
             incoming.state == PROCESS_RUNNING &&
             incoming.owner_cpu == cpu_id &&
             incoming.stack_pointer == incoming_stack &&
             incoming.address_space == incoming_address_space;
        if (!ok) failure_stage = 12U;
    }

    process_t outgoing;
    if (ok)
    {
        ok = process_snapshot((uint32_t)outgoing_pid, &outgoing) &&
             outgoing.state == PROCESS_TERMINATED &&
             outgoing.owner_cpu == cpu_id;
        if (!ok) failure_stage = 5U;
    }

    if (ok)
    {
        ok = process_finish_context_switch((uint32_t)outgoing_pid, cpu_id);
        if (!ok) failure_stage = 6U;
    }
    if (ok)
    {
        ok = !process_snapshot((uint32_t)outgoing_pid, &outgoing);
        if (!ok) failure_stage = 7U;
    }

    /* Restore the boot context before cleaning up the incoming test slot. */
    if (process_current_pid() != 1U)
    {
        int restored = process_switch_to(1U);
        if (!restored)
            restored = process_set_running(1U);
        ok = restored && ok;
        if (!restored && failure_stage == 0U) failure_stage = 8U;
    }

    if (outgoing_pid > 0 && process_current_pid() != (uint32_t)outgoing_pid)
    {
        if (process_snapshot((uint32_t)outgoing_pid, &outgoing) &&
            outgoing.state == PROCESS_TERMINATED &&
            outgoing.owner_cpu == cpu_id)
        {
            int finished = process_finish_context_switch(
                (uint32_t)outgoing_pid, cpu_id);
            ok = finished && ok;
            if (!finished && failure_stage == 0U) failure_stage = 9U;
        }
    }

    if (incoming_pid > 0)
    {
        process_t incoming;
        if (process_snapshot((uint32_t)incoming_pid, &incoming))
        {
            if (incoming.state != PROCESS_TERMINATED)
            {
                int terminated = process_terminate((uint32_t)incoming_pid);
                ok = terminated && ok;
                if (!terminated && failure_stage == 0U) failure_stage = 10U;
            }

            /*
             * process_terminate reaps immediately when the slot is unowned.
             * A missing record after that call is therefore the expected
             * successful cleanup path, not a failed snapshot.
             */
            if (process_snapshot((uint32_t)incoming_pid, &incoming))
            {
                int reaped = incoming.state == PROCESS_TERMINATED &&
                             incoming.owner_cpu == PROCESS_CPU_UNOWNED &&
                             process_reap((uint32_t)incoming_pid);
                ok = reaped && ok;
                if (!reaped && failure_stage == 0U) failure_stage = 11U;
            }
        }
    }

    if (!ok)
    {
        switch (failure_stage)
        {
            case 1U: serial_write("VANTAOS_PROCESS_DEFERRED_FAIL_CREATE\n"); break;
            case 2U: serial_write("VANTAOS_PROCESS_DEFERRED_FAIL_SET_RUNNING\n"); break;
            case 3U: serial_write("VANTAOS_PROCESS_DEFERRED_FAIL_TERMINATE\n"); break;
            case 4U: serial_write("VANTAOS_PROCESS_DEFERRED_FAIL_PREPARE\n"); break;
            case 5U: serial_write("VANTAOS_PROCESS_DEFERRED_FAIL_OWNER_CHECK\n"); break;
            case 6U: serial_write("VANTAOS_PROCESS_DEFERRED_FAIL_FINISH\n"); break;
            case 7U: serial_write("VANTAOS_PROCESS_DEFERRED_FAIL_REAP_CHECK\n"); break;
            case 8U: serial_write("VANTAOS_PROCESS_DEFERRED_FAIL_RESTORE\n"); break;
            case 9U: serial_write("VANTAOS_PROCESS_DEFERRED_FAIL_CLEAN_OUTGOING\n"); break;
            case 10U: serial_write("VANTAOS_PROCESS_DEFERRED_FAIL_TERMINATE_INCOMING\n"); break;
            case 11U: serial_write("VANTAOS_PROCESS_DEFERRED_FAIL_REAP_INCOMING\n"); break;
            case 12U: serial_write("VANTAOS_PROCESS_DEFERRED_FAIL_INCOMING_OWNER\n"); break;
            default: serial_write("VANTAOS_PROCESS_DEFERRED_FAIL_UNKNOWN\n"); break;
        }
    }

    serial_write(ok ?
        "VANTAOS_PROCESS_DEFERRED_SWITCH_OK\n" :
        "VANTAOS_PROCESS_DEFERRED_SWITCH_FAIL\n");
}


static void process_owned_terminated_reuse_probe(void)
{
    int pid = process_create_kernel(
        "owned-terminated-probe",
        process_current_pid(),
        scheduler_probe_entry
    );
    if (pid < 0)
    {
        serial_write("VANTAOS_PROCESS_OWNED_REUSE_FAIL\n");
        return;
    }

    int ok = process_set_running((uint32_t)pid);
    if (ok)
        ok = process_terminate((uint32_t)pid);

    process_t snapshot;
    if (ok)
        ok = process_snapshot((uint32_t)pid, &snapshot) &&
             snapshot.state == PROCESS_TERMINATED &&
             snapshot.owner_cpu != PROCESS_CPU_UNOWNED;

    int replacement = process_create_kernel(
        "process-slot-reuse-probe",
        process_current_pid(),
        scheduler_probe_entry
    );
    if (replacement < 0 || replacement == pid)
        ok = 0;

    /*
     * Restore the boot process before cleaning up either slot. The fallback
     * is only for a failed regression: it avoids leaving the probe as the
     * logical current process and contaminating later tests.
     */
    if (process_current_pid() != 1U)
    {
        int restored = process_switch_to(1U);
        if (!restored)
            restored = process_set_running(1U);
        ok = restored && ok;
    }

    if (process_snapshot((uint32_t)pid, &snapshot))
    {
        if (snapshot.state == PROCESS_TERMINATED)
            ok = process_reap((uint32_t)pid) && ok;
        else
            ok = process_terminate((uint32_t)pid) && ok;
    }

    if (replacement >= 0 && replacement != pid &&
        process_snapshot((uint32_t)replacement, &snapshot))
    {
        if (snapshot.state == PROCESS_TERMINATED)
            ok = process_reap((uint32_t)replacement) && ok;
        else
            ok = process_terminate((uint32_t)replacement) && ok;
    }

    serial_write(ok ?
        "VANTAOS_PROCESS_OWNED_REUSE_OK\n" :
        "VANTAOS_PROCESS_OWNED_REUSE_FAIL\n");
}


static int process_idle_lifecycle_probe(void)
{
    uint32_t checked = 0U;
    for (uint32_t cpu = 0; cpu < cpu_local_capacity(); cpu++)
    {
        const cpu_local_t* local = cpu_local_get(cpu);
        if (!local)
            continue;

        uint32_t idle_pid = __atomic_load_n(&local->idle_pid,
                                            __ATOMIC_ACQUIRE);
        if (idle_pid == 0U)
            continue;

        process_t before;
        process_t after;
        if (!process_snapshot(idle_pid, &before) ||
            process_terminate(idle_pid) ||
            process_reap(idle_pid) ||
            !process_snapshot(idle_pid, &after) ||
            after.pid != before.pid ||
            after.state != before.state ||
            after.owner_cpu != before.owner_cpu ||
            after.affinity_cpu != before.affinity_cpu ||
            after.stack_pointer != before.stack_pointer ||
            after.kernel_stack != before.kernel_stack ||
            after.address_space != before.address_space ||
            __atomic_load_n(&local->idle_pid, __ATOMIC_ACQUIRE) != idle_pid)
        {
            serial_write("VANTAOS_IDLE_PROCESS_LIFECYCLE_FAIL\n");
            return 0;
        }
        checked++;
    }

    if (checked == 0U)
    {
        serial_write("VANTAOS_IDLE_PROCESS_LIFECYCLE_FAIL\n");
        return 0;
    }

    serial_write("VANTAOS_IDLE_PROCESS_LIFECYCLE_OK\n");
    return 1;
}

static int gui_expect_event(uint32_t pid, uint32_t window_id,
                             uint32_t expected_type)
{
    vanta_gui_event_t event;
    return gui_server_poll_event(pid, window_id, &event) == 1 &&
           event.type == expected_type;
}

static void gui_focus_input_regression_probe(void)
{
    if (!graphics_is_initialized() ||
        graphics_get_width() < 400U || graphics_get_height() < 300U)
    {
        serial_write("VANTAOS_GUI_INPUT_REGRESSION_FAIL_GRAPHICS\n");
        return;
    }

    uint32_t pid = process_current_pid();
    vanta_gui_window_request_t first_request = {
        .width = 240U, .height = 160U, .title = "GUI Probe A"
    };
    vanta_gui_window_request_t second_request = {
        .width = 240U, .height = 160U, .title = "GUI Probe B"
    };
    int first = gui_server_create_window(pid, &first_request);
    if (first <= 0)
    {
        serial_write("VANTAOS_GUI_INPUT_REGRESSION_FAIL_FIRST_WINDOW\n");
        return;
    }

    int ok = gui_expect_event(pid, (uint32_t)first,
                              VANTA_GUI_EVENT_FOCUS_GAINED);
    int second = gui_server_create_window(pid, &second_request);
    ok = second > 0 && ok;
    if (second > 0)
    {
        ok = gui_expect_event(pid, (uint32_t)first,
                              VANTA_GUI_EVENT_FOCUS_LOST) && ok;
        ok = gui_expect_event(pid, (uint32_t)second,
                              VANTA_GUI_EVENT_FOCUS_GAINED) && ok;
        ok = gui_server_close_window(pid, (uint32_t)second) == 0 && ok;
        ok = gui_expect_event(pid, (uint32_t)first,
                              VANTA_GUI_EVENT_FOCUS_GAINED) && ok;
    }

    const int32_t window_x =
        ((int32_t)graphics_get_width() - (int32_t)first_request.width) / 2;
    const int32_t window_y =
        ((int32_t)graphics_get_height() - (int32_t)first_request.height) / 2;
    if (ok)
    {
        ok = gui_server_mouse_click(window_x + 40, window_y + 80, 1) == 1;
        vanta_gui_event_t event;
        ok = gui_server_poll_event(pid, (uint32_t)first, &event) == 1 &&
             event.type == VANTA_GUI_EVENT_POINTER_DOWN &&
             event.x == 32 && event.y == 30 && event.value == 1U && ok;
        ok = gui_server_poll_event(pid, (uint32_t)first, NULL) == -14 && ok;
        ok = gui_server_mouse_click(window_x + 40, window_y + 80, 3) == 1 && ok;
        ok = gui_server_poll_event(pid, (uint32_t)first, &event) == 1 &&
             event.type == VANTA_GUI_EVENT_POINTER_DOWN &&
             event.value == 3U && ok;
    }

    if (ok)
    {
        for (int i = 0; i < 40; i++)
            gui_server_mouse_move(window_x + 20 + i, window_y + 70);

        vanta_gui_event_t event;
        int count = 0;
        while (gui_server_poll_event(pid, (uint32_t)first, &event) == 1)
        {
            if (event.type != VANTA_GUI_EVENT_POINTER_MOVE ||
                (count == 0 && event.x != 20) ||
                (count == 31 && event.x != 51))
                ok = 0;
            count++;
        }
        ok = count == 32 && ok;
    }

    if (ok)
    {
        ok = gui_server_set_owner_visible(pid, 0) == 1 && ok;
        ok = gui_expect_event(pid, (uint32_t)first,
                              VANTA_GUI_EVENT_FOCUS_LOST) && ok;
        ok = gui_server_mouse_click(window_x + 40, window_y + 80, 1) == 0 && ok;
        ok = gui_server_set_owner_visible(pid, 1) == 1 && ok;
        ok = gui_expect_event(pid, (uint32_t)first,
                              VANTA_GUI_EVENT_FOCUS_GAINED) && ok;
        gui_server_notify_theme_changed();
        ok = gui_expect_event(pid, (uint32_t)first,
                              VANTA_GUI_EVENT_THEME_CHANGED) && ok;
    }

    if (ok)
    {
        ok = gui_server_mouse_click(
            window_x + (int32_t)first_request.width - 20,
            window_y + 20, 1) == 1;
        ok = gui_expect_event(pid, (uint32_t)first,
                              VANTA_GUI_EVENT_CLOSE_REQUEST) && ok;
    }

    if (second > 0)
        ok = gui_server_close_window(pid, (uint32_t)second) == -9 && ok;
    ok = gui_server_close_window(pid, (uint32_t)first) == 0 && ok;
    ok = gui_server_poll_event(pid, (uint32_t)first, &((vanta_gui_event_t){0})) == -9 && ok;

    serial_write(ok ?
        "VANTAOS_GUI_INPUT_REGRESSION_OK\n" :
        "VANTAOS_GUI_INPUT_REGRESSION_FAIL\n");
}

static void gui_dead_owner_cleanup_probe(void)
{
    int pid = process_create_kernel(
        "gui-cleanup-probe", process_current_pid(), scheduler_probe_entry);
    if (pid <= 0)
    {
        serial_write("VANTAOS_GUI_DEAD_OWNER_CLEANUP_FAIL_CREATE\n");
        return;
    }

    vanta_gui_window_request_t request = {
        .width = 240U,
        .height = 160U,
        .title = "Cleanup Probe"
    };
    int window = gui_server_create_window((uint32_t)pid, &request);
    if (window <= 0)
    {
        (void)process_terminate((uint32_t)pid);
        serial_write("VANTAOS_GUI_DEAD_OWNER_CLEANUP_FAIL_WINDOW\n");
        return;
    }

    if (!process_terminate((uint32_t)pid))
    {
        (void)gui_server_close_window((uint32_t)pid, (uint32_t)window);
        serial_write("VANTAOS_GUI_DEAD_OWNER_CLEANUP_FAIL_TERMINATE\n");
        return;
    }

    gui_server_cleanup_dead_windows();
    vanta_gui_event_t event;
    if (gui_server_poll_event((uint32_t)pid, (uint32_t)window, &event) != -9)
    {
        (void)gui_server_close_window((uint32_t)pid, (uint32_t)window);
        serial_write("VANTAOS_GUI_DEAD_OWNER_CLEANUP_FAIL_RETAINED\n");
        return;
    }

    serial_write("VANTAOS_GUI_DEAD_OWNER_CLEANUP_OK\n");
}

void os_selftest_initialize(void)
{
    if (!process_idle_lifecycle_probe())
        return;
    memory_selftest_initialize();
    filesystem_selftest_initialize();
    process_state_transition_probe();
    process_deferred_switch_probe();
    process_owned_terminated_reuse_probe();
    gui_focus_input_regression_probe();
    gui_dead_owner_cleanup_probe();

    if (process_create_kernel(NULL, process_current_pid(),
                              scheduler_probe_entry) != -1 ||
        process_create_kernel("invalid-entry", process_current_pid(),
                              NULL) != -1 ||
        process_create_kernel_on_cpu("invalid-cpu", process_current_pid(),
                                     scheduler_probe_entry,
                                     CPU_LOCAL_MAX) != -1 ||
        process_switch_to(0) != 0 ||
        process_switch_to(PROCESS_MAX) != 0 ||
        process_set_running(PROCESS_MAX) != 0 ||
        process_mark_ready(0) != 0 ||
        process_terminate(0) != 0 ||
        process_wake(PROCESS_MAX) != 0)
    {
        serial_write("VANTAOS_PROCESS_API_GUARDS_FAIL\n");
        return;
    }

    serial_write("VANTAOS_PROCESS_API_GUARDS_OK\n");

    int probe_pid = process_create_kernel(
        "scheduler-probe-name-that-exceeds-the-process-name-limit",
        process_current_pid(),
        scheduler_probe_entry
    );

    if (probe_pid < 0)
    {
        serial_write("VANTAOS_SCHEDULER_PROBE_CREATE_FAIL\n");
        return;
    }

    scheduler_probe_pid = (uint32_t)probe_pid;
    process_t probe;
    if (!process_snapshot(scheduler_probe_pid, &probe) ||
        probe.name[PROCESS_NAME_MAX] != '\0')
    {
        serial_write("VANTAOS_PROCESS_NAME_FAIL\n");
        return;
    }

    serial_write("VANTAOS_PROCESS_NAME_OK\n");

    if (process_create_kernel(
            "sched-reap-check",
            process_current_pid(),
            scheduler_reap_observer_entry
        ) < 0)
    {
        serial_write("VANTAOS_SCHEDULER_OBSERVER_CREATE_FAIL\n");
    }
}


#endif
