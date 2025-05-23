/*
 * Copyright (c) 2017 Nordic Semiconductor ASA
 * Copyright (c) 2016-2017 Linaro Limited
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stddef.h>
#include <errno.h>
#include <string.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/kernel.h>
#include <zephyr/init.h>
#include <zephyr/logging/log.h>

#include <zephyr/sys/__assert.h>
#include <zephyr/sys/byteorder.h>

#include "bootutil/bootutil_public.h"
#include <zephyr/dfu/mcuboot.h>

#if defined(CONFIG_MCUBOOT_BOOTLOADER_MODE_RAM_LOAD)
#include <bootutil/boot_status.h>
#include <zephyr/retention/blinfo.h>
#endif

#include "mcuboot_priv.h"

LOG_MODULE_REGISTER(mcuboot_dfu, LOG_LEVEL_DBG);

/*
 * Helpers for image headers and trailers, as defined by mcuboot.
 */

/*
 * Strict defines: the definitions in the following block contain
 * values which are MCUboot implementation requirements.
 */

/* Header: */
#define BOOT_HEADER_MAGIC_V1 0x96f3b83d
#define BOOT_HEADER_SIZE_V1 32

enum IMAGE_INDEXES {
	IMAGE_INDEX_INVALID = -1,
	IMAGE_INDEX_0,
	IMAGE_INDEX_1,
	IMAGE_INDEX_2
};

#if USE_PARTITION_MANAGER
#include <pm_config.h>

#if CONFIG_MCUBOOT_APPLICATION_IMAGE_NUMBER != -1
/* Sysbuild */
#ifdef CONFIG_MCUBOOT
/* lib is part of MCUboot -> operate on the primary application slot */
#define ACTIVE_SLOT_FLASH_AREA_ID	PM_MCUBOOT_PRIMARY_ID
#else
/* TODO: Add firmware loader support */
/* lib is part of the app -> operate on active slot */
#if defined(CONFIG_NCS_IS_VARIANT_IMAGE)
#define ACTIVE_SLOT_FLASH_AREA_ID	PM_MCUBOOT_SECONDARY_ID
#else
#define ACTIVE_SLOT_FLASH_AREA_ID	PM_MCUBOOT_PRIMARY_ID
#endif
#endif /* CONFIG_MCUBOOT */
#else
/* Legacy child/parent */
#if CONFIG_BUILD_WITH_TFM
	#define PM_ADDRESS_OFFSET (PM_MCUBOOT_PAD_SIZE + PM_TFM_SIZE)
#else
	#define PM_ADDRESS_OFFSET (PM_MCUBOOT_PAD_SIZE)
#endif

#ifdef CONFIG_MCUBOOT
	/* lib is part of MCUboot -> operate on the primary application slot */
	#define ACTIVE_SLOT_FLASH_AREA_ID	PM_MCUBOOT_PRIMARY_ID
#else
	/* lib is part of the App -> operate on active slot */
#if (PM_ADDRESS - PM_ADDRESS_OFFSET) == PM_MCUBOOT_PRIMARY_ADDRESS
	#define ACTIVE_SLOT_FLASH_AREA_ID	PM_MCUBOOT_PRIMARY_ID
#elif (PM_ADDRESS - PM_ADDRESS_OFFSET) == PM_MCUBOOT_SECONDARY_ADDRESS
	#define ACTIVE_SLOT_FLASH_AREA_ID	PM_MCUBOOT_SECONDARY_ID
#else
	#error Missing partition definitions.
#endif
#endif /* CONFIG_MCUBOOT */
#endif /* CONFIG_MCUBOOT_APPLICATION_IMAGE_NUMBER != -1 */

#else

#if defined(CONFIG_MCUBOOT_BOOTLOADER_MODE_RAM_LOAD)
/* For RAM LOAD mode, the active image must be fetched from the bootloader */
#define ACTIVE_SLOT_FLASH_AREA_ID boot_fetch_active_slot()
#define INVALID_SLOT_ID 255
#else
/* Get active partition. zephyr,code-partition chosen node must be defined */
#define ACTIVE_SLOT_FLASH_AREA_ID DT_FIXED_PARTITION_ID(DT_CHOSEN(zephyr_code_partition))
#endif

#endif /* USE_PARTITION_MANAGER */

/*
 * Raw (on-flash) representation of the v1 image header.
 */
struct mcuboot_v1_raw_header {
	uint32_t header_magic;
	uint32_t image_load_address;
	uint16_t header_size;
	uint16_t pad;
	uint32_t image_size;
	uint32_t image_flags;
	struct {
		uint8_t major;
		uint8_t minor;
		uint16_t revision;
		uint32_t build_num;
	} version;
	uint32_t pad2;
} __packed;

