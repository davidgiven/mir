#!/usr/bin/env python3
"""Skeleton for generating a C++ Motif header from a mir JSON artifact."""

import argparse
from itertools import product
from graphlib import CycleError, TopologicalSorter
import json
import re
from pathlib import Path


def parse_args(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "input",
        nargs="?",
        type=Path,
        default=Path("mir.json"),
        help="input artifact path (default: mir.json)",
    )
    parser.add_argument(
        "-o",
        "--output",
        type=Path,
        default=Path("cmotif_impl.h"),
        help="output C++ header path (default: cmotif_impl.h)",
    )
    return parser, parser.parse_args(argv)


def index_widgets(widgets):
    """Choose one entry per human name and resolve superclass symbols."""
    indexed = {}
    for symbol, widget in sorted(widgets.items()):
        superclass = widgets.get(widget.get("superclass"))
        indexed.setdefault(
            widget["name"],
            {
                "symbol": symbol,
                "parent": superclass["name"] if superclass else None,
                "resources": widget.get("resources", {}),
                "constraints": widget.get("constraints", {}),
                "methods": widget.get("methods", {}),
            },
        )
    return indexed


def index_constructors(widgets):
    """Assign shared-header constructors to their best matching widget."""
    owners = {}
    for _, widget in sorted(widgets.items()):
        name = widget["name"]
        for constructor in widget.get("constructors", {}):
            if (constructor not in owners
                    or constructor_widget_name(constructor) == name):
                owners[constructor] = name
    constructors = {}
    for constructor, owner in owners.items():
        constructors.setdefault(owner, []).append(constructor)
    for name, functions in constructors.items():
        functions.sort(
            key=lambda function: (constructor_widget_name(function) != name, function)
        )
    return constructors


def constructor_widget_name(constructor):
    return constructor.replace("XmCreate", "Xm", 1)


def resource_method_name(name):
    """Convert a resource name into the suffix of a C++ accessor."""
    name = re.sub(r"[^a-zA-Z0-9_]", "_", name)
    return name[:1].upper() + name[1:]


def resource_parameter(ctype):
    """Format a value parameter, placing array dimensions after its name."""
    array = re.fullmatch(r"(.+?)\s*((?:\[[^\]]*\]\s*)+)", ctype)
    parameter = f"{array[1]} value{array[2]}" if array else f"{ctype} value"
    return parameter, bool(array)


def generate_accessors(resources, methods=None):
    """Emit live-widget setters, getters, and callback registration methods."""
    accessors = []
    signatures = {
        (method["name"][:1].lower() + method["name"][1:],
         tuple("".join(parameter["type"].split()) for parameter in method["parameters"]))
        for method in (methods or {}).values()
    }
    for resource_name, resource in sorted(resources.items()):
        method_name = resource_method_name(resource_name)
        if resource.get("rtype") == "Callback":
            payload = resource.get("callback_payload", "void")
            accessors.append(f"""    Derived& add{method_name}(CWidgetImplBase::Callback<{payload}> callback) {{
                        this->template addCallback<{payload}>({json.dumps(resource_name)}, std::move(callback));
                        return static_cast<Derived&>(*this);
                    }}""")
        ctype = resource.get("ctype")
        if ctype is None:
            continue
        parameter, is_array = resource_parameter(ctype)
        if (f"set{method_name}", ("".join(ctype.split()),)) not in signatures:
            accessors.append(f"""    Derived& set{method_name}({parameter}) {{
                XtVaSetValues(this->_widget, {json.dumps(resource_name)}, value, nullptr);
                return static_cast<Derived&>(*this);
            }}""")
        if ctype.strip() in {"XmString", "_XmString"}:
            accessors.append(f"""    Derived& set{method_name}(std::unique_ptr<CXmString> value) {{
                return set{method_name}(static_cast<XmString>(*value));
            }}
            Derived& set{method_name}(const char* value) {{
                return set{method_name}(std::make_unique<CXmString>(value));
            }}""")
        if not is_array and (f"get{method_name}", ()) not in signatures:
            accessors.append(f"""{ctype} get{method_name}() {{
                    {parameter};
                    XtVaGetValues(this->_widget, {json.dumps(resource_name)}, &value, nullptr);
                    return value;
                }}""")
    return "\n".join(accessors)


