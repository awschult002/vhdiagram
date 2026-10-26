CC      ?= cc
AR      ?= ar
CFLAGS  ?= -O2
CFLAGS  += -std=c99 -Wall -Wextra -pedantic -MMD
X       := $(if $(filter Windows_NT,$(OS)),.exe,)
EXE     := vhdiagram$(X)

# Headless core: everything in src/ except the program entry point.
CORE_SRC := $(filter-out src/main.c,$(wildcard src/*.c))
CORE_OBJ := $(CORE_SRC:.c=.o)
CORE_LIB := build/libvhcore.a
APP_OBJ  := src/main.o

TEST_SRC := $(wildcard tests/*_test.c)
TEST_OBJ := $(TEST_SRC:.c=.o)
TEST_BIN := $(patsubst tests/%.c,build/%$(X),$(TEST_SRC))

all: $(EXE)

$(EXE): $(APP_OBJ) $(CORE_LIB)
	$(CC) $(CFLAGS) -o $@ $(APP_OBJ) $(CORE_LIB) $(LDFLAGS) $(LDLIBS)

$(CORE_LIB): $(CORE_OBJ)
	@mkdir -p build
	rm -f $@
	$(AR) rcs $@ $(CORE_OBJ)

tests/%.o: CPPFLAGS += -Isrc

build/%$(X): tests/%.o $(CORE_LIB)
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ $< $(CORE_LIB) $(LDFLAGS) $(LDLIBS)

test: $(EXE) $(TEST_BIN)
	@for t in $(TEST_BIN); do echo "== $$t"; ./$$t || exit 1; done
	sh tests/smoke.sh

# Corpus run over hdl-modules (needs git + network); see tests/corpus.sh.
build/corpus_run$(X): tests/corpus_run.o $(CORE_LIB)
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ $< $(CORE_LIB) $(LDFLAGS) $(LDLIBS)

build/scan_dump$(X): tests/scan_dump.o $(CORE_LIB)
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ $< $(CORE_LIB) $(LDFLAGS) $(LDLIBS)

corpus: build/corpus_run$(X) build/scan_dump$(X)
	sh tests/corpus.sh build/corpus_run$(X) build/scan_dump$(X)

# Sanitizer run: test + corpus under ASan/UBSan in a separate tree (build/san).
SAN := -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all
san:
	@mkdir -p build/san/src
	cp -R Makefile tests build/san/ && cp src/*.[ch] build/san/src/
	rm -f build/san/tests/*.[od]
	ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 \
	UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1 \
	$(MAKE) -C build/san CFLAGS="$(SAN)" LDFLAGS="$(SAN)" test corpus

clean:
	rm -rf build
	rm -f $(EXE) $(APP_OBJ) $(CORE_OBJ) $(TEST_OBJ)
	rm -f $(APP_OBJ:.o=.d) $(CORE_OBJ:.o=.d) $(TEST_OBJ:.o=.d)
	rm -f tests/corpus_run.o tests/corpus_run.d tests/scan_dump.o tests/scan_dump.d

-include $(APP_OBJ:.o=.d) $(CORE_OBJ:.o=.d) $(TEST_OBJ:.o=.d)
.PHONY: all test clean corpus san
.SECONDARY: $(TEST_OBJ) tests/corpus_run.o tests/scan_dump.o
