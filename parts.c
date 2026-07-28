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

// subdirectories are just files, allocated through the FAT (tutorial 10,
// slide 16), so this is basically "read the whole file" but we keep the
// block numbers around too since we might need to write into this
// directory later
dirhandle_t load_subdir(FILE *img, superblock_t *sb, uint32_t *fat,
                        uint32_t start_block, uint32_t self_block, uint32_t self_offset)
{
    dirhandle_t dh;
    dh.is_root = 0;
    dh.self_block = self_block;
    dh.self_offset = self_offset;

    uint32_t count = 1;
    uint32_t b = start_block;
    while (fat[b] != FAT_EOF)
    {
        b = fat[b];
        count++;
    }

    dh.n_blocks = count;
    dh.blocknums = malloc(count * sizeof(uint32_t));
    dh.data = malloc((size_t)count * sb->block_size);
    if (dh.blocknums == NULL || dh.data == NULL)
        die("Out of memory");

    b = start_block;
    for (uint32_t i = 0; i < count; i++)
    {
        dh.blocknums[i] = b;
        fseek(img, (long)b * sb->block_size, SEEK_SET);
        fread(dh.data + (size_t)i * sb->block_size, sb->block_size, 1, img);
        b = fat[b];
    }
    return dh;
}

void free_dirhandle(dirhandle_t *dh)
{
    if (dh->data)
        free(dh->data);
    if (dh->blocknums)
        free(dh->blocknums);
    dh->data = NULL;
    dh->blocknums = NULL;
}

// linear search through every directory entry slot for one with a
// matching filename. "slot" here just means the index if you imagine all
// of this directory's blocks laid end to end as one big array of 64-byte
// entries (so slot 8 is the first entry of the 2nd block, etc). gotta
// skip anything with STATUS_USED not set - could just be old, deleted, or
// never-used entries (recall bit 0 of status = "in use").
int dirhandle_find(dirhandle_t *dh, superblock_t *sb, const char *name, dirent_t *out, uint32_t *out_slot)
{
    uint32_t entries_per_block = sb->block_size / sizeof(dirent_t);
    uint32_t n_slots = dh->n_blocks * entries_per_block;

    for (uint32_t slot = 0; slot < n_slots; slot++)
    {
        dirent_t *e = (dirent_t *)(dh->data + (size_t)slot * sizeof(dirent_t));
        if (!(e->status & STATUS_USED))
            continue;
        if (strcmp(e->filename, name) == 0)
        {
            // found it - copy it out and fix up the big-endian fields
            // before handing it back so the caller can just use them
            memcpy(out, e, sizeof(dirent_t));
            out->start_block = ntohl(out->start_block);
            out->num_blocks = ntohl(out->num_blocks);
            out->file_size = ntohl(out->file_size);
            if (out_slot != NULL)
                *out_slot = slot;
            return 1;
        }
    }
    return 0;
}

// same idea as above but looking for any slot that's NOT in use, so
// diskput knows where it can write a brand new entry
int dirhandle_free_slot(dirhandle_t *dh, superblock_t *sb)
{
    uint32_t entries_per_block = sb->block_size / sizeof(dirent_t);
    uint32_t n_slots = dh->n_blocks * entries_per_block;

    for (uint32_t slot = 0; slot < n_slots; slot++)
    {
        dirent_t *e = (dirent_t *)(dh->data + (size_t)slot * sizeof(dirent_t));
        if (!(e->status & STATUS_USED))
            return (int)slot;
    }
    return -1; // directory is full, caller needs to dirhandle_grow() first
}

