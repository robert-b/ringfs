/*
 * Copyright © 2014 Kosma Moczek <kosma@cloudyourcar.com>
 * This program is free software. It comes without any warranty, to the extent
 * permitted by applicable law. You can redistribute it and/or modify it under
 * the terms of the Do What The Fuck You Want To Public License, Version 2, as
 * published by Sam Hocevar. See the COPYING file for more details.
 */

/**
 * @defgroup ringfs_impl RingFS implementation
 * @details
 *
 * @{
 */

#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "ringfs.h"

/**
 * @defgroup sector
 * @{
 */

enum sector_status
{
    SECTOR_ERASED     = 0xFFFFFFFF, /**< Default state after NOR flash erase. */
    SECTOR_FREE       = 0xFFFFFF00, /**< Sector erased. */
    SECTOR_IN_USE     = 0xFFFF0000, /**< Sector contains valid data. */
    SECTOR_ERASING    = 0xFF000000, /**< Sector should be erased. */
    SECTOR_FORMATTING = 0x00000000, /**< The entire partition is being formatted. */
};

struct sector_header
{
    uint32_t status;
    uint32_t version;
};

// local prototypes
static int32_t  _sector_address(ringfs_t * const fs, int32_t sector_offset);
static int32_t  _sector_get_status(ringfs_t * const fs, int32_t sector, uint32_t * const status);
static int32_t  _sector_set_status(ringfs_t * const fs, int32_t sector, uint32_t status);
static int32_t  _sector_free(ringfs_t * const fs, int32_t sector);

static int32_t  _slot_address(ringfs_t * const fs, struct ringfs_loc * const loc);
static int32_t  _slot_get_status(ringfs_t * const fs, struct ringfs_loc * const loc, uint32_t * const status);
static int32_t  _slot_set_status(ringfs_t * const fs, struct ringfs_loc * const loc, uint32_t status);

static bool _loc_equal(struct ringfs_loc * const a, struct ringfs_loc * const b);
static void _loc_advance_sector(ringfs_t * const fs, struct ringfs_loc * const loc);
static void _loc_advance_slot(ringfs_t * const fs, struct ringfs_loc * const loc);

static int32_t _sector_address(ringfs_t * const fs, int32_t sector_offset)
{
    return (fs->flash->sector_offset + sector_offset) * fs->flash->sector_size;
}

static int32_t _sector_free(ringfs_t * const fs, int32_t sector)
{
    const int32_t sector_addr = _sector_address(fs, sector);
    const int32_t sector_size = fs->flash->sector_size;
    const int32_t offs        = sector_size - (int32_t)sizeof(struct sector_header) + (int32_t)offsetof(struct sector_header, version);

    _sector_set_status(fs, sector, SECTOR_ERASING);
    fs->flash->sector_erase(fs->flash, sector_addr);
    fs->flash->program(
    		            fs->flash,
                        sector_addr + offs,
                        &fs->version,
					    sizeof(fs->version)
					   );
    _sector_set_status(fs, sector, SECTOR_FREE);
    return 0;
}

static int32_t _sector_get_status(ringfs_t * const fs, int32_t sector, uint32_t * const status)
{
    const int32_t sector_addr = _sector_address(fs, sector);
    const int32_t sector_size = fs->flash->sector_size;
    const int32_t offs        = sector_size - (int32_t)sizeof(struct sector_header) + (int32_t)offsetof(struct sector_header, status);
	int32_t       res;

    res = fs->flash->read(
    		              fs->flash,
                          sector_addr + offs,
                          status,
						  (uint32_t)sizeof(int32_t)
						 );

    return res;
}

static int32_t _sector_set_status(ringfs_t * const fs, int32_t sector, uint32_t status)
{
    const int32_t sector_addr = _sector_address(fs, sector);
    const int32_t sector_size = fs->flash->sector_size;
    const int32_t offs        = sector_size - (int32_t)sizeof(struct sector_header) + (int32_t)offsetof(struct sector_header, status);
	int32_t       res;

    res = fs->flash->program(
    		                 fs->flash,
		                     sector_addr + offs,
                             &status,
							 sizeof(status)
							);

    return res;
}

