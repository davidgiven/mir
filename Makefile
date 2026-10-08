# mir -- Motif introspection / reflection
# Build system: GNU Make

CXX      ?= g++
CXXFLAGS ?= -std=c++23 -Wall -Wextra -Wpedantic -O2
LDFLAGS  ?=

SRC := src/main.cc
BIN := mir
OBJ := .obj/main.o

.PHONY: all clean

all: $(BIN)

$(OBJ): $(SRC)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(BIN): $(OBJ)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	rm -rf build

# TODO: fail with a clear diagnostic if Xm/Xm.h or libclang is not found.
# Example (to be enabled once static/dynamic extraction is wired):
#   HAVE_XM := $(shell echo '\#include <Xm/Xm.h>' | $(CXX) -E -x c++ - -o /dev/null 2>/dev/null && echo yes || echo no)
#   ifeq ($(HAVE_XM),no)
#   $(error Xm/Xm.h not found -- install libxt-dev / libmotif-dev (Debian) or motif-devel (Fedora))
#   endif
