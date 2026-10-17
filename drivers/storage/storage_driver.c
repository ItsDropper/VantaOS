#include "storage_driver.h"
#include "spinlock.h"
#define STORAGE_BINDING_MAX 64U
typedef struct { const struct pci_device* device; const storage_driver_t* driver; } binding_t;
static const storage_driver_t* drivers[STORAGE_DRIVER_MAX];
static uint32_t drivers_count;
static binding_t bindings[STORAGE_BINDING_MAX];
static uint32_t bindings_count;
static spinlock_t lock=SPINLOCK_INITIALIZER;
static int valid_name(const char* n) {
 if(!n||!n[0])return 0;
 for(uint32_t i=0;i<STORAGE_DRIVER_NAME_MAX;i++)if(!n[i])return 1;
 return 0;
}
int storage_driver_register(const storage_driver_t* d) {
 if(!d||!valid_name(d->name)||!d->ids||!d->id_count||!d->probe)return 0;
 uint64_t f=spinlock_lock_irqsave(&lock);
 for(uint32_t i=0;i<drivers_count;i++)if(drivers[i]==d){spinlock_unlock_irqrestore(&lock,f);return 0;}
 if(drivers_count==STORAGE_DRIVER_MAX){spinlock_unlock_irqrestore(&lock,f);return 0;}
 drivers[drivers_count++]=d; spinlock_unlock_irqrestore(&lock,f); return 1;
}
uint32_t storage_driver_count(void) {
 uint64_t f=spinlock_lock_irqsave(&lock);uint32_t n=drivers_count;
 spinlock_unlock_irqrestore(&lock,f);return n;
}
static int matches(const storage_pci_id_t* id,const struct pci_device* d) {
 return (id->vendor_id==0xFFFFU||id->vendor_id==d->vendor_id)&&
 (id->device_id==0xFFFFU||id->device_id==d->device_id)&&
 (id->class_code==0xFFU||id->class_code==d->class_code)&&
 (id->subclass==0xFFU||id->subclass==d->subclass)&&
 (id->prog_if==0xFFU||id->prog_if==d->prog_if);
}
static int is_bound(const struct pci_device* d) {
 for(uint32_t i=0;i<bindings_count;i++)if(bindings[i].device==d)return 1;
 return 0;
}
uint32_t storage_probe_pci_devices(void) {
 const storage_driver_t* snapshot[STORAGE_DRIVER_MAX];
 uint64_t f=spinlock_lock_irqsave(&lock);uint32_t n=drivers_count;
 for(uint32_t i=0;i<n;i++)snapshot[i]=drivers[i];
 spinlock_unlock_irqrestore(&lock,f);
 uint32_t newly_bound=0;
 for(int p=0;p<pci_get_device_count();p++) {
  const struct pci_device* dev=pci_get_device(p);
  if(!dev||dev->class_code!=0x01U)continue;
  f=spinlock_lock_irqsave(&lock);int claimed=is_bound(dev);spinlock_unlock_irqrestore(&lock,f);
  if(claimed)continue;
  for(uint32_t i=0;i<n;i++) {
   const storage_driver_t* drv=snapshot[i];int match=0;
   for(uint32_t j=0;j<drv->id_count;j++)if(matches(&drv->ids[j],dev)){match=1;break;}
   if(!match)continue;
   storage_probe_result_t result=drv->probe(dev);
   if(result==STORAGE_PROBE_FAILED)break;
   if(result!=STORAGE_PROBE_BOUND)continue;
   f=spinlock_lock_irqsave(&lock);
   if(bindings_count<STORAGE_BINDING_MAX&&!is_bound(dev)) {
    bindings[bindings_count].device=dev;bindings[bindings_count].driver=drv;
    bindings_count++;newly_bound++;
   }
   spinlock_unlock_irqrestore(&lock,f);break;
  }
 }
 return newly_bound;
}
const char* storage_driver_bound_name(const struct pci_device* dev) {
 if(!dev)return 0;
 uint64_t f=spinlock_lock_irqsave(&lock);const char* name=0;
 for(uint32_t i=0;i<bindings_count;i++)if(bindings[i].device==dev){name=bindings[i].driver->name;break;}
 spinlock_unlock_irqrestore(&lock,f);return name;
}