def method_parameter(ctype, name):
    """Insert a parameter name into a C type spelling."""
    pointer = re.search(r"\(\s*\*\s*\)", ctype)
    if pointer:
        return ctype[:pointer.start()] + f"(*{name})" + ctype[pointer.end():]
    array = re.fullmatch(r"(.+?)\s*((?:\[[^\]]*\]\s*)+)", ctype)
    return f"{array[1]} {name}{array[2]}" if array else f"{ctype} {name}"


def named_method_parameters(method):
    """Keep signature order, assigning collision-free names to unnamed arguments."""
    reserved = {parameter["name"] for parameter in method["parameters"]
                if parameter["name"]}
    parameters = []
    for position, parameter in enumerate(method["parameters"], 1):
        name = parameter["name"]
        if not name:
            name = f"arg{position}"
            while name in reserved:
                name += "_"
        reserved.add(name)
        parameters.append((name, parameter))
    return parameters


def generate_methods(methods):
    """Forward described widget methods to their global C functions."""
    wrappers = []
    for symbol, method in sorted(methods.items()):
        parameters = named_method_parameters(method)
        name = method["name"][:1].lower() + method["name"][1:]
        variants = [
            (parameter["type"], "const char*", "std::unique_ptr<CXmString>")
            if parameter["type"].strip() in {"XmString", "_XmString"}
            else (parameter["type"],)
            for _, parameter in parameters
        ]
        for types in product(*variants):
            declarations = ", ".join(method_parameter(ctype, key)
                                     for (key, _), ctype in zip(parameters, types))
            arguments = ["this->_widget"]
            for (key, parameter), ctype in zip(parameters, types):
                if parameter["type"].strip() in {"XmString", "_XmString"}:
                    if ctype == "const char*":
                        key = f"static_cast<XmString>(CXmString({key}))"
                    elif ctype == "std::unique_ptr<CXmString>":
                        key = f"static_cast<XmString>(*{key})"
                arguments.append(key)
            call = f"{symbol}({', '.join(arguments)})"
            return_type = method["return_type"]
            if return_type.strip() == "void":
                return_type = "Derived&"
                body = f"{call};\n                return static_cast<Derived&>(*this);"
            else:
                body = f"return {call};"
            wrappers.append(f"""    {return_type} {name}({declarations}) {{
                {body}
            }}""")
    return "\n".join(wrappers)


def generate_widget_class(name, symbol, parent, resources, constraints=None, methods=None):
    superclass = "C" + parent if parent is not None else "CWidget"
    setters = generate_accessors(resources, methods)
    method_wrappers = generate_methods(methods or {})
    constraint_setters = generate_builder_methods(constraints or {}).replace("this->set", "widget->set")
    constraints_class = """template <typename Builder>
                class ConstraintsBuilder {
                public:
                    explicit ConstraintsBuilder(Builder& value) : widget(&value) {}
                    template <template <typename...> class Derived,
                        typename CWidgetClassT, typename Parent, typename... Args>
                    explicit ConstraintsBuilder(
                        CWidgetBuilderBase<Derived, CWidgetClassT, Parent, Args...>& value)
                        : widget(static_cast<Builder*>(&value)) {}
                    ConstraintsBuilder& operator=(const ConstraintsBuilder&) { return *this; }
                    CONSTRAINT_SETTERS
                private:
                    Builder* widget;
                };""" if constraints else ""
    constraints_class = constraints_class.replace("CONSTRAINT_SETTERS", constraint_setters)
    return f"""
            template <typename Derived> class C{name}Impl : public {superclass}Impl<Derived> {{
            public:
                using {superclass}Impl<Derived>::{superclass}Impl;
                using {superclass}Impl<Derived>::addCallback;
                {setters}
                {method_wrappers}
            }};
            class C{name} : public C{name}Impl<C{name}> {{
            public:
                using C{name}Impl<C{name}>::C{name}Impl;
                {constraints_class}
            }};
        """


def generate_builder_methods(resources):
    """Emit resource setters that move accumulated arguments, excluding callbacks."""
    setters = []
    for resource_name, resource in sorted(resources.items()):
        ctype = resource.get("ctype")
        if ctype is None or resource.get("rtype") == "Callback":
            continue
        method_name = resource_method_name(resource_name)
        parameter, _ = resource_parameter(ctype)
        setters.append(f"""    auto set{method_name}({parameter}) {{
        return this->set({json.dumps(resource_name)}, value);
    }}""")
        if ctype.strip() in {"XmString", "_XmString"}:
            setters.append(f"""    auto set{method_name}(std::unique_ptr<CXmString> value) {{
        return this->set({json.dumps(resource_name)}, std::move(value));
    }}
    auto set{method_name}(const char* value) {{
        return this->set({json.dumps(resource_name)}, std::make_unique<CXmString>(value));
    }}""")
    return "\n".join(setters)


