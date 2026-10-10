# An ordinary user can install without special permissions. This is why the
# default is under $HOME and not /usr/local. Give a privileged location on the
# command line: "sudo make install PREFIX=/usr/local". You must give the
# location. See check-install-prefix.
PREFIX        ?= $(HOME)/.local
BINDIR        := $(PREFIX)/bin
DESTINATION   := CLIPSVN
VERSION       ?= 0.1.0

# The private, versioned location for the real binary. It is never on PATH.
LIBEXECDIR    := $(PREFIX)/libexec/$(DESTINATION)-$(VERSION)

# The system-wide share directory for CLIPSVN data.
DATADIR       := $(PREFIX)/share/$(DESTINATION)

WRAPPER       := $(DESTINATION)-$(VERSION)

# ---------------------------------------------------------------------------
# The CLIPS version to build against.
#
#   6.4.2    the 6.4.2 release tarball from SourceForge (the default)
#   svn-6x   branches/64x of the CLIPS Subversion repository
#   svn-7x   branches/70x
#
# The two branches are pinned to a revision. A build today and a build next
# month give the same result. Set CLIPS_SVN_REV to move a branch:
#
#   make CLIPS_VERSION=svn-7x
#   make CLIPS_VERSION=svn-7x CLIPS_SVN_REV=980
#   make CLIPS_VERSION=svn-7x CLIPS_SVN_REV=HEAD
#
# Each version is fetched and built in its own directory. A change to a
# different version does not rebuild the version that you leave. A change to a
# different Subversion version under a CLIPS version also does not.
# ---------------------------------------------------------------------------

CLIPS_VERSION  ?= 6.4.2
CLIPS_VERSIONS := 6.4.2 svn-6x svn-7x

ARCHIVE     := clips_core_source_642.tar.gz
ARCHIVE_URL ?= https://sourceforge.net/projects/clipsrules/files/CLIPS/6.4.2/$(ARCHIVE)

CLIPS_SVN_ROOT   ?= https://svn.code.sf.net/p/clipsrules/code
CLIPS_SVN_6X_REV ?= 967
CLIPS_SVN_7X_REV ?= 978

# The digest that SourceForge publishes for clips_core_source_642.tar.gz. It
# applies only to the release. A branch export changes over time and has no
# published digest. So the variable is empty for a branch, and the fetch
# script skips the check.
CLIPS_SHA256_6.4.2 := 608a1eb2fc6e9caff30d63d684095f0bca7108f2294d21ee6f5617427c10455a
CLIPS_SHA256 ?= $(CLIPS_SHA256_$(CLIPS_VERSION))
export CLIPS_SHA256

ifeq ($(CLIPS_VERSION),6.4.2)
  CLIPS_TAG    := 6.4.2
  CLIPS_FETCH  := tarball "$(ARCHIVE_URL)" "$(ARCHIVE)"
  CLIPS_ORIGIN := the 6.4.2 release tarball
else ifeq ($(CLIPS_VERSION),svn-6x)
  CLIPS_SVN_URL ?= $(CLIPS_SVN_ROOT)/branches/64x/core
  CLIPS_SVN_REV ?= $(CLIPS_SVN_6X_REV)
  CLIPS_TAG     := svn-6x-r$(CLIPS_SVN_REV)
  CLIPS_FETCH   := svn "$(CLIPS_SVN_URL)" "$(CLIPS_SVN_REV)"
  CLIPS_ORIGIN  := branches/64x at r$(CLIPS_SVN_REV)
else ifeq ($(CLIPS_VERSION),svn-7x)
  CLIPS_SVN_URL ?= $(CLIPS_SVN_ROOT)/branches/70x/core
  CLIPS_SVN_REV ?= $(CLIPS_SVN_7X_REV)
  CLIPS_TAG     := svn-7x-r$(CLIPS_SVN_REV)
  CLIPS_FETCH   := svn "$(CLIPS_SVN_URL)" "$(CLIPS_SVN_REV)"
  CLIPS_ORIGIN  := branches/70x at r$(CLIPS_SVN_REV)
else
  $(error CLIPS_VERSION is '$(CLIPS_VERSION)': expected one of $(CLIPS_VERSIONS))
endif

# The fetched tree is kept apart from the build tree. Then a removal of the
# object files does not cause a new fetch of the sources, and CI has something
# useful to cache.
CLIPS_SRC_DIR := vendor/clips-source/$(CLIPS_TAG)
BUILD_DIR     := vendor/clips-build/$(CLIPS_TAG)
TARGET        := $(BUILD_DIR)/clips

CLIPS_SRC_STAMP := $(CLIPS_SRC_DIR)/.clips-source
BUILD_STAMP     := $(BUILD_DIR)/.clips-source

