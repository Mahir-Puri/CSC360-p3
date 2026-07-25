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

// opposite of decode_time - split year back into hi/lo bytes manually.
// this is basically doing our own htons() by hand for the 2-byte year,
// which is fine since these are raw bytes, not a uint16_t field
void encode_time(uint8_t t[7], int year, int mon, int day, int hour, int min, int sec)
{
    t[0] = (year >> 8) & 0xFF;
    t[1] = year & 0xFF;
    t[2] = (uint8_t)mon;
    t[3] = (uint8_t)day;
    t[4] = (uint8_t)hour;
    t[5] = (uint8_t)min;
    t[6] = (uint8_t)sec;
}

// uses time()+localtime() like tutorial 10 slide 15 showed
void now_to_time(uint8_t t[7])
{
    time_t raw = time(NULL);
    struct tm *lt = localtime(&raw);
    encode_time(t, lt->tm_year + 1900, lt->tm_mon + 1, lt->tm_mday, lt->tm_hour, lt->tm_min, lt->tm_sec);
}

// path helpers

// splits something like "/sub_dir/foo.txt" into dirpath="/sub_dir" and
// filename="foo.txt". diskget and diskput both need this since their path
// argument is "which directory" + "which file" mashed together. finds the
// LAST slash with strrchr so it works no matter how many directories deep
// the path goes.
void split_last(const char *path, char *dirpath, char *filename)
{
    const char *slash = strrchr(path, '/');
    if (slash == NULL)
    {
        // no slash at all, just assume they meant the root
        strcpy(dirpath, "/");
        strcpy(filename, path);
        return;
    }
    strcpy(filename, slash + 1);
    if (slash == path)
    {
        // path was like "/foo.txt" - the "directory part" is just root
        strcpy(dirpath, "/");
    }
    else
    {
        size_t len = (size_t)(slash - path);
        memcpy(dirpath, path, len);
        dirpath[len] = '\0';
    }
}

// breaks a directory path like "/a/b/c" into {"a", "b", "c"} so we can
// walk down one directory at a time. uses strtok with "/" as the
// delimiter, which conveniently also skips empty pieces (so "//a//b/"
// still comes out as just {"a", "b"})
int split_path(const char *path, char comps[][FILENAME_LEN], int max_comps)
{
    int count = 0;
    char buf[1024];
    strncpy(buf, path, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    char *tok = strtok(buf, "/");
    while (tok != NULL && count < max_comps)
    {
        strncpy(comps[count], tok, FILENAME_LEN - 1);
        comps[count][FILENAME_LEN - 1] = '\0';
        count++;
        tok = strtok(NULL, "/");
    }
    return count;
}

// directories

// the root directory normally lives in the fixed, contiguous region given
// by the superblock. if diskput ever had to grow it (ran out of the
// original entries), the extra blocks get linked on with the FAT just
// like any other file, starting from the last contiguous block so we
// follow that chain here too, in case it was extended by an earlier run
dirhandle_t load_root(FILE *img, superblock_t *sb, uint32_t *fat)
{
    dirhandle_t dh;
    dh.is_root = 1;
    dh.self_block = 0;
    dh.self_offset = 0;
    dh.n_blocks = sb->root_blocks;
    dh.blocknums = malloc(dh.n_blocks * sizeof(uint32_t));
    if (dh.blocknums == NULL)
        die("Out of memory");
    for (uint32_t i = 0; i < sb->root_blocks; i++)
        dh.blocknums[i] = sb->root_start + i;

    uint32_t last_real = dh.blocknums[dh.n_blocks - 1];
    while (1)
    {
        uint32_t nxt = fat[last_real];
        if (nxt == FAT_RESERVED || nxt == FAT_FREE || nxt == FAT_EOF)
            break;
        dh.blocknums = realloc(dh.blocknums, (dh.n_blocks + 1) * sizeof(uint32_t));
        if (dh.blocknums == NULL)
            die("Out of memory");
        dh.blocknums[dh.n_blocks] = nxt;
        dh.n_blocks++;
        last_real = nxt;
    }

    dh.data = malloc((size_t)dh.n_blocks * sb->block_size);
    if (dh.data == NULL)
        die("Out of memory");
    for (uint32_t i = 0; i < dh.n_blocks; i++)
    {
        fseek(img, (long)dh.blocknums[i] * sb->block_size, SEEK_SET);
        fread(dh.data + (size_t)i * sb->block_size, sb->block_size, 1, img);
    }
    return dh;
}