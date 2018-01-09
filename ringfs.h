/*
 * Copyright © 2014 Kosma Moczek <kosma@cloudyourcar.com>
 * This program is free software. It comes without any warranty, to the extent
 * permitted by applicable law. You can redistribute it and/or modify it under
 * the terms of the Do What The Fuck You Want To Public License, Version 2, as
 * published by Sam Hocevar. See the COPYING file for more details.
 */

#ifndef RINGFS_H
#define RINGFS_H

/**
 * @defgroup ringfs_api RingFS API
 * @{
 */

#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

/**
 * Flash memory + partition descriptor.
 */
struct ringfs_flash_partition
{
    int32_t sector_size;            /**< Sector size, in bytes. */
    int32_t sector_offset;          /**< Partition offset, in sectors. */
    int32_t sector_count;           /**< Partition size, in sectors. */

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
struct ringfs_loc {
    int32_t sector;
    int32_t slot;
};

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
int32_t ringfs_init(ringfs_t *fs, struct ringfs_flash_partition *flash, uint32_t version, int32_t object_size);

/**
 * Format the flash memory.
 *
 * @param fs Initialized RingFS instance.
 * @returns Zero on success, -1 on failure.
 */
int32_t ringfs_format(ringfs_t *fs);

/**
 * Scan the flash memory for a valid filesystem.
 *
 * @param fs Initialized RingFS instance.
 * @returns Zero on success, -1 on failure.
 */
int32_t ringfs_scan(ringfs_t *fs);

/**
 * Calculate maximum RingFS capacity.
 *
 * @param fs Initialized RingFS instance.
 * @returns Maximum capacity on success, -1 on failure.
 */
int32_t ringfs_capacity(ringfs_t *fs);

/**
 * Calculate approximate object count.
 * Runs in O(1).
 *
 * @param fs Initialized RingFS instance.
 * @returns Estimated object count on success, -1 on failure.
 */
int32_t ringfs_count_estimate(ringfs_t *fs);

/**
 * Calculate exact object count.
 * Runs in O(n).
 *
 * @param fs Initialized RingFS instance.
 * @returns Exact object count on success, -1 on failure.
 */
int32_t ringfs_count_exact(ringfs_t *fs);

/**
 * Append an object at the end of the ring. Deletes oldest objects as needed.
 *
 * @param fs Initialized RingFS instance.
 * @param object Object to be stored.
 * @returns Zero on success, -1 on failure.
 */
int32_t ringfs_append(ringfs_t *fs, const void *object);

/**
 * Fetch next object from the ring, oldest-first. Advances read cursor.
 *
 * @param fs Initialized RingFS instance.
 * @param object Buffer to store retrieved object.
 * @returns Zero on success, -1 on failure.
 */
int32_t ringfs_fetch(ringfs_t *fs, void *object);

/**
 * Discard all fetched objects up to the read cursor.
 *
 * @param fs Initialized RingFS instance.
 * @returns Zero on success, -1 on failure.
 */
int32_t ringfs_discard(ringfs_t *fs);

int32_t ringfs_item_discard(ringfs_t *fs);

/**
 * Rewind the read cursor back to the oldest object.
 *
 * @param fs Initialized RingFS instance.
 * @returns Zero on success, -1 on failure.
 */
int32_t ringfs_rewind(ringfs_t *fs);

/**
 * Dump filesystem metadata. For debugging purposes.
 * @param stream File stream to write to.
 * @param fs Initialized RingFS instance.
 */
void ringfs_dump(FILE *stream, ringfs_t *fs);

/**
 * @}
 */

#endif

/* vim: set ts=4 sw=4 et: */