# vendor/clips points to the version that was built last. Then the paths in
# the README, in the examples, and in tests/run.sh remain valid.
CLIPS_LINK := vendor/clips

# The binary that the tests run: the one that was built last, unless the
# environment or the command line names a different one.
CLIPS     ?=
CLIPS_BIN  = $(if $(strip $(CLIPS)),$(CLIPS),$(TARGET))

UNAME_S       := $(shell uname -s)

ifeq ($(UNAME_S),Darwin)
  CLIPS_OS := DARWIN
else
  CLIPS_OS := LINUX
endif

# ---------------------------------------------------------------------------
# Subversion.
#
# By default, the makefile downloads the pinned source releases of APR,
# APR-util, SQLite, and Subversion. It checks each download against the
# SHA-256 digest that it records. It builds them as static libraries in
# vendor/, and links them into the binary. So the library that this binding
# calls is the one that its documentation describes, and the installed binary
# has no library to find at run time.
#
# The build is small. LZ4 and utf8proc are the copies that Subversion
# bundles. SQLite is the amalgamation, compiled into libsvn_subr. There is no
# Serf, so the vendored build reaches a repository by file:// and svn://
# only. The machine must supply zlib and Expat, which APR-util and
# Subversion need, and which every distribution packages: zlib1g-dev and
# libexpat1-dev on Debian and Ubuntu.
#
# SVN_SYSTEM=1 links the Subversion that the machine has. This is for a
# packager who prefers the system library to a copy of it. That library has
# whatever access methods the distribution built, usually http:// and
# https:// too.
# ---------------------------------------------------------------------------

# The versions that this binding is built and tested against. You can pin any
# of them. A plain "make" builds 1.14.5.
#
#     make SVN_VERSION=1.14.3
#
# A version that is not in the table below needs its digest:
#
#     make SVN_VERSION=1.14.6 SVN_SHA256=<the digest apache.org publishes>
#
SVN_VERSION  ?= 1.14.5
SVN_VERSIONS := 1.14.3 1.14.4 1.14.5

# The Apache Software Foundation publishes a digest for each release. These
# are those digests. scripts/fetch-tarball.sh reports a download that does not
# match.
SVN_SHA256_1.14.3 := 949efd451a09435f7e8573574c71c7b71b194d844890fa49cd61d2262ea1a440
SVN_SHA256_1.14.4 := 44ead116e72e480f10f123c914bb6f9f8c041711c041ed7abff1b8634a199e3c
SVN_SHA256_1.14.5 := e78a29e7766b8b7b354497d08f71a55641abc53675ce1875584781aae35644a1

SVN_SHA256 ?= $(SVN_SHA256_$(SVN_VERSION))

APR_VERSION    ?= 1.7.6
APR_SHA256     ?= 49030d92d2575da735791b496dc322f3ce5cff9494779ba8cc28c7f46c5deb32
APU_VERSION    ?= 1.6.3
APU_SHA256     ?= a41076e3710746326c3945042994ad9a4fcac0ce0277dd8fea076fec3c9772b5

# The amalgamation, one sqlite3.c, from the year directory that sqlite.org
# files it under.
SQLITE_VERSION ?= 3530400
SQLITE_YEAR    ?= 2026
SQLITE_SHA256  ?= 0e9483900e92cd5de8fd48d16bf9200145a61f7fd5be542a5ac81d8a9516eb9c

APACHE_MIRROR ?= https://archive.apache.org/dist
SVN_URL       ?= $(APACHE_MIRROR)/subversion/subversion-$(SVN_VERSION).tar.bz2
APR_URL       ?= $(APACHE_MIRROR)/apr/apr-$(APR_VERSION).tar.bz2
APU_URL       ?= $(APACHE_MIRROR)/apr/apr-util-$(APU_VERSION).tar.bz2
SQLITE_URL    ?= https://sqlite.org/$(SQLITE_YEAR)/sqlite-autoconf-$(SQLITE_VERSION).tar.gz

VENDOR        := $(CURDIR)/vendor
SVN_ARCHIVE   := $(VENDOR)/subversion-$(SVN_VERSION).tar.bz2
APR_ARCHIVE   := $(VENDOR)/apr-$(APR_VERSION).tar.bz2
APU_ARCHIVE   := $(VENDOR)/apr-util-$(APU_VERSION).tar.bz2
SQLITE_ARCHIVE:= $(VENDOR)/sqlite-autoconf-$(SQLITE_VERSION).tar.gz

