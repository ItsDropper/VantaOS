/*
 * Copyright (c) 2026 ItsDropper
 * Licensed under the MIT License. See LICENSE.
 */
#include "hda.h"
#include "serial.h"
static void put8(uint8_t v){static const char d[]="0123456789ABCDEF";char b[2]={d[v>>4],d[v&15U]};serial_write_bytes(b,2U);}
static void put16(uint16_t v){put8((uint8_t)(v>>8));put8((uint8_t)v);}
static void put64(uint64_t v){put16((uint16_t)(v>>48));put16((uint16_t)(v>>32));put16((uint16_t)(v>>16));put16((uint16_t)v);}
void hda_initialize(void)
{
 int count=pci_get_device_count(); if(count<0)return;
 for(int i=0;i<count;++i){
  const struct pci_device *d=pci_get_device(i); hda_controller_info_t info;
  hda_probe_status_t status;
  if(!d||d->class_code!=0x04U||d->subclass!=0x03U||d->prog_if!=0x00U)continue;
  status=hda_probe_controller(d,&info);
  if(status!=HDA_PROBE_OK){
   serial_write("VANTAOS_HW_HDA_PROBE_SKIPPED bdf=");
   put8(d->bus);serial_write(":");put8(d->slot);serial_write(".");put8(d->function);
   serial_write(" status=");put8((uint8_t)status);serial_write("\n");continue;
  }
  serial_write("VANTAOS_HW_HDA_CONTROLLER bdf=");
  put8(d->bus);serial_write(":");put8(d->slot);serial_write(".");put8(d->function);
  serial_write(" bar0=0x");put64(info.register_base);
  serial_write(" version=");put8(info.version_major);serial_write(".");put8(info.version_minor);
  serial_write(" gcap=0x");put16(info.capabilities);
  serial_write(" codec_mask=0x");put16(info.codec_presence);
  serial_write(" reset=");serial_write(info.controller_in_reset?"yes":"no");
  serial_write("\n");
 }
}