/**
 * @}
 * @defgroup slot
 * @{
 */

enum slot_status
{
    SLOT_ERASED   = 0xFFFFFFFF, /**< Default state after NOR flash erase. */
    SLOT_RESERVED = 0xFFFFFF00, /**< Write started but not yet committed. */
    SLOT_VALID    = 0xFFFF0000, /**< Write committed, slot contains valid data. */
    SLOT_GARBAGE  = 0xFF000000, /**< Slot contents discarded and no longer valid. */
};

struct slot_header
{
    uint32_t status;
};

static int32_t _slot_address(ringfs_t * const fs, struct ringfs_loc * const loc)
{
	const int32_t slot_offs = ((int32_t)sizeof(struct slot_header) + fs->object_size) * loc->slot;

    return _sector_address(fs, loc->sector) + slot_offs;
}

static int32_t _slot_get_status(ringfs_t * const fs, struct ringfs_loc *const loc, uint32_t * const status)
{
	const int32_t offs      = offsetof(struct slot_header, status);
	const int32_t slot_addr = _slot_address(fs, loc);

    return fs->flash->read(fs->flash,
            slot_addr + offs,
            status, sizeof(*status));
}

static int32_t _slot_set_status(ringfs_t * const fs, struct ringfs_loc * const loc, uint32_t status)
{
	const int32_t offs      = offsetof(struct slot_header, status);
	const int32_t slot_addr = _slot_address(fs, loc);

    return fs->flash->program(fs->flash, 
            slot_addr + offs,
            &status, sizeof(status));
}

/**
 * @}
 * @defgroup loc
 * @{
 */

static bool _loc_equal(struct ringfs_loc * const a, struct ringfs_loc * const b)
{
    return (a->sector == b->sector) && (a->slot == b->slot);
}

/** Advance a location to the beginning of the next sector. */
static void _loc_advance_sector(ringfs_t * const fs, struct ringfs_loc * const loc)
{
    loc->slot = 0;
    loc->sector++;
    if (loc->sector >= fs->flash->sector_count)
    {
        loc->sector = 0;
    }

    return;
}

/** Advance a location to the next slot, advancing the sector too if needed. */
static void _loc_advance_slot(ringfs_t * const fs, struct ringfs_loc * const loc)
{
    loc->slot++;
    if (loc->slot >= fs->slots_per_sector)
    {
        _loc_advance_sector(fs, loc);
    }

    return;
}

/**
 * @}
 */

/* And here we go. */

int32_t ringfs_init(ringfs_t * const fs, struct ringfs_flash_partition * const flash, uint32_t version, int32_t object_size)
{
    /* Copy arguments to instance. */
    fs->flash = flash;
    fs->version = version;
    fs->object_size = object_size;

    /* Precalculate commonly used values. */
    fs->slots_per_sector = (fs->flash->sector_size - (int32_t)sizeof(struct sector_header)) /
                           ((int32_t)sizeof(struct slot_header) + fs->object_size);

    fs->cache_filling_level = 0;
    memset(fs->cache, 0, sizeof(fs->cache));

    return 0;
}

int32_t ringfs_format(ringfs_t * const fs)
{
    /* Mark all sectors to prevent half-erased filesystems. */
    for (int32_t sector = 0; sector < fs->flash->sector_count; sector++)
    {
        _sector_set_status(fs, sector, SECTOR_FORMATTING);
    }

    /* Erase, update version, mark as free. */
    for (int32_t sector = 0; sector < fs->flash->sector_count; sector++)
    {
        _sector_free(fs, sector);
    }

    /* Start reading & writing at the first sector. */
    fs->read.sector = 0;
    fs->read.slot = 0;
    fs->write.sector = 0;
    fs->write.slot = 0;
    fs->cursor.sector = 0;
    fs->cursor.slot = 0;

    return 0;
}