# Each tarball unpacks to <name>-<version>/. APR and APR-util are installed
# in the same prefix as the Subversion that they were built for.
SVN_SRC       ?= $(VENDOR)/subversion-$(SVN_VERSION)
APR_SRC       ?= $(VENDOR)/apr-$(APR_VERSION)
APU_SRC       ?= $(VENDOR)/apr-util-$(APU_VERSION)
SQLITE_SRC    ?= $(VENDOR)/sqlite-autoconf-$(SQLITE_VERSION)
SVN_PREFIX    ?= $(VENDOR)/svn-$(SVN_VERSION)

# The parallelism of the three configure-and-make builds. The Subversion
# build is the one that takes time.
NPROC ?= $(shell nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 2)

# Static, position independent, and without every optional dependency: no
# Serf (HTTP), no keyring or KWallet (password stores), no GPG agent, no
# libmagic, no translations, no bindings. SQLite is the amalgamation. LZ4 and
# utf8proc are the bundled copies.
SVN_CONFIGURE_OPTS ?= --disable-shared --enable-static --with-pic \
                      --with-lz4=internal --with-utf8proc=internal \
                      --without-serf --without-apxs --without-swig \
                      --without-gnome-keyring --without-kwallet \
                      --without-gpg-agent --with-libmagic=no \
                      --disable-nls --disable-plaintext-password-storage

APR_CONFIGURE_OPTS ?= --disable-shared --enable-static --with-pic
APU_CONFIGURE_OPTS ?= --disable-shared --enable-static --with-pic

PKG_CONFIG ?= pkg-config

# The static libraries, in the order that the linker needs them: each one
# before the ones that it calls. The apr-1-config and apu-1-config scripts
# add what APR needs on this machine: libuuid, libcrypt, pthreads.
SVN_STATIC_LIBS := -lsvn_client-1 -lsvn_wc-1 -lsvn_ra-1 -lsvn_ra_local-1 \
                   -lsvn_ra_svn-1 -lsvn_repos-1 -lsvn_fs-1 -lsvn_fs_fs-1 \
                   -lsvn_fs_x-1 -lsvn_fs_util-1 -lsvn_delta-1 -lsvn_diff-1 \
                   -lsvn_subr-1

ifeq ($(SVN_SYSTEM),1)
  # The distribution's headers and libraries. Subversion 1.14 installs
  # pkg-config files; a machine without them gets the usual defaults, with
  # apr-1-config and apu-1-config for the APR half.
  APR_CONFIG  ?= apr-1-config
  APU_CONFIG  ?= apu-1-config
  SVN_CFLAGS  ?= $(shell $(PKG_CONFIG) --cflags libsvn_client 2>/dev/null \
                         || echo "-I/usr/include/subversion-1 $$($(APR_CONFIG) --includes 2>/dev/null)")
  # Debian and Ubuntu put the libraries that libsvn_subr itself links
  # (expat, zlib, LZ4, utf8proc) on the public Libs line of libsvn_subr.pc,
  # and libsvn-dev does not depend on their -dev packages. A dynamic link
  # against libsvn_subr-1.so does not need them, and the wrappers do not
  # call them, so they are removed: "-lutf8proc" would otherwise fail the
  # link on a machine without libutf8proc-dev.
  SVN_PRIVATE_LIBS := -lexpat -lz -llz4 -lutf8proc
  SVN_LDLIBS  ?= $(filter-out $(SVN_PRIVATE_LIBS), \
                   $(shell $(PKG_CONFIG) --libs libsvn_client libsvn_repos libsvn_fs libsvn_ra libsvn_subr 2>/dev/null \
                         || echo "-lsvn_client-1 -lsvn_repos-1 -lsvn_fs-1 -lsvn_ra-1 -lsvn_delta-1 -lsvn_subr-1 $$($(APU_CONFIG) --link-ld 2>/dev/null) $$($(APR_CONFIG) --link-ld 2>/dev/null)"))
  SVN_DEP     :=
  SVN_ORIGIN  := the Subversion this machine has
else
  SVN_LIB     := $(SVN_PREFIX)/lib/libsvn_client-1.a
  APR_LIB     := $(SVN_PREFIX)/lib/libapr-1.a
  APU_LIB     := $(SVN_PREFIX)/lib/libaprutil-1.a
  APR_CONFIG  := $(SVN_PREFIX)/bin/apr-1-config
  APU_CONFIG  := $(SVN_PREFIX)/bin/apu-1-config
  SVN_CFLAGS  := -I$(SVN_PREFIX)/include/subversion-1 -I$(SVN_PREFIX)/include/apr-1
  # A recursive assignment, so make reads the two config scripts at link
  # time, after they exist.
  SVN_LDLIBS   = -L$(SVN_PREFIX)/lib $(SVN_STATIC_LIBS) \
                 $(shell $(APU_CONFIG) --link-ld --libs 2>/dev/null || echo -laprutil-1 -lexpat) \
                 $(shell $(APR_CONFIG) --link-ld --libs 2>/dev/null || echo -lapr-1 -lpthread) \
                 -lz
  SVN_DEP     := $(SVN_LIB)
  SVN_ORIGIN  := Subversion $(SVN_VERSION), built from source in vendor/
