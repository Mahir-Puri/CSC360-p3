// structs.h
//
// structs + constants for CSC360FS, taken straight from the assignment spec
// (README.md section 4) and tutorial 9/10 slides.
//
// all the actual function bodies are in parts.c - this file is just the
// "shape" of the file system (superblock, directory entry) plus the
// prototypes so parts.c doesn't have to be read top to bottom to know
// what's available.

#ifndef STRUCTS_H
#define STRUCTS_H

#include <stdint.h>
#include <stdio.h>

#define FS_ID "CSC360FS"

// bits in the directory entry "status" byte (tutorial 9, slide 14)
#define STATUS_USED 0x01
#define STATUS_FILE 0x02
#define STATUS_DIR 0x04

// special FAT values (tutorial 9, slide 18)
#define FAT_FREE 0x00000000
#define FAT_RESERVED 0x00000001
#define FAT_EOF 0xFFFFFFFF

#define FILENAME_LEN 31 // 30 chars + null terminator, per the spec

// superblock is 8+2+4+4+4+4+4 = 30 bytes, packed so there's no compiler padding
// (tutorial 9, slide 22 - "packing structs"). we only ever read/write
// exactly sizeof(superblock_t) bytes from block 0, so it's fine that this
// struct is smaller than a full 512-byte block - the rest of the block is
// just unused on disk and we never touch it.
//
// Every field here is stored BIG ENDIAN on disk (README.md
// section 5), so after reading this struct in we have to run every
// multi-byte field through ntohs/ntohl before using it (and htons/htonl
// before writing it back out). read_superblock() in parts.c does this.
typedef struct __attribute__((packed))
{
    char fs_id[8];        // should always read "CSC360FS"
    uint16_t block_size;  // usually 512, but large.img uses 1024
    uint32_t fs_size;     // total blocks in the whole image
    uint32_t fat_start;   // block number where the FAT begins
    uint32_t fat_blocks;  // how many blocks the FAT takes up
    uint32_t root_start;  // block number where the root directory begins
    uint32_t root_blocks; // how many blocks the root directory takes up
} superblock_t;