// appends one more block to a directory that's run out of free entries.
// works for both root and subdirectories - always link through the FAT
// off of the current last block, same idea as growing a regular file
void dirhandle_grow(FILE *img, superblock_t *sb, uint32_t *fat, uint32_t n_fat_entries, dirhandle_t *dh)
{
    uint32_t new_block = alloc_block(fat, n_fat_entries);
    fat[dh->blocknums[dh->n_blocks - 1]] = new_block;
    fat[new_block] = FAT_EOF;

    uint32_t new_n = dh->n_blocks + 1;
    dh->blocknums = realloc(dh->blocknums, new_n * sizeof(uint32_t));
    dh->data = realloc(dh->data, (size_t)new_n * sb->block_size);
    if (dh->blocknums == NULL || dh->data == NULL)
        die("Out of memory");
    dh->blocknums[new_n - 1] = new_block;
    memset(dh->data + (size_t)dh->n_blocks * sb->block_size, 0, sb->block_size);
    dh->n_blocks = new_n;

    uint8_t *zeros = calloc(1, sb->block_size);
    fseek(img, (long)new_block * sb->block_size, SEEK_SET);
    fwrite(zeros, sb->block_size, 1, img);
    free(zeros);

    if (!dh->is_root)
    {
        // this directory's own entry (num_blocks field) lives in its
        // parent, need to bump that too. status(1) + start_block(4) = 5
        // bytes in, that's where num_blocks starts
        fseek(img, (long)dh->self_block * sb->block_size + dh->self_offset + 5, SEEK_SET);
        uint32_t nb = htonl(new_n);
        fwrite(&nb, sizeof(uint32_t), 1, img);
    }
}

// writes one dirent_t into a specific slot of a directory, both in our
// in-memory copy (dh->data) and on the actual disk image. this is the one
// place diskput actually creates/updates a directory entry, whether it's
// for a new subdirectory or the final file.
void dirhandle_write_entry(FILE *img, superblock_t *sb, dirhandle_t *dh, uint32_t slot, dirent_t *entry)
{
    // entry comes in with host-order numbers (easier for us to work
    // with), gotta flip them to big endian before it touches the disk
    dirent_t disk_entry = *entry;
    disk_entry.start_block = htonl(disk_entry.start_block);
    disk_entry.num_blocks = htonl(disk_entry.num_blocks);
    disk_entry.file_size = htonl(disk_entry.file_size);

    memcpy(dh->data + (size_t)slot * sizeof(dirent_t), &disk_entry, sizeof(dirent_t));

    // slot -> which block + which byte offset inside that block
    uint32_t entries_per_block = sb->block_size / sizeof(dirent_t);
    uint32_t block_idx = slot / entries_per_block;
    uint32_t byte_off = (slot % entries_per_block) * sizeof(dirent_t);

    fseek(img, (long)dh->blocknums[block_idx] * sb->block_size + byte_off, SEEK_SET);
    fwrite(&disk_entry, sizeof(dirent_t), 1, img);
}

// walks a read-only path (used by disklist / diskget) starting at root.
// returns a dirhandle with data == NULL if any component is missing or
// isn't actually a directory
static dirhandle_t resolve_dir(FILE *img, superblock_t *sb, uint32_t *fat,
                               char comps[][FILENAME_LEN], int n_comps)
{
    dirhandle_t cur = load_root(img, sb, fat);
    uint32_t entries_per_block = sb->block_size / sizeof(dirent_t);

    for (int i = 0; i < n_comps; i++)
    {
        dirent_t e;
        uint32_t slot;
        if (!dirhandle_find(&cur, sb, comps[i], &e, &slot) || !(e.status & STATUS_DIR))
        {
            free_dirhandle(&cur);
            dirhandle_t bad;
            memset(&bad, 0, sizeof(bad));
            return bad;
        }
        uint32_t pblock = cur.blocknums[slot / entries_per_block];
        uint32_t poffset = (slot % entries_per_block) * sizeof(dirent_t);
        dirhandle_t next = load_subdir(img, sb, fat, e.start_block, pblock, poffset);
        free_dirhandle(&cur);
        cur = next;
    }
    return cur;
}

// diskinfo

