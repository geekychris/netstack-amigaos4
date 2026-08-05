# netstack-amigaos4 — top-level Makefile.
#
# Invoked inside the walkero/amigagccondocker:os4-gcc11-arm64
# container by scripts/build.sh. Do not run on the host directly —
# CC name (ppc-amigaos-gcc) only exists inside the container.
#
# Per-phase targets — each compiles what it can today:
#   phase1  : OSAL stubs (compiles; most impls are TODO panics)
#   phase2  : engine main + init stubs (compiles; boots nothing)
#   phase3  : bsdsocket library skeleton (compiles; every call ENOSYS)
#   phase4  : NetDev registry + ring + reference driver stubs (compiles)
#   phase5  : SANA-II bridge + bench stubs (compiles)
#
# Nothing here PRODUCES a working artifact yet. `make all` just
# proves the toolchain builds every .c we've written so far.

CC     = ppc-amigaos-gcc
STRIP  = ppc-amigaos-strip
AR     = ppc-amigaos-ar

CFLAGS = -mcrt=newlib -mhard-float -O2 -mcpu=440 -Wall -Wextra \
         -D__PPC__ -D__USE_OLD_TIMEVAL__ \
         -I./include -I./include/netstack -I./include/rumpuser

# CFLAGS for compiling imported NetBSD rump kernel source.
# See docs/rump_build_probe.md for the derivation of these.
RUMP_ROOT   = vendor/netbsd-rump
RUMP_OPT    = $(RUMP_ROOT)/sys/rump/include/opt/opt_rumpkernel.h
RUMP_CFLAGS = -mcrt=newlib -mhard-float -O2 -mcpu=440 \
              -D__PPC__ -ffreestanding -fno-strict-aliasing \
              -Wno-format-zero-length -Wno-pointer-sign \
              -imacros $(RUMP_OPT) \
              -I$(RUMP_ROOT)/sys \
              -I$(RUMP_ROOT)/sys/rump/include \
              -I$(RUMP_ROOT)/common/include \
              -I./include -I./include/netstack -I./include/rumpuser

BUILD = build

# -------- Source lists --------

PHASE1_SRC = \
    src/phase1_osal/osal_memory.c \
    src/phase1_osal/osal_lock.c   \
    src/phase1_osal/osal_thread.c \
    src/phase1_osal/osal_timer.c  \
    src/phase1_osal/osal_diag.c   \
    src/phase1_osal/mbuf_pool.c   \
    src/phase1_osal/rump_kern_globals.c \
    src/phase1_osal/rump_atomics.c \
    src/phase1_osal/rump_link_stubs.c

PHASE2_SRC = \
    src/phase2_engine/engine_main.c \
    src/phase2_engine/engine_init.c \
    src/phase2_engine/fdtable.c

PHASE3_SRC = \
    src/phase3_bsdsocket/bsdsocket_library.c \
    src/phase3_bsdsocket/socket_ipc.c

PHASE4_SRC = \
    src/phase4_netdev/netdev_registry.c   \
    src/phase4_netdev/netdev_ring.c       \
    src/phase4_netdev/reference_driver.c

PHASE5_SRC = \
    src/phase5_testing/sana2_bridge.c \
    src/phase5_testing/bench_stub.c

# -------- Object lists --------

PHASE1_OBJ = $(PHASE1_SRC:src/%.c=$(BUILD)/%.o)
PHASE2_OBJ = $(PHASE2_SRC:src/%.c=$(BUILD)/%.o)
PHASE3_OBJ = $(PHASE3_SRC:src/%.c=$(BUILD)/%.o)
PHASE4_OBJ = $(PHASE4_SRC:src/%.c=$(BUILD)/%.o)
PHASE5_OBJ = $(PHASE5_SRC:src/%.c=$(BUILD)/%.o)

ALL_OBJ = $(PHASE1_OBJ) $(PHASE2_OBJ) $(PHASE3_OBJ) $(PHASE4_OBJ) $(PHASE5_OBJ)

# -------- Tests --------

PHASE1_TESTS = $(BUILD)/tests/test_threads $(BUILD)/tests/test_sleep $(BUILD)/tests/test_cv
PHASE2_TESTS = $(BUILD)/tests/test_engine_ping
PHASE3_TESTS = $(BUILD)/tests/test_client_rpc $(BUILD)/tests/test_echo \
               $(BUILD)/tests/test_bsdlib

TESTLDFLAGS = -lauto

# -------- Targets --------

.PHONY: all phase1 phase2 phase3 phase4 phase5 tests clean rump-probe

# Rump kernel compile probe — builds rump's OWN locks.c (the
# hypercall-wrapper implementation of mutex/cv/rwlock that
# replaces sys/kern/kern_mutex.c). This is what a real rump
# build uses; kern_mutex.c is NOT in the rump-kern SRCS list.
#
# Deliverable: rump_locks.o with mutex_init/enter/exit,
# cv_init/wait/signal/broadcast — all defined, unresolved refs
# only to rumpuser_* (which our Phase 1 OSAL provides) and a
# handful of kernel globals (hz, panic, rump_threads, etc. —
# provided by rump_kern_globals.c).
rump-probe: $(BUILD)/rump_locks.o
	@echo "=== rump-probe: rump/librump/rumpkern/locks.c built ==="
	@ls -la $<
	@echo ""
	@echo "=== defined symbols ==="
	@ppc-amigaos-nm $< | grep -E '^[0-9a-f]+ [TDR] ' | head -20
	@echo ""
	@echo "=== unresolved (should ONLY be rumpuser_* or kernel globals) ==="
	@ppc-amigaos-nm $< | grep '^ *U ' | head -20

