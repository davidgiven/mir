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
    # Expose resource-name macros as literals instead of string-table offsets.
    clang_args = ["-DXMSTRINGDEFINES", "-DXTSTRINGDEFINES", *args.clang_arg]
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

    # USRs identify the same declaration across independent translation units.
    definitions = {}
    widget_classes = {}
    for unit in translation_units:
        for cursor in unit.cursor.get_children():
            if cursor.kind != cindex.CursorKind.VAR_DECL or not cursor.is_definition():
                continue
            usr = cursor.get_usr()
            if usr not in definitions or get_initializer(cursor) is not None:
                definitions[usr] = cursor
        source = Path(unit.spelling).resolve()
        for cursor in find_widget_classes(unit, source, cindex):
            widget_classes.setdefault(cursor.get_usr(), cursor)

    record_to_widget = {}
    for widget_class in sorted(widget_classes.values(), key=lambda cursor: cursor.spelling):
        record = get_class_record(widget_class, cindex, definitions)
        if record is not None:
            record_to_widget.setdefault(record.get_usr(), widget_class.spelling)

    widgets = {}
    for widget_class in widget_classes.values():
        class_name = get_class_name(widget_class, cindex, definitions)
        if class_name is not None:
            widget = {}
            superclass = get_superclass(widget_class, cindex, definitions, record_to_widget)
            if superclass is not None:
                widget["superclass"] = superclass
            widgets[widget_class.spelling] = widget
            widget["resources"] = dict(get_resources(widget_class, cindex, definitions))

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


def get_initializer(cursor):
    return next((child for child in cursor.get_children()
                 if child.kind.is_expression()), None)


def get_class_record(widget_class, cindex, definitions=None):
    """Resolve a WidgetClass pointer to its ClassRec across loaded TUs."""
    kind = cindex.CursorKind
    definitions = definitions or {}
    definition = definitions.get(widget_class.get_usr()) or widget_class.get_definition()
    if definition is None:
        return None
    initializer = get_initializer(definition)
    if initializer is None:
        return None

    for reference in initializer.walk_preorder():
        if reference.kind != kind.DECL_REF_EXPR:
            continue
        target = reference.referenced
        if target is None or target.kind != kind.VAR_DECL:
            continue
        if target.type.get_canonical().kind != cindex.TypeKind.RECORD:
            continue
        return definitions.get(target.get_usr()) or target.get_definition() or target
    return None


def get_core_fields(widget_class, cindex, definitions=None):
    """Resolve the class record's leading core part to field initializers."""
    kind = cindex.CursorKind
    record = get_class_record(widget_class, cindex, definitions)
    if record is not None:
        record_initializer = next((child for child in record.get_children()
                                   if child.kind == kind.INIT_LIST_EXPR), None)
        if record_initializer is None:
            return None
        # WidgetClass points to the leading CoreClassPart of the concrete
        # class record. Motif names that member core_class; Xt uses core.
        # Object and gadget records have layout-compatible leading parts.
        fields = list(record_initializer.type.get_canonical().get_fields())
        values = list(record_initializer.get_children())
        if not fields or not values or fields[0].spelling not in (
                "core", "core_class", "object_class", "rect_class"):
            return None
        core = values[0]
        if core.kind != kind.INIT_LIST_EXPR:
            return None
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


def get_superclass(widget_class, cindex, definitions=None, record_to_widget=None):
    """Map core.superclass's ClassRec reference to its WidgetClass symbol."""
    fields = get_core_fields(widget_class, cindex, definitions) or {}
    value = fields.get("superclass")
    if value is not None:
        for reference in value.walk_preorder():
            if reference.kind == cindex.CursorKind.DECL_REF_EXPR:
                target = reference.referenced
                if target is not None and target.kind == cindex.CursorKind.VAR_DECL:
                    return (record_to_widget or {}).get(target.get_usr())
    return None


def get_resources(widget_class, cindex, definitions=None):
    """Read names, declared resource types, and C types from core.resources."""
    definitions = definitions or {}
    fields = get_core_fields(widget_class, cindex, definitions) or {}
    resources = fields.get("resources")
    if resources is None:
        return
    for reference in resources.walk_preorder():
        if reference.kind != cindex.CursorKind.DECL_REF_EXPR:
            continue
        target = reference.referenced
        if target is None or target.kind != cindex.CursorKind.VAR_DECL:
            continue
        array = definitions.get(target.get_usr()) or target.get_definition()
        if array is None:
            continue
        initializer = get_initializer(array)
        if initializer is None or initializer.kind != cindex.CursorKind.INIT_LIST_EXPR:
            continue
        for entry in initializer.get_children():
            if entry.kind != cindex.CursorKind.INIT_LIST_EXPR:
                continue
            values = list(entry.get_children())
            if not values:
                continue
            for literal in values[0].walk_preorder():
                if literal.kind == cindex.CursorKind.STRING_LITERAL:
                    try:
                        name = ast.literal_eval(literal.spelling)
                    except (ValueError, SyntaxError):
                        break
                    resource = {}
                    if len(values) > 2:
                        for type_literal in values[2].walk_preorder():
                            if type_literal.kind == cindex.CursorKind.STRING_LITERAL:
                                try:
                                    resource["rtype"] = ast.literal_eval(type_literal.spelling)
                                except (ValueError, SyntaxError):
                                    pass
                                break
                    if len(values) > 3:
                        resource_type = get_sizeof_type(values[3], cindex)
                        if resource_type is not None:
                            resource["ctype"] = resource_type
                    yield name, resource
                    break
        return


def get_sizeof_type(expression, cindex):
    """Extract the written C type, preserving typedef names and pointers."""
    for cursor in expression.walk_preorder():
        if cursor.kind != cindex.CursorKind.CXX_UNARY_EXPR:
            continue
        tokens = [token.spelling for token in cursor.get_tokens()
                  if token.kind != cindex.TokenKind.COMMENT]
        if len(tokens) < 4 or tokens[:2] != ["sizeof", "("] or tokens[-1] != ")":
            continue
        # sizeof(expression) has an expression child; sizeof(TYPE) does not.
        if any(child.kind.is_expression() for child in cursor.get_children()):
            continue
        return " ".join(tokens[2:-1])
    return None


if __name__ == "__main__":
    raise SystemExit(main())
