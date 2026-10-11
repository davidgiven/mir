PYTHON ?= python3
MOTIF_SOURCE ?= $(HOME)/src/motif/lib/Xm
MKMIR_ARGS ?=

.PHONY: all test

all: mir.json include/cmotif_impl.h

mir.json: src/mkmir.py
	$(PYTHON) src/mkmir.py "$(MOTIF_SOURCE)" -o "$@" $(MKMIR_ARGS)

include/cmotif_impl.h: mir.json
	$(PYTHON) src/mkcpp.py "$<" -o "$@"

test:
	$(PYTHON) -m unittest discover -s tests