int32_t ringfs_scan(ringfs_t * const fs)
{
    uint32_t previous_sector_status = SECTOR_FREE;
    /* The read sector is the first IN_USE sector *after* a FREE sector
     * (or the first one). */
    int32_t read_sector = 0;
    /* The write sector is the last IN_USE sector *before* a FREE sector
     * (or the last one). */
    int32_t write_sector = fs->flash->sector_count - 1;
    /* There must be at least one FREE sector available at all times. */
    bool free_seen = false;
    /* If there's no IN_USE sector, we start at the first one. */
    bool used_seen = false;

    /* Iterate over sectors. */
    for (int32_t sector = 0; sector < fs->flash->sector_count; sector++)
    {
        const int32_t sector_size = fs->flash->sector_size;
        const int32_t offs        = sector_size - (int32_t)sizeof(struct sector_header) + (int32_t)offsetof(struct sector_header, status);
        const int32_t addr        = _sector_address(fs, sector);

        /* Read sector header. */
        struct sector_header header;
        int32_t len = fs->flash->read(fs->flash, addr+offs, &header, sizeof(header));
        (void)len;

        /* Detect partially-formatted partitions. */
        if (header.status == SECTOR_FORMATTING)
        {
            //printf("ringfs_scan: partially formatted partition\r\n");
            return -1;
        }

        /* Detect and fix partially erased sectors. */
        if (header.status == SECTOR_ERASING || header.status == SECTOR_ERASED)
        {
            _sector_free(fs, sector);
            header.status = SECTOR_FREE;
        }

        /* Detect corrupted sectors. */
        if (header.status != SECTOR_FREE && header.status != SECTOR_IN_USE)
        {
            //printf("ringfs_scan: corrupted sector %d\r\n", sector);
            return -1;
        }

        /* Detect obsolete versions. We can't do this earlier because the version
         * could have been invalid due to a partial erase. */
        if (header.version != fs->version)
        {
            //printf("ringfs_scan: incompatible version 0x%08"PRIx32"\r\n", header.version);
            return -1;
        }

        /* Record the presence of a FREE sector. */
        if (header.status == SECTOR_FREE)
        {
            free_seen = true;
        }

        /* Record the presence of a IN_USE sector. */
        if (header.status == SECTOR_IN_USE)
        {
            used_seen = true;
        }

        /* Update read & write sectors according to the above rules. */
        if (header.status == SECTOR_IN_USE && previous_sector_status == SECTOR_FREE)
        {
            read_sector = sector;
        }

        if (header.status == SECTOR_FREE && previous_sector_status == SECTOR_IN_USE)
        {
            write_sector = sector-1;
        }

        previous_sector_status = header.status;
    }

    /* Detect the lack of a FREE sector. */
    if ( !free_seen )
    {
        //printf("ringfs_scan: invariant violated: no FREE sector found\r\n");
        return -1;
    }

    /* Start writing at the first sector if the file system is empty. */
    if ( !used_seen )
    {
        write_sector = 0;
    }

    /* Scan the write sector and skip all occupied slots at the beginning. */
    fs->write.sector = write_sector;
    fs->write.slot = 0;
    while (fs->write.sector == write_sector)
    {
        uint32_t status;
        _slot_get_status(fs, &fs->write, &status);
        if (status == SLOT_ERASED)
        {
            break;
        }

        _loc_advance_slot(fs, &fs->write);
    }
    /* If the sector was full, we're at the beginning of a FREE sector now. */

    /* Position the read head at the start of the first IN_USE sector, then skip
     * over garbage/invalid slots until something of value is found or we reach
     * the write head which means there's no data. */
    fs->read.sector = read_sector;
    fs->read.slot   = 0;
    while (!_loc_equal(&fs->read, &fs->write))
    {
        uint32_t status;
        _slot_get_status(fs, &fs->read, &status);
        if (status == SLOT_VALID)
        {
            break;
        }

        _loc_advance_slot(fs, &fs->read);
    }

    /* Move the read cursor to the read head position. */
    fs->cursor = fs->read;

    return 0;
}