int diskinfo_run(const char *image)
{
    FILE *img = open_image(image, "rb");
    superblock_t sb;
    read_superblock(img, &sb);

    uint32_t n_entries;
    uint32_t *fat = load_fat(img, &sb, &n_entries);

    // just tally up whatever's actually in the FAT - don't need to work
    // out which blocks "should" be reserved ourselves, the image already
    // has that info baked in (tutorial 9, slide 18)
    uint32_t free_blocks = 0, reserved_blocks = 0, allocated_blocks = 0;
    uint32_t count;
    if (sb.fs_size < n_entries)
        count = sb.fs_size;
    else
        count = n_entries;
    for (uint32_t i = 0; i < count; i++)
    {
        if (fat[i] == FAT_FREE)
            free_blocks++;
        else if (fat[i] == FAT_RESERVED)
            reserved_blocks++;
        else
            allocated_blocks++; // covers both "points to next block" and FAT_EOF
    }

    printf("Super block information:\n");
    printf("Block size: %u\n", sb.block_size);
    printf("Block count: %u\n", sb.fs_size);
    printf("FAT starts: %u\n", sb.fat_start);
    printf("FAT blocks: %u\n", sb.fat_blocks);
    printf("Root directory start: %u\n", sb.root_start);
    printf("Root directory blocks: %u\n", sb.root_blocks);
    printf("\n");
    printf("FAT information:\n");
    printf("Free Blocks: %u\n", free_blocks);
    printf("Reserved Blocks: %u\n", reserved_blocks);
    printf("Allocated Blocks: %u\n", allocated_blocks);

    free(fat);
    fclose(img);
    return 0;
}

// disklist

int disklist_run(const char *image, const char *path)
{
    FILE *img = open_image(image, "rb");
    superblock_t sb;
    read_superblock(img, &sb);
    uint32_t n_entries;
    uint32_t *fat = load_fat(img, &sb, &n_entries); // need this to follow subdirs

    char comps[MAX_PATH_COMPONENTS][FILENAME_LEN];
    int n_comps = split_path(path, comps, MAX_PATH_COMPONENTS);

    // path defaults to "/" (see main() below), so with 0 components this
    // just gives us the root directory straight away
    dirhandle_t dir = resolve_dir(img, &sb, fat, comps, n_comps);
    if (dir.data == NULL)
    {
        fprintf(stderr, "Directory not found.\n");
        free(fat);
        fclose(img);
        return 1;
    }

    // print every valid entry in the order it's stored - spec doesn't say
    // anything about sorting, so just go slot by slot
    uint32_t entries_per_block = sb.block_size / sizeof(dirent_t);
    uint32_t n_slots = dir.n_blocks * entries_per_block;
    for (uint32_t slot = 0; slot < n_slots; slot++)
    {
        dirent_t *e = (dirent_t *)(dir.data + (size_t)slot * sizeof(dirent_t));
        if (!(e->status & STATUS_USED))
            continue;

        uint32_t file_size = ntohl(e->file_size);
        int year, mon, day, hour, min, sec;
        decode_time(e->creation_time, &year, &mon, &day, &hour, &min, &sec);

        char type;
        if (e->status & STATUS_DIR)
            type = 'D';
        else
            type = 'F';
        // F/D + space, 10-char size + space, 30-char name + space, timestamp
        // (README.md section 3.2)
        printf("%c %10u %30s %04d/%02d/%02d %02d:%02d:%02d\n",
               type, file_size, e->filename, year, mon, day, hour, min, sec);
    }

    free_dirhandle(&dir);
    free(fat);
    fclose(img);
    return 0;
}

// diskget