/*
 * End of strict defines
 */

#if defined(CONFIG_MCUBOOT_BOOTLOADER_MODE_RAM_LOAD)
uint8_t boot_fetch_active_slot(void)
{
	int rc;
	uint8_t slot;

	rc = blinfo_lookup(BLINFO_RUNNING_SLOT, &slot, sizeof(slot));

	if (rc <= 0) {
		LOG_ERR("Failed to fetch active slot: %d", rc);

		return INVALID_SLOT_ID;
	}

	LOG_DBG("Active slot: %d", slot);

	return slot;
}
#else  /* CONFIG_MCUBOOT_BOOTLOADER_MODE_RAM_LOAD */
uint8_t boot_fetch_active_slot(void)
{
	return ACTIVE_SLOT_FLASH_AREA_ID;
}
#endif /* CONFIG_MCUBOOT_BOOTLOADER_MODE_RAM_LOAD */

#if defined(CONFIG_MCUBOOT_BOOTLOADER_MODE_SWAP_USING_OFFSET)
size_t boot_get_image_start_offset(uint8_t area_id)
{
	size_t off = 0;
	int image = IMAGE_INDEX_INVALID;

	if (area_id == FIXED_PARTITION_ID(slot1_partition)) {
		image = IMAGE_INDEX_0;
#if FIXED_PARTITION_EXISTS(slot3_partition)
	} else if (area_id == FIXED_PARTITION_ID(slot3_partition)) {
		image = IMAGE_INDEX_1;
#endif
#if FIXED_PARTITION_EXISTS(slot5_partition)
	} else if (area_id == FIXED_PARTITION_ID(slot5_partition)) {
		image = IMAGE_INDEX_2;
#endif
	}

	if (image != IMAGE_INDEX_INVALID) {
		/* Need to check status from primary slot to get correct offset for secondary
		 * slot image header
		 */
		const struct flash_area *fa;
		uint32_t num_sectors = SWAP_USING_OFFSET_SECTOR_UPDATE_BEGIN;
		struct flash_sector sector_data;
		int rc;

		rc = flash_area_open(area_id, &fa);
		if (rc) {
			LOG_ERR("Flash open area %u failed: %d", area_id, rc);
			goto done;
		}

		if (mcuboot_swap_type_multi(image) != BOOT_SWAP_TYPE_REVERT) {
			/* For swap using offset mode, the image starts in the second sector of
			 * the upgrade slot, so apply the offset when this is needed, do this by
			 * getting information on first sector only, this is expected to return an
			 * error as there are more slots, so allow the not enough memory error
			 */
			rc = flash_area_get_sectors(area_id, &num_sectors, &sector_data);
			if ((rc != 0 && rc != -ENOMEM) ||
			    num_sectors != SWAP_USING_OFFSET_SECTOR_UPDATE_BEGIN) {
				LOG_ERR("Failed to get sector details: %d", rc);
			} else {
				off = sector_data.fs_size;
			}
		}

		flash_area_close(fa);
	}

done:
	LOG_DBG("Start offset for area %u: 0x%x", area_id, off);
	return off;
}
#endif /* CONFIG_MCUBOOT_BOOTLOADER_MODE_SWAP_USING_OFFSET */

static int boot_read_v1_header(uint8_t area_id,
			       struct mcuboot_v1_raw_header *v1_raw)
{
	const struct flash_area *fa;
	int rc;
	size_t off = boot_get_image_start_offset(area_id);

	rc = flash_area_open(area_id, &fa);
	if (rc) {
		return rc;
	}

	/*
	 * Read and validty-check the raw header.
	 */
	rc = flash_area_read(fa, off, v1_raw, sizeof(*v1_raw));
	flash_area_close(fa);
	if (rc) {
		return rc;
	}

	v1_raw->header_magic = sys_le32_to_cpu(v1_raw->header_magic);
	v1_raw->header_size = sys_le16_to_cpu(v1_raw->header_size);

	/*
	 * Validity checks.
	 *
	 * Larger values in header_size than BOOT_HEADER_SIZE_V1 are
	 * possible, e.g. if Zephyr was linked with
	 * CONFIG_ROM_START_OFFSET > BOOT_HEADER_SIZE_V1.
	 */
	if ((v1_raw->header_magic != BOOT_HEADER_MAGIC_V1) ||
	    (v1_raw->header_size < BOOT_HEADER_SIZE_V1)) {
		return -EIO;
	}

	v1_raw->image_load_address =
		sys_le32_to_cpu(v1_raw->image_load_address);
	v1_raw->header_size = sys_le16_to_cpu(v1_raw->header_size);
	v1_raw->image_size = sys_le32_to_cpu(v1_raw->image_size);
	v1_raw->image_flags = sys_le32_to_cpu(v1_raw->image_flags);
	v1_raw->version.revision =
		sys_le16_to_cpu(v1_raw->version.revision);
	v1_raw->version.build_num =
		sys_le32_to_cpu(v1_raw->version.build_num);

