// SPDX-License-Identifier: GPL-2.0
/*
 * Simple block driver for the CppHDL Tribe SD controller.
 *
 * The hardware exposes one SD-card-like byte stream behind a small MMIO
 * command interface.  Block BIO fragments are submitted as a descriptor FIFO
 * so the controller can DMA directly to non-contiguous Linux pages.
 */

#include <linux/bio.h>
#include <linux/blkdev.h>
#include <linux/dma-mapping.h>
#include <linux/errno.h>
#include <linux/hdreg.h>
#include <linux/highmem.h>
#include <linux/io.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>

#define TRIBE_SD_NAME              "tribesd"
#define TRIBE_SD_SECTOR_BYTES      512

#define TRIBE_SD_REG_CONTROL       0x00
#define TRIBE_SD_REG_STATUS        0x04
#define TRIBE_SD_REG_CMD           0x08
#define TRIBE_SD_REG_ARG           0x0c
#define TRIBE_SD_REG_LEN           0x10
#define TRIBE_SD_REG_DMA_ADDR      0x14
#define TRIBE_SD_REG_TXDATA        0x18
#define TRIBE_SD_REG_RXDATA        0x1c
#define TRIBE_SD_REG_IRQ_ENABLE    0x20
#define TRIBE_SD_REG_IRQ_PENDING   0x24
#define TRIBE_SD_REG_DMA_DESC_ADDR 0x28
#define TRIBE_SD_REG_DMA_DESC_LEN  0x2c
#define TRIBE_SD_REG_DMA_DESC_PUSH 0x30
#define TRIBE_SD_REG_DMA_DESC_STATUS 0x34

#define TRIBE_SD_CTRL_START        BIT(0)
#define TRIBE_SD_CTRL_WRITE        BIT(1)
#define TRIBE_SD_CTRL_DMA          BIT(2)
#define TRIBE_SD_CTRL_CLEAR_DONE   BIT(4)

#define TRIBE_SD_STATUS_BUSY       BIT(0)
#define TRIBE_SD_STATUS_DONE       BIT(1)
#define TRIBE_SD_STATUS_ERROR      BIT(2)
#define TRIBE_SD_STATUS_RX_VALID   BIT(3)
#define TRIBE_SD_STATUS_TX_READY   BIT(4)
#define TRIBE_SD_STATUS_DESC_READY BIT(6)

#define TRIBE_SD_DESC_READY        BIT(0)

#define TRIBE_SD_CMD_READ          0x51
#define TRIBE_SD_CMD_WRITE         0x58
#define TRIBE_SD_TIMEOUT_LOOPS     10000000
#define TRIBE_SD_MAX_DESCS         32
#define TRIBE_SD_MAX_HW_SECTORS    64

struct tribe_sd {
	struct device *dev;
	void __iomem *regs;
	struct request_queue *queue;
	struct gendisk *disk;
	struct mutex lock;
	int major;
	sector_t capacity_sectors;
};

static inline u32 tribe_sd_readl(struct tribe_sd *sd, u32 reg)
{
	return readl(sd->regs + reg);
}

static inline void tribe_sd_writel(struct tribe_sd *sd, u32 reg, u32 value)
{
	writel(value, sd->regs + reg);
}

static int tribe_sd_wait_status(struct tribe_sd *sd, u32 mask, u32 *status)
{
	u32 value = 0;
	u32 i;

	for (i = 0; i < TRIBE_SD_TIMEOUT_LOOPS; ++i) {
		value = tribe_sd_readl(sd, TRIBE_SD_REG_STATUS);
		if (value & mask) {
			if (status)
				*status = value;
			return 0;
		}
		cpu_relax();
	}

	if (status)
		*status = value;
	dev_err(sd->dev, "timeout waiting for status mask 0x%x, status 0x%x\n",
		mask, value);
	return -ETIMEDOUT;
}

static int tribe_sd_finish(struct tribe_sd *sd)
{
	u32 status;
	int ret;

	ret = tribe_sd_wait_status(sd, TRIBE_SD_STATUS_DONE, &status);
	if (ret)
		return ret;
	if (status & TRIBE_SD_STATUS_ERROR)
		return -EIO;
	return 0;
}

static int tribe_sd_wait_desc_ready(struct tribe_sd *sd)
{
	u32 status;

	return tribe_sd_wait_status(sd, TRIBE_SD_STATUS_DESC_READY, &status);
}

static int tribe_sd_push_desc(struct tribe_sd *sd, phys_addr_t addr, u32 len)
{
	int ret;

	if (!len)
		return 0;

	ret = tribe_sd_wait_desc_ready(sd);
	if (ret)
		return ret;

	tribe_sd_writel(sd, TRIBE_SD_REG_DMA_DESC_ADDR, (u32)addr);
	tribe_sd_writel(sd, TRIBE_SD_REG_DMA_DESC_LEN, len);
	tribe_sd_writel(sd, TRIBE_SD_REG_DMA_DESC_PUSH, 1);
	return 0;
}

static int __maybe_unused tribe_sd_transfer(struct tribe_sd *sd, struct bio *bio)
{
	struct bvec_iter iter;
	struct bio_vec bvec;
	dma_addr_t dma_addrs[TRIBE_SD_MAX_DESCS];
	u32 dma_lens[TRIBE_SD_MAX_DESCS];
	sector_t sector = bio->bi_iter.bi_sector;
	u32 descs = 0;
	u32 total = bio->bi_iter.bi_size;
	enum dma_data_direction dma_dir =
		bio_data_dir(bio) == WRITE ? DMA_TO_DEVICE : DMA_FROM_DEVICE;
	int ret = 0;
	u32 i;

	if ((total & (TRIBE_SD_SECTOR_BYTES - 1)) ||
	    sector + (total >> 9) > sd->capacity_sectors)
		return -EIO;

	tribe_sd_writel(sd, TRIBE_SD_REG_CONTROL, TRIBE_SD_CTRL_CLEAR_DONE);
	tribe_sd_writel(sd, TRIBE_SD_REG_CMD,
			bio_data_dir(bio) == WRITE ? TRIBE_SD_CMD_WRITE : TRIBE_SD_CMD_READ);
	tribe_sd_writel(sd, TRIBE_SD_REG_ARG, (u32)sector);
	tribe_sd_writel(sd, TRIBE_SD_REG_LEN, total);

	bio_for_each_segment(bvec, bio, iter) {
		dma_addr_t addr;

		if (descs >= TRIBE_SD_MAX_DESCS) {
			ret = -EIO;
			goto unmap;
		}

		addr = dma_map_page(sd->dev, bvec.bv_page, bvec.bv_offset,
				    bvec.bv_len, dma_dir);
		if (dma_mapping_error(sd->dev, addr)) {
			ret = -EIO;
			goto unmap;
		}

		dma_addrs[descs] = addr;
		dma_lens[descs] = bvec.bv_len;
		++descs;

		ret = tribe_sd_push_desc(sd, addr, bvec.bv_len);
		if (ret)
			goto unmap;
	}

	tribe_sd_writel(sd, TRIBE_SD_REG_CONTROL,
			TRIBE_SD_CTRL_START | TRIBE_SD_CTRL_DMA |
			(bio_data_dir(bio) == WRITE ? TRIBE_SD_CTRL_WRITE : 0));
	ret = tribe_sd_finish(sd);

unmap:
	for (i = 0; i < descs; ++i)
		dma_unmap_page(sd->dev, dma_addrs[i], dma_lens[i], dma_dir);
	return ret;
}

static int tribe_sd_pio_transfer(struct tribe_sd *sd, struct bio *bio)
{
	struct bvec_iter iter;
	struct bio_vec bvec;
	sector_t sector = bio->bi_iter.bi_sector;
	u32 total = bio->bi_iter.bi_size;
	u32 done = 0;
	bool write = bio_data_dir(bio) == WRITE;
	int ret = 0;

	if ((total & (TRIBE_SD_SECTOR_BYTES - 1)) ||
	    sector + (total >> 9) > sd->capacity_sectors)
		return -EIO;

	/*
	 * Tribe currently has no Linux-visible D-cache maintenance operation.
	 * Descriptor DMA can update RAM behind stale CPU cache lines, so the
	 * in-kernel block path uses PIO until cache-coherent DMA is available.
	 */
	tribe_sd_writel(sd, TRIBE_SD_REG_CONTROL, TRIBE_SD_CTRL_CLEAR_DONE);
	tribe_sd_writel(sd, TRIBE_SD_REG_CMD,
			write ? TRIBE_SD_CMD_WRITE : TRIBE_SD_CMD_READ);
	tribe_sd_writel(sd, TRIBE_SD_REG_ARG, (u32)sector);
	tribe_sd_writel(sd, TRIBE_SD_REG_LEN, total);
	tribe_sd_writel(sd, TRIBE_SD_REG_CONTROL,
			TRIBE_SD_CTRL_START | (write ? TRIBE_SD_CTRL_WRITE : 0));

	bio_for_each_segment(bvec, bio, iter) {
		void *kaddr = kmap_atomic(bvec.bv_page);
		u8 *buf = (u8 *)kaddr + bvec.bv_offset;
		u32 i;

		for (i = 0; i < bvec.bv_len; ++i, ++done) {
			u32 status;

			if (done >= total) {
				break;
			}

			if (write) {
				ret = tribe_sd_wait_status(sd, TRIBE_SD_STATUS_TX_READY, &status);
				if (ret) {
					kunmap_atomic(kaddr);
					return ret;
				}
				tribe_sd_writel(sd, TRIBE_SD_REG_TXDATA, buf[i]);
			}
			else {
				ret = tribe_sd_wait_status(sd, TRIBE_SD_STATUS_RX_VALID, &status);
				if (ret) {
					kunmap_atomic(kaddr);
					return ret;
				}
				buf[i] = tribe_sd_readl(sd, TRIBE_SD_REG_RXDATA) & 0xffu;
			}
		}

		kunmap_atomic(kaddr);
	}

	if (done != total)
		return -EIO;
	return tribe_sd_finish(sd);
}

