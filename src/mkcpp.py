#!/usr/bin/env python3
"""Skeleton for generating a C++ Motif header from a mir JSON artifact."""

import argparse
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


def generate_accessors(resources):
    """Emit live-widget setters, getters, and callback registration methods."""
    accessors = []
    for resource_name, resource in sorted(resources.items()):
        method_name = resource_method_name(resource_name)
        if resource.get("rtype") == "Callback":
            accessors.append(f"""    Derived& add{method_name}(std::function<void(XtPointer)> callback) {{
                        addCallback({json.dumps(resource_name)}, callback);
                        return static_cast<Derived&>(*this);
                    }}""")
        ctype = resource.get("ctype")
        if ctype is None:
            continue
        parameter, is_array = resource_parameter(ctype)
        accessors.append(f"""    Derived& set{method_name}({parameter}) {{
                XtVaSetValues(this->_widget, {json.dumps(resource_name)}, value, nullptr);
                return static_cast<Derived&>(*this);
            }}""")
        if not is_array:
            accessors.append(f"""{ctype} get{method_name}() {{
                    {parameter};
                    XtVaGetValues(this->_widget, {json.dumps(resource_name)}, &value, nullptr);
                    return value;
                }}""")
    return "\n".join(accessors)


def generate_widget_class(name, symbol, parent, resources):
    superclass = "C" + parent if parent is not None else "CWidget"
    setters = generate_accessors(resources)
    return f"""
            template <typename Derived> class C{name}Impl : public {superclass}Impl<Derived> {{
            public:
                using {superclass}Impl<Derived>::{superclass}Impl;
                using {superclass}Impl<Derived>::addCallback;
                {setters}
            }};
            class C{name} : public C{name}Impl<C{name}> {{
            public:
                using C{name}Impl<C{name}>::C{name}Impl;
                static C{name} create(Widget parent, const char* name) {{
                    return C{name}(XtCreateWidget(name, {symbol}, parent, nullptr, 0));
                }}
            }};
        """


def generate_builder_methods(resources):
    """Emit immutable resource setters, excluding callback resources."""
    setters = []
    for resource_name, resource in sorted(resources.items()):
        ctype = resource.get("ctype")
        if ctype is None or resource.get("rtype") == "Callback":
            continue
        method_name = resource_method_name(resource_name)
        parameter, _ = resource_parameter(ctype)
        setters.append(f"""    auto set{method_name}({parameter}) const {{
        return this->set({json.dumps(resource_name)}, value);
    }}""")
    return "\n".join(setters)


def generate_builder_impl(name, resources):
    builder_methods = generate_builder_methods(resources)
    return f"""
template <template <typename...> class Derived, typename CWidgetClassT,
    typename... Pairs>
class C{name}BuilderImpl :
    public CWidgetBuilderBase<Derived, CWidgetClassT, Pairs...> {{
public:
    C{name}BuilderImpl() = default;
    using CWidgetBuilderBase<Derived,
        CWidgetClassT, Pairs...>::CWidgetBuilderBase;
{builder_methods}
}};

"""


def generate_constructor_builder(name, constructor):
    builder = f"C{constructor_widget_name(constructor)}Builder"
    base = f"C{name}BuilderImpl<{builder}, C{name}, Pairs...>"
    return f"""
template <typename... Pairs>
class {builder} : public {base} {{
public:
    using {base}::C{name}BuilderImpl;

    C{name} build() {{
        auto arglist = this->buildArgList();
        return C{name}({constructor}(this->_parent,
            const_cast<char*>(this->_name), arglist.data(),
            static_cast<Cardinal>(arglist.size())));
    }}
}};

{builder}(Widget, const char*) -> {builder}<>;
template <typename... Pairs>
{builder}(Widget, const char*, std::tuple<Pairs...>) -> {builder}<Pairs...>;
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
    blocks = ["// Generated by mkcpp.py.\n#pragma once\n\n#include <functional>"]
    inherited_resources = {}
    for name in ordered_names:
        widget = widgets[name]
        resources = dict(inherited_resources.get(widget["parent"], {}))
        resources.update(widget["resources"])
        inherited_resources[name] = resources
        blocks.append(generate_widget_class(
            name, widget["symbol"], widget["parent"], widget["resources"]
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
