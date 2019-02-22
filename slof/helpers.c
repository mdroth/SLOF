/******************************************************************************
 * Copyright (c) 2007, 2012, 2013 IBM Corporation
 * All rights reserved.
 * This program and the accompanying materials
 * are made available under the terms of the BSD License
 * which accompanies this distribution, and is available at
 * http://www.opensource.org/licenses/bsd-license.php
 *
 * Contributors:
 *     IBM Corporation - initial implementation
 *****************************************************************************/
/*
 * All functions concerning interface to slof
 */

#include <stdio.h>
#include <string.h>
#include <cpu.h>
#include "helpers.h"
#include "paflof.h"
#include "../lib/libnet/time.h"   /* TODO: Get rid of old timer code */

/**
 * get msec-timer value
 * access to HW register
 * overrun will occur if boot exceeds 1.193 hours (49 days)
 *
 * @param   -
 * @return  actual timer value in ms as 32bit
 */
uint32_t SLOF_GetTimer(void)
{
	forth_eval("get-msecs");
	return (uint32_t) forth_pop();
}

void SLOF_msleep(uint32_t time)
{
	time = SLOF_GetTimer() + time;
	while (time > SLOF_GetTimer())
		cpu_relax();
}

void SLOF_usleep(uint32_t time)
{
	forth_push(time);
	forth_eval("us");
}

static unsigned int dest_timer;

void set_timer(int val)
{
	dest_timer = SLOF_GetTimer() + val;
}

int get_timer()
{
	return dest_timer - SLOF_GetTimer();
}

int get_sec_ticks(void)
{
	return 1000;	/* number of ticks in 1 second */
}

#undef DEBUG
//#define DEBUG
#ifdef DEBUG
#define dprintf(_x ...) do { printf ("%s: ", __func__); printf(_x); } while (0);
#else
#define dprintf(_x ...)
#endif

void *SLOF_dma_alloc(long size)
{
    dprintf("called dma_alloc, size: %lx\n", size);
	forth_push(size);
	forth_eval("dma-alloc");
    dprintf("returning from dma_alloc\n");
	return (void *)forth_pop();
}

void SLOF_dma_free(void *virt, long size)
{
	forth_push((long)virt);
	forth_push(size);
	forth_eval("dma-free");
}

void *SLOF_alloc_mem(long size)
{
    dprintf("called alloc_mem, size: %lx\n", size);
	forth_push(size);
	forth_eval("alloc-mem");
    dprintf("returning from alloc_mem\n");
	return (void *)forth_pop();
}

void *SLOF_alloc_mem_aligned(long size, long align)
{
	unsigned long addr = (unsigned long)SLOF_alloc_mem(size);

	if (addr % align) {
		SLOF_free_mem((void *)addr, size);
		addr = (unsigned long)SLOF_alloc_mem(size + align - 1);
		addr = addr + align - 1;
		addr = addr & ~(align - 1);
	}

	return (void *)addr;
}

void SLOF_free_mem(void *addr, long size)
{
	forth_push((long)addr);
	forth_push(size);
	forth_eval("free-mem");
}

long SLOF_dma_map_in(void *virt, long size, int cacheable)
{
	forth_push((long)virt);
	forth_push(size);
	forth_push(cacheable);
	forth_eval("dma-map-in");
	return forth_pop();
}

void SLOF_dma_map_out(long phys, void *virt, long size)
{
	forth_push((long)virt);
	forth_push((long)phys);
	forth_push(size);
	forth_eval("dma-map-out");
}

long SLOF_pci_config_read32(long offset)
{
	forth_push(offset);
	forth_eval("config-l@");
	return forth_pop();
}

long SLOF_pci_config_read16(long offset)
{
	forth_push(offset);
	forth_eval("config-w@");
	return forth_pop();
}

long SLOF_pci_config_read8(long offset)
{
	forth_push(offset);
	forth_eval("config-b@");
	return forth_pop();
}

void SLOF_pci_config_write32(long offset, long value)
{
	forth_push(value);
	forth_push(offset);
	forth_eval("config-l!");
}

void SLOF_pci_config_write16(long offset, long value)
{
	forth_push(value);
	forth_push(offset);
	forth_eval("config-w!");
}

void SLOF_pci_config_write8(long offset, long value)
{
	forth_push(value);
	forth_push(offset);
	forth_eval("config-b!");
}

void *SLOF_translate_my_address(void *addr)
{
	forth_push((long)addr);
	forth_eval("translate-my-address");
	return (void *)forth_pop();
}

int write_mm_log(char *data, unsigned int len, unsigned short type)
{
	forth_push((unsigned long)data);
	forth_push(len);
	forth_push(type);

	return forth_eval_pop("write-mm-log");
}

static void SLOF_encode_response(void *addr, size_t size,char *s)
{
	forth_push((unsigned long)addr);
	forth_push(size);
	forth_eval("encode-bytes");
	forth_push((unsigned long)s);
	forth_push(strlen(s));
	forth_eval("set-chosen");
}

void SLOF_encode_bootp_response(void *addr, size_t size)
{
	SLOF_encode_response(addr, size, "bootp-response");
}

void SLOF_encode_dhcp_response(void *addr, size_t size)
{
	SLOF_encode_response(addr, size, "dhcp-response");
}
