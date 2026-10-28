/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "gui_event_queue.h"
#include <assert.h>
#include <stdio.h>
#define N 4U
static vanta_gui_event_t at(vanta_gui_event_t*q,uint32_t h,uint32_t i){return q[(h+i)%N];}
static int push(vanta_gui_event_t*q,uint32_t*h,uint32_t*n,uint32_t t,int32_t x,uint32_t v)
{vanta_gui_event_t e={t,x,0,v,0};return gui_event_queue_push(q,N,h,n,&e);}
int main(void)
{
 vanta_gui_event_t q[N]={0},before[N];uint32_t h=0,n=0;
 assert(push(q,&h,&n,VANTA_GUI_EVENT_POINTER_MOVE,1,0));
 assert(push(q,&h,&n,VANTA_GUI_EVENT_POINTER_MOVE,8,0));
 assert(n==1&&at(q,h,0).x==8);
 assert(push(q,&h,&n,VANTA_GUI_EVENT_POINTER_DOWN,8,1));
 assert(push(q,&h,&n,VANTA_GUI_EVENT_POINTER_MOVE,10,0));
 assert(n==3&&at(q,h,1).type==VANTA_GUI_EVENT_POINTER_DOWN&&at(q,h,2).x==10);
 h=0;n=0;
 assert(push(q,&h,&n,VANTA_GUI_EVENT_KEY,0,11));
 assert(push(q,&h,&n,VANTA_GUI_EVENT_POINTER_DOWN,2,1));
 assert(push(q,&h,&n,VANTA_GUI_EVENT_FOCUS_GAINED,0,0));
 assert(push(q,&h,&n,VANTA_GUI_EVENT_CLOSE_REQUEST,0,0));
 for(uint32_t i=0;i<N;i++)before[i]=at(q,h,i);
 assert(!push(q,&h,&n,VANTA_GUI_EVENT_POINTER_MOVE,12,0)&&n==N);
 for(uint32_t i=0;i<N;i++)assert(at(q,h,i).type==before[i].type&&at(q,h,i).x==before[i].x&&at(q,h,i).value==before[i].value);
 h=0;n=0;
 assert(push(q,&h,&n,VANTA_GUI_EVENT_KEY,0,1));
 assert(push(q,&h,&n,VANTA_GUI_EVENT_POINTER_MOVE,3,0));
 assert(push(q,&h,&n,VANTA_GUI_EVENT_KEY,0,2));
 assert(push(q,&h,&n,VANTA_GUI_EVENT_POINTER_DOWN,3,1));
 assert(push(q,&h,&n,VANTA_GUI_EVENT_POINTER_UP,3,1));
 assert(n==N&&at(q,h,0).value==1&&at(q,h,1).value==2);
 assert(at(q,h,2).type==VANTA_GUI_EVENT_POINTER_DOWN&&at(q,h,3).type==VANTA_GUI_EVENT_POINTER_UP);
 h=(h+1U)%N;--n;
 assert(push(q,&h,&n,VANTA_GUI_EVENT_KEY,0,3));
 assert(n==N&&at(q,h,3).value==3);
 puts("VANTAOS_GUI_EVENT_QUEUE_OK");return 0;
}
