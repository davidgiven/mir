PYTHON ?= python3
MOTIF_SOURCE ?= $(HOME)/src/motif/lib/Xm
MKMIR_ARGS ?=

.PHONY: all mir.json test

all: mir.json

# Re-extract on demand so changes to external Motif sources and headers count.
mir.json:
	$(PYTHON) src/mkmir.py "$(MOTIF_SOURCE)" -o "$@" $(MKMIR_ARGS)

test:
	$(PYTHON) -m unittest discover -s tests