RUMP_LOCKS_CFLAGS = $(RUMP_CFLAGS) \
                    -D_RUMPKERNEL \
                    -I$(RUMP_ROOT)/sys/rump/librump/rumpkern

$(BUILD)/rump_locks.o: $(RUMP_ROOT)/sys/rump/librump/rumpkern/locks.c
	@mkdir -p $(BUILD)
	$(CC) $(RUMP_LOCKS_CFLAGS) -c $< -o $@

# The old file-that-shouldn't-be-built target, kept because it
# proves the RUMP_CFLAGS work on a raw sys/kern file too.
$(BUILD)/rump_kern_mutex.o: $(RUMP_ROOT)/sys/kern/kern_mutex.c
	@mkdir -p $(BUILD)
	$(CC) $(RUMP_CFLAGS) -c $< -o $@

all: phase1 phase2 phase3 phase4 phase5 tests
	@echo ""
	@echo "=== netstack-amigaos4 skeleton build complete ==="
	@echo "    all phases compile; nothing shippable yet."
	@echo "    see docs/roadmap.md for what happens next."

phase1: $(BUILD)/libnetstack_osal.a
	@echo "phase1: OSAL objects built"

phase2: $(PHASE2_OBJ)
	@echo "phase2: engine objects built"

phase3: $(BUILD)/libnetstack_client.a $(BUILD)/netstack.library
	@echo "phase3: bsdsocket / client-lib objects built"

$(BUILD)/netstack.library: $(BUILD)/phase3_bsdsocket/bsdsocket_library.o \
                             $(BUILD)/phase3_bsdsocket/socket_ipc.o \
                             $(PHASE2_OBJ) $(BUILD)/libnetstack_osal.a
	$(CC) $^ -o $@ -nostartfiles \
	    -Wl,-z,common-page-size=4096 \
	    -Wl,-z,max-page-size=4096
	$(STRIP) --strip-all $@

phase4: $(BUILD)/libnetstack_netdev.a
	@echo "phase4: NetDev objects built"

phase5: $(PHASE5_OBJ)
	@echo "phase5: testing/bridge objects built"

tests: $(PHASE1_TESTS) $(PHASE2_TESTS) $(PHASE3_TESTS)
	@echo "tests: phase1 + phase2 + phase3 test binaries built"

$(BUILD)/tests/test_threads: tests/phase1/test_threads.c $(BUILD)/libnetstack_osal.a
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $< $(BUILD)/libnetstack_osal.a -o $@ $(TESTLDFLAGS)

$(BUILD)/tests/test_sleep: tests/phase1/test_sleep.c $(BUILD)/libnetstack_osal.a
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $< $(BUILD)/libnetstack_osal.a -o $@ $(TESTLDFLAGS)

$(BUILD)/tests/test_cv: tests/phase1/test_cv.c $(BUILD)/libnetstack_osal.a
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $< $(BUILD)/libnetstack_osal.a -o $@ $(TESTLDFLAGS)

$(BUILD)/tests/test_engine_ping: tests/phase2/test_engine_ping.c \
                                  $(BUILD)/libnetstack_osal.a $(PHASE2_OBJ)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $< $(PHASE2_OBJ) $(BUILD)/libnetstack_osal.a -o $@ $(TESTLDFLAGS)

$(BUILD)/tests/test_client_rpc: tests/phase3/test_client_rpc.c \
                                 $(BUILD)/libnetstack_client.a \
                                 $(BUILD)/libnetstack_osal.a $(PHASE2_OBJ)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $< $(PHASE2_OBJ) \
	    $(BUILD)/libnetstack_client.a $(BUILD)/libnetstack_osal.a \
	    -o $@ $(TESTLDFLAGS)

$(BUILD)/tests/test_echo: tests/phase3/test_echo.c \
                           $(BUILD)/libnetstack_client.a \
                           $(BUILD)/libnetstack_osal.a $(PHASE2_OBJ)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $< $(PHASE2_OBJ) \
	    $(BUILD)/libnetstack_client.a $(BUILD)/libnetstack_osal.a \
	    -o $@ $(TESTLDFLAGS)

# test_bsdlib links against the OS-installed bsdsocket.library
# at runtime — no linker deps on it here.
$(BUILD)/tests/test_rump_init: tests/phase2/test_rump_init.c \
                                $(BUILD)/librump.a \
                                $(BUILD)/libnetstack_osal.a
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $< \
	    $(BUILD)/librump.a $(BUILD)/libnetstack_osal.a \
	    -o $@ $(TESTLDFLAGS)

$(BUILD)/tests/test_bsdlib: tests/phase3/test_bsdlib.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $< -o $@ $(TESTLDFLAGS)

# Static libs — the useful shape for the eventual link steps.
$(BUILD)/libnetstack_osal.a: $(PHASE1_OBJ)
	$(AR) rcs $@ $^

$(BUILD)/libnetstack_client.a: $(PHASE3_OBJ)
	$(AR) rcs $@ $^

$(BUILD)/libnetstack_netdev.a: $(PHASE4_OBJ)
	$(AR) rcs $@ $^

# Generic .c → .o rule; mkdir the phase subdir on demand.
$(BUILD)/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD)
