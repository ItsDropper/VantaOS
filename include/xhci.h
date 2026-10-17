#ifndef XHCI_H
#define XHCI_H

/* Initialize the first usable PCI xHCI host controller. Safe to call once. */
void xhci_initialize(void);

/* True after controller reset, rings, and Run/Stop have completed. */
int xhci_controller_online(void);

/* Bit N is set when a port-status-change event was observed for port N. */
unsigned int xhci_take_port_events(void);

#endif
