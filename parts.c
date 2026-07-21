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