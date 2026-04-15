/*
 * tilepack.c — Single-file offline tile storage reader.
 *
 * Loads a .tilepack index into RAM and serves tile data via fseek/fread.
 * Binary search on the sorted index gives O(log n) lookup.
 */

#include "tilepack.h"
#include "logging.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------- helpers ---------------------------------------------------- */

/* Comparison key: pack zoom into top 8 bits, then x, then y.
 * This matches the sort order the Python packer uses. */
static inline int entry_cmp(const TilePackEntry *a, const TilePackEntry *b)
{
    if (a->zoom != b->zoom) return (int)a->zoom - (int)b->zoom;
    if (a->x    != b->x)    return (a->x < b->x) ? -1 : 1;
    if (a->y    != b->y)    return (a->y < b->y) ? -1 : 1;
    return 0;
}

/* ---------- public API ------------------------------------------------- */

bool tilepack_open(TilePack *pack, const char *path)
{
    memset(pack, 0, sizeof(*pack));

    FILE *fp = fopen(path, "rb");
    if (!fp) return false;

    /* Read header (16 bytes) */
    uint8_t hdr[TILEPACK_HEADER_SIZE];
    if (fread(hdr, 1, TILEPACK_HEADER_SIZE, fp) != TILEPACK_HEADER_SIZE) {
        log_write("[TILEPACK] %s: header read failed\n", path);
        fclose(fp);
        return false;
    }

    /* Validate magic */
    if (memcmp(hdr, TILEPACK_MAGIC, 4) != 0) {
        log_write("[TILEPACK] %s: bad magic\n", path);
        fclose(fp);
        return false;
    }

    /* Parse header fields (little-endian) */
    uint32_t version, tile_count, data_offset;
    memcpy(&version,     hdr + 4,  4);
    memcpy(&tile_count,  hdr + 8,  4);
    memcpy(&data_offset, hdr + 12, 4);

    if (version != TILEPACK_VERSION) {
        log_write("[TILEPACK] %s: unsupported version %u\n", path, (unsigned)version);
        fclose(fp);
        return false;
    }

    if (tile_count == 0) {
        log_write("[TILEPACK] %s: empty pack\n", path);
        fclose(fp);
        return false;
    }

    /* Sanity: index must fit between header and data_offset */
    uint32_t expected_data = TILEPACK_HEADER_SIZE + tile_count * TILEPACK_ENTRY_SIZE;
    if (data_offset < expected_data) {
        log_write("[TILEPACK] %s: data_offset %u too small for %u entries\n",
                  path, (unsigned)data_offset, (unsigned)tile_count);
        fclose(fp);
        return false;
    }

    /* Allocate and read index */
    TilePackEntry *index = (TilePackEntry *)malloc(tile_count * sizeof(TilePackEntry));
    if (!index) {
        log_write("[TILEPACK] %s: malloc failed for %u entries\n", path, (unsigned)tile_count);
        fclose(fp);
        return false;
    }

    size_t index_bytes = tile_count * TILEPACK_ENTRY_SIZE;
    if (fread(index, 1, index_bytes, fp) != index_bytes) {
        log_write("[TILEPACK] %s: index read failed\n", path);
        free(index);
        fclose(fp);
        return false;
    }

    pack->fp          = fp;
    pack->tile_count  = tile_count;
    pack->data_offset = data_offset;
    pack->index       = index;
    LightLock_Init(&pack->lock);

    log_write("[TILEPACK] loaded %s: %u tiles, data@%u\n",
              path, (unsigned)tile_count, (unsigned)data_offset);
    return true;
}

void tilepack_close(TilePack *pack)
{
    if (!pack) return;
    if (pack->fp) {
        fclose((FILE *)pack->fp);
        pack->fp = NULL;
    }
    if (pack->index) {
        free(pack->index);
        pack->index = NULL;
    }
    pack->tile_count  = 0;
    pack->data_offset = 0;
}

uint8_t *tilepack_read_tile(TilePack *pack, int zoom, int x, int y, size_t *out_size)
{
    if (!pack || !pack->index || !pack->fp || pack->tile_count == 0)
        return NULL;

    /* Binary search for (zoom, x, y) */
    TilePackEntry key;
    memset(&key, 0, sizeof(key));
    key.zoom = (uint8_t)zoom;
    key.x    = (uint32_t)x;
    key.y    = (uint32_t)y;

    uint32_t lo = 0, hi = pack->tile_count;
    const TilePackEntry *found = NULL;

    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        int cmp = entry_cmp(&key, &pack->index[mid]);
        if (cmp == 0) {
            found = &pack->index[mid];
            break;
        } else if (cmp < 0) {
            hi = mid;
        } else {
            lo = mid + 1;
        }
    }

    if (!found) return NULL;

    /* Sanity check tile size (1 MB cap, same as disk cache) */
    if (found->size == 0 || found->size > 1024 * 1024)
        return NULL;

    uint8_t *buf = (uint8_t *)malloc(found->size);
    if (!buf) return NULL;

    /* Seek to data_offset + entry offset, read tile bytes.
     * Lock protects the fseek+fread pair from concurrent worker threads. */
    LightLock_Lock(&pack->lock);
    FILE *fp = (FILE *)pack->fp;
    long file_pos = (long)pack->data_offset + (long)found->offset;

    if (fseek(fp, file_pos, SEEK_SET) != 0) {
        LightLock_Unlock(&pack->lock);
        free(buf);
        return NULL;
    }

    size_t nread = fread(buf, 1, found->size, fp);
    LightLock_Unlock(&pack->lock);

    if (nread != found->size) {
        free(buf);
        return NULL;
    }

    if (out_size) *out_size = found->size;
    return buf;
}