	return 0;
}

int boot_read_bank_header(uint8_t area_id,
			  struct mcuboot_img_header *header,
			  size_t header_size)
{
	int rc;
	struct mcuboot_v1_raw_header v1_raw;
	struct mcuboot_img_sem_ver *sem_ver;
	size_t v1_min_size = (sizeof(uint32_t) +
			      sizeof(struct mcuboot_img_header_v1));

	/*
	 * Only version 1 image headers are supported.
	 */
	if (header_size < v1_min_size) {
		return -ENOMEM;
	}
	rc = boot_read_v1_header(area_id, &v1_raw);
	if (rc) {
		return rc;
	}

	/*
	 * Copy just the fields we care about into the return parameter.
	 *
	 * - header_magic:       skip (only used to check format)
	 * - image_load_address: skip (only matters for PIC code)
	 * - header_size:        skip (only used to check format)
	 * - image_size:         include
	 * - image_flags:        skip (all unsupported or not relevant)
	 * - version:            include
	 */
	header->mcuboot_version = 1U;
	header->h.v1.image_size = v1_raw.image_size;
	sem_ver = &header->h.v1.sem_ver;
	sem_ver->major = v1_raw.version.major;
	sem_ver->minor = v1_raw.version.minor;
	sem_ver->revision = v1_raw.version.revision;
	sem_ver->build_num = v1_raw.version.build_num;
	return 0;
}

int mcuboot_swap_type_multi(int image_index)
{
	return boot_swap_type_multi(image_index);
}

int mcuboot_swap_type(void)
{
#ifdef FLASH_AREA_IMAGE_SECONDARY
	return boot_swap_type();
#else
	return BOOT_SWAP_TYPE_NONE;
#endif

}

int boot_request_upgrade(int permanent)
{
#ifdef FLASH_AREA_IMAGE_SECONDARY
	int rc;

	rc = boot_set_pending(permanent);
	if (rc) {
		return -EFAULT;
	}
#endif /* FLASH_AREA_IMAGE_SECONDARY */
	return 0;
}

int boot_request_upgrade_multi(int image_index, int permanent)
{
	int rc;

	rc = boot_set_pending_multi(image_index, permanent);
	if (rc) {
		return -EFAULT;
	}
	return 0;
}

bool boot_is_img_confirmed(void)
{
	struct boot_swap_state state;
	const struct flash_area *fa;
	int rc;

	rc = flash_area_open(ACTIVE_SLOT_FLASH_AREA_ID, &fa);
	if (rc) {
		return false;
	}

	rc = boot_read_swap_state(fa, &state);
	if (rc != 0) {
		return false;
	}

	if (state.magic == BOOT_MAGIC_UNSET) {
		/* This is initial/preprogramed image.
		 * Such image can neither be reverted nor physically confirmed.
		 * Treat this image as confirmed which ensures consistency
		 * with `boot_write_img_confirmed...()` procedures.
		 */
		return true;
	}

	return state.image_ok == BOOT_FLAG_SET;
}

int boot_write_img_confirmed(void)
{
	const struct flash_area *fa;
	int rc = 0;

	if (flash_area_open(ACTIVE_SLOT_FLASH_AREA_ID, &fa) != 0) {
		return -EIO;
	}

	rc = boot_set_next(fa, true, true);

	flash_area_close(fa);

	return rc;
}

int boot_write_img_confirmed_multi(int image_index)
{
	int rc;

	rc = boot_set_confirmed_multi(image_index);
	if (rc) {
		return -EIO;
	}

	return 0;
}

int boot_erase_img_bank(uint8_t area_id)
{
	const struct flash_area *fa;
	int rc;

	rc = flash_area_open(area_id, &fa);
	if (rc) {
		return rc;
	}

	rc = flash_area_flatten(fa, 0, fa->fa_size);

	flash_area_close(fa);

	return rc;
}

ssize_t boot_get_trailer_status_offset(size_t area_size)
{
	return (ssize_t)area_size - BOOT_MAGIC_SZ - BOOT_MAX_ALIGN * 2;
}

ssize_t boot_get_area_trailer_status_offset(uint8_t area_id)
{
	int rc;
	const struct flash_area *fa;
	ssize_t offset;

	rc = flash_area_open(area_id, &fa);
	if (rc) {
		return rc;
	}

	offset = boot_get_trailer_status_offset(fa->fa_size);

	flash_area_close(fa);

	if (offset < 0) {
		return -EFAULT;
	}

	return offset;
}
