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
/* Re-enable the 8259 inputs unmasked by pic_remap() (timer, keyboard, mouse). */
void pic_restore_default_masks(void);
/* Handle 8259 vectors that have no dedicated handler (spurious IRQ7/IRQ15). */
void pic_handle_unexpected_irq(unsigned int vector);
#endif