endif

# The release flags of the CLIPS makefile. A CFLAGS on the command line
# replaces them.
CLIPS_CFLAGS   ?= -std=c99 -O3 -fno-strict-aliasing
DEBUG_CFLAGS   ?= -std=c99 -O0 -g

CFLAGS  = $(CLIPS_CFLAGS) $(SVN_CFLAGS)
LDLIBS  = -lm $(SVN_LDLIBS)

# ---------------------------------------------------------------------------
# The configuration of the last build.
#
# The Subversion version that the objects were compiled against depends on
# CFLAGS. The version that they are linked against depends on LDLIBS. Make
# tracks neither. Without this file, a change of Subversion version in the
# same tree would link the old object files and make a binary for the wrong
# version, with no warning. The file is in the build tree, one for each CLIPS
# version, because it describes the objects there.
# ---------------------------------------------------------------------------

BUILD_FLAGS := $(BUILD_DIR)/.build-flags

VALGRIND       ?= valgrind
VALGRIND_FLAGS ?= --leak-check=full --errors-for-leak-kinds=none \
                  --num-callers=12 --error-exitcode=1

COVDIR     := $(CURDIR)/coverage
COV_CC     ?= gcc
GCOV       ?= gcov
COV_CFLAGS := -std=c99 -O0 -g --coverage

.PHONY: all clips clips-source debug deps subversion install install-bin \
        uninstall clean distclean test test-suite test-examples test-valgrind \
        coverage test-versions test-clips docs docs-check help \
        check-install-prefix check-build-tools \
        print-svn-version print-svn-versions print-svn-prefix \
        print-clips print-clips-versions print-clips-target \
        print-cflags print-ldlibs FORCE

all: clips

clips: $(TARGET)
	@:

deps: $(SVN_DEP) $(CLIPS_SRC_STAMP)

subversion: $(SVN_DEP)

$(BUILD_FLAGS): FORCE
	@mkdir -p $(@D)
	@printf '%s\n' '$(CFLAGS) $(LDLIBS)' | cmp -s - $@ 2>/dev/null || \
	    printf '%s\n' '$(CFLAGS) $(LDLIBS)' > $@

FORCE:

# ---------------------------------------------------------------------------
# Fetch and build CLIPS.
#
# scripts/fetch-clips.sh puts a clean tree in $(CLIPS_SRC_DIR). The build
# happens in a copy of that tree, because the CLIPS makefile finds
# userfunctions.c only next to the CLIPS sources. The copy is made one time.
# So a change to userfunctions.c recompiles one file, not all of them.
# ---------------------------------------------------------------------------

$(CLIPS_SRC_STAMP): | scripts/fetch-clips.sh
	./scripts/fetch-clips.sh $(CLIPS_FETCH) "$(CLIPS_SRC_DIR)"

$(BUILD_STAMP): $(CLIPS_SRC_STAMP)
	mkdir -p "$(BUILD_DIR)"
	cp -R "$(CLIPS_SRC_DIR)/." "$(BUILD_DIR)/"
	touch "$@"

# vendor/clips is a symlink to the version that was built last.
define point_clips_link
	@[ ! -e "$(CLIPS_LINK)" ] || [ -L "$(CLIPS_LINK)" ] || rm -rf "$(CLIPS_LINK)"
	@ln -sfn "clips-build/$(CLIPS_TAG)" "$(CLIPS_LINK)"
	@echo "$(CLIPS_LINK) -> clips-build/$(CLIPS_TAG)  ($(CLIPS_ORIGIN))"
endef

$(TARGET): userfunctions.c $(SVN_DEP) $(BUILD_FLAGS) $(BUILD_STAMP)
	cp userfunctions.c $(BUILD_DIR)/
	$(MAKE) -C $(BUILD_DIR) LDLIBS="$(LDLIBS)" CFLAGS="$(CFLAGS)"
	$(point_clips_link)

debug: userfunctions.c $(SVN_DEP) $(BUILD_FLAGS) $(BUILD_STAMP)
	cp userfunctions.c $(BUILD_DIR)/
	$(MAKE) -C $(BUILD_DIR) debug LDLIBS="$(LDLIBS)" CFLAGS="$(DEBUG_CFLAGS) $(SVN_CFLAGS)"
	$(point_clips_link)

