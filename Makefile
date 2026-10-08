# mir -- Motif introspection / reflection
# Build system: GNU Make

CXX      ?= g++
CXXFLAGS ?= -std=c++23 -Wall -Wextra -Wpedantic -O2
LDFLAGS  ?=

# -- libclang / LLVM configuration -----------------------------------------

LLVM_CONFIG ?= llvm-config

# Fallback to versioned binary if unversioned not found (Debian: only
# llvm-config-<ver> exists).
ifeq (,$(shell which $(LLVM_CONFIG) 2>/dev/null))
  LLVM_CONFIG := $(shell command -v llvm-config-21 2>/dev/null || command -v llvm-config-20 2>/dev/null || command -v llvm-config-19 2>/dev/null || command -v llvm-config-18 2>/dev/null || command -v llvm-config-17 2>/dev/null || command -v llvm-config-16 2>/dev/null || echo llvm-config)
endif
ifeq (,$(shell which $(LLVM_CONFIG) 2>/dev/null))
  $(error llvm-config not found -- install clang-devel / llvm-devel (>= 16))
endif

LLVM_VERSION := $(shell $(LLVM_CONFIG) --version 2>/dev/null | cut -d. -f1)
ifeq ($(shell test $(LLVM_VERSION) -lt 16 2>/dev/null && echo yes || echo no),yes)
  $(error llvm version $(LLVM_VERSION) too old -- need >= 16)
endif

LLVM_CXXFLAGS := $(shell $(LLVM_CONFIG) --cxxflags 2>/dev/null)
LLVM_LDFLAGS  := $(shell $(LLVM_CONFIG) --ldflags 2>/dev/null)

# llvm-config emits -std=c++17 which would override our -std=c++23 if
# appended afterwards (last -std wins). Strip it.
LLVM_CXXFLAGS := $(filter-out -std=%,$(LLVM_CXXFLAGS))

# Verify required headers are reachable with the current flags.
HAVE_XM := $(shell echo '\#include <Xm/Xm.h>' | $(CXX) -E -x c++ - -o /dev/null 2>/dev/null && echo yes || echo no)
ifeq ($(HAVE_XM),no)
  $(error Xm/Xm.h not found -- install libxt-dev / libmotif-dev (Debian) or motif-devel (Fedora))
endif

HAVE_LIBCLANG := $(shell echo '\#include <clang-c/Index.h>' | $(CXX) $(LLVM_CXXFLAGS) -E -x c++ - -o /dev/null 2>/dev/null && echo yes || echo no)
ifeq ($(HAVE_LIBCLANG),no)
  $(error clang-c/Index.h not found -- install libclang-dev / clang-devel (>= 16))
endif

CXXFLAGS += $(LLVM_CXXFLAGS)
LDFLAGS  += $(LLVM_LDFLAGS) -lclang

# -- build rules -----------------------------------------------------------

SRC := src/main.cc
BIN := mir
OBJ := .obj/main.o
JSON := mir.json

.PHONY: all clean

all: $(BIN) $(JSON)

$(OBJ): $(SRC)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(BIN): $(OBJ)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

$(JSON): $(BIN)
	./$(BIN) --headers /usr/include/Xm --output=$@

clean:
	rm -rf .obj $(BIN) build $(JSON)

