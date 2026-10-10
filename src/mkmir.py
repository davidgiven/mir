#!/usr/bin/env python3
"""Skeleton for extracting Xt/Motif metadata from source using libclang."""

import argparse
import ast
import json
import os
import shutil
import subprocess
from pathlib import Path


def parse_args(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "sources", nargs="*", type=Path,
        help="C files or directories to search recursively for .c files",
    )
    parser.add_argument(
        "-o", "--output", type=Path, default=Path("mir.json"),
        help="output artifact path (default: mir.json)",
    )
    parser.add_argument("--libclang", type=Path, help="path to the libclang shared library")
    parser.add_argument("--prefix", default="xm", help="toolkit prefix (default: xm)")
    parser.add_argument(
        "--clang-arg", action="append", default=[],
        help="compiler argument; repeat as needed (e.g. --clang-arg=-I/usr/include)",
    )
    return parser, parser.parse_args(argv)


def find_c_files(sources):
    """Yield each C file once, including files under source directories."""
    seen = set()

    def raise_walk_error(error):
        raise error

    for source in sources:
        if source.is_file():
            candidates = [source]
        elif source.is_dir():
            candidates = (
                Path(root) / name
                for root, directories, files in os.walk(source, onerror=raise_walk_error)
                for name in sorted(files)
            )
        else:
            raise OSError(f"source path is not a file or directory: {source}")

        for candidate in candidates:
            if candidate.suffix != ".c" or not candidate.is_file():
                continue
            path = candidate.resolve()
            if path not in seen:
                seen.add(path)
                yield path


def main(argv=None):
    parser, args = parse_args(argv)
    try:
        from clang import cindex
    except ImportError:
        parser.error("libclang Python bindings are required (module: clang.cindex)")

    try:
        if args.libclang is not None:
            cindex.Config.set_library_file(str(args.libclang))
        index = cindex.Index.create()
    except cindex.LibclangError as exc:
        parser.error(f"could not initialize libclang: {exc}")

    # Keep each independent translation unit available for future processing.
    clang_args = list(args.clang_arg)
    # Python wheels can bundle libclang without its standard C headers.
    # Use the installed compiler's resource directory unless explicitly set.
    compiler = shutil.which("clang")
    if compiler and not any(arg.startswith("-resource-dir") for arg in clang_args):
        result = subprocess.run(
            [compiler, "-print-resource-dir"], capture_output=True, text=True,
            check=False,
        )
        resource_dir = Path(result.stdout.strip())
        if result.returncode == 0 and (resource_dir / "include" / "stddef.h").is_file():
            clang_args.extend(["-isystem", str(resource_dir / "include")])
    translation_units = []
    try:
        for source in find_c_files(args.sources):
            unit = index.parse(str(source), args=clang_args)
            translation_units.append(unit)
    except (OSError, cindex.TranslationUnitLoadError) as exc:
        parser.error(f"could not load source: {exc}")

    definitions = {
        cursor.get_usr(): cursor
        for unit in translation_units
        for cursor in unit.cursor.get_children()
        if cursor.kind == cindex.CursorKind.VAR_DECL and cursor.is_definition()
    }
    widgets = {}
    for unit in translation_units:
        source = Path(unit.spelling).resolve()
        for widget_class in find_widget_classes(unit, source, cindex):
            class_name = get_class_name(widget_class, cindex, definitions)
            if class_name is not None:
                widget = {}
                superclass = get_superclass(widget_class, cindex, definitions)
                if superclass is not None:
                    widget["superclass"] = superclass
                widgets[widget_class.spelling] = widget

    artifact = {"prefix": args.prefix, "widgets": widgets}
    try:
        args.output.write_text(json.dumps(artifact, indent=2, sort_keys=True) + "\n",
                               encoding="utf-8")
    except OSError as exc:
        parser.error(f"could not write output: {exc}")
    return 0


def find_widget_classes(unit, source, cindex):
    """Find external WidgetClass variables declared in the source itself."""
    seen = set()
    for cursor in unit.cursor.get_children():
        if (cursor.kind != cindex.CursorKind.VAR_DECL
                or cursor.linkage != cindex.LinkageKind.EXTERNAL
                or cursor.location.file is None
                or Path(cursor.location.file.name).resolve() != source):
            continue

        # Follow typedef aliases without matching arbitrary pointers that
        # merely have the same underlying representation as WidgetClass.
        declaration = cursor.type.get_declaration()
        while declaration.kind == cindex.CursorKind.TYPEDEF_DECL:
            if declaration.spelling == "WidgetClass":
                if cursor.spelling not in seen:
                    seen.add(cursor.spelling)
                    yield cursor
                break
            declaration = declaration.underlying_typedef_type.get_declaration()


def get_core_fields(widget_class, cindex, definitions=None):
    """Resolve the class record's leading core part to field initializers."""
    kind = cindex.CursorKind
    definitions = definitions or {}
    definition = widget_class.get_definition() or definitions.get(widget_class.get_usr())
    if definition is None:
        return None
    initializer = next((child for child in definition.get_children()
                        if child.kind.is_expression()), None)
    if initializer is None:
        return None

    for reference in initializer.walk_preorder():
        if reference.kind != kind.DECL_REF_EXPR:
            continue
        target = reference.referenced
        if target is None or target.kind != kind.VAR_DECL:
            continue
        record = target.get_definition() or definitions.get(target.get_usr())
        if record is None:
            continue
        record_initializer = next((child for child in record.get_children()
                                   if child.kind == kind.INIT_LIST_EXPR), None)
        if record_initializer is None:
            continue
        # WidgetClass points to the leading CoreClassPart of the concrete
        # class record. Motif names that member core_class; Xt uses core.
        # Object and gadget records have layout-compatible leading parts.
        fields = list(record_initializer.type.get_canonical().get_fields())
        values = list(record_initializer.get_children())
        if not fields or not values or fields[0].spelling not in (
                "core", "core_class", "object_class", "rect_class"):
            continue
        core = values[0]
        if core.kind != kind.INIT_LIST_EXPR:
            continue
        core_fields = list(core.type.get_canonical().get_fields())
        return {field.spelling: value
                for field, value in zip(core_fields, core.get_children())}
    return None


def get_class_name(widget_class, cindex, definitions=None):
    fields = get_core_fields(widget_class, cindex, definitions) or {}
    value = fields.get("class_name")
    if value is not None:
        for literal in value.walk_preorder():
            if literal.kind == cindex.CursorKind.STRING_LITERAL:
                try:
                    return ast.literal_eval(literal.spelling)
                except (ValueError, SyntaxError):
                    return None
    return None


def get_superclass(widget_class, cindex, definitions=None):
    """Return the symbol referenced by core.superclass, usually a ClassRec."""
    fields = get_core_fields(widget_class, cindex, definitions) or {}
    value = fields.get("superclass")
    if value is not None:
        for reference in value.walk_preorder():
            if reference.kind == cindex.CursorKind.DECL_REF_EXPR:
                target = reference.referenced
                if target is not None and target.kind == cindex.CursorKind.VAR_DECL:
                    return target.spelling
    return None


if __name__ == "__main__":
    raise SystemExit(main())