# Fetch the selected CLIPS without a build, to fill a cache or to look at a
# branch.
clips-source: $(CLIPS_SRC_STAMP)
	@cat "$(CLIPS_SRC_STAMP)"

# ---------------------------------------------------------------------------
# The vendored Subversion: APR, then APR-util, then Subversion with the
# SQLite amalgamation, all into one prefix.
# ---------------------------------------------------------------------------

check-build-tools:
	@missing=; \
	for h in zlib.h expat.h; do \
	    printf '#include <%s>\n' "$$h" | $(CC) -E -x c - >/dev/null 2>&1 || missing="$$missing $$h"; \
	done; \
	if [ -n "$$missing" ]; then \
	    echo "makefile: cannot build Subversion $(SVN_VERSION) from source: no$$missing" >&2; \
	    echo >&2; \
	    echo "  APR-util and Subversion need the zlib and Expat headers." >&2; \
	    echo "  Debian/Ubuntu:  sudo apt-get install zlib1g-dev libexpat1-dev" >&2; \
	    echo "  Fedora/RHEL:    sudo dnf install zlib-devel expat-devel" >&2; \
	    echo "  macOS:          both come with the command line tools" >&2; \
	    echo >&2; \
	    echo "  Or build against the Subversion already on this machine:" >&2; \
	    echo "      make SVN_SYSTEM=1" >&2; \
	    exit 1; \
	fi

$(SVN_ARCHIVE): | scripts/fetch-tarball.sh
	@[ -n "$(SVN_SHA256)" ] || { \
	    echo "makefile: no digest is pinned for Subversion $(SVN_VERSION)." >&2; \
	    echo >&2; \
	    echo "  The versions with one are:" >&2; \
	    echo "      $(SVN_VERSIONS)" >&2; \
	    echo >&2; \
	    echo "  For any other, pass the digest that apache.org publishes:" >&2; \
	    echo "      make SVN_VERSION=$(SVN_VERSION) SVN_SHA256=<digest>" >&2; \
	    echo "  The tarball is at $(SVN_URL)" >&2; \
	    exit 1; \
	}
	./scripts/fetch-tarball.sh "$(SVN_URL)" "$(SVN_SHA256)" "$@"

$(APR_ARCHIVE): | scripts/fetch-tarball.sh
	./scripts/fetch-tarball.sh "$(APR_URL)" "$(APR_SHA256)" "$@"

$(APU_ARCHIVE): | scripts/fetch-tarball.sh
	./scripts/fetch-tarball.sh "$(APU_URL)" "$(APU_SHA256)" "$@"

$(SQLITE_ARCHIVE): | scripts/fetch-tarball.sh
	./scripts/fetch-tarball.sh "$(SQLITE_URL)" "$(SQLITE_SHA256)" "$@"

$(SVN_SRC)/configure: $(SVN_ARCHIVE)
	tar -xjf "$(SVN_ARCHIVE)" -C "$(VENDOR)"
	touch "$@"

$(APR_SRC)/configure: $(APR_ARCHIVE)
	tar -xjf "$(APR_ARCHIVE)" -C "$(VENDOR)"
	touch "$@"

$(APU_SRC)/configure: $(APU_ARCHIVE)
	tar -xjf "$(APU_ARCHIVE)" -C "$(VENDOR)"
	touch "$@"

$(SQLITE_SRC)/sqlite3.c: $(SQLITE_ARCHIVE)
	tar -xzf "$(SQLITE_ARCHIVE)" -C "$(VENDOR)"
	touch "$@"

# A vendor/svn-<version> that is already populated is used as it is. It can
# come from an earlier build here, from a cache, or from a copy of an
# installation. For it, no tarball is fetched, nothing is configured, and
# nothing is compiled. This is why there are two rules, not one. Make builds
# a missing prerequisite even if the target that needs it is current. So the
# source trees must not be prerequisites at all, not only order-only ones.
ifeq ($(wildcard $(SVN_LIB)),)

$(APR_LIB): $(APR_SRC)/configure
	@$(MAKE) --no-print-directory check-build-tools
	cd "$(APR_SRC)" && ./configure --prefix="$(SVN_PREFIX)" $(APR_CONFIGURE_OPTS) > configure.log
	$(MAKE) -C "$(APR_SRC)" -j$(NPROC) > "$(APR_SRC)/make.log"
	$(MAKE) -C "$(APR_SRC)" install > "$(APR_SRC)/install.log"

