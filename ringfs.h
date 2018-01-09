/*
 * Copyright © 2014 Kosma Moczek <kosma@cloudyourcar.com>
 * This program is free software. It comes without any warranty, to the extent
 * permitted by applicable law. You can redistribute it and/or modify it under
 * the terms of the Do What The Fuck You Want To Public License, Version 2, as
 * published by Sam Hocevar. See the COPYING file for more details.
 */

#ifndef RINGFS_H
#define RINGFS_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup ringfs_api RingFS API
 * @{
 */

#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

#define VISUALIZE_SECTORS_AND_SLOTS	1
#undef VISUALIZE_SECTORS_AND_SLOTS


/**
 * Flash memory + partition descriptor.
 */
struct ringfs_flash_partition
{
    int32_t sector_size;            /**< Sector size, in bytes. */
    int32_t sector_offset;          /**< Partition offset, in sectors. */
    int32_t sector_count;           /**< Partition size, in sectors. */
    void *  user_data;              /**< User data */

    /**
     * Erase a sector.
     * @param address Any address inside the sector.
     * @returns Zero on success, -1 on failure.
     */
    int32_t (*sector_erase)(struct ringfs_flash_partition *flash, int32_t address);
    /**
     * Program flash memory bits by toggling them from 1 to 0.
     * @param address Start address, in bytes.
     * @param data Data to program.
     * @param size Size of data.
     * @returns size on success, -1 on failure.
     */
    int32_t (*program)(struct ringfs_flash_partition *flash, int32_t address, const void *data, int32_t size);
    /**
     * Read flash memory.
     * @param address Start address, in bytes.
     * @param data Buffer to store read data.
     * @param size Size of data.
     * @returns size on success, -1 on failure.
     */
    int32_t (*read)(struct ringfs_flash_partition *flash, int32_t address, void *data, int32_t size);
};

/** @private */
struct ringfs_loc
{
    int32_t sector;
    int32_t slot;
};

// page cache of 256 bytes contains slot status and cached data bytes
#define CACHE_SIZE (256-4)
/**
 * RingFS instance. Should be initialized with ringfs_init() befure use.
 * Structure fields should not be accessed directly.
 * */
typedef struct ringfs
{
    /* Constant values, set once at ringfs_init(). */
    struct ringfs_flash_partition *flash;
    uint32_t version;
    int32_t object_size;
    /* Cached values. */
    int32_t slots_per_sector;

    /* Read/write pointers. Modified as needed. */
    struct ringfs_loc read;
    struct ringfs_loc write;
    struct ringfs_loc cursor;

    uint8_t	cache[CACHE_SIZE];
    int32_t	cache_filling_level;
} ringfs_t;

/**
 * Initialize a RingFS instance. Must be called before the instance can be used
 * with the other ringfs_* functions.
 *
 * @param fs RingFS instance to be initialized.
 * @param flash Flash memory interface. Must be implemented externally.
 * @param version Object version. Should be incremented whenever the object's
 *                semantics or size change in a backwards-incompatible way.
 * @param object_size Size of one stored object, in bytes.
 * @returns Zero on success, -1 on failure.
 */
int32_t ringfs_init(ringfs_t * const fs, struct ringfs_flash_partition * const flash, uint32_t version, int32_t object_size);

/**
 * Format the flash memory.
 *
 * @param fs Initialized RingFS instance.
 * @returns Zero on success, -1 on failure.
 */
int32_t ringfs_format(ringfs_t * const fs);

/**
 * Scan the flash memory for a valid filesystem.
 *
 * @param fs Initialized RingFS instance.
 * @returns Zero on success, -1 on failure.
 */
int32_t ringfs_scan(ringfs_t * const fs);

/**
 * Calculate maximum RingFS capacity.
 *
 * @param fs Initialized RingFS instance.
 * @returns Maximum capacity on success, -1 on failure.
 */
int32_t ringfs_capacity(ringfs_t * const fs);

/**
 * Calculate approximate object count.
 * Runs in O(1).
 *
 * @param fs Initialized RingFS instance.
 * @returns Estimated object count on success, -1 on failure.
 */
int32_t ringfs_count_estimate(ringfs_t * const fs);

/**
 * Calculate exact object count.
 * Runs in O(n).
 *
 * @param fs Initialized RingFS instance.
 * @returns Exact object count on success, -1 on failure.
 */
int32_t ringfs_count_exact(ringfs_t * const fs);

/**
 * Append an object at the end of the ring. Deletes oldest objects as needed.
 *
 * @param fs Initialized RingFS instance.
 * @param object Object to be stored.
 * @returns Zero on success, -1 on failure.
 */
int32_t ringfs_append(ringfs_t * const fs, const void * const object);

/**
 * Append an object at the end of the cache.
 *
 * @param fs Initialized RingFS instance.
 * @param object Object to be stored.
 * @param size amount of bytes to store
 * @returns Zero on success, -1 on failure.
 */
int32_t ringfs_append_to_cache(ringfs_t * const fs, const void * const object, int32_t size);

/**
 * Fetch next object from the ring, oldest-first. Advances read cursor.
 *
 * @param fs Initialized RingFS instance.
 * @param object Buffer to store retrieved object.
 * @returns Zero on success, -1 on failure.
 */
int32_t ringfs_fetch(ringfs_t * const fs, void * const object);

/**
 * Discard all fetched objects up to the read cursor.
 *
 * @param fs Initialized RingFS instance.
 * @returns Zero on success, -1 on failure.
 */
int32_t ringfs_discard(ringfs_t * const fs);

int32_t ringfs_item_discard(ringfs_t * const fs);

/**
 * Rewind the read cursor back to the oldest object.
 *
 * @param fs Initialized RingFS instance.
 * @returns Zero on success, -1 on failure.
 */
int32_t ringfs_rewind(ringfs_t * const fs);

/**
 * callback definition allowing to erase a sector from a low priority thread
 */
typedef void (*tErase_Sector_callback)(const int32_t sector2erase);

/**
 * @brief sector erase for the given sector
 *
 * @param   fs 				pointer to the initialized RingFS instance.
 * @param	sector2erase	sector to be erased
 */
void ringfs_erase_sector(ringfs_t * const fs, const int32_t sector2erase);

/**
 * Dump filesystem metadata. For debugging purposes.
 * @param stream File stream to write to.
 * @param fs Initialized RingFS instance.
 */
#ifdef VISUALIZE_SECTORS_AND_SLOTS
void ringfs_dump(FILE *stream, ringfs_t * const fs);
#endif
#ifdef __cplusplus
}
#endif

/**
 * @}
 */

#endif

/* vim: set ts=4 sw=4 et: */
