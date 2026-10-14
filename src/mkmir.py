#!/usr/bin/env python3
"""Skeleton for extracting Xt/Motif metadata from source using libclang."""

import argparse
import ast
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

# WidgetClass symbol stems, without the conventional Class/WidgetClass suffix.
WIDGET_CLASS_BLACKLIST = {}


def is_blacklisted_widget(symbol):
    for suffix in ("WidgetClass", "GadgetClass", "ObjectClass", "Class"):
        if symbol.endswith(suffix):
            symbol = symbol[: -len(suffix)]
            break
    return symbol in WIDGET_CLASS_BLACKLIST


def parse_args(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "sources",
        nargs="*",
        type=Path,
        help="C files or directories to search recursively for .c files",
    )
    parser.add_argument(
        "-o",
        "--output",
        type=Path,
        default=Path("mir.json"),
        help="output artifact path (default: mir.json)",
    )
    parser.add_argument(
        "--libclang", type=Path, help="path to the libclang shared library"
    )
    parser.add_argument(
        "--prefix", default="Xm", help="toolkit prefix (default: Xm)"
    )
    parser.add_argument(
        "--clang-arg",
        action="append",
        default=[],
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
                for root, directories, files in os.walk(
                    source, onerror=raise_walk_error
                )
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
        parser.error(
            "libclang Python bindings are required (module: clang.cindex)"
        )

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
    if compiler and not any(
        arg.startswith("-resource-dir") for arg in clang_args
    ):
        result = subprocess.run(
            [compiler, "-print-resource-dir"],
            capture_output=True,
            text=True,
            check=False,
        )
        resource_dir = Path(result.stdout.strip())
        if (
            result.returncode == 0
            and (resource_dir / "include" / "stddef.h").is_file()
        ):
            clang_args.extend(["-isystem", str(resource_dir / "include")])
    translation_units = []
    try:
        sources = list(find_c_files(args.sources))
        for number, source in enumerate(sources, 1):
            print(f"Parsing [{number}/{len(sources)}] {source}",
                  file=sys.stderr, flush=True)
            unit = index.parse(str(source), args=clang_args)
            translation_units.append(unit)
    except (OSError, cindex.TranslationUnitLoadError) as exc:
        parser.error(f"could not load source: {exc}")

    # USRs identify the same declaration across independent translation units.
    definitions = {}
    widget_classes = {}
    for number, unit in enumerate(translation_units, 1):
        print(f"Indexing [{number}/{len(translation_units)}] {unit.spelling}",
              file=sys.stderr, flush=True)
        for cursor in unit.cursor.get_children():
            if (
                cursor.kind != cindex.CursorKind.VAR_DECL
                or not cursor.is_definition()
            ):
                continue
            usr = cursor.get_usr()
            if usr not in definitions or get_initializer(cursor) is not None:
                definitions[usr] = cursor
        source = Path(unit.spelling).resolve()
        for cursor in find_widget_classes(unit, source, cindex):
            if is_blacklisted_widget(cursor.spelling):
                continue
            widget_classes.setdefault(cursor.get_usr(), cursor)

    record_to_widget = {}
    for widget_class in sorted(
        widget_classes.values(), key=lambda cursor: cursor.spelling
    ):
        record = get_class_record(widget_class, cindex, definitions)
        if record is not None:
            record_to_widget.setdefault(record.get_usr(), widget_class.spelling)

    print("Collecting methods and constructors", file=sys.stderr, flush=True)
    methods = find_widget_methods(translation_units, cindex)
    constructors = find_widget_constructors(translation_units, args.prefix, cindex)
    callback_payloads = find_callback_payloads(translation_units, cindex)
    print("Extracting widget resources", file=sys.stderr, flush=True)
    widgets = {}
    callback_fields = {}
    instance_classes = {}
    for widget_class in widget_classes.values():
        class_name = get_class_name(widget_class, cindex, definitions)
        if class_name is not None:
            widget = {"name": class_name}
            record = get_class_record(widget_class, cindex, definitions)
            if (
                record is not None
                and record.is_definition()
                and get_initializer(record) is not None
                and record.location.file is not None
            ):
                widget["sourcefile"] = str(
                    Path(record.location.file.name).resolve()
                )
                header = get_header_path(widget["sourcefile"], args.prefix)
                if header is not None:
                    widget["header"] = header
            superclass = get_superclass(
                widget_class, cindex, definitions, record_to_widget
            )
            if superclass is not None:
                widget["superclass"] = superclass
            widgets[widget_class.spelling] = widget
            instance_classes[widget_class.spelling] = set()
            callback_fields[widget_class.spelling] = {}
            widget["resources"] = dict(
                get_resources(widget_class, cindex, definitions, callback_payloads,
                              callback_fields[widget_class.spelling],
                              instance_classes[widget_class.spelling])
            )
            constraint_fields = get_class_part_fields(
                widget_class, ("constraint_part", "constraint_class", "constraint"),
                cindex, definitions,
            )
            if constraint_fields is not None:
                widget["constraints"] = dict(get_resource_entries(
                    widget_class,
                    constraint_fields.get("constraints", constraint_fields.get("resources")),
                    cindex, definitions, callback_payloads,
                ))
            widget["methods"] = {
                name: {
                    "name": name[len(class_name) :],
                    "return_type": method.result_type.spelling,
                    "parameters": get_method_parameters(method),
                }
                for name, method in methods.items()
                if name.startswith(class_name)
            }
    # A subclass may use an inherited callback field with its own payload.
    for symbol, widget in widgets.items():
        ancestor = widget.get("superclass")
        seen = {symbol}
        inherited = {}
        ancestor_instances = set()
        while ancestor in widgets and ancestor not in seen:
            seen.add(ancestor)
            ancestor_instances.update(instance_classes[ancestor])
            for name, field in callback_fields[ancestor].items():
                inherited.setdefault(name, (field, widgets[ancestor]["resources"][name]))
            ancestor = widgets[ancestor].get("superclass")
        for name, (field, resource) in inherited.items():
            if name in widget["resources"]:
                continue
            payloads = {}
            for instance in instance_classes[symbol] - ancestor_instances:
                for payload, sites in callback_payloads.get((instance, field), {}).items():
                    payloads.setdefault(payload, set()).update(sites)
            if not payloads:
                continue
            override = dict(resource)
            override.pop("callback_payload", None)
            set_callback_payload(override, payloads,
                                 f"{widget.get('sourcefile', symbol)}: warning: {symbol}.{name}")
            widget["resources"][name] = override

    # Some source files include only private headers; read their public header
    # independently when it has not contributed any constructor declarations.
    parsed_headers = set()
    for widget in widgets.values():
        header = widget.get("header")
        if header is None or header in constructors or header in parsed_headers:
            continue
        parsed_headers.add(header)
        path = Path(widget["sourcefile"]).with_suffix(".h")
        if not path.is_file():
            continue
        try:
            print(f"Parsing public header {path}", file=sys.stderr, flush=True)
            unit = index.parse(str(path), args=["-x", "c", *clang_args])
        except cindex.TranslationUnitLoadError as exc:
            parser.error(f"could not load public header: {exc}")
        translation_units.append(unit)
        header_functions = find_widget_constructors([unit], args.prefix, cindex)
        for key, functions in header_functions.items():
            constructors.setdefault(key, {}).update(functions)

    for widget in widgets.values():
        widget["constructors"] = {
            name: {"name": name.removeprefix("XmCreate")}
            for name in constructors.get(widget.get("header"), {})
        }

    print("Collecting API types", file=sys.stderr, flush=True)
    artifact = {
        "prefix": args.prefix,
        "widgets": widgets,
        "types": get_api_types(widgets, translation_units, cindex),
    }
    try:
        args.output.write_text(
            json.dumps(artifact, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )
    except OSError as exc:
        parser.error(f"could not write output: {exc}")
    print(f"Wrote {len(widgets)} widgets to {args.output}",
          file=sys.stderr, flush=True)
    return 0


def get_header_path(sourcefile, prefix):
    """Keep the source path from the toolkit directory and change its suffix."""
    parts = Path(sourcefile).parts
    for index, part in enumerate(parts[:-1]):
        if part.casefold() == prefix.casefold():
            return Path(*parts[index:]).with_suffix(".h").as_posix()
    return None


def get_api_types(widgets, translation_units, cindex):
    """Describe enums and integral or pointer typedefs referenced by the API."""

    def type_key(spelling):
        # sizeof tokens have different spacing from libclang type spellings.
        return "".join(
            token
            for token in spelling.split()
            if token not in ("const", "volatile", "restrict")
        )

    referenced = set()
    for widget in widgets.values():
        if widget.get("constructors"):
            referenced.update(("Widget", "char *", "ArgList", "Cardinal"))
        for resources in (widget["resources"], widget.get("constraints", {})):
            for resource in resources.values():
                if "ctype" in resource:
                    referenced.add(resource["ctype"])
        for method in widget["methods"].values():
            referenced.add(method["return_type"])
            referenced.update(
                parameter["type"] for parameter in method["parameters"].values()
            )

    enum_types = {}
    integer_types = {}
    pointer_types = {}
    integral_kinds = {
        getattr(cindex.TypeKind, name)
        for name in (
            "BOOL",
            "CHAR_U",
            "CHAR_S",
            "UCHAR",
            "SCHAR",
            "WCHAR",
            "CHAR16",
            "CHAR32",
            "USHORT",
            "SHORT",
            "UINT",
            "INT",
            "ULONG",
            "LONG",
            "ULONGLONG",
            "LONGLONG",
            "UINT128",
            "INT128",
        )
    }
    enum_fields = {}
    anonymous_enums = set()
    named_enums = set()
    for unit in translation_units:
        pending = list(unit.cursor.get_children())
        while pending:
            cursor = pending.pop()
            if cursor.kind in (
                cindex.CursorKind.STRUCT_DECL,
                cindex.CursorKind.UNION_DECL,
            ):
                pending.extend(cursor.get_children())
            if cursor.kind not in (
                cindex.CursorKind.ENUM_DECL,
                cindex.CursorKind.TYPEDEF_DECL,
            ):
                continue
            canonical = cursor.type.get_canonical()
            if (
                cursor.kind == cindex.CursorKind.TYPEDEF_DECL
                and canonical.kind in integral_kinds
            ):
                integer_types[type_key(cursor.type.spelling)] = (
                    canonical.spelling
                )
            if (
                cursor.kind == cindex.CursorKind.TYPEDEF_DECL
                and canonical.kind == cindex.TypeKind.POINTER
                and canonical.get_pointee().kind
                not in (
                    cindex.TypeKind.FUNCTIONPROTO,
                    cindex.TypeKind.FUNCTIONNOPROTO,
                )
            ):
                underlying = cursor.underlying_typedef_type
                # Resolve aliases to the pointer, preserving typedefs in its
                # pointee (e.g. XmStringTable points to XmString).
                while underlying.kind != cindex.TypeKind.POINTER:
                    alias = underlying.get_declaration()
                    if alias.kind != cindex.CursorKind.TYPEDEF_DECL:
                        underlying = canonical
                        break
                    underlying = alias.underlying_typedef_type
                pointer_types[type_key(cursor.type.spelling)] = (
                    underlying.get_pointee().spelling
                )
            if canonical.kind == cindex.TypeKind.ENUM:
                declaration = canonical.get_declaration()
                declaration = declaration.get_definition() or declaration
                usr = declaration.get_usr()
                enum_types[type_key(cursor.type.spelling)] = usr
                if (
                    cursor.kind == cindex.CursorKind.TYPEDEF_DECL
                    or not declaration.is_anonymous()
                ):
                    named_enums.add(usr)
                else:
                    anonymous_enums.add(usr)
                if declaration.is_definition():
                    enum_fields[usr] = {
                        field.spelling: field.enum_value
                        for field in declaration.get_children()
                        if field.kind == cindex.CursorKind.ENUM_CONSTANT_DECL
                    }

    types = {
        spelling: {
            "type": "enum",
            "fields": enum_fields.get(enum_types[type_key(spelling)], {}),
        }
        for spelling in sorted(referenced)
        if type_key(spelling) in enum_types
    }
    for spelling in sorted(referenced):
        if spelling not in types and type_key(spelling) in integer_types:
            types[spelling] = {
                "type": "integer",
                "ctype": integer_types[type_key(spelling)],
            }
        elif spelling not in types and type_key(spelling) in pointer_types:
            types[spelling] = {
                "type": "pointer",
                "ctype": pointer_types[type_key(spelling)],
            }
    types["XtEnum"] = {
        "type": "enum",
        "fields": {
            name: value
            for usr in sorted(anonymous_enums - named_enums)
            for name, value in enum_fields.get(usr, {}).items()
            if name.startswith(("Xm", "Xt"))
        },
    }
    return types


def find_widget_classes(unit, source, cindex):
    """Find external WidgetClass variables declared in the source itself."""
    seen = set()
    for cursor in unit.cursor.get_children():
        if (
            cursor.kind != cindex.CursorKind.VAR_DECL
            or cursor.linkage != cindex.LinkageKind.EXTERNAL
            or cursor.location.file is None
            or Path(cursor.location.file.name).resolve() != source
        ):
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
    return next(
        (
            child
            for child in cursor.get_children()
            if child.kind.is_expression()
        ),
        None,
    )


def find_widget_methods(translation_units, cindex):
    """Index global functions taking Widget as their first parameter."""
    methods = {}
    for unit in translation_units:
        for cursor in unit.cursor.get_children():
            if (
                cursor.kind != cindex.CursorKind.FUNCTION_DECL
                or cursor.linkage != cindex.LinkageKind.EXTERNAL
            ):
                continue
            argument = next(cursor.get_arguments(), None)
            if argument is None:
                continue
            declaration = argument.type.get_declaration()
            while declaration.kind == cindex.CursorKind.TYPEDEF_DECL:
                if declaration.spelling == "Widget":
                    previous = methods.get(cursor.spelling)
                    if (
                        previous is None
                        or cursor.is_definition()
                        or (
                            not previous.is_definition()
                            and sum(
                                bool(arg.spelling)
                                for arg in cursor.get_arguments()
                            )
                            > sum(
                                bool(arg.spelling)
                                for arg in previous.get_arguments()
                            )
                        )
                    ):
                        methods[cursor.spelling] = cursor
                    break
                declaration = (
                    declaration.underlying_typedef_type.get_declaration()
                )
    return dict(sorted(methods.items()))


def find_widget_constructors(translation_units, prefix, cindex):
    """Index public XmCreate declarations with the standard Xt signature."""
    constructors = {}
    for unit in translation_units:
        signature_types = {
            cursor.spelling: cursor.type.get_canonical()
            for cursor in unit.cursor.get_children()
            if cursor.kind == cindex.CursorKind.TYPEDEF_DECL
            and cursor.spelling in ("Widget", "ArgList", "Cardinal")
        }
        if len(signature_types) != 3:
            continue
        for cursor in unit.cursor.get_children():
            if (cursor.kind != cindex.CursorKind.FUNCTION_DECL
                    or cursor.linkage != cindex.LinkageKind.EXTERNAL
                    or not cursor.spelling.startswith("XmCreate")
                    or cursor.location.file is None):
                continue
            arguments = list(cursor.get_arguments())
            if (len(arguments) != 4 or cursor.type.is_function_variadic()
                    or cursor.result_type.get_canonical() != signature_types["Widget"]
                    or arguments[0].type.get_canonical() != signature_types["Widget"]
                    or arguments[1].type.get_canonical().spelling != "char *"
                    or arguments[2].type.get_canonical() != signature_types["ArgList"]
                    or arguments[3].type.get_canonical() != signature_types["Cardinal"]):
                continue
            path = Path(cursor.location.file.name)
            if path.suffix != ".h":
                continue
            header = get_header_path(path, prefix)
            if header is None:
                continue
            functions = constructors.setdefault(header, {})
            previous = functions.get(cursor.spelling)
            if (previous is None
                    or sum(bool(arg.spelling) for arg in cursor.get_arguments())
                    > sum(bool(arg.spelling) for arg in previous.get_arguments())):
                functions[cursor.spelling] = cursor
    return {header: dict(sorted(functions.items()))
            for header, functions in constructors.items()}


def get_method_parameters(method):
    """Describe signature parameters after the leading Widget argument."""
    arguments = list(method.get_arguments())[1:]
    reserved = {
        argument.spelling for argument in arguments if argument.spelling
    }
    parameters = {}
    for position, argument in enumerate(arguments, start=1):
        key = argument.spelling or f"arg{position}"
        if not argument.spelling:
            while key in reserved or key in parameters:
                key += "_"
        parameters[key] = {
            "name": argument.spelling,
            "type": argument.type.spelling,
        }
    return parameters


def get_class_record(widget_class, cindex, definitions=None):
    """Resolve a WidgetClass pointer to its ClassRec across loaded TUs."""
    kind = cindex.CursorKind
    definitions = definitions or {}
    definition = (
        definitions.get(widget_class.get_usr()) or widget_class.get_definition()
    )
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
        return (
            definitions.get(target.get_usr())
            or target.get_definition()
            or target
        )
    return None


def get_class_part_fields(widget_class, part_names, cindex, definitions=None):
    """Resolve a named class-record part to its field initializers."""
    record = get_class_record(widget_class, cindex, definitions)
    if record is None:
        return None
    initializer = get_initializer(record)
    if initializer is None or initializer.kind != cindex.CursorKind.INIT_LIST_EXPR:
        return None
    for field, value in zip(initializer.type.get_canonical().get_fields(),
                            initializer.get_children()):
        if field.spelling not in part_names:
            continue
        if value.kind != cindex.CursorKind.INIT_LIST_EXPR:
            return None
        return {member.spelling: expression
                for member, expression in zip(value.type.get_canonical().get_fields(),
                                               value.get_children())}
    return None


def get_core_fields(widget_class, cindex, definitions=None):
    """Resolve the class record's leading core part to field initializers."""
    kind = cindex.CursorKind
    record = get_class_record(widget_class, cindex, definitions)
    if record is not None:
        record_initializer = next(
            (
                child
                for child in record.get_children()
                if child.kind == kind.INIT_LIST_EXPR
            ),
            None,
        )
        if record_initializer is None:
            return None
        # WidgetClass points to the leading CoreClassPart of the concrete
        # class record. Motif names that member core_class; Xt uses core.
        # Object and gadget records have layout-compatible leading parts.
        fields = list(record_initializer.type.get_canonical().get_fields())
        values = list(record_initializer.get_children())
        if (
            not fields
            or not values
            or fields[0].spelling
            not in ("core", "core_class", "object_class", "rect_class")
        ):
            return None
        core = values[0]
        if core.kind != kind.INIT_LIST_EXPR:
            return None
        core_fields = list(core.type.get_canonical().get_fields())
        return {
            field.spelling: value
            for field, value in zip(core_fields, core.get_children())
        }
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


def get_superclass(
    widget_class, cindex, definitions=None, record_to_widget=None
):
    """Map core.superclass's ClassRec reference to its WidgetClass symbol."""
    fields = get_core_fields(widget_class, cindex, definitions) or {}
    value = fields.get("superclass")
    if value is not None:
        for reference in value.walk_preorder():
            if reference.kind == cindex.CursorKind.DECL_REF_EXPR:
                target = reference.referenced
                if (
                    target is not None
                    and target.kind == cindex.CursorKind.VAR_DECL
                ):
                    return (record_to_widget or {}).get(target.get_usr())
    return None


def find_call_expressions(unit, cindex):
    """Let libclang recurse, retaining only call cursors for Python analysis."""
    calls = []
    call_kind = cindex.CursorKind.CALL_EXPR.value

    def visit(cursor, parent, data):
        # Read the CXCursor kind directly to avoid a binding call and enum
        # conversion for every AST node. Only retained cursors need a TU owner.
        if cursor._kind_id == call_kind:
            cursor._tu = unit
            calls.append(cursor)
        return 2  # CXChildVisit_Recurse, including calls nested in arguments.

    # Use the binding's registered callback type (its name varies by version).
    visit_children = cindex.conf.lib.clang_visitChildren
    visitor = visit_children.argtypes[1](visit)
    visit_children(unit.cursor, visitor, None)
    return calls


def find_callback_payloads(translation_units, cindex):
    """Index payloads by the receiver record and callback-list field USRs."""
    payloads = {}
    for number, unit in enumerate(translation_units, 1):
        print(f"Scanning callbacks [{number}/{len(translation_units)}] {unit.spelling}",
              file=sys.stderr, flush=True)
        for call in find_call_expressions(unit, cindex):
            function = call.referenced
            if function is None or function.spelling != "XtCallCallbackList":
                continue
            arguments = list(call.get_arguments())
            if len(arguments) != 3:
                continue
            field = get_offset_field(arguments[-2], cindex)
            if field is None:
                continue
            receiver = get_callback_receiver_usr(arguments[0], arguments[1], cindex)
            if receiver is None:
                continue
            payload = arguments[-1]
            # Strip the implicit conversion to XtPointer, and explicit void*
            # casts, to recover the type supplied by the caller.
            while True:
                children = [child for child in payload.get_children()
                            if child.kind.is_expression()]
                canonical = payload.type.get_canonical()
                void_pointer = (
                    canonical.kind == cindex.TypeKind.POINTER
                    and canonical.get_pointee().kind == cindex.TypeKind.VOID
                )
                if len(children) != 1 or not (
                    payload.kind == cindex.CursorKind.UNEXPOSED_EXPR
                    or (payload.kind == cindex.CursorKind.CSTYLE_CAST_EXPR
                        and void_pointer)
                ):
                    break
                payload = children[0]
            # Null pointers and opaque void pointers provide no payload type.
            canonical = payload.type.get_canonical()
            if (canonical.kind != cindex.TypeKind.POINTER
                    or canonical.get_pointee().kind == cindex.TypeKind.VOID):
                continue
            pointee = payload.type.get_pointee()
            if pointee.kind == cindex.TypeKind.INVALID:
                pointee = canonical.get_pointee()
            location = call.location
            site = f"{location.file}:{location.line}:{location.column}"
            payloads.setdefault((receiver, field.get_usr()), {}).setdefault(
                pointee.spelling, set()
            ).add(site)
    return payloads


def get_record_reference_usr(expression, cindex):
    for cursor in expression.walk_preorder():
        if cursor.kind == cindex.CursorKind.TYPE_REF and cursor.referenced is not None:
            record = cursor.referenced.type.get_canonical()
            if record.kind == cindex.TypeKind.RECORD:
                return record.get_declaration().get_usr()
    return None


def get_callback_receiver_usr(receiver, callbacks, cindex):
    # Peel the API's conversion to Widget, retaining a concrete widget cast.
    while True:
        canonical = receiver.type.get_canonical()
        if canonical.kind == cindex.TypeKind.POINTER:
            pointee = canonical.get_pointee()
            if (pointee.kind == cindex.TypeKind.RECORD
                    and pointee.get_declaration().spelling != "_WidgetRec"):
                return pointee.get_declaration().get_usr()
        children = [c for c in receiver.get_children() if c.kind.is_expression()]
        if receiver.kind not in (cindex.CursorKind.UNEXPOSED_EXPR,
                                 cindex.CursorKind.CSTYLE_CAST_EXPR,
                                 cindex.CursorKind.PAREN_EXPR) or len(children) != 1:
            break
        receiver = children[0]
    # A generic Widget argument can still have a typed callback-list receiver.
    for cursor in callbacks.walk_preorder():
        if cursor.kind == cindex.CursorKind.MEMBER_REF_EXPR:
            for child in cursor.get_children():
                canonical = child.type.get_canonical()
                if canonical.kind == cindex.TypeKind.POINTER:
                    pointee = canonical.get_pointee()
                    if pointee.kind == cindex.TypeKind.RECORD:
                        return pointee.get_declaration().get_usr()
    return None


def set_callback_payload(resource, payloads, context):
    if len(payloads) == 1:
        resource["callback_payload"] = next(iter(payloads))
        return
    if payloads:
        details = "; ".join(
            f"{payload!r} at {', '.join(sorted(sites))}"
            for payload, sites in sorted(payloads.items())
        )
        message = f"conflicting callback payload types: {details}"
    else:
        message = "could not determine callback payload type"
    print(f"{context}: {message}", file=sys.stderr)


def get_resources(widget_class, cindex, definitions=None, callback_payloads=None,
                  callback_fields=None, instance_types=None):
    """Read names, declared resource types, and C types from core.resources."""
    fields = get_core_fields(widget_class, cindex, definitions) or {}
    yield from get_resource_entries(
        widget_class, fields.get("resources"), cindex, definitions, callback_payloads,
        callback_fields, instance_types,
    )


def get_resource_entries(widget_class, resources, cindex, definitions=None,
                         callback_payloads=None, callback_fields=None,
                         instance_types=None):
    """Read an XtResource array referenced by a class-record initializer."""
    definitions = definitions or {}
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
        if (
            initializer is None
            or initializer.kind != cindex.CursorKind.INIT_LIST_EXPR
        ):
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
                    resource = {"set": True, "get": True}
                    for index, field in ((1, "class"), (2, "rtype")):
                        if len(values) <= index:
                            continue
                        for field_literal in values[index].walk_preorder():
                            if (
                                field_literal.kind
                                == cindex.CursorKind.STRING_LITERAL
                            ):
                                try:
                                    resource[field] = ast.literal_eval(
                                        field_literal.spelling
                                    )
                                except (ValueError, SyntaxError):
                                    pass
                                break
                    sizeof_type = (
                        get_sizeof_type(values[3], cindex)
                        if len(values) > 3
                        else None
                    )
                    field_type = (
                        get_offset_field_type(values[4], cindex)
                        if len(values) > 4
                        else None
                    )
                    resource_type = (
                        field_type if field_type is not None else sizeof_type
                    )
                    instance = (get_record_reference_usr(values[4], cindex)
                                if len(values) > 4 else None)
                    if instance_types is not None and instance is not None:
                        instance_types.add(instance)
                    if resource_type is not None:
                        resource["ctype"] = resource_type
                    if resource.get("rtype") == "Callback":
                        field = (get_offset_field(values[4], cindex)
                                 if len(values) > 4 else None)
                        field_usr = field.get_usr() if field is not None else None
                        if callback_fields is not None and field_usr is not None:
                            callback_fields[name] = field_usr
                        payloads = (callback_payloads or {}).get((instance, field_usr), {})
                        set_callback_payload(resource, payloads,
                            f"{entry.location}: warning: {widget_class.spelling}.{name}")
                    yield name, resource
                    break
        return


def get_offset_field(expression, cindex):
    """Read the final field in offsetof or a nested member expression."""
    offset_field = None
    expression_field = None
    for cursor in expression.walk_preorder():
        if cursor.kind not in (
            cindex.CursorKind.MEMBER_REF,
            cindex.CursorKind.MEMBER_REF_EXPR,
        ):
            continue
        field = cursor.referenced
        if field is not None and field.kind == cindex.CursorKind.FIELD_DECL:
            if cursor.kind == cindex.CursorKind.MEMBER_REF:
                offset_field = field
            elif expression_field is None:
                # Member expressions nest from the final field inward.
                expression_field = field
    return offset_field if offset_field is not None else expression_field


def get_offset_field_type(expression, cindex):
    """Read the declared type of the final offset field."""
    field = get_offset_field(expression, cindex)
    return field.type.spelling if field is not None else None


def get_sizeof_type(expression, cindex):
    """Extract the written C type, preserving typedef names and pointers."""
    for cursor in expression.walk_preorder():
        if cursor.kind != cindex.CursorKind.CXX_UNARY_EXPR:
            continue
        tokens = [
            token.spelling
            for token in cursor.get_tokens()
            if token.kind != cindex.TokenKind.COMMENT
        ]
        if (
            len(tokens) < 4
            or tokens[:2] != ["sizeof", "("]
            or tokens[-1] != ")"
        ):
            continue
        # sizeof(expression) has an expression child; sizeof(TYPE) does not.
        if any(child.kind.is_expression() for child in cursor.get_children()):
            continue
        return " ".join(tokens[2:-1])
    return None


if __name__ == "__main__":
    raise SystemExit(main())