$(APU_LIB): $(APR_LIB) $(APU_SRC)/configure
	cd "$(APU_SRC)" && ./configure --prefix="$(SVN_PREFIX)" $(APU_CONFIGURE_OPTS) \
	    --with-apr="$(SVN_PREFIX)" --with-expat=/usr > configure.log
	$(MAKE) -C "$(APU_SRC)" -j$(NPROC) > "$(APU_SRC)/make.log"
	$(MAKE) -C "$(APU_SRC)" install > "$(APU_SRC)/install.log"

$(SVN_LIB): $(APU_LIB) $(SVN_SRC)/configure $(SQLITE_SRC)/sqlite3.c
	cd "$(SVN_SRC)" && ./configure --prefix="$(SVN_PREFIX)" $(SVN_CONFIGURE_OPTS) \
	    --with-apr="$(SVN_PREFIX)" --with-apr-util="$(SVN_PREFIX)" \
	    --with-sqlite="$(SQLITE_SRC)/sqlite3.c" > configure.log
	$(MAKE) -C "$(SVN_SRC)" -j$(NPROC) > "$(SVN_SRC)/make.log"
	$(MAKE) -C "$(SVN_SRC)" install > "$(SVN_SRC)/install.log"
	@echo "Subversion $(SVN_VERSION) is built in $(SVN_PREFIX)"

else

$(SVN_LIB): ;

endif

print-svn-version:
	@echo '$(SVN_VERSION)'

print-svn-versions:
	@echo '$(SVN_VERSIONS)'

print-svn-prefix:
	@echo '$(SVN_PREFIX)'

print-clips-versions:
	@echo '$(CLIPS_VERSIONS)'

print-clips-target:
	@echo '$(TARGET)'

# The flags this configuration compiles and links with, for a build that
# wants to use the same Subversion: "make -s print-ldlibs".
print-cflags:
	@echo '$(CFLAGS)'

print-ldlibs:
	@echo '$(LDLIBS)'

print-clips:
	@echo 'CLIPS_VERSION $(CLIPS_VERSION)'
	@echo 'origin        $(CLIPS_ORIGIN)'
	@echo 'source        $(CLIPS_SRC_DIR)'
	@echo 'build         $(BUILD_DIR)'
	@echo 'binary        $(TARGET)'
	@echo 'subversion    $(SVN_ORIGIN)'
	@[ -f "$(CLIPS_SRC_STAMP)" ] && printf 'fetched       ' && cat "$(CLIPS_SRC_STAMP)" || true

install: check-install-prefix clips install-bin
	@echo
	@echo "installed $(DESTINATION) $(VERSION) in $(PREFIX)"
	@echo "  $(BINDIR)/$(DESTINATION)"
	@case ":$$PATH:" in \
	    *":$(BINDIR):"*) ;; \
	    *) echo; echo "note: $(BINDIR) is not on your PATH." ;; \
	esac

# ---------------------------------------------------------------------------
# sudo resets HOME to the home directory of root. So a "sudo make install"
# without a PREFIX would install into /root/.local with no warning. Each step
# would report success, and the files would not be where the user looks for
# them. The purpose of sudo here is to name the location. So a command that
# does not name it is an error.
# ---------------------------------------------------------------------------

PREFIX_NAMED := $(if $(filter command line environment,$(origin PREFIX)),yes,no)

check-install-prefix:
	@if [ "$$(id -u)" -eq 0 ] && [ "$(PREFIX_NAMED)" = no ]; then \
	    echo "makefile: running as root with no PREFIX named." >&2; \
	    echo >&2; \
	    echo "  sudo resets HOME, so this would install into $(PREFIX)," >&2; \
	    echo "  which is almost certainly not what you meant. Name the" >&2; \
	    echo "  location instead:" >&2; \
	    echo >&2; \
	    echo "      sudo make install PREFIX=/usr/local" >&2; \
	    echo >&2; \
	    echo "  Or install as yourself, which needs no sudo at all:" >&2; \
	    echo >&2; \
	    echo "      make install" >&2; \
	    exit 1; \
	fi

install-bin:
	install -d "$(LIBEXECDIR)" "$(BINDIR)"
	install -m755 "$(TARGET)" "$(LIBEXECDIR)/clips"
	sed -e 's|@REAL@|$(LIBEXECDIR)/clips|g' \
	    -e 's|@VERSION@|$(VERSION)|g' \
	    CLIPSVN.in > "$(BINDIR)/$(WRAPPER)"
	chmod 755 "$(BINDIR)/$(WRAPPER)"
	ln -sfn "$(WRAPPER)" "$(BINDIR)/$(DESTINATION)"

uninstall:
	rm -f "$(BINDIR)/$(WRAPPER)" "$(BINDIR)/$(DESTINATION)"
	rm -rf "$(LIBEXECDIR)"
	rmdir "$(DATADIR)" 2>/dev/null || true

