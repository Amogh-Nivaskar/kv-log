CC     = gcc
CFLAGS = -g -Wall -Wextra

# `make asan` rebuilds everything with AddressSanitizer + UBSan into its own
# directory (so sanitized and plain objects never get mixed) and runs the tests.
SANITIZE  = -fsanitize=address,undefined -fno-omit-frame-pointer
ASAN_DIR  = build-asan

# Where objects and binaries go. "." for a normal build, ASAN_DIR for `make asan`.
BUILD = .

# ---- Find files automatically -------------------------------------------
# Main program
MAIN      = main.c
# Every file named test_*.c is a separate test program
TEST_SRCS = $(wildcard test_*.c)
# Every OTHER .c file is a subsystem (hashmap.c, and later storage.c, etc.)
LIB_SRCS  = $(filter-out $(MAIN) $(TEST_SRCS), $(wildcard *.c))

LIB_OBJS  = $(addprefix $(BUILD)/,$(LIB_SRCS:.c=.o))
TESTS     = $(addprefix $(BUILD)/,$(TEST_SRCS:.c=))

# ---- Targets ------------------------------------------------------------
all: $(BUILD)/kv $(TESTS)

# kv = kv.c + all subsystems
$(BUILD)/kv: $(BUILD)/main.o $(LIB_OBJS)
	$(CC) $(CFLAGS) $^ -o $@

# each test_X = test_X.c + all subsystems
$(BUILD)/test_%: $(BUILD)/test_%.o $(LIB_OBJS)
	$(CC) $(CFLAGS) $^ -o $@

# compile any .c into a .o (recompile if any header or this Makefile changes)
$(BUILD)/%.o: %.c *.h Makefile
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

# build and run every test
test: $(TESTS)
	@for t in $(TESTS); do echo "== $$t =="; ./$$t || exit 1; done

# build and run every test under AddressSanitizer + UBSan
asan:
	$(MAKE) test BUILD=$(ASAN_DIR) CFLAGS="$(CFLAGS) $(SANITIZE)"

clean:
	rm -f *.o kv $(notdir $(TESTS))
	rm -rf $(ASAN_DIR)

.PHONY: all test asan clean
.SECONDARY:
