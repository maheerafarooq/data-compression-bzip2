# BZip2 course build – see project description, §6.1
# Required targets: all, clean, windows
# Requires: GNU Make, GCC (Linux/macOS) or MinGW-w64 (Windows / MSYS2)
#
# Linux/macOS:        make            then  ./bzip2_impl
# Windows (MSYS2):    mingw32-make    then  ./bzip2_impl.exe
# Windows (cmd.exe):  mingw32-make    then  bzip2_impl.exe
# Cross (Linux→Win):  make windows CC=x86_64-w64-mingw32-gcc

CC      ?= gcc
CFLAGS  += -std=c11 -Wall -Wextra -O2 -Iinclude
TARGET  = bzip2_impl
# Course pipeline order (orchestrated in pipeline.c):
#   block_division → rle1 → bwt → mtf → rle2 → huffman
SRC     = src/main.c src/pipeline.c src/block.c src/rle1.c src/bwt.c \
          src/mtf.c src/rle2.c src/huffman.c src/range.c src/config.c
HDRS    = $(wildcard include/*.h)

# Platform detection: .exe + cmd.exe-friendly clean on plain Windows.
ifeq ($(OS),Windows_NT)
  EXE      := .exe
  RM       := del /q
  RUN_PFX  :=
else
  EXE      :=
  RM       := rm -f
  RUN_PFX  := ./
endif

.PHONY: all clean windows run

# Default: native executable for this host (on Windows, produces .exe).
all: $(TARGET)$(EXE)

# Build and run default action: pipeline on every file in benchmarks/
# producing results/results.csv (see config.ini, §2.2).
run: $(TARGET)$(EXE)
	$(RUN_PFX)$(TARGET)$(EXE)

$(TARGET)$(EXE): $(SRC) $(HDRS)
	$(CC) $(CFLAGS) -o $(TARGET)$(EXE) $(SRC)

# Required by §6.1 — same as "all"; cross-build from Linux:
#   make windows CC=x86_64-w64-mingw32-gcc
windows: all

clean:
	-$(RM) $(TARGET) $(TARGET)$(EXE)