# The trees under vendor/clips-source are kept. They are slow to fetch again,
# and a build writes nothing there. The vendored Subversion is also kept. Use
# distclean to remove it.
clean:
	-rm -rf vendor/clips-build "$(CLIPS_LINK)"
	-rm -rf "$(COVDIR)" tests/tmp examples/tmp

distclean:
	rm -rf vendor
	rm -f "$(ARCHIVE)"

# ---------------------------------------------------------------------------
# Tests. tests/run.sh runs the in-process suite and then the examples. It
# checks each example against the .expected file next to it. So an example
# that stops working fails the build. The suite makes every repository that
# it uses under tests/tmp. The tests do not need an installed svn or a
# network.
# ---------------------------------------------------------------------------

test: all
	CLIPS="$(CLIPS_BIN)" ./tests/run.sh

test-suite: all
	CLIPS="$(CLIPS_BIN)" ./tests/run.sh suite

test-examples: all
	CLIPS="$(CLIPS_BIN)" ./tests/run.sh examples

test-valgrind: all
	@command -v $(VALGRIND) >/dev/null 2>&1 || { \
	    echo "$(VALGRIND) not found: install valgrind, or point at it with VALGRIND=/path/to/valgrind" >&2; \
	    exit 1; \
	}
	CLIPS="$(VALGRIND) $(VALGRIND_FLAGS) $(CLIPS_BIN)" ./tests/run.sh suite

# The complete suite against every supported Subversion release, one after
# the other. Each release is fetched and built in its own directory, or
# reused if it is already in vendor/.
test-versions:
	./scripts/test-versions.sh

# The complete suite against every CLIPS that this binds to, one after the
# other. The interpreter is the variable here, so one Subversion serves all
# of them.
#
# CLIPS= clears an inherited value. Each version must be tested against its
# own binary, or the test has no meaning.
test-clips:
	@for v in $(CLIPS_VERSIONS); do \
	    echo; \
	    echo "=== CLIPS_VERSION=$$v ==="; \
	    $(MAKE) --no-print-directory CLIPS= CLIPS_VERSION="$$v" test || exit 1; \
	done

# ---------------------------------------------------------------------------
# API.md.
#
# The descriptions are written by hand, in docs/api-descriptions.md. The
# generator makes the structure: which functions exist, in which order, and
# with which arguments. The generator stops if a registered function has no
# description, or if a description names a function that is not registered.
# This keeps the two in agreement.
# ---------------------------------------------------------------------------

DOCS_GEN   := docs/gen-api-docs.sh
DOCS_INPUT := $(DOCS_GEN) docs/api-template.md docs/api-descriptions.md \
              userfunctions.c makefile

docs: API.md

API.md: $(DOCS_INPUT)
	$(DOCS_GEN) "$@"

docs-check: $(DOCS_INPUT)
	@$(DOCS_GEN) "$(CURDIR)/API.md.check" >/dev/null
	@if cmp -s API.md API.md.check; then \
	    rm -f API.md.check; echo "API.md is up to date"; \
	else \
	    diff -u API.md API.md.check | head -40; rm -f API.md.check; \
	    echo "API.md is stale: run 'make docs'" >&2; exit 1; \
	fi

