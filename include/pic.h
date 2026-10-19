/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#ifndef PIC_H
#define PIC_H
void pic_remap(void);
void pic_send_eoi(unsigned int irq);
void pic_mask_all(void);
#endif
