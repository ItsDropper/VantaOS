#include "net.h"
#include "spinlock.h"
#define NET_DRIVER_NAME_MAX 32U
typedef struct { const struct pci_device* device; const net_pci_driver_t* driver; } net_binding_t;
static const net_pci_driver_t* registered_drivers[NET_PCI_DRIVER_MAX];
static uint32_t registered_driver_count;
static net_binding_t bindings[NET_PCI_BINDING_MAX];
static uint32_t binding_count;
static spinlock_t driver_lock = SPINLOCK_INITIALIZER;
static int valid_driver(const net_pci_driver_t* d) {
    if (!d || !d->name || !d->name[0] || !d->ids || !d->id_count || !d->probe) return 0;
    for (uint32_t i=0;i<NET_DRIVER_NAME_MAX;i++) if (!d->name[i]) return 1;
    return 0;
}
int net_pci_driver_register(const net_pci_driver_t* d) {
    if (!valid_driver(d)) return 0;
    uint64_t f=spinlock_lock_irqsave(&driver_lock);
    for(uint32_t i=0;i<registered_driver_count;i++) if(registered_drivers[i]==d) {
        spinlock_unlock_irqrestore(&driver_lock,f); return 0;
    }
    if(registered_driver_count==NET_PCI_DRIVER_MAX) {
        spinlock_unlock_irqrestore(&driver_lock,f); return 0;
    }
    registered_drivers[registered_driver_count++]=d;
    spinlock_unlock_irqrestore(&driver_lock,f); return 1;
}
static int matches(const net_pci_id_t* id,const struct pci_device* d) {
    return (id->vendor_id==0xFFFFU||id->vendor_id==d->vendor_id) &&
           (id->device_id==0xFFFFU||id->device_id==d->device_id) &&
           (id->class_code==0xFFU||id->class_code==d->class_code) &&
           (id->subclass==0xFFU||id->subclass==d->subclass) &&
           (id->prog_if==0xFFU||id->prog_if==d->prog_if);
}
static int bound(const struct pci_device* d) {
    for(uint32_t i=0;i<binding_count;i++) if(bindings[i].device==d) return 1;
    return 0;
}
uint32_t net_probe_pci_devices(void) {
    const net_pci_driver_t* snapshot[NET_PCI_DRIVER_MAX];
    uint64_t f=spinlock_lock_irqsave(&driver_lock);
    uint32_t count=registered_driver_count;
    for(uint32_t i=0;i<count;i++) snapshot[i]=registered_drivers[i];
    spinlock_unlock_irqrestore(&driver_lock,f);
    uint32_t added=0;
    for(int p=0;p<pci_get_device_count();p++) {
        const struct pci_device* dev=pci_get_device(p);
        if(!dev||dev->class_code!=0x02U) continue;
        f=spinlock_lock_irqsave(&driver_lock); int claimed=bound(dev); spinlock_unlock_irqrestore(&driver_lock,f);
        if(claimed) continue;
        for(uint32_t i=0;i<count;i++) {
            const net_pci_driver_t* drv=snapshot[i]; int match=0;
            for(uint32_t j=0;j<drv->id_count;j++) if(matches(&drv->ids[j],dev)){match=1;break;}
            if(!match) continue;
            net_pci_probe_result_t result=drv->probe(dev);
            if(result==NET_PCI_PROBE_FAILED) break;
            if(result!=NET_PCI_PROBE_BOUND) continue;
            f=spinlock_lock_irqsave(&driver_lock);
            if(binding_count<NET_PCI_BINDING_MAX&&!bound(dev)) {
                bindings[binding_count++]=(net_binding_t){dev,drv}; ++added;
            }
            spinlock_unlock_irqrestore(&driver_lock,f); break;
        }
    }
    return added;
}
const char* net_pci_bound_driver(const struct pci_device* dev) {
    if(!dev) return 0;
    uint64_t f=spinlock_lock_irqsave(&driver_lock); const char* name=0;
    for(uint32_t i=0;i<binding_count;i++) if(bindings[i].device==dev){name=bindings[i].driver->name;break;}
    spinlock_unlock_irqrestore(&driver_lock,f); return name;
}

size_t net_pci_poll_devices(void) {
    const net_pci_driver_t* snapshot[NET_PCI_BINDING_MAX];
    uint32_t n=0;uint64_t f=spinlock_lock_irqsave(&driver_lock);
    for(uint32_t i=0;i<binding_count;i++)
        if(bindings[i].driver->poll)snapshot[n++]=bindings[i].driver;
    spinlock_unlock_irqrestore(&driver_lock,f);
    size_t work=0;
    for(uint32_t i=0;i<n;i++) {
        int result=snapshot[i]->poll();
        if(result>0)work+=(size_t)result;
    }
    return work;
}
