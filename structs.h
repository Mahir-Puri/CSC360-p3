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