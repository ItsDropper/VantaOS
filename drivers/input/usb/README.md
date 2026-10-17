# USB input drivers

USB input drivers belong here, separated by transport and HID class:

1. xHCI controller support will own PCI discovery, controller setup, transfer
   rings, DMA, and controller events.
2. HID support will enumerate keyboard/mouse interfaces and parse report
   descriptors.
3. USB drivers will register devices with the bus-neutral input core and emit
   normalized events.

No USB input driver is implemented yet. The current PS/2 drivers remain the
active path under drivers/input/ps/2/. The empty Makefile manifest is
intentional: do not report USB input support until enumeration and reports work.
