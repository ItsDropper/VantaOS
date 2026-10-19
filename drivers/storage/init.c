/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#include "storage_init.h"

#include "ata.h"
#include "nvme.h"

/* Keep storage-device startup and registration order inside the storage layer. */
void storage_initialize(void)
{
    ata_initialize();
    nvme_initialize();
}
