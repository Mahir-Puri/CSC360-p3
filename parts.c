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