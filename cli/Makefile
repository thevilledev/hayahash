CC      ?= cc
CFLAGS  ?= -O2 -std=c11 -Wall -Wextra -Werror
INC     = -I..

.PHONY: all check clean install

all: hayasum

hayasum: hayasum.c ../hayahash.h
	$(CC) $(CFLAGS) $(INC) -o $@ hayasum.c

# Smoke: empty stdin digests must match the known v0.5 empty vectors.
check: hayasum
	test "$$(./hayasum -b 64 </dev/null | awk '{print $$1}')" = "68ac507cf298ca3f"
	test "$$(./hayasum -b 128 </dev/null | awk '{print $$1}')" = "ace2141f6ba3086868ac507cf298ca3f"
	printf 'hello world' | ./hayasum -b 64 | awk '{print $$1}' | grep -qx '4524b96611bfc05a'
	@echo "hayasum check ok"

PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin

install: hayasum
	install -d "$(DESTDIR)$(BINDIR)"
	install -m 755 hayasum "$(DESTDIR)$(BINDIR)/hayasum"

clean:
	rm -f hayasum
