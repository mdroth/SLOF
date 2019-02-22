/******************************************************************************
 * Copyright (c) 2011 IBM Corporation
 * All rights reserved.
 * This program and the accompanying materials
 * are made available under the terms of the BSD License
 * which accompanies this distribution, and is available at
 * http://www.opensource.org/licenses/bsd-license.php
 *
 * Contributors:
 *     IBM Corporation - initial implementation
 *****************************************************************************/

#include <stdio.h>
#include <cpu.h>
#include <helpers.h>
#include <byteorder.h>
#include "virtio.h"
#include "helpers.h"
#include "virtio-blk.h"
#include "virtio-internal.h"
#include "paflof.h"

#define DEFAULT_SECTOR_SIZE 512
#define DRIVER_FEATURE_SUPPORT  (VIRTIO_BLK_F_BLK_SIZE | VIRTIO_F_VERSION_1 | VIRTIO_F_IOMMU_PLATFORM)

#undef DEBUG
//#define DEBUG
#ifdef DEBUG
#define dprintf(_x ...) do { printf ("%s: ", __func__); printf(_x); } while (0);
#else
#define dprintf(_x ...)
#endif

/**
 * Initialize virtio-block device.
 * @param  dev  pointer to virtio device information
 */
int
virtioblk_init(struct virtio_device *dev)
{
	int blk_size = DEFAULT_SECTOR_SIZE;
	uint64_t features;
	int status = VIRTIO_STAT_ACKNOWLEDGE;

    dprintf("virtioblk_init: marker 0\n");

	/* Reset device */
	virtio_reset_device(dev);

	/* Acknowledge device. */
	virtio_set_status(dev, status);

	/* Tell HV that we know how to drive the device. */
	status |= VIRTIO_STAT_DRIVER;
	virtio_set_status(dev, status);

	if (dev->is_modern) {
		/* Negotiate features and sets FEATURES_OK if successful */
		if (virtio_negotiate_guest_features(dev, DRIVER_FEATURE_SUPPORT))
			goto dev_error;

		virtio_get_status(dev, &status);
	} else {
		/* Device specific setup - we support F_BLK_SIZE */
		virtio_set_guest_features(dev,  VIRTIO_BLK_F_BLK_SIZE);
	}

	if (virtio_queue_init_vq(dev, &dev->vq, 0))
		goto dev_error;

	dev->vq.avail->flags = virtio_cpu_to_modern16(dev, VRING_AVAIL_F_NO_INTERRUPT);
	dev->vq.avail->idx = 0;

	/* Tell HV that setup succeeded */
	status |= VIRTIO_STAT_DRIVER_OK;
	virtio_set_status(dev, status);

	features = virtio_get_host_features(dev);
	if (features & VIRTIO_BLK_F_BLK_SIZE) {
		blk_size = virtio_get_config(dev,
					     offset_of(struct virtio_blk_cfg, blk_size),
					     sizeof(blk_size));
	}
    dprintf("virtioblk_init: marker 1\n");

	return blk_size;
dev_error:
	printf("%s: failed\n", __func__);
	status |= VIRTIO_STAT_FAILED;
	virtio_set_status(dev, status);
	return 0;
}


/**
 * Shutdown the virtio-block device.
 * @param  dev  pointer to virtio device information
 */
void
virtioblk_shutdown(struct virtio_device *dev)
{
    dprintf("virtioblk_shutdown: marker 0\n");
	/* Quiesce device */
	virtio_set_status(dev, VIRTIO_STAT_FAILED);

	/* Reset device */
	virtio_reset_device(dev);
    dprintf("virtioblk_shutdown: marker 1\n");
}

static void fill_blk_hdr(struct virtio_blk_req *blkhdr, bool is_modern,
                         uint32_t type, uint32_t ioprio, uint32_t sector)
{
	if (is_modern) {
		blkhdr->type = cpu_to_le32(type);
		blkhdr->ioprio = cpu_to_le32(ioprio);
		blkhdr->sector = cpu_to_le64(sector);
	} else {
		blkhdr->type = type;
		blkhdr->ioprio = ioprio;
		blkhdr->sector = sector;
	}
}