help:
	@printf 'CLIPSVN targets:\n\n'
	@printf '  %-16s %s\n' \
	    all             'build the binary against one CLIPS (the default)' \
	    debug           'the same build with debugging symbols' \
	    subversion      'fetch, verify and build the pinned Subversion only' \
	    clips-source    'fetch the selected CLIPS without building it' \
	    test            'the whole suite and the examples: tests/run.sh' \
	    test-suite      'only the in-process suite, tests/test.bat' \
	    test-examples   'only the examples, checked against examples/*.expected' \
	    test-valgrind   'the in-process suite under valgrind' \
	    test-versions   'run the whole suite against every supported Subversion' \
	    test-clips      'run the whole suite against all three CLIPS versions' \
	    coverage        'line coverage of userfunctions.c' \
	    docs            'regenerate API.md' \
	    docs-check      'fail if API.md is not what docs would write' \
	    install         'install the binary and its wrapper under PREFIX' \
	    uninstall       'remove what install put there' \
	    clean           'remove the build trees, coverage and test scratch' \
	    distclean       'also remove the fetched CLIPS and Subversion sources'
	@printf '\nSubversion, APR, APR-util and SQLite are pinned, downloaded,\n'
	@printf 'checked against the SHA-256 this makefile records for each, and\n'
	@printf 'built statically. This build uses Subversion %s.\n' '$(SVN_VERSION)'
	@printf '\nSupported: %s\n' '$(SVN_VERSIONS)'
	@printf '\n  SVN_SYSTEM=1          link the Subversion this machine has\n'
	@printf '  SVN_VERSION=          pin a different release (also set SVN_SHA256)\n'
	@printf '  SVN_CONFIGURE_OPTS=   the configure options for the vendored build\n'
	@printf '  SVN_PREFIX=           use a Subversion already built and installed\n'
	@printf '\nCLIPS is built from one of three sources, selected with\n'
	@printf 'CLIPS_VERSION. This build uses %s: %s.\n' \
	    '$(CLIPS_VERSION)' '$(CLIPS_ORIGIN)'
	@printf '\n  CLIPS_VERSION=6.4.2   the release tarball from SourceForge\n'
	@printf '  CLIPS_VERSION=svn-6x  branches/64x, pinned at r%s\n' '$(CLIPS_SVN_6X_REV)'
	@printf '  CLIPS_VERSION=svn-7x  branches/70x, pinned at r%s\n' '$(CLIPS_SVN_7X_REV)'
	@printf '  CLIPS_SVN_REV=        build a branch at another revision,\n'
	@printf '                        or at HEAD (needs svn installed)\n'
	@printf '  CLIPS_SVN_URL=        take the branch from somewhere else\n'
	@printf '\nEach version is fetched and built under its own directory, and\n'
	@printf 'vendor/clips points at the one built last. "make print-clips"\n'
	@printf 'says which that is. "make -s print-cflags" and "make -s print-ldlibs"\n'
	@printf 'print the flags this configuration compiles and links with.\n'
	@printf '\ninstall goes to %s and needs no privileges.\n' '$(PREFIX)'
	@printf 'For a system-wide location, name it:\n'
	@printf '\n      sudo make install PREFIX=/usr/local\n'
	@printf '\nOther variables: PREFIX, VERSION, CLIPS (which binary the tests\n'
	@printf 'run), VALGRIND, COV_CC, GCOV, NPROC.\n'

# ---------------------------------------------------------------------------
# Line coverage of the wrappers, and the list of wrappers that no test
# entered.
# ---------------------------------------------------------------------------
coverage: clips
	mkdir -p "$(COVDIR)"
	cp userfunctions.c "$(COVDIR)/"
	$(COV_CC) -c -D$(CLIPS_OS) $(COV_CFLAGS) $(SVN_CFLAGS) -I"$(BUILD_DIR)" \
	    -o "$(COVDIR)/userfunctions.o" "$(COVDIR)/userfunctions.c"
	cp "$(BUILD_DIR)/libclips.a" "$(COVDIR)/libclips.a"
	ar d "$(COVDIR)/libclips.a" userfunctions.o
	ar r "$(COVDIR)/libclips.a" "$(COVDIR)/userfunctions.o"
	$(COV_CC) -o "$(COVDIR)/clips" "$(BUILD_DIR)/main.o" \
	    -L"$(COVDIR)" -lclips --coverage $(LDLIBS)
	rm -f "$(COVDIR)/userfunctions.gcda"
	: > "$(COVDIR)/never-entered.txt"
	CLIPS="$(COVDIR)/clips" ./tests/run.sh || true
	cd "$(COVDIR)" && $(GCOV) -b -f userfunctions.c > by-function.txt
	@echo
	@sed -n "/^File .*userfunctions\.c/,/^Creating/p" "$(COVDIR)/by-function.txt" | grep -v '^Creating'
	@grep -v '^[[:space:]]*//' userfunctions.c | grep 'AddUDF(' \
	    | grep -oE '"Svn[A-Za-z0-9_]*Function"' | tr -d '"' | sort -u \
	    > "$(COVDIR)/registered-udfs.txt"
	@awk -v out="$(COVDIR)/never-entered.txt" \
	     'NR==FNR { udf[$$0]=1; next } \
	      /^Function / { fn=substr($$2,2,length($$2)-2); want=(fn in udf); next } \
	      /^Lines executed:/ && want { \
	        want=0; p=$$2; sub(/^executed:/,"",p); sub(/%$$/,"",p); \
	        n=$$4+0; fns++; tot+=n; cov+=p*n/100; \
	        if (p+0==0) { zero++; print fn > out } } \
	      END { if (tot) printf "\nUDF handlers only: %d registered, %d lines, %.1f%% executed (%d never entered)\n", fns, tot, 100*cov/tot, zero }' \
	    "$(COVDIR)/registered-udfs.txt" "$(COVDIR)/by-function.txt"
	@echo
	@echo "per-function detail: $(COVDIR)/by-function.txt"
	@echo "annotated source:    $(COVDIR)/userfunctions.c.gcov"
	@echo "never entered:       $(COVDIR)/never-entered.txt"
