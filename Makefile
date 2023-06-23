LIBS = -lcurl
CFLAGS =  -O2 -std=c99 -pedantic -Wextra -Wall -Wno-stringop-overflow -Wno-sign-compare
PREFIX = /usr/local/bin
CACHE = $(shell if [ "$$XDG_CACHE_HOME" ]; then echo "$$XDG_CACHE_HOME"; else echo "$$HOME"/.cache; fi)

all: sbm

clean:
	rm -f sbm $(CACHE)/sbm

sbm: sbm.c
	$(CC) sbm.c -o sbm $(CFLAGS) $(LIBS)
	strip sbm

install: sbm
	install ./sbm $(PREFIX)/sbm