int diskget_run(const char *image, const char *path, const char *destfile)
{
    FILE *img = open_image(image, "rb");
    superblock_t sb;
    read_superblock(img, &sb);
    uint32_t n_entries;
    uint32_t *fat = load_fat(img, &sb, &n_entries);

    char dirpath[1024], filename[FILENAME_LEN];
    split_last(path, dirpath, filename);

    char comps[MAX_PATH_COMPONENTS][FILENAME_LEN];
    int n_comps = split_path(dirpath, comps, MAX_PATH_COMPONENTS);

    dirhandle_t dir = resolve_dir(img, &sb, fat, comps, n_comps);
    dirent_t e;
    // spec says the same error message covers both cases: the
    // sub-directory doesn't exist, OR it exists but doesn't have this
    // file in it. also treat "it's actually a directory" as not found,
    // since diskget only copies files.
    if (dir.data == NULL || !dirhandle_find(&dir, &sb, filename, &e, NULL) || (e.status & STATUS_DIR))
    {
        printf("Requested file %s not found in %s.\n", filename, dirpath);
        if (dir.data != NULL)
            free_dirhandle(&dir);
        free(fat);
        fclose(img);
        return 1;
    }

    FILE *out = fopen(destfile, "wb");
    if (out == NULL)
        die("Could not open destination file");

    // walk the FAT chain (taken the concept from tutorial 10, slide 8-9), but only write out
    // exactly file_size bytes total the last block may have extra
    // padding on disk that isn't actually part of the file
    uint32_t remaining = e.file_size;
    uint32_t block = e.start_block;
    uint8_t *buf = malloc(sb.block_size);
    while (remaining > 0)
    {
        fseek(img, (long)block * sb.block_size, SEEK_SET);
        fread(buf, sb.block_size, 1, img);
        uint32_t towrite;
        if (remaining < sb.block_size)
            towrite = remaining;
        else
            towrite = sb.block_size;
        fwrite(buf, 1, towrite, out);
        remaining -= towrite;
        block = fat[block];
    }
    free(buf);
    fclose(out);

    free_dirhandle(&dir);
    free(fat);
    fclose(img);
    return 0;
}

// diskput

