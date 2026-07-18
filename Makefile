# builds all 4 programs from the same parts.c, using -D flags to pick which main() runs

CC = gcc
CFLAGS = -Wall -g

all: diskinfo disklist diskget diskput

diskinfo: parts.c structs.h
	$(CC) $(CFLAGS) -DDISKINFO -o diskinfo parts.c

disklist: parts.c structs.h
	$(CC) $(CFLAGS) -DDISKLIST -o disklist parts.c

diskget: parts.c structs.h
	$(CC) $(CFLAGS) -DDISKGET -o diskget parts.c

diskput: parts.c structs.h
	$(CC) $(CFLAGS) -DDISKPUT -o diskput parts.c

clean:
	rm -f diskinfo disklist diskget diskput

.PHONY: all clean