int32_t ringfs_capacity(ringfs_t * const fs)
{
    return fs->slots_per_sector * (fs->flash->sector_count - 1);
}

int32_t ringfs_count_estimate(ringfs_t * const fs)
{
    int32_t sector_diff = (fs->write.sector - fs->read.sector + fs->flash->sector_count) %
        fs->flash->sector_count;

    return sector_diff * fs->slots_per_sector + fs->write.slot - fs->read.slot;
}

int32_t ringfs_count_exact(ringfs_t * const fs)
{
    int32_t count = 0;

    /* Use a temporary loc for iteration. */
    struct ringfs_loc loc = fs->read;
    while (!_loc_equal(&loc, &fs->write))
    {
        uint32_t status;
        _slot_get_status(fs, &loc, &status);
        
        if (status == SLOT_VALID)
        {
            count++;
        }

        _loc_advance_slot(fs, &loc);
    }

    return count;
}

/**
 * @brief append the data chunk to the cache.
 *        each page for the S25FL164 will be filled.
 *
 * @param fs
 * @param object
 * @param size
 * @return
 */
int32_t ringfs_append_to_cache(ringfs_t * const fs, const void * const object, int32_t size)
{
	int32_t res = size;

	if ((size + fs->cache_filling_level) > CACHE_SIZE)
	{
		// write cached page
		res = ringfs_append(fs, fs->cache);
		if (!res)
		{
			res = size;
		}
	    fs->cache_filling_level = 0;
	    //memset(fs->cache, 0, CACHE_SIZE);
	}

	memcpy(&fs->cache[fs->cache_filling_level], object, (size_t)size);
    fs->cache_filling_level += size;

	return res;
}

int32_t ringfs_append(ringfs_t * const fs, const void * const object)
{
    uint32_t status;

    /*
     * There are three sectors involved in appending a value:
     * - the sector where the append happens: it has to be writable
     * - the next sector: it must be free (invariant)
     * - the next-next sector: read & cursor heads are moved there if needed
     */

    /* Make sure the next sector is free. */
    int32_t next_sector = (fs->write.sector+1) % fs->flash->sector_count;
    _sector_get_status(fs, next_sector, &status);
    if (status != SECTOR_FREE)
    {
        /* Next sector must be freed. But first... */

        /* Move the read & cursor heads out of the way. */
        if (fs->read.sector == next_sector)
        {
            _loc_advance_sector(fs, &fs->read);
        }

        if (fs->cursor.sector == next_sector)
        {
            _loc_advance_sector(fs, &fs->cursor);
        }

        /* Free the next sector. */
        _sector_free(fs, next_sector);
    }

    /* Now we can make sure the current write sector is writable. */
    _sector_get_status(fs, fs->write.sector, &status);
    if (status == SECTOR_FREE)
    {
        /* Free sector. Mark as used. */
        _sector_set_status(fs, fs->write.sector, SECTOR_IN_USE);
    }
    else if (status != SECTOR_IN_USE)
    {
        //printf("ringfs_append: corrupted filesystem\r\n");
        return -1;
    }

    /* Preallocate slot. */
    _slot_set_status(fs, &fs->write, SLOT_RESERVED);

    /* Write object. */
    const int32_t slot_addr = _slot_address(fs, &fs->write);
    const int32_t slot_h_size = sizeof(struct slot_header);
    fs->flash->program(
    		           fs->flash,
                       slot_addr + slot_h_size,
                       object,
					   fs->object_size
					  );

    (void) object;

    /* Commit write. */
    _slot_set_status(fs, &fs->write, SLOT_VALID);

    /* Advance the write head. */
    _loc_advance_slot(fs, &fs->write);

    return 0; // fs->object_size;
}


