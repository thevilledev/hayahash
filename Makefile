# Install the header-only C reference and a pkg-config file.
#
#   make install PREFIX=/usr/local
#   pkg-config --cflags hayahash
#
# Embedding by copying hayahash.h remains supported; this target is for
# system / distro packaging.

PREFIX       ?= /usr/local
INCLUDEDIR   ?= $(PREFIX)/include
LIBDIR       ?= $(PREFIX)/lib
PKGCONFIGDIR ?= $(LIBDIR)/pkgconfig

VERSION := $(shell tr -d ' \n' < VERSION)

.PHONY: all install uninstall check-install clean

all:
	@echo "hayahash $(VERSION) is header-only."
	@echo "Install with: make install PREFIX=$(PREFIX)"

hayahash.pc: hayahash.pc.in VERSION
	sed -e 's|@prefix@|$(PREFIX)|g' \
	    -e 's|@VERSION@|$(VERSION)|g' \
	    hayahash.pc.in > $@

install: hayahash.h hayahash.pc
	install -d "$(DESTDIR)$(INCLUDEDIR)"
	install -m 644 hayahash.h "$(DESTDIR)$(INCLUDEDIR)/hayahash.h"
	install -d "$(DESTDIR)$(PKGCONFIGDIR)"
	install -m 644 hayahash.pc "$(DESTDIR)$(PKGCONFIGDIR)/hayahash.pc"

uninstall:
	rm -f "$(DESTDIR)$(INCLUDEDIR)/hayahash.h"
	rm -f "$(DESTDIR)$(PKGCONFIGDIR)/hayahash.pc"

# Stage into a temporary prefix and confirm pkg-config resolves the header.
check-install:
	rm -rf .install-check
	$(MAKE) install DESTDIR="$(CURDIR)/.install-check" PREFIX=/usr
	test -f .install-check/usr/include/hayahash.h
	test -f .install-check/usr/lib/pkgconfig/hayahash.pc
	PKG_CONFIG_PATH="$(CURDIR)/.install-check/usr/lib/pkgconfig" \
		pkg-config --exact-version=$(VERSION) hayahash
	@# pkg-config omits -I/usr/include from --cflags; check the variable.
	test "$$(PKG_CONFIG_PATH="$(CURDIR)/.install-check/usr/lib/pkgconfig" \
		pkg-config --variable=includedir hayahash)" = "/usr/include"
	rm -rf .install-check
	@echo "install + pkg-config ok for hayahash $(VERSION)"

clean:
	rm -f hayahash.pc
	rm -rf .install-check
