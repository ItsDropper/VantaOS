#ifndef USB_H
#define USB_H

/* Initialize root-port USB HID boot-protocol devices after input_initialize(). */
void usb_initialize(void);

/* Poll xHCI completions and deliver ready HID reports. */
void usb_poll(void);

#endif
