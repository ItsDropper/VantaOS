#include "user_mode.h"
#include "process.h"

static int initialized;

void user_mode_initialize(void)
{
    /*
     * Ring-3 entry is intentionally kept behind this boundary. The
     * scheduler/process layer can now create independent task contexts;
     * the architecture-specific privilege transition is installed next.
     */
    initialized = 1;
}

int user_mode_start_test_process(void)
{
    if (!initialized)
        return -1;

    return -1;
}
