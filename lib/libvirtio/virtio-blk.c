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
#include <string.h>
#include "virtio.h"
#include "helpers.h"
#include "virtio-blk.h"
#include "virtio-internal.h"

#define DEFAULT_SECTOR_SIZE 512
#define DRIVER_FEATURE_SUPPORT  (VIRTIO_BLK_F_BLK_SIZE | VIRTIO_F_VERSION_1 | VIRTIO_F_IOMMU_PLATFORM)

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
	/* Quiesce device */
	virtio_set_status(dev, VIRTIO_STAT_FAILED);

	/* Reset device */
	virtio_reset_device(dev);
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

/*
 * dma-map-in rounds addresses down to nearest 4K boundary, then uses
 * length argument as the offset relative to that boundary. To handle
 * buffers that are not necessarily 4K-aligned we add in the offset
 * from the preceeding 4K boundary to make sure all the bytes/pages
 * get mapped
 */
#define DMA_MAP_LEN(addr, len) \
	((uint64_t)addr & 0xFFF) ? (((uint64_t)addr & 0xFFF) + len) : len

static void
fill_blk_desc(struct vring_desc *vq_desc, uint32_t vq_size, int id,
              unsigned int type, bool modern,
			  uint64_t blkhdr_addr, uint32_t blkhdr_len, 
			  uint64_t buf_addr, uint32_t buf_len,
			  uint64_t status_addr)
{
	struct vring_desc *desc;

	/* Set up virtqueue descriptor for header */
   	desc = &vq_desc[id];
	virtio_fill_desc(desc, modern, (uint64_t)blkhdr_addr, blkhdr_len,
	                 VRING_DESC_F_NEXT, (id + 1) % vq_size);

	/* Set up virtqueue descriptor for data */
	desc = &vq_desc[(id + 1) % vq_size];
	virtio_fill_desc(desc, modern, buf_addr, buf_len,
	                 VRING_DESC_F_NEXT | ((type & 1) ? 0 : VRING_DESC_F_WRITE),
	                 (id + 2) % vq_size);

	/* Set up virtqueue descriptor for status */
	desc = &vq_desc[(id + 2) % vq_size];
	virtio_fill_desc(desc, modern, (uint64_t)status_addr, 1, VRING_DESC_F_WRITE,
	                 0);
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
	int id;
	struct virtio_blk_req blkhdr;
	//struct virtio_blk_config *blkconf;
	uint64_t capacity;
	uint32_t vq_size, time;
	struct vring_avail *vq_avail;		/* "Available" vring */
	struct vring_used *vq_used;		/* "Used" vring */
	volatile uint8_t status = -1;
	volatile uint16_t *current_used_idx;
	uint16_t last_used_idx, avail_idx;
	int blk_size = DEFAULT_SECTOR_SIZE;
	int ret = 0;
	long blkhdr_ioba, buf_ioba, status_ioba = 0;

	//printf("virtioblk_transfer: dev=%p buf=%p blocknum=%lli cnt=%li type=%i\n",
	//      dev, buf, blocknum, cnt, type);

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

	vq_size = virtio_get_qsize(dev, 0);
	vq_avail = dev->vq.avail;
	vq_used = dev->vq.used;

	avail_idx = virtio_modern16_to_cpu(dev, vq_avail->idx);

	last_used_idx = vq_used->idx;
	current_used_idx = &vq_used->idx;

	/* Set up header */
	fill_blk_hdr(&blkhdr, dev->is_modern, type | VIRTIO_BLK_T_BARRIER,
		     1, blocknum * blk_size / DEFAULT_SECTOR_SIZE);

	/* Determine descriptor index */
	id = (avail_idx * 3) % vq_size;

	if (dev->use_iommu) {
		buf_ioba = SLOF_dma_map_in(buf, DMA_MAP_LEN(buf, cnt * blk_size), true);
		blkhdr_ioba = SLOF_dma_map_in(&blkhdr, sizeof(struct virtio_blk_req),
		                              true);
		status_ioba = SLOF_dma_map_in((void *)&status, 1, true);
		fill_blk_desc(dev->vq.desc, vq_size, id, type, dev->is_modern,
		              blkhdr_ioba, sizeof(struct virtio_blk_req),
		              buf_ioba, cnt * blk_size, status_ioba);
	} else {
		fill_blk_desc(dev->vq.desc, vq_size, id, type, dev->is_modern,
		              (uint64_t)&blkhdr, sizeof(struct virtio_blk_req),
		              (uint64_t)buf, cnt * blk_size, (uint64_t)&status);
	}

	vq_avail->ring[avail_idx % vq_size] = virtio_cpu_to_modern16 (dev, id);
	mb();
	vq_avail->idx = virtio_cpu_to_modern16(dev, avail_idx + 1);

	/* Tell HV that the queue is ready */
	virtio_queue_notify(dev, 0);

	/* Wait for host to consume the descriptor */
	time = SLOF_GetTimer() + VIRTIO_TIMEOUT;
	while (*current_used_idx == last_used_idx) {
		// do something better
		mb();
		if (time < SLOF_GetTimer())
			break;
	}

	if (status == 0) {
		ret = cnt;
		goto exit_success;
	}

	printf("virtioblk_transfer failed! type=%i, status = %i\n",
	       type, status);
	ret = 0;

exit_success:
	if (dev->use_iommu) {
		SLOF_dma_map_out(buf_ioba, buf, DMA_MAP_LEN(buf, cnt * blk_size));
		SLOF_dma_map_out(blkhdr_ioba, &blkhdr, sizeof(struct virtio_blk_req));
		SLOF_dma_map_out(status_ioba, (void *)&status, 1);
	}
	return ret;
}