int diskput_run(const char *image, const char *srcfile, const char *destpath)
{
    FILE *src = fopen(srcfile, "rb");
    if (src == NULL)
    {
        printf("Source file %s not found.\n", srcfile);
        return 1;
    }

    // get the size of the source file - fseek to the end, then ftell
    // (tutorial 10, slide 14)
    fseek(src, 0, SEEK_END);
    long size = ftell(src);
    fseek(src, 0, SEEK_SET);

    // read the whole source file into memory up front - simplest way to
    // handle it since we need the total size anyway to know how many
    // blocks to allocate
    uint8_t *filedata;
    if (size > 0)
        filedata = malloc((size_t)size);
    else
        filedata = malloc(1);
    if (size > 0)
        fread(filedata, 1, (size_t)size, src);
    fclose(src);

    FILE *img = open_image(image, "r+b");
    superblock_t sb;
    read_superblock(img, &sb);
    uint32_t n_entries;
    uint32_t *fat = load_fat(img, &sb, &n_entries);

    char dirpath[1024], filename[FILENAME_LEN];
    split_last(destpath, dirpath, filename);

    char comps[MAX_PATH_COMPONENTS][FILENAME_LEN];
    int n_comps = split_path(dirpath, comps, MAX_PATH_COMPONENTS);

    uint32_t entries_per_block = sb.block_size / sizeof(dirent_t);
    dirhandle_t cur = load_root(img, &sb, fat);

    // walk down the path one component at a time, creating any
    // subdirectory that doesn't exist yet. "cur" is always the directory
    // we're currently standing in - by the end of this loop it'll be the
    // final destination directory (gotcha from tutorial 11: the missing
    // part of the path could be several levels deep, e.g. /a/b/c/file.txt
    // where NONE of a, b, c exist yet - this loop handles that fine since
    // it just keeps creating + descending one level at a time)
    for (int i = 0; i < n_comps; i++)
    {
        dirent_t e;
        uint32_t slot;
        if (dirhandle_find(&cur, &sb, comps[i], &e, &slot))
        {
            // this part of the path already exists, just go into it
            if (!(e.status & STATUS_DIR))
                die("A component of the destination path is not a directory");

            uint32_t pblock = cur.blocknums[slot / entries_per_block];
            uint32_t poffset = (slot % entries_per_block) * sizeof(dirent_t);
            dirhandle_t next = load_subdir(img, &sb, fat, e.start_block, pblock, poffset);
            free_dirhandle(&cur);
            cur = next;
            continue;
        }

        // doesn't exist - need to create it. first find (or make) room
        // for a new directory entry in the CURRENT directory
        int free_slot = dirhandle_free_slot(&cur, &sb);
        if (free_slot < 0)
        {
            dirhandle_grow(img, &sb, fat, n_entries, &cur);
            free_slot = dirhandle_free_slot(&cur, &sb);
        }

        // a brand new directory just needs one block to start - it'll
        // have room for 8 entries (512 / 64) before it needs to grow
        uint32_t new_block = alloc_block(fat, n_entries);
        fat[new_block] = FAT_EOF;

        dirent_t newdir;
        memset(&newdir, 0, sizeof(newdir));
        newdir.status = STATUS_USED | STATUS_DIR;
        newdir.start_block = new_block;
        newdir.num_blocks = 1;
        newdir.file_size = 0; // directories don't really have a "size"
        now_to_time(newdir.creation_time);
        memcpy(newdir.modified_time, newdir.creation_time, 7);
        strncpy(newdir.filename, comps[i], FILENAME_LEN - 1);
        memset(newdir.unused, 0xFF, sizeof(newdir.unused));

        // write the new directory's entry into the PARENT (cur), then
        // remember where we just wrote it, since that's where we'll come
        // back to bump num_blocks if this new directory ever grows
        dirhandle_write_entry(img, &sb, &cur, (uint32_t)free_slot, &newdir);

        uint32_t pblock = cur.blocknums[free_slot / entries_per_block];
        uint32_t poffset = ((uint32_t)free_slot % entries_per_block) * sizeof(dirent_t);

        // zero out the new directory's one block so all 8 of its entries
        // start off looking "unused" (status byte 0)
        uint8_t *zeros = calloc(1, sb.block_size);
        fseek(img, (long)new_block * sb.block_size, SEEK_SET);
        fwrite(zeros, sb.block_size, 1, img);
        free(zeros);

        // now step into the directory we just created and keep going
        dirhandle_t next = load_subdir(img, &sb, fat, new_block, pblock, poffset);
        free_dirhandle(&cur);
        cur = next;
    }

    // "cur" is now the actual destination directory. check if a file with
    // this name is already sitting there - if so we overwrite it: free
    // its old block chain back to the FAT (walking it just like reading a
    // file, except marking every block FAT_FREE instead of copying it)
    // and reuse its same directory entry slot. otherwise just grab a free
    // slot like we did for the subdirectories above.
    dirent_t existing;
    uint32_t existing_slot;
    int slot_int;
    uint8_t creation_time[7];
    int have_creation_time = 0;

    if (dirhandle_find(&cur, &sb, filename, &existing, &existing_slot))
    {
        if (existing.status & STATUS_DIR)
            die("A directory already exists with that name");

        uint32_t b = existing.start_block;
        while (1)
        {
            uint32_t next_b = fat[b];
            fat[b] = FAT_FREE;
            if (next_b == FAT_EOF)
                break;
            b = next_b;
        }

        slot_int = (int)existing_slot;
        // keep the original creation time, only the modified time should change
        memcpy(creation_time, existing.creation_time, 7);
        have_creation_time = 1;
    }
    else
    {
        slot_int = dirhandle_free_slot(&cur, &sb);
        if (slot_int < 0)
        {
            dirhandle_grow(img, &sb, fat, n_entries, &cur);
            slot_int = dirhandle_free_slot(&cur, &sb);
        }
    }

    // now actually allocate blocks for the file data and write it to
    // disk, one block at a time, chaining them together in the FAT as we
    // go (this is the "linked allocation" tutorial 10 talked about).
    // don't assume the size is a multiple of the block size (tutorial 11
    // gotcha!) - zero the buffer first each time so the leftover tail of
    // the last block is just zeros instead of garbage. an empty (0 byte)
    // file still gets exactly 1 block, per the other tutorial 11 gotcha.
    uint32_t nblocks;
    if (size == 0)
        nblocks = 1;
    else
        nblocks = (uint32_t)((size + sb.block_size - 1) / sb.block_size);
    uint32_t first_block = 0, prev_block = 0;
    uint8_t *blockbuf = malloc(sb.block_size);

    for (uint32_t i = 0; i < nblocks; i++)
    {
        uint32_t b = alloc_block(fat, n_entries);
        if (i == 0)
            first_block = b;
        else
            fat[prev_block] = b; // link the previous block to this one
        fat[b] = FAT_EOF;        // this one's the end of the chain for now
        prev_block = b;

        memset(blockbuf, 0, sb.block_size);
        long offset = (long)i * sb.block_size;
        long remaining = size - offset;
        if (remaining > 0)
        {
            long towrite;
            if (remaining < sb.block_size)
                towrite = remaining;
            else
                towrite = sb.block_size;
            memcpy(blockbuf, filedata + offset, (size_t)towrite);
        }
        fseek(img, (long)b * sb.block_size, SEEK_SET);
        fwrite(blockbuf, sb.block_size, 1, img);
    }
    free(blockbuf);
    free(filedata);

    // last step - write the directory entry itself so disklist/diskget
    // can actually find this file afterwards
    dirent_t entry;
    memset(&entry, 0, sizeof(entry));
    entry.status = STATUS_USED | STATUS_FILE;
    entry.start_block = first_block;
    entry.num_blocks = nblocks;
    entry.file_size = (uint32_t)size;
    if (have_creation_time)
        memcpy(entry.creation_time, creation_time, 7);
    else
        now_to_time(entry.creation_time);
    now_to_time(entry.modified_time);
    strncpy(entry.filename, filename, FILENAME_LEN - 1);
    memset(entry.unused, 0xFF, sizeof(entry.unused));

    dirhandle_write_entry(img, &sb, &cur, (uint32_t)slot_int, &entry);

    // everything above only touched our in-memory copy of the FAT (fat[]),
    // so this is the one spot that actually flushes all those block
    // allocations/frees back out to the disk image
    save_fat(img, &sb, fat, n_entries);

    free_dirhandle(&cur);
    free(fat);
    fclose(img);
    return 0;
}

// main - picks which part to run based on which -D flag the Makefile
// used to compile this file

int main(int argc, char **argv)
{
#ifdef DISKINFO
    if (argc != 2)
    {
        fprintf(stderr, "usage: %s <disk image>\n", argv[0]);
        return 1;
    }
    return diskinfo_run(argv[1]);
#elif defined(DISKLIST)
    if (argc < 2 || argc > 3)
    {
        fprintf(stderr, "usage: %s <disk image> [path]\n", argv[0]);
        return 1;
    }
    const char *path;
    if (argc == 3)
        path = argv[2];
    else
        path = "/";
    return disklist_run(argv[1], path);
#elif defined(DISKGET)
    if (argc != 4)
    {
        fprintf(stderr, "usage: %s <disk image> <path in image> <dest file>\n", argv[0]);
        return 1;
    }
    return diskget_run(argv[1], argv[2], argv[3]);
#elif defined(DISKPUT)
    if (argc != 4)
    {
        fprintf(stderr, "usage: %s <disk image> <src file> <dest path in image>\n", argv[0]);
        return 1;
    }
    return diskput_run(argv[1], argv[2], argv[3]);
#else
#error "No part specified - define one of DISKINFO/DISKLIST/DISKGET/DISKPUT"
#endif
}