static void load_vpc(void) {
    unsigned long phandle;
    char *vpc_marker = "marker-virtio-parent-calls";

	dprintf("load_vpc called\n");
    forth_eval("my-parent ?dup IF ihandle>phandle THEN");
    phandle = forth_pop();
    if (phandle == 0) {
	    dprintf("load_vpc no phandle, skipping vpc load\n");
        return;
    } else {
	    dprintf("load_vpc got phandle: %lx\n", phandle);
    }

    forth_push((unsigned long)vpc_marker);
    forth_push(strlen(vpc_marker));
    forth_push(phandle);
    forth_eval("find-method");

    if (forth_pop()) {
        forth_pop();
	    dprintf("load_vpc already loaded\n");
    } else {
        dprintf("load_vpc unable to locate method %s, loading VPC\n", vpc_marker);
        forth_eval("s\" virtio-parent-calls-internal.fs\" INCLUDED");
    }
}

/**
 * Read / write blocks
 * @param  reg  pointer to "reg" property
 * @param  buf  pointer to destination buffer
 * @param  blocknum  block number of the first block that should be transfered
 * @param  cnt  amount of blocks that should be transfered
 * @param  type  VIRTIO_BLK_T_OUT for write, VIRTIO_BLK_T_IN for read transfers
 * @return number of blocks that have been transfered successfully
 */
