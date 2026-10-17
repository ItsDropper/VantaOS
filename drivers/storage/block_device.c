#include "block_device.h"
#include "spinlock.h"
static block_device_t* devices[BLOCK_DEVICE_MAX];
static uint32_t device_count_value;
static spinlock_t registry_lock = SPINLOCK_INITIALIZER;
static int valid_sector_size(uint32_t n) { return n >= 512U && n <= 65536U && !(n & (n-1U)); }
static int valid_name(const char* n) {
 if (!n || !n[0]) return 0;
 for (uint32_t i=0;i<BLOCK_DEVICE_NAME_MAX;i++) if (!n[i]) return 1;
 return 0;
}
static int same_name(const char* a,const char* b) {
 for (uint32_t i=0;i<BLOCK_DEVICE_NAME_MAX;i++) { if(a[i]!=b[i]) return 0; if(!a[i]) return 1; }
 return 0;
}
block_status_t block_device_register(block_device_t* d) {
 if(!d || !valid_name(d->name) || !valid_sector_size(d->sector_size) ||
    !d->sector_count || !d->ops || !d->ops->read) return BLOCK_STATUS_INVALID;
 uint64_t f=spinlock_lock_irqsave(&registry_lock);
 for(uint32_t i=0;i<device_count_value;i++) if(devices[i]==d || same_name(devices[i]->name,d->name)) {
  spinlock_unlock_irqrestore(&registry_lock,f); return BLOCK_STATUS_BUSY;
 }
 if(device_count_value==BLOCK_DEVICE_MAX) { spinlock_unlock_irqrestore(&registry_lock,f); return BLOCK_STATUS_NO_MEMORY; }
 d->active_operations=0; d->online=1; devices[device_count_value++]=d;
 spinlock_unlock_irqrestore(&registry_lock,f); return BLOCK_STATUS_OK;
}
block_status_t block_device_unregister(block_device_t* d) {
 if(!d) return BLOCK_STATUS_INVALID;
 uint64_t f=spinlock_lock_irqsave(&registry_lock);
 for(uint32_t i=0;i<device_count_value;i++) if(devices[i]==d) {
  d->online=0;
  if(__atomic_load_n(&d->active_operations,__ATOMIC_ACQUIRE)!=0U) {
   __atomic_store_n(&d->online,1U,__ATOMIC_RELEASE);
   spinlock_unlock_irqrestore(&registry_lock,f); return BLOCK_STATUS_BUSY;
  }
  for(uint32_t j=i+1;j<device_count_value;j++) devices[j-1]=devices[j];
  devices[--device_count_value]=0;
  spinlock_unlock_irqrestore(&registry_lock,f); return BLOCK_STATUS_OK;
 }
 spinlock_unlock_irqrestore(&registry_lock,f); return BLOCK_STATUS_NOT_READY;
}
uint32_t block_device_count(void) {
 uint64_t f=spinlock_lock_irqsave(&registry_lock); uint32_t n=device_count_value;
 spinlock_unlock_irqrestore(&registry_lock,f); return n;
}
block_device_t* block_device_get(uint32_t i) {
 uint64_t f=spinlock_lock_irqsave(&registry_lock);
 block_device_t* d=i<device_count_value?devices[i]:0;
 spinlock_unlock_irqrestore(&registry_lock,f); return d;
}
block_device_t* block_device_find(const char* n) {
 if(!n) return 0;
 uint64_t f=spinlock_lock_irqsave(&registry_lock); block_device_t* d=0;
 for(uint32_t i=0;i<device_count_value;i++) if(same_name(devices[i]->name,n)){d=devices[i];break;}
 spinlock_unlock_irqrestore(&registry_lock,f); return d;
}
static block_status_t begin_io(block_device_t* d,uint64_t lba,uint32_t n,const void* b) {
 if(!d||!b||!n)return BLOCK_STATUS_INVALID;
 uint64_t f=spinlock_lock_irqsave(&registry_lock);
 if(!__atomic_load_n(&d->online,__ATOMIC_ACQUIRE)) {
  spinlock_unlock_irqrestore(&registry_lock,f);return BLOCK_STATUS_NOT_READY;
 }
 if(lba>=d->sector_count||(uint64_t)n>d->sector_count-lba) {
  spinlock_unlock_irqrestore(&registry_lock,f);return BLOCK_STATUS_OUT_OF_RANGE;
 }
 __atomic_add_fetch(&d->active_operations,1U,__ATOMIC_ACQ_REL);
 spinlock_unlock_irqrestore(&registry_lock,f);return BLOCK_STATUS_OK;
}
static void end_io(block_device_t* d) {
 __atomic_sub_fetch(&d->active_operations,1U,__ATOMIC_RELEASE);
}
block_status_t block_device_read(block_device_t* d,uint64_t lba,uint32_t n,void* b) {
 block_status_t s=begin_io(d,lba,n,b); if(s!=BLOCK_STATUS_OK)return s;
 s=d->ops->read(d,lba,n,b); end_io(d); return s;
}
block_status_t block_device_write(block_device_t* d,uint64_t lba,uint32_t n,const void* b) {
 block_status_t s=begin_io(d,lba,n,b); if(s!=BLOCK_STATUS_OK)return s;
 if(d->flags&BLOCK_DEVICE_READ_ONLY)s=BLOCK_STATUS_READ_ONLY;
 else if(!d->ops->write)s=BLOCK_STATUS_UNSUPPORTED;
 else s=d->ops->write(d,lba,n,b);
 end_io(d); return s;
}
block_status_t block_device_flush(block_device_t* d) {
 if(!d)return BLOCK_STATUS_INVALID;
 uint64_t f=spinlock_lock_irqsave(&registry_lock);
 if(!__atomic_load_n(&d->online,__ATOMIC_ACQUIRE)) {
  spinlock_unlock_irqrestore(&registry_lock,f);return BLOCK_STATUS_NOT_READY;
 }
 __atomic_add_fetch(&d->active_operations,1U,__ATOMIC_ACQ_REL);
 spinlock_unlock_irqrestore(&registry_lock,f);
 block_status_t s=(!d->ops||!d->ops->flush)?BLOCK_STATUS_UNSUPPORTED:d->ops->flush(d);
 end_io(d);return s;
}