void ringfs_erase_sector(ringfs_t * const fs, const int32_t sector2erase)
{
    /* Free the next sector. */
    _sector_free(fs, sector2erase);
}


int32_t ringfs_fetch(ringfs_t * const fs, void * const object)
{
    /* Advance forward in search of a valid slot. */
    while (!_loc_equal(&fs->cursor, &fs->write))
    {
        uint32_t status;

        _slot_get_status(fs, &fs->cursor, &status);

        if (status == SLOT_VALID)
        {
            fs->flash->read(fs->flash,
                    _slot_address(fs, &fs->cursor) + (int32_t)sizeof(struct slot_header),
                    object, fs->object_size);
            _loc_advance_slot(fs, &fs->cursor);
            return 0;
        }

        _loc_advance_slot(fs, &fs->cursor);
    }

    return -1;
}

int32_t ringfs_discard(ringfs_t * const fs)
{
    while (!_loc_equal(&fs->read, &fs->cursor))
    {
        _slot_set_status(fs, &fs->read, SLOT_GARBAGE);
        _loc_advance_slot(fs, &fs->read);
    }

    return 0;
}

int32_t ringfs_item_discard(ringfs_t * const fs)
{
        _slot_set_status(fs, &fs->read, SLOT_GARBAGE);
        _loc_advance_slot(fs, &fs->read);

    return 0;
}

int32_t ringfs_rewind(ringfs_t * const fs)
{
    fs->cursor = fs->read;
    return 0;
}

#ifdef VISUALIZE_SECTORS_AND_SLOTS
void ringfs_dump(FILE *stream, ringfs_t * const fs)
{
    const char *description;

    fprintf(stream, "RingFS read: {%d,%d} cursor: {%d,%d} write: {%d,%d}\n",
            (int)fs->read.sector, (int)fs->read.slot,
            (int)fs->cursor.sector, (int)fs->cursor.slot,
            (int)fs->write.sector, (int)fs->write.slot);

    for (int sector=0; sector<fs->flash->sector_count; sector++)
    {
        const int32_t addr        = _sector_address(fs, sector);
        const int32_t sector_size = fs->flash->sector_size;
        const int32_t offs        = sector_size - (int32_t)sizeof(struct sector_header) + (int32_t)offsetof(struct sector_header, status);

        /* Read sector header. */
        struct sector_header header;
        fs->flash->read(fs->flash, addr+offs, &header, sizeof(header));

        switch (header.status)
        {
            case SECTOR_ERASED:     description = "ERASED";     break;
            case SECTOR_FREE:       description = "FREE";       break;
            case SECTOR_IN_USE:     description = "IN_USE";     break;
            case SECTOR_ERASING:    description = "ERASING";    break;
            case SECTOR_FORMATTING: description = "FORMATTING"; break;
            default:                description = "UNKNOWN";    break;
        }

        fprintf(stream, "[%04d] [v=0x%08"PRIx32"] [%-10s] ",
                sector, (int)header.version, description);

        for (int32_t slot=0; slot<fs->slots_per_sector; slot++)
        {
            struct ringfs_loc loc = { sector, slot };
            uint32_t status;
            _slot_get_status(fs, &loc, &status);

            switch (status)
            {
                case SLOT_ERASED:   description = "E"; break;
                case SLOT_RESERVED: description = "R"; break;
                case SLOT_VALID:    description = "V"; break;
                case SLOT_GARBAGE:  description = "G"; break;
                default:            description = "?"; break;
            }

            fprintf(stream, "%s", description);
        }

        fprintf(stream, "\n");
    }

    fflush(stream);
}
#endif
/**
 * @}
 */

/* vim: set ts=4 sw=4 et: */