int
virtioblk_transfer(struct virtio_device *dev, char *buf, uint64_t blocknum,
                   long cnt, unsigned int type)
{
	struct vring_desc *desc;
	int id;
	struct virtio_blk_req *blkhdr;
	//struct virtio_blk_config *blkconf;
	uint64_t capacity;
	uint32_t vq_size, time, time_start;
	struct vring_desc *vq_desc;		/* Descriptor vring */
	struct vring_avail *vq_avail;		/* "Available" vring */
	struct vring_used *vq_used;		/* "Used" vring */
	volatile uint8_t status = -1;
    uint8_t *status_ptr;
	volatile uint16_t *current_used_idx;
	uint16_t last_used_idx, avail_idx;
	int blk_size = DEFAULT_SECTOR_SIZE;
    bool sanity_check = false;
    char *tmp_buf;
    int ret = 0;

	dprintf("virtioblk_transfer called\n");

    load_vpc();

	dprintf("virtioblk_transfer 0: buf: %p, blocknum: %lu, cnt: %ld\n", buf, blocknum, cnt);

	//printf("virtioblk_transfer: dev=%p buf=%p blocknum=%lli cnt=%li type=%i\n",
	//	dev, buf, blocknum, cnt, type);

	/* Check whether request is within disk capacity */
	capacity = virtio_get_config(dev,
			offset_of(struct virtio_blk_cfg, capacity),
			sizeof(capacity));
	if (blocknum + cnt - 1 > capacity) {
		puts("virtioblk_transfer: Access beyond end of device!");
		return 0;
	}

	blk_size = virtio_get_config(dev,
			offset_of(struct virtio_blk_cfg, blk_size),
			sizeof(blk_size));
	if (blk_size % DEFAULT_SECTOR_SIZE) {
		fprintf(stderr, "virtio-blk: Unaligned sector size %d\n", blk_size);
		return 0;
	}

	dprintf("virtioblk_transfer 1: buf: %p, blocknum: %lu, blk_size: %lu, cnt: %ld\n", buf, blocknum, blk_size, cnt);
	vq_size = virtio_get_qsize(dev, 0);
    if (!dev->use_iommu) {
	    vq_desc = virtio_get_vring_desc(dev, 0);
	    vq_avail = virtio_get_vring_avail(dev, 0);
	    vq_used = virtio_get_vring_used(dev, 0);
        status = SLOF_dma_alloc(1);
        status_ptr = &status;
	    blkhdr = SLOF_alloc_mem(0x1000);
        tmp_buf = buf;
    } else {
	    vq_desc = dev->vq.desc;
	    vq_avail = dev->vq.avail;
	    vq_used = dev->vq.used;
        status_ptr = SLOF_dma_alloc(1);
        *status_ptr = -1;
        blkhdr = SLOF_dma_alloc(sizeof(struct virtio_blk_req));
        tmp_buf = SLOF_dma_alloc(blk_size * cnt);
    }

    dprintf("virtioblk_transfer 2, actual: vq_desc: %p, vq_avail: %p, vq_used: %p\n",
           vq_desc, vq_avail, vq_used);
    dprintf("virtioblk_transfer 2, device: vq_desc: %p, vq_avail: %p, vq_used: %p\n",
	       virtio_get_vring_desc(dev, 0),
           virtio_get_vring_avail(dev, 0),
           virtio_get_vring_used(dev, 0));
    dprintf("virtioblk_transfer 2, cached: vq_desc: %p, vq_avail: %p, vq_used: %p\n",
           dev->vq.desc, dev->vq.avail, dev->vq.used);


	avail_idx = virtio_modern16_to_cpu(dev, vq_avail->idx);

	last_used_idx = vq_used->idx;
	current_used_idx = &vq_used->idx;

    dprintf("virtioblk_transfer: avail_idx: %d, last_used: %d, current_used: %d\n",
            avail_idx, last_used_idx, *current_used_idx);

	/* Set up header */
	fill_blk_hdr(blkhdr, dev->is_modern, type | VIRTIO_BLK_T_BARRIER,
		     1, blocknum * blk_size / DEFAULT_SECTOR_SIZE);

	/* Determine descriptor index */
	id = (avail_idx * 3) % vq_size;

    dprintf("virtioblk_transfer 3\n");
	/* Set up virtqueue descriptor for header */
	desc = &vq_desc[id];
	virtio_fill_desc2(desc, dev->is_modern, dev->use_iommu, (uint64_t)blkhdr,
			 sizeof(struct virtio_blk_req),
			 VRING_DESC_F_NEXT, (id + 1) % vq_size);

    dprintf("virtioblk_transfer 4\n");
	/* Set up virtqueue descriptor for data */
	desc = &vq_desc[(id + 1) % vq_size];
	virtio_fill_desc2(desc, dev->is_modern, dev->use_iommu, (uint64_t)tmp_buf, cnt * blk_size,
			 VRING_DESC_F_NEXT | ((type & 1) ? 0 : VRING_DESC_F_WRITE),
			 (id + 2) % vq_size);

    dprintf("virtioblk_transfer 5\n");
	/* Set up virtqueue descriptor for status */
	desc = &vq_desc[(id + 2) % vq_size];
	virtio_fill_desc2(desc, dev->is_modern, dev->use_iommu, (uint64_t)status_ptr, 1,
			 VRING_DESC_F_WRITE, 0);

	vq_avail->ring[avail_idx % vq_size] = virtio_cpu_to_modern16 (dev, id);
	mb();
	vq_avail->idx = virtio_cpu_to_modern16(dev, avail_idx + 1);

	/* Tell HV that the queue is ready */
	virtio_queue_notify(dev, 0);

	/* Wait for host to consume the descriptor */
    time_start = SLOF_GetTimer();
	time = time_start + VIRTIO_TIMEOUT;
	while (*current_used_idx == last_used_idx) {
		// do something better
		mb();
		if (time < SLOF_GetTimer()) {
	        printf("virtioblk_transfer timed out, start: %d, current: %d (%d ms timeout)\n", time_start, time, VIRTIO_TIMEOUT);
			break;
        }
	}

	if (*status_ptr == 0) {
        ret = cnt;
        goto exit_success;
    }

	printf("virtioblk_transfer failed! type=%i, status = %i\n",
	       type, status);

    ret = 0;

exit_success:
    if (dev->use_iommu) {
        memcpy(buf, tmp_buf, blk_size * cnt);
        SLOF_dma_free(blkhdr, sizeof(struct virtio_blk_req));
        SLOF_dma_free(tmp_buf, blk_size * cnt);
        SLOF_dma_free(status_ptr, 1);
    } else {
        SLOF_free_mem(blkhdr, sizeof(struct virtio_blk_req));
    }
	return ret;
}
