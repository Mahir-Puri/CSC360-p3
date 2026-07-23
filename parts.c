// parts.c
//
// this is the "single source, #ifdef trick" approach from tutorial 9
// (slide 20 "generating multiple binaries from a single source") - the
// Makefile compiles this file 4 times with a different -D flag each time,
// so main() just calls whichever _run function matches.
//
// all the actual FAT/directory logic lives in this file too, shared by
// all 4 programs.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <arpa/inet.h>

#include "structs.h"

#define MAX_PATH_COMPONENTS 64

// ---------------------------------------------------------------------
// basic helpers
// ---------------------------------------------------------------------

// for stuff that should never really happen (bad disk image, out of
// memory, disk full) - just print an error and bail. the spec says return
// code should be non-zero on failure, so exit(1) covers that.
void die(const char *msg)
{
    fprintf(stderr, "%s\n", msg);
    exit(1);
}

// mode is "rb" for diskinfo/disklist/diskget (read only) and "r+b" for
// diskput (needs to write into the existing image too)
FILE *open_image(const char *path, const char *mode)
{
    FILE *f = fopen(path, mode);
    if (f == NULL)
        die("Could not open disk image");
    return f;
}

// superblock fields are stored big endian on disk (README.md section 5),
// so everything needs to go through ntohs/ntohl once we read it in
void read_superblock(FILE *img, superblock_t *sb)
{
    fseek(img, 0, SEEK_SET);
    if (fread(sb, sizeof(superblock_t), 1, img) != 1)
        die("Could not read superblock");

    sb->block_size = ntohs(sb->block_size);
    sb->fs_size = ntohl(sb->fs_size);
    sb->fat_start = ntohl(sb->fat_start);
    sb->fat_blocks = ntohl(sb->fat_blocks);
    sb->root_start = ntohl(sb->root_start);
    sb->root_blocks = ntohl(sb->root_blocks);

    if (memcmp(sb->fs_id, FS_ID, 8) != 0)
        die("Not a CSC360FS image");
}

// tutorial 9 said to load the whole FAT into memory up front since we'll
// be using it constantly
uint32_t *load_fat(FILE *img, superblock_t *sb, uint32_t *n_entries_out)
{
    uint32_t n_entries = ((uint32_t)sb->fat_blocks * sb->block_size) / 4;
    uint32_t *fat = malloc(n_entries * sizeof(uint32_t));
    if (fat == NULL)
        die("Out of memory");

    fseek(img, (long)sb->fat_start * sb->block_size, SEEK_SET);
    if (fread(fat, sizeof(uint32_t), n_entries, img) != n_entries)
        die("Could not read FAT");

    for (uint32_t i = 0; i < n_entries; i++)
        fat[i] = ntohl(fat[i]);

    *n_entries_out = n_entries;
    return fat;
}

// only diskput actually needs this - writes the whole in-memory FAT back
// to disk at the very end, after all the block allocations are done. has
// to convert back to big endian first since we've been working with the
// host-order copy the whole time
void save_fat(FILE *img, superblock_t *sb, uint32_t *fat, uint32_t n_entries)
{
    uint32_t *disk_fat = malloc(n_entries * sizeof(uint32_t));
    if (disk_fat == NULL)
        die("Out of memory");

    for (uint32_t i = 0; i < n_entries; i++)
        disk_fat[i] = htonl(fat[i]);

    fseek(img, (long)sb->fat_start * sb->block_size, SEEK_SET);
    fwrite(disk_fat, sizeof(uint32_t), n_entries, img);
    free(disk_fat);
}

// just does a linear scan for the first free block, like the tutorial
// suggested ("just doing a linear scan through the FAT is good")
uint32_t alloc_block(uint32_t *fat, uint32_t n_entries)
{
    for (uint32_t i = 0; i < n_entries; i++)
    {
        if (fat[i] == FAT_FREE)
        {
            fat[i] = FAT_EOF; // caller re-chains this if it's not the first block
            return i;
        }
    }
    die("No space left on disk image");
    return 0;
}

// times: 7 raw bytes, YYYYMMDDHHMMSS, all stored as numbers not strings

// the year is the only part that doesn't fit in one byte, so it's stored
// as 2 bytes big endian (first byte is the high byte) everything else
// (month/day/hour/min/sec) is just a plain single byte number
void decode_time(const uint8_t t[7], int *year, int *mon, int *day, int *hour, int *min, int *sec)
{
    *year = (t[0] << 8) | t[1];
    *mon = t[2];
    *day = t[3];
    *hour = t[4];
    *min = t[5];
    *sec = t[6];
}