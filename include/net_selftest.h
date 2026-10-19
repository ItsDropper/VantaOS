/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#ifndef VANTAOS_NET_SELFTEST_H
#define VANTAOS_NET_SELFTEST_H

#ifdef VANTAOS_SELF_TEST
/* Networking-owned regression entry point; absent from production builds. */
void network_framework_selftest(void);
#endif

#endif
