# p3 - A Simple File System (CSC360FS)

This is my implementation of the CSC360FS assignment. It reads and
writes a FAT-style file system image, using 4 separate command line
programs: `diskinfo`, `disklist`, `diskget`, and `diskput`.

## Files in this repo

```
structs.h - structs for the superblock/directory entry + function prototypes
parts.c   - everything else: all 4 programs, plus the shared FAT/directory logic
Makefile  - builds all 4 binaries
```

## How to compile

```
make
```

This produces 4 executables in this directory: `diskinfo`, `disklist`,
`diskget`, `diskput`. `make clean` removes them.

All 4 are actually built from the same `parts.c` file - the Makefile
compiles it 4 times, each time with a different `-D` flag
(`-DDISKINFO`, `-DDISKLIST`, `-DDISKGET`, `-DDISKPUT`) so that
`main()` at the bottom of the file knows which part to run.
