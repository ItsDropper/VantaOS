#ifndef PIC_H
#define PIC_H
void pic_remap(void);
void pic_send_eoi(unsigned int irq);
void pic_mask_all(void);
#endif
