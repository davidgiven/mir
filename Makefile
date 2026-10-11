PYTHON ?= python3
MOTIF_SOURCE ?= $(HOME)/src/motif/lib/Xm
MKMIR_ARGS ?=

.PHONY: all test

all: mir.json include/cmotif_impl.h hello

mir.json: src/mkmir.py
	$(PYTHON) src/mkmir.py "$(MOTIF_SOURCE)" -o "$@" $(MKMIR_ARGS)

include/cmotif_impl.h: mir.json src/mkcpp.py
	$(PYTHON) src/mkcpp.py "$<" -o "$@"
	clang-format -i "$@"

hello: example/hello.cc src/cmotif.cc include/cmotif_impl.h include/cmotif.h
	$(CXX) -std=c++20 -Iinclude -I$(MOTIF_SOURCE) -o "$@" example/hello.cc src/cmotif.cc -lXm -lXt -lX11 -I. -Os -g

test:
	$(PYTHON) -m unittest discover -s tests
