#ifndef XHCI_H
#define XHCI_H

/* Initialize the first usable PCI xHCI host controller. Safe to call once. */
void xhci_initialize(void);
int xhci_submit_command(uint32_t type, uint64_t parameter,
                        uint32_t status, uint32_t control_flags,
                        uint32_t* completion_code, uint32_t* slot_id);
uint32_t xhci_port_count(void);
uint32_t xhci_port_status(uint32_t port);
int xhci_reset_port(uint32_t port);

/* True after controller reset, rings, and Run/Stop have completed. */
int xhci_controller_online(void);

/* Bit N is set when a port-status-change event was observed for port N. */
unsigned int xhci_take_port_events(void);

#endif
