#ifndef TILEPACK_H
#define TILEPACK_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <3ds/synchronization.h>

/*
 * Tilepack — single-file offline tile storage for 3DS Google Maps.
 *
 * Instead of 15,000+ individual tile files on the SD card (which take forever
 * to transfer due to FAT32 per-file overhead), tiles are packed into one binary
 * file with an in-memory index for O(log n) lookup.
 *
 * File format (all integers little-endian):
 *
 *   Offset  Size  Description
 *   ------  ----  -----------
 *   0       4     Magic bytes: "3DTP"
 *   4       4     Version: 1
 *   8       4     Tile count (N)
 *   12      4     Data section offset from file start
 *
 *   16 .. 16+N*16   Index entries (sorted by key for binary search)
 *     Each entry (16 bytes):
 *       uint8_t  zoom
 *       uint8_t  reserved[3]
 *       uint32_t x
 *       uint32_t y
 *       uint32_t offset   (from start of data section)
 *       uint32_t size     (tile data size in bytes)
 *
 *   Data section: concatenated raw tile bytes (PNG or JPEG)
 *
 * Pack files live at:
 *   sdmc:/3ds_google_maps/tiles/street.tilepack
 *   sdmc:/3ds_google_maps/tiles/sat.tilepack
 */

#define TILEPACK_MAGIC       "3DTP"
#define TILEPACK_VERSION     1
#define TILEPACK_HEADER_SIZE 16
#define TILEPACK_ENTRY_SIZE 20

/* On-disk and in-memory index entry (16 bytes, packed) */
typedef struct __attribute__((packed)) {
    uint8_t  zoom;
    uint8_t  reserved[3];
    uint32_t x;
    uint32_t y;
    uint32_t offset;    /* byte offset from start of data section */
    uint32_t size;      /* tile data size in bytes */
} TilePackEntry;

/* Loaded tilepack handle */
typedef struct {
    void        *fp;          /* FILE* kept open for reads */
    uint32_t     tile_count;
    uint32_t     data_offset; /* absolute file offset where data section starts */
    TilePackEntry *index;     /* malloc'd array of tile_count entries */
    LightLock    lock;        /* protects fseek+fread from concurrent threads */
} TilePack;

/*
 * Open a tilepack file and load its index into RAM.
 * Returns true on success. On failure, pack is zeroed.
 * The file handle stays open for subsequent tile reads.
 */
bool tilepack_open(TilePack *pack, const char *path);

/*
 * Close a tilepack and free its index.
 * Safe to call on a zeroed/unloaded TilePack.
 */
void tilepack_close(TilePack *pack);

/*
 * Look up a tile in the pack index using binary search.
 * On success, returns a malloc'd buffer with the raw tile bytes
 * (PNG or JPEG), sets *out_size, and the caller must free() it.
 * Returns NULL if the tile is not in this pack.
 */
uint8_t *tilepack_read_tile(TilePack *pack, int zoom, int x, int y, size_t *out_size);

#endif /* TILEPACK_H */