def generate_builder_impl(name, resources):
    builder_methods = generate_builder_methods(resources)
    return f"""
template <template <typename...> class Derived, typename CWidgetClassT,
    typename Parent, typename... Pairs>
class C{name}BuilderImpl :
    public CWidgetBuilderBase<Derived, CWidgetClassT, Parent, Pairs...> {{
public:
    C{name}BuilderImpl() = default;
    using CWidgetBuilderBase<Derived,
        CWidgetClassT, Parent, Pairs...>::CWidgetBuilderBase;
{builder_methods}
}};

"""


def generate_constructor_builder(name, constructor):
    builder = f"C{constructor_widget_name(constructor)}Builder"
    base = f"C{name}BuilderImpl<{builder}, C{name}, Parent, Pairs...>"
    constraint_base = f"Parent::template ConstraintsBuilder<{builder}<Parent, Pairs...>>"
    return f"""
template <std::derived_from<CWidgetImplBase> Parent, typename... Pairs>
class {builder} : public {base}, public {constraint_base} {{
public:
    template <std::same_as<Parent> ParentT>
    {builder}(const ParentT& parent, const char* name)
        : {base}(parent, name), {constraint_base}(*this) {{}}

    template <std::same_as<Parent> ParentT>
    {builder}(const ParentT& parent, const char* name, std::tuple<Pairs...> pairs)
        : {base}(parent, name, std::move(pairs)),
          {constraint_base}(*this) {{}}

    {builder}(const {builder}& other)
        requires (std::is_copy_constructible_v<Pairs> && ...)
        : {base}(other), {constraint_base}(*this) {{}}

    {builder}({builder}&& other)
        : {base}(std::move(other)), {constraint_base}(*this) {{}}

    {builder}& operator=(const {builder}&)
        requires (std::is_copy_assignable_v<Pairs> && ...) = default;
    {builder}& operator=({builder}&&) = default;

    C{name} build() {{
        auto arglist = this->buildArgList();
        return C{name}({constructor}(this->_parent,
            const_cast<char*>(this->_name), arglist.data(),
            static_cast<Cardinal>(arglist.size())));
    }}
}};

template <std::derived_from<CWidgetImplBase> Parent>
{builder}(const Parent&, const char*) -> {builder}<Parent>;
template <std::derived_from<CWidgetImplBase> Parent, typename... Pairs>
{builder}(const Parent&, const char*, std::tuple<Pairs...>) -> {builder}<Parent, Pairs...>;
"""


def generate_header(artifact):
    """Emit widget classes in inheritance order, then constructor builders."""
    widgets = index_widgets(artifact["widgets"])
    constructors = index_constructors(artifact["widgets"])
    hierarchy = {
        name: (widget["parent"],) if widget["parent"] is not None else ()
        for name, widget in widgets.items()
    }
    ordered_names = list(TopologicalSorter(hierarchy).static_order())
    blocks = ["// Generated by mkcpp.py.\n#pragma once\n\n#include <functional>\n#include <memory>"]
    inherited_resources = {}
    for name in ordered_names:
        widget = widgets[name]
        resources = dict(inherited_resources.get(widget["parent"], {}))
        resources.update(widget["resources"])
        inherited_resources[name] = resources
        blocks.append(generate_widget_class(
            name, widget["symbol"], widget["parent"], widget["resources"],
            widget["constraints"], widget["methods"]
        ))
        blocks.append(generate_builder_impl(name, resources))
    for name in ordered_names:
        blocks.extend(
            generate_constructor_builder(name, constructor)
            for constructor in constructors.get(name, ())
        )
    return "\n\n".join(blocks) + "\n"


def main(argv=None):
    parser, args = parse_args(argv)
    try:
        artifact = json.loads(args.input.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as exc:
        parser.error(f"could not read input: {exc}")

    try:
        args.output.write_text(generate_header(artifact), encoding="utf-8")
    except CycleError as exc:
        parser.error(f"cyclic widget hierarchy: {exc}")
    except OSError as exc:
        parser.error(f"could not write output: {exc}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
