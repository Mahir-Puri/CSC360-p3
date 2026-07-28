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

## How to run each part

```
./diskinfo <image>
./disklist <image> [path]              (path defaults to / if left out)
./diskget  <image> <path in image> <output file on your computer>
./diskput  <image> <file on your computer> <path in image>
```

Example, using the provided test images:

```
$ ./diskinfo createTestImage/test.img
Super block information:
Block size: 512
Block count: 6400
FAT starts: 1
FAT blocks: 50
Root directory start: 51
Root directory blocks: 8

FAT information:
Free Blocks: 6341
Reserved Blocks: 49
Allocated Blocks: 10

$ ./disklist createTestImage/non-empty.img /
D          0                              . 2024/11/20 19:57:09
D          0                        sub_Dir 2024/11/20 19:57:20
F         16                       test.txt 2024/11/20 19:57:37
F      31128                        cat.jpg 2024/11/20 19:57:54

$ ./diskget createTestImage/non-empty.img /sub_Dir/test.txt out.txt
$ sha1sum out.txt

$ ./diskput createTestImage/test.img readme.txt /notes/readme.txt
```

The last command creates `/notes` since it doesn't exist yet, then
copies `readme.txt` into it.

## What works

| Part | Program    | Status                                             |
| ---- | ---------- | -------------------------------------------------- |
| I    | `diskinfo` | Works - output matches the spec exactly            |
| II   | `disklist` | Works - root and sub-directories, any depth        |
| III  | `diskget`  | Works - including the "not found" error message    |
| IV   | `diskput`  | Works - including creating missing sub-directories |

All 4 parts are implemented and working correctly, as far as I've
tested. Here's what each one actually does:

### Part I - `diskinfo`

Reads the superblock (block 0) and the FAT, then prints out:

- the superblock fields (block size, total blocks, where the FAT and
  root directory are and how big they are)
- a count of how many blocks in the FAT are free, reserved, or
  allocated to a file/directory

This one doesn't change anything on disk, it just reads and reports.
**Status: working**, output format matches the spec's sample exactly.

### Part II - `disklist`

Lists the contents of a directory (root by default, or a given path
like `/sub_dir`). To get there, it starts at the root directory and,
for each part of the path, looks up that name and follows its FAT
chain to load the next directory - same idea as reading a file, just
that the "file" is a directory's list of entries. Once it's in the
right directory, it prints one line per entry (`F`/`D`, size, name,
creation time), skipping any entry marked unused.

**Status: working** for the root directory and sub-directories at any
depth (tested down to 5 levels deep in `large.img`).

### Part III - `diskget`

Copies a file out of the image onto the host computer. It splits the
given path into "which directory" and "which file", walks to that
directory the same way `disklist` does, then looks for the file by
name. If the directory or the file can't be found, it prints
`Requested file <filename> not found in <path>.` and stops. If it is
found, it follows the file's FAT chain block by block, copying out
exactly `file_size` bytes (so any leftover space in the last block
doesn't get included).

**Status: working**, confirmed with `sha1sum` that copied files are
byte-for-byte identical to the originals.

### Part IV - `diskput`

Copies a file from the host computer into the image. It:

1. Reads the source file in and checks it exists (prints
   `Source file <filename> not found.` and stops if not).
2. Walks down the destination path one directory at a time, creating
   any directory that doesn't exist yet (this can be several levels in
   a row, e.g. none of `/a/b/c` existing yet).
3. In the final directory, checks if a file with that name is already
   there - if so, frees its old blocks first so they can be reused.
4. Allocates enough blocks for the new file's data (at least 1, even
   for an empty file), links them together in the FAT, and writes the
   data out.
5. Writes/updates the directory entry (name, size, start block,
   timestamps) so the file shows up correctly afterwards.

**Status: working**, including creating multiple missing directories
at once, overwriting an existing file, and 0-byte files.

## A quick word on how it's built

- Structs for the superblock and directory entries are `packed` (no
  compiler padding) so they line up byte-for-byte with what's on disk
  (tutorial 9).
- Everything on disk is big endian, so every multi-byte number gets
  passed through `ntohs`/`ntohl` when reading, and `htons`/`htonl` when
  writing (tutorial 9).
- The whole FAT is loaded into memory once at the start of each
  program, since it gets used constantly (tutorial 10).
- Finding a free block for a new file/directory is just a linear scan
  through the in-memory FAT for the first free entry (tutorial 10/11).
- Subdirectories are read the same way as regular files - follow the
  FAT chain starting from the directory entry's start block
  (tutorial 10).

## How I tested it

Used the 3 test images given in `createTestImage/` (`test.img`,
`non-empty.img`, `large.img`):

- `diskinfo` output compared line-by-line against the sample in the spec.
- `disklist` compared against the sample listings in the spec, for the
  root directory and several sub-directories (including a 5-levels-deep
  path in `large.img`).
- `diskget`'d files back out and checked them with `sha1sum` against
  the hashes given in `large.img`'s `/hashes.txt` - they matched.
- `diskput`'d a file into a path where none of the sub-directories
  existed yet, then `diskget`'d it back out and diffed it against the
  original - identical.
- Tried an empty (0 byte) file, an overwrite of an existing file, and
  the two error cases (missing source file for diskput, missing
  file/path for diskget) - all behave correctly.
- Re-ran everything after a clean `make clean && make` to make sure it
  builds from scratch with no leftover state.

## Extra Notes

- Deleting files/directories isn't implemented since it's not part of
  the spec.
- The root directory's size normally comes from the superblock and is
  fixed, but if it ever needs more entries than it started with, my
  code extends it by chaining on an extra block through the FAT
  (same idea as growing a regular file). This isn't something the
  spec talks about directly, so flagging it here just in case.
