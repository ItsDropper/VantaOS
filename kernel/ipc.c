/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "ipc.h"
#include "crypto_rng.h"
#include "spinlock.h"
#include <stddef.h>
#include <stdint.h>
#define IPC_ENDPOINTS_MAX 32U
typedef struct { uint32_t sender_pid; uint32_t length; uint8_t bytes[IPC_MESSAGE_MAX]; } ipc_message_t;
typedef struct { uint64_t token; uint32_t owner_pid; uint32_t head; uint32_t count; uint32_t active; ipc_message_t queue[IPC_QUEUE_DEPTH]; } ipc_endpoint_t;
static ipc_endpoint_t endpoints[IPC_ENDPOINTS_MAX];
static spinlock_t ipc_lock = SPINLOCK_INITIALIZER;
static int token_exists_locked(uint64_t token) { for(uint32_t i=0;i<IPC_ENDPOINTS_MAX;i++) if(endpoints[i].active && endpoints[i].token==token) return 1; return 0; }
int64_t ipc_endpoint_create(uint32_t owner_pid) {
 if(!owner_pid) return -22; uint64_t token=0U;
 if(!crypto_rng_fill(&token,sizeof(token))) return -19;
 token &= 0x7FFFFFFFFFFFFFFFULL;
 if(!token) return -19;
 uint64_t flags=spinlock_lock_irqsave(&ipc_lock); uint32_t slot=IPC_ENDPOINTS_MAX;
 for(uint32_t i=0;i<IPC_ENDPOINTS_MAX;i++) if(!endpoints[i].active){slot=i;break;}
 if(slot==IPC_ENDPOINTS_MAX || token_exists_locked(token)){spinlock_unlock_irqrestore(&ipc_lock,flags);return slot==IPC_ENDPOINTS_MAX?-12:-11;}
 ipc_endpoint_t *ep=&endpoints[slot]; ep->token=token; ep->owner_pid=owner_pid; ep->head=0; ep->count=0; ep->active=1;
 for(uint32_t i=0;i<IPC_QUEUE_DEPTH;i++){ep->queue[i].sender_pid=0;ep->queue[i].length=0;}
 spinlock_unlock_irqrestore(&ipc_lock,flags); return (int64_t)token;
}
int ipc_message_send(uint64_t token,uint32_t sender_pid,const void *message,size_t length) {
 if(!token || !sender_pid || !message || !length || length>IPC_MESSAGE_MAX) return -22;
 uint64_t flags=spinlock_lock_irqsave(&ipc_lock); ipc_endpoint_t *ep=NULL;
 for(uint32_t i=0;i<IPC_ENDPOINTS_MAX;i++) if(endpoints[i].active && endpoints[i].token==token){ep=&endpoints[i];break;}
 if(!ep){spinlock_unlock_irqrestore(&ipc_lock,flags);return -9;}
 if(ep->count==IPC_QUEUE_DEPTH){spinlock_unlock_irqrestore(&ipc_lock,flags);return -11;}
 uint32_t tail=(ep->head+ep->count)%IPC_QUEUE_DEPTH; ipc_message_t *dst=&ep->queue[tail]; const uint8_t *src=(const uint8_t*)message;
 dst->sender_pid=sender_pid; dst->length=(uint32_t)length; for(size_t i=0;i<length;i++) dst->bytes[i]=src[i]; ++ep->count;
 spinlock_unlock_irqrestore(&ipc_lock,flags); return 0;
}
int ipc_message_receive(uint64_t token,uint32_t receiver_pid,void *message,size_t capacity) {
 if(!token || !receiver_pid || !message || !capacity || capacity>IPC_MESSAGE_MAX) return -22;
 uint64_t flags=spinlock_lock_irqsave(&ipc_lock); ipc_endpoint_t *ep=NULL;
 for(uint32_t i=0;i<IPC_ENDPOINTS_MAX;i++) if(endpoints[i].active && endpoints[i].token==token){ep=&endpoints[i];break;}
 if(!ep){spinlock_unlock_irqrestore(&ipc_lock,flags);return -9;}
 if(ep->owner_pid!=receiver_pid){spinlock_unlock_irqrestore(&ipc_lock,flags);return -1;}
 if(!ep->count){spinlock_unlock_irqrestore(&ipc_lock,flags);return -11;}
 ipc_message_t *src=&ep->queue[ep->head]; if(src->length>capacity){spinlock_unlock_irqrestore(&ipc_lock,flags);return -90;}
 uint8_t *dst=(uint8_t*)message; for(uint32_t i=0;i<src->length;i++) dst[i]=src->bytes[i]; int result=(int)src->length;
 src->length=0; src->sender_pid=0; ep->head=(ep->head+1U)%IPC_QUEUE_DEPTH; --ep->count;
 spinlock_unlock_irqrestore(&ipc_lock,flags); return result;
}
int ipc_endpoint_close(uint64_t token,uint32_t owner_pid) {
 if(!token || !owner_pid) return -22; uint64_t flags=spinlock_lock_irqsave(&ipc_lock);
 for(uint32_t i=0;i<IPC_ENDPOINTS_MAX;i++){ipc_endpoint_t *ep=&endpoints[i]; if(!ep->active || ep->token!=token) continue;
 if(ep->owner_pid!=owner_pid){spinlock_unlock_irqrestore(&ipc_lock,flags);return -1;} ep->active=0;ep->token=0;ep->owner_pid=0;ep->head=0;ep->count=0;spinlock_unlock_irqrestore(&ipc_lock,flags);return 0;}
 spinlock_unlock_irqrestore(&ipc_lock,flags);return -9;
}
void ipc_process_cleanup(uint32_t pid) {
 if(!pid) return; uint64_t flags=spinlock_lock_irqsave(&ipc_lock);
 for(uint32_t i=0;i<IPC_ENDPOINTS_MAX;i++){ipc_endpoint_t *ep=&endpoints[i];if(ep->active && ep->owner_pid==pid){ep->active=0;ep->token=0;ep->owner_pid=0;ep->head=0;ep->count=0;}}
 spinlock_unlock_irqrestore(&ipc_lock,flags);
}