static blk_qc_t tribe_sd_make_request(struct request_queue *queue, struct bio *bio)
{
	struct tribe_sd *sd = queue->queuedata;
	int ret;

	mutex_lock(&sd->lock);
	ret = tribe_sd_transfer(sd, bio);
	mutex_unlock(&sd->lock);

	if (ret)
		bio_io_error(bio);
	else
		bio_endio(bio);
	return ret ? BLK_QC_T_NONE : BLK_QC_T_NONE;
}

static int tribe_sd_getgeo(struct block_device *bdev, struct hd_geometry *geo)
{
	struct tribe_sd *sd = bdev->bd_disk->private_data;

	geo->heads = 4;
	geo->sectors = 16;
	geo->cylinders = sd->capacity_sectors / (geo->heads * geo->sectors);
	return 0;
}

static const struct block_device_operations tribe_sd_fops = {
	.owner = THIS_MODULE,
	.getgeo = tribe_sd_getgeo,
};

static int tribe_sd_probe(struct platform_device *pdev)
{
	struct tribe_sd *sd;
	struct resource *res;
	u32 capacity = 131072;
	int ret;

	sd = devm_kzalloc(&pdev->dev, sizeof(*sd), GFP_KERNEL);
	if (!sd)
		return -ENOMEM;

	sd->dev = &pdev->dev;
	mutex_init(&sd->lock);
	of_property_read_u32(pdev->dev.of_node, "tribe,capacity-sectors", &capacity);
	sd->capacity_sectors = capacity;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	sd->regs = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(sd->regs))
		return PTR_ERR(sd->regs);

	sd->major = register_blkdev(0, TRIBE_SD_NAME);
	if (sd->major < 0)
		return sd->major;

	sd->queue = blk_alloc_queue(GFP_KERNEL);
	if (!sd->queue) {
		ret = -ENOMEM;
		goto err_unregister;
	}
	blk_queue_make_request(sd->queue, tribe_sd_make_request);
	blk_queue_logical_block_size(sd->queue, TRIBE_SD_SECTOR_BYTES);
	blk_queue_physical_block_size(sd->queue, TRIBE_SD_SECTOR_BYTES);
	blk_queue_max_hw_sectors(sd->queue, TRIBE_SD_MAX_HW_SECTORS);
	blk_queue_max_segments(sd->queue, TRIBE_SD_MAX_DESCS);
	blk_queue_max_segment_size(sd->queue, PAGE_SIZE);
	blk_queue_flag_set(QUEUE_FLAG_NONROT, sd->queue);
	blk_queue_flag_clear(QUEUE_FLAG_ADD_RANDOM, sd->queue);
	sd->queue->queuedata = sd;

	sd->disk = alloc_disk(16);
	if (!sd->disk) {
		ret = -ENOMEM;
		goto err_queue;
	}
	sd->disk->major = sd->major;
	sd->disk->first_minor = 0;
	sd->disk->fops = &tribe_sd_fops;
	sd->disk->private_data = sd;
	sd->disk->queue = sd->queue;
	sd->disk->flags = GENHD_FL_EXT_DEVT;
	snprintf(sd->disk->disk_name, DISK_NAME_LEN, TRIBE_SD_NAME);
	set_capacity(sd->disk, sd->capacity_sectors);

	platform_set_drvdata(pdev, sd);
	tribe_sd_writel(sd, TRIBE_SD_REG_IRQ_ENABLE, 0);
	tribe_sd_writel(sd, TRIBE_SD_REG_IRQ_PENDING, 1);
	add_disk(sd->disk);

	dev_info(&pdev->dev, "registered %s, %llu sectors\n", TRIBE_SD_NAME,
		 (unsigned long long)sd->capacity_sectors);
	return 0;

err_queue:
	blk_cleanup_queue(sd->queue);
err_unregister:
	unregister_blkdev(sd->major, TRIBE_SD_NAME);
	return ret;
}

static int tribe_sd_remove(struct platform_device *pdev)
{
	struct tribe_sd *sd = platform_get_drvdata(pdev);

	del_gendisk(sd->disk);
	put_disk(sd->disk);
	blk_cleanup_queue(sd->queue);
	unregister_blkdev(sd->major, TRIBE_SD_NAME);
	return 0;
}

static const struct of_device_id tribe_sd_of_match[] = {
	{ .compatible = "cpphdl,tribe-sd" },
	{ }
};
MODULE_DEVICE_TABLE(of, tribe_sd_of_match);

static struct platform_driver tribe_sd_driver = {
	.probe = tribe_sd_probe,
	.remove = tribe_sd_remove,
	.driver = {
		.name = TRIBE_SD_NAME,
		.of_match_table = tribe_sd_of_match,
	},
};
module_platform_driver(tribe_sd_driver);

MODULE_DESCRIPTION("CppHDL Tribe SD block driver");
MODULE_LICENSE("GPL");
