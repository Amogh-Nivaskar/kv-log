CC     = gcc
CFLAGS = -g -Wall -Wextra -Isrc

# `make asan` rebuilds everything with AddressSanitizer + UBSan into its own
# directory (so sanitized and plain objects never get mixed) and runs the tests.
SANITIZE  = -fsanitize=address,undefined -fno-omit-frame-pointer
ASAN_DIR  = build-asan

# Where objects and binaries go: build/ normally, ASAN_DIR for `make asan`.
BUILD = build

# ---- Layout -------------------------------------------------------------
#   src/    all program source and headers
#   tests/  one test_*.c per test program
#   build/  generated objects and binaries
vpath %.c src tests

# ---- Find files automatically -------------------------------------------
# Main program
MAIN      = main.c
# Every file named tests/test_*.c is a separate test program
TEST_SRCS = $(notdir $(wildcard tests/test_*.c))
# Every OTHER src/*.c file is a subsystem (hashmap.c, kv.c, ...)
LIB_SRCS  = $(filter-out $(MAIN), $(notdir $(wildcard src/*.c)))
HEADERS   = $(wildcard src/*.h)

LIB_OBJS  = $(addprefix $(BUILD)/,$(LIB_SRCS:.c=.o))
TESTS     = $(addprefix $(BUILD)/,$(TEST_SRCS:.c=))

# ---- Targets ------------------------------------------------------------
all: $(BUILD)/kv $(TESTS)

# kv = main.c + all subsystems
$(BUILD)/kv: $(BUILD)/main.o $(LIB_OBJS)
	$(CC) $(CFLAGS) $^ -o $@

# each test_X = test_X.c + all subsystems
$(BUILD)/test_%: $(BUILD)/test_%.o $(LIB_OBJS)
	$(CC) $(CFLAGS) $^ -o $@

# compile any .c into a .o (recompile if any header or this Makefile changes)
$(BUILD)/%.o: %.c $(HEADERS) Makefile
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

# build and run every test
test: $(TESTS)
	@for t in $(TESTS); do echo "== $$t =="; ./$$t || exit 1; done

# build and run every test under AddressSanitizer + UBSan
asan:
	$(MAKE) test BUILD=$(ASAN_DIR) CFLAGS="$(CFLAGS) $(SANITIZE)"

clean:
	rm -rf build $(ASAN_DIR)

.PHONY: all test asan clean
.SECONDARY:
