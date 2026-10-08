# AGENTS.md — mir

## Project Overview

`mir` (Motif Introspection / Reflection) produces a **machine-readable description of an X Intrinsics (Xt) widget set, primarily Motif (OpenMotif / LessTif)**.

The description is the single source of truth for generating **multiple sets of language bindings** (e.g., Python, Rust, Ada — exact targets TBD). Do not hand-write bindings or duplicate widget metadata; fix the generator and the description instead.

### What is being described

- Xt Intrinsics core: `Widget`, `Composite`, `Constraint`, `Shell`, `Object` etc. (`<X11/Intrinsic.h>`, `<X11/StringDefs.h>`, `<X11/Shell.h>`)
- Motif widget set: everything under `<Xm/*.h>` — `XmPushButton`, `XmTextField`, `XmForm`, `XmRowColumn`, etc.
- For each widget class: inheritance chain, resources (XtResourceList / XtResource), constraint resources, class record, callbacks (`XtCallbackList`), enumerated types, convenience functions (`XmCreate*`), translation tables and actions where relevant.

### Output

A single versioned machine-readable artifact (default: `mir.json`, JSON Schema defined in `schema/mir.schema.json`). Consumers must not parse headers directly — they consume this artifact.

Field stability matters. Adding a widget/resource is a minor change; removing or renaming is major. Keep `schema/` and `schema_version` in sync.

## Architecture — Two-Phase Extraction

No single technique is sufficient for Xt/Motif, so `mir` combines both. Keep the phases separate and merge late.

### 1. Static — Header Parsing (C++ / libclang)

- **Tooling:** C++23, Clang Tooling / `libclang` / `clang-c` (not regex, not ad-hoc parsing).
- **Inputs:** Preprocessed headers reachable from `Xm/Xm.h`, `Xm/*.h`, `X11/Intrinsic.h`, `X11/Shell.h`, `X11/StringDefs.h`. Must handle:
  - Guarded `#ifdef` / `XmVersion` / vendor extensions
  - `XtResource` / `XmResource` macros, `externaldef` patterns
  - Typedefs, enums, `#define` constants that become `XmN*`/`XmC*`/`XmR*` strings
  - Function prototypes for `XmCreate*`, `XmVaCreate*`, setters/getters
- **Outputs:** Raw AST-derived records: widgets found, declared resources, types, enums, defines, function signatures, include provenance, header location (`file:line`).

Point the parser at the *real installed headers* (`libxm-dev`, `libxt-dev` on Debian/Ubuntu, or vendored Motif). Do not vendor a copy of Motif headers.

### 2. Dynamic — Run-Time Reflection (Xt/Motif linked binary)

Some information only exists at run time in the widget's `CoreClassRec` / `Xm*ClassRec`:

- `XtGetResourceList` / `XtGetConstraintResourceList` for inherited and merged resources (including resources defined in superclasses via `Core`, `Composite`, `Constraint`, `XmManager`, `XmPrimitive`)
- Default values, `XtResource.resource_type` (`XmR*`), `resource_size`, `default_addr`
- Actual widget class hierarchy via `XtClass(widget)->core_class.superclass`
- Run-time enumeration values that depend on link-time constants

Implementation:

- The tool links against `-lXt -lXm -lX11` and initializes `XtToolkitInitialize` / `XtOpenDisplay` (with `Xvfb` / headless `DISPLAY` in CI).
- For each known `WidgetClass` pointer (e.g., `xmPushButtonWidgetClass`), instantiate a hidden widget hierarchy, query `XtGetResourceList`, and dump JSON. No visible windows required.
- This harness is the ground truth for resource merging and inheritance. If static and dynamic disagree, dynamic wins for resources, static wins for function signatures.

### Merge

`mir` (the main binary in `src/mir.cpp`) orchestrates: run header parse → run reflection harness (subprocess or linked) → merge → validate against `schema/mir.schema.json` → emit `mir.json`.

## Repository Layout

```
mir/
├── AGENTS.md
├── schema/mir.schema.json    # JSON Schema for the emitted artifact
├── src/
│   ├── mir.cpp               # entry point / merge / CLI
├── test/
│   ├── data/                 # minimal Motif headers for unit tests
│   └── golden/               # golden mir.json snippets for diff tests
├── tools/                    # binding generators (examples / smoke tests)
└── docs/
```

If a directory does not yet exist, create it following this layout.

## Build & Run

The build system is GNU Make.

```bash
make -j$(nproc)
build/mir --validate mir.json   # validate against schema
```

Dependencies (install via system package manager, not vendored):

- `clang` + `libclang-dev` + `llvm` (>= 16)
- `libxt-dev`, `libmotif-dev` or `libxm4-dev` / `motif-dev` (OpenMotif 2.3.x)
- `libx11-dev`, `libxft-dev` if Motif was built with Xft
- `nlohmann-json` or equivalent (prefer header-only, pinned via CMake FetchContent)

Make must fail with a clear diagnostic if `Xm/Xm.h` or `libclang` is not found.

## Output Schema (summary)

Will be created incrementally as the project develops.

## Non-Goals

- Full C++ binding for Xt/Motif (this tool only *describes*; binding generators consume the description).
- Supporting non-Xt widget sets in v1 (GTK/Qt are out of scope).
- GUI designer or interactive builder — strictly a headless CLI.

## Conventions

- Name class data members with a leading underscore (`_name`); local
  variables and function parameters stay bare.
- Declare non-static free-function prototypes in headers with `extern`.
- Reformat changed sources with `clang-format` (per the repo `.clang-format`)
  before committing.
- Never commit anything to the VCS — the user will always handle commits manually.
- For very large numbers (e.g. nanosecond values) always use `'` digit separators grouped as `000'000` (e.g. `1'000'000'000`, `100'000'000`).
- Use /* */ block comments throughout.
- Development is done iteratively, in small steps. Follow the user's request precisely, and don't extrapolate unless explicitly asked to.
- Keep the AGENTS.md up to date.

## References

- Xt Intrinsics: `X Toolkit Intrinsics — C Language Interface` (X11R7 docs)
- Motif 2.1 → 2.3: `Motif Programmer's Guide` / `Xm` man pages (`man XmPushButton`)
- Clang Tooling: https://clang.llvm.org/docs/Tooling.html , https://clang.llvm.org/docs/LibClang.html
- JSON Schema: https://json-schema.org/
