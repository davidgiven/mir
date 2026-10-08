/* mir -- Motif introspection / reflection. */

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <getopt.h>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

#include "clang.h"

#include <X11/Intrinsic.h>
#include <X11/IntrinsicP.h>
#include <Xm/Xm.h>

#include <dlfcn.h>

#include <nlohmann/json.hpp>

namespace fs = std::filesystem;

/* Collect all header files under the given directories. */
static std::vector<fs::path> collectHeaders(
    const std::vector<std::string>& headerDirs)
{
    std::vector<fs::path> headers;

    for (const auto& dirStr : headerDirs)
    {
        fs::path dir(dirStr);
        if (!fs::exists(dir))
        {
            continue;
        }
        if (fs::is_regular_file(dir))
        {
            headers.push_back(dir);
            continue;
        }
        if (!fs::is_directory(dir))
        {
            continue;
        }
        bool isX11 = (dir.filename() == "X11");
        for (auto& entry : fs::recursive_directory_iterator(dir))
        {
            if (!entry.is_regular_file())
            {
                continue;
            }
            auto ext = entry.path().extension().string();
            if (ext != ".h" && ext != ".H")
            {
                continue;
            }
            if (isX11)
            {
                /* For the system Xt headers, only consider top-level public
                 * headers. Private headers (*P.h, *I.h) and internal headers
                 * (Xwindows.h etc.) as well as subdirectories (Xaw,
                 * extensions, dri, …) are not part of the public widget API
                 * and would fail full C99 parsing without additional
                 * prerequisites. Skipping them keeps strict parsing
                 * successful. */
                if (entry.path().parent_path() != dir)
                {
                    continue;
                }
                std::string fname = entry.path().filename().string();
                if (fname.size() >= 3 &&
                    (fname.substr(fname.size() - 3) == "P.h" ||
                        fname.substr(fname.size() - 3) == "I.h"))
                {
                    continue;
                }
                static const std::unordered_set<std::string> skip = {
                    "Xwindows.h",
                    "Xwinsock.h",
                    "PassivGraI.h",
                    "TranslateI.h",
                    "InitialI.h",
                };
                if (skip.find(fname) != skip.end())
                {
                    continue;
                }
            }
            headers.push_back(entry.path());
        }
    }

    std::sort(headers.begin(), headers.end());

    return headers;
}

/* Compute the human-readable widget name by stripping the toolkit
 * prefix from the front and "WidgetClass" from the end, when present. */
static std::string computeHumanName(
    const std::string& name, const std::string& prefix)
{
    std::string result = name;
    if (!prefix.empty() && result.rfind(prefix, 0) == 0)
    {
        result = result.substr(prefix.size());
    }
    const std::string suffix = "WidgetClass";
    if (result.size() >= suffix.size() &&
        result.compare(result.size() - suffix.size(), suffix.size(), suffix) ==
            0)
    {
        result = result.substr(0, result.size() - suffix.size());
    }
    return result;
}

/* Case-insensitive prefix check: does `s` start with `prefix` ignoring
 * ASCII case? */
static bool startsWithIgnoreCase(
    const std::string& s, const std::string& prefix)
{
    if (prefix.size() > s.size())
    {
        return false;
    }
    for (size_t i = 0; i < prefix.size(); ++i)
    {
        unsigned char a = static_cast<unsigned char>(
            std::tolower(static_cast<unsigned char>(s[i])));
        unsigned char b = static_cast<unsigned char>(
            std::tolower(static_cast<unsigned char>(prefix[i])));
        if (a != b)
        {
            return false;
        }
    }
    return true;
}

/* Filter `functions` to only those starting with `prefix + humanName`
 * (case-insensitive). `prefix` and `humanName` are concatenated as-is and
 * then compared ignoring case. For each match, strip the prefix and widget
 * name to produce the human-readable method name. Returns a sorted map from
 * full method symbol (e.g. XmCascadeButtonHighlight) to an object containing
 * the human name (e.g. Highlight). */
static std::map<std::string, nlohmann::json> filterMethods(
    const std::vector<std::string>& functions,
    const std::string& prefix,
    const std::string& humanName)
{
    std::string wanted = prefix + humanName;
    std::map<std::string, nlohmann::json> result;
    for (auto& fn : functions)
    {
        if (startsWithIgnoreCase(fn, wanted))
        {
            std::string methodName = fn.substr(wanted.size());
            if (methodName.empty())
            {
                continue;
            }
            nlohmann::json obj = nlohmann::json::object();
            obj["name"] = methodName;
            result.emplace(fn, std::move(obj));
        }
    }
    return result;
}

/* Build a map from WidgetClass pointer value to its classSymbol string.
 * This is used to resolve superclass pointers back to their symbolic names.
 * The map is seeded from the WidgetClass symbols discovered by parsing the
 * headers the tool was shown (e.g. /usr/include/X11 for Xt Intrinsics). No
 * hard-coded fallback list is used; callers must pass the relevant header
 * directories so the real definitions are visible. */
static std::map<WidgetClass, std::string> buildClassPointerMap(
    const std::map<std::string, std::string>& dedup)
{
    std::map<WidgetClass, std::string> map;

    auto tryAdd = [&](const std::string& sym)
    {
        void* var = dlsym(RTLD_DEFAULT, sym.c_str());
        if (var == nullptr)
        {
            return;
        }
        WidgetClass wc = *(WidgetClass*)var;
        if (wc == nullptr)
        {
            return;
        }
        if (map.find(wc) == map.end())
        {
            map[wc] = sym;
        }
    };

    for (auto& kv : dedup)
    {
        tryAdd(kv.first);
    }

    return map;
}

/* Look up `classSymbol`'s superclass via its WidgetClass's
 * core_class.superclass pointer. The pointer is resolved to a classSymbol using
 * `ptrMap`. This read does not require XtInitializeWidgetClass and thus avoids
 * crashes observed for some classes during initialization. If the superclass
 * pointer is null (root class), an empty string is returned. If the pointer
 * cannot be resolved, the superclass's class_name is returned as a fallback, or
 * empty on failure. */
static std::string getSuperclassSymbol(const std::string& classSymbol,
    const std::map<WidgetClass, std::string>& ptrMap)
{
    dlerror();
    void* var = dlsym(RTLD_DEFAULT, classSymbol.c_str());
    if (var == nullptr)
    {
        const char* err = dlerror();
        std::cerr << "warning: dlsym failed for '" << classSymbol
                  << "' when looking up superclass: " << (err ? err : "unknown")
                  << "\n";
        return "";
    }
    WidgetClass wc = *(WidgetClass*)var;
    if (wc == nullptr)
    {
        std::cerr << "warning: WidgetClass '" << classSymbol
                  << "' is null when looking up superclass\n";
        return "";
    }
    WidgetClass sc = wc->core_class.superclass;
    if (sc == nullptr)
    {
        return "";
    }
    auto it = ptrMap.find(sc);
    if (it != ptrMap.end())
    {
        return it->second;
    }
    /* Fallback: search by class_name among known pointers. */
    if (sc->core_class.class_name != nullptr)
    {
        std::string cname = sc->core_class.class_name;
        for (auto& kv : ptrMap)
        {
            if (kv.first->core_class.class_name != nullptr &&
                cname == kv.first->core_class.class_name)
            {
                return kv.second;
            }
        }
        std::cerr << "warning: superclass pointer for '" << classSymbol
                  << "' (class_name " << cname
                  << ") not found in map -- using class_name as fallback\n";
        return cname;
    }
    std::cerr << "warning: superclass for '" << classSymbol
              << "' not found in map and has no class_name\n";
    return "";
}

/* Look up `classSymbol` via dlsym() and return its resource names as a
 * sorted map to empty JSON objects. Reads the class record directly via
 * XtGetResourceList() without calling XtInitializeWidgetClass() and without
 * forking -- crashes are not caught. */
static std::map<std::string, nlohmann::json> getResources(
    const std::string& classSymbol)
{
    std::map<std::string, nlohmann::json> result;

    /* Clear any stale error. */
    dlerror();

    void* sym = dlsym(RTLD_DEFAULT, classSymbol.c_str());
    if (sym == nullptr)
    {
        const char* err = dlerror();
        std::cerr << "warning: dlsym failed for '" << classSymbol
                  << "': " << (err ? err : "unknown") << "\n";
        return result;
    }

    /* Symbol is an external WidgetClass variable, so dlsym returns its
     * address; dereference to obtain the actual WidgetClass pointer. */
    WidgetClass wc = *(WidgetClass*)sym;
    if (wc == nullptr)
    {
        std::cerr << "warning: WidgetClass '" << classSymbol << "' is null\n";
        return result;
    }

    /* Collect resources defined in superclasses so they can be ignored. */
    std::unordered_set<std::string> ancestorResources;
    for (WidgetClass sc = wc->core_class.superclass; sc != nullptr;
        sc = sc->core_class.superclass)
    {
        XtResourceList sList = nullptr;
        Cardinal sNum = 0;
        XtGetResourceList(sc, &sList, &sNum);
        for (Cardinal j = 0; j < sNum; ++j)
        {
            const char* sName = sList[j].resource_name;
            if (sName == nullptr || sName[0] == '\0')
            {
                continue;
            }
            ancestorResources.insert(sName);
        }
        if (sList != nullptr)
        {
            XtFree((char*)sList);
        }
    }

    XtResourceList resList = nullptr;
    Cardinal num = 0;
    XtGetResourceList(wc, &resList, &num);
    for (Cardinal i = 0; i < num; ++i)
    {
        const char* name = resList[i].resource_name;
        if (name == nullptr || name[0] == '\0')
        {
            continue;
        }
        /* Ignore resources defined in a superclass. */
        if (ancestorResources.find(name) != ancestorResources.end())
        {
            continue;
        }
        result.emplace(name, nlohmann::json::object());
    }
    if (resList != nullptr)
    {
        XtFree((char*)resList);
    }

    return result;
}

int main(int argc, char* argv[])
{
    std::vector<std::string> headerDirs;
    std::vector<std::string> includePaths;
    std::string outputPath;
    std::string prefix;

    static struct option longOptions[] = {
        {"headers", required_argument, nullptr, 'H'},
        {"include", required_argument, nullptr, 'I'},
        {"output",  required_argument, nullptr, 'o'},
        {"prefix",  required_argument, nullptr, 'p'},
        {nullptr,   0,                 nullptr, 0  },
    };

    int opt = 0;
    int longIndex = 0;
    while (
        (opt = getopt_long(argc, argv, "o:I:", longOptions, &longIndex)) != -1)
    {
        switch (opt)
        {
            case 'H':
                headerDirs.emplace_back(optarg);
                break;

            case 'I':
                includePaths.emplace_back(optarg);
                break;

            case 'o':
                outputPath = optarg;
                break;

            case 'p':
                prefix = optarg;
                break;

            case '?':
            default:
                return 1;
        }
    }

    std::vector<fs::path> headers = collectHeaders(headerDirs);

    std::vector<std::pair<std::string, std::string>> widgetClasses;
    std::map<std::string, std::vector<std::string>> functionsByFile;

    /* Parse each header independently and leave unresolved symbols
     * unresolved. Use the clang-c API via the wrapper in clang.h/clang.cc
     * rather than the C++ Tooling API. Each header is parsed via an artificial
     * translation unit that includes <X11/Intrinsic.h> first so that Xt/Motif
     * headers that depend on Intrinsic definitions (e.g. Composite.h,
     * Core.h, Object.h) see the real definitions from /usr/include/X11.
     * Full parsing is performed with C99 mode and warnings disabled; any
     * error causes a hard failure. Include paths supplied via -I/--include
     * are forwarded to the clang invocation. */
    std::vector<std::string> args;
    args.push_back("-std=c99");
    args.push_back("-w");
    /* Ensure the clang builtin headers (stddef.h etc.) are found. When using
     * the clang-c API directly, the resource directory is not always inferred.
     * Query clang for its resource directory and pass it explicitly. */
    {
        FILE* pipe = popen("clang -print-resource-dir 2>/dev/null", "r");
        if (pipe != nullptr)
        {
            char buf[1024] = {0};
            if (fgets(buf, sizeof(buf), pipe) != nullptr)
            {
                std::string dir(buf);
                while (
                    !dir.empty() && (dir.back() == '\n' || dir.back() == '\r'))
                {
                    dir.pop_back();
                }
                if (!dir.empty())
                {
                    args.push_back("-resource-dir");
                    args.push_back(dir);
                }
            }
            pclose(pipe);
        }
    }
    for (auto& ip : includePaths)
    {
        args.push_back("-I" + ip);
    }

    clang::Index index(0, 0);

    for (auto& h : headers)
    {
        std::string code =
            "#include <X11/Intrinsic.h>\n#include \"" + h.string() + "\"\n";
        std::string dummy = h.string() + ".c";

        CXErrorCode ec = CXError_Success;
        clang::TranslationUnit tu = clang::parse(
            index, dummy, args, dummy, code, CXTranslationUnit_None, &ec);
        if (!tu)
        {
            std::cerr << "error: failed to parse " << h << " (CXError " << ec
                      << ")\n";
            return 1;
        }
        if (tu.hasError())
        {
            std::cerr << "error: failed to parse " << h << " (diagnostics)\n";
            unsigned n = tu.numDiagnostics();
            for (unsigned i = 0; i < n; ++i)
            {
                CXDiagnostic d = clang_getDiagnostic(tu.get(), i);
                CXString s = clang_formatDiagnostic(
                    d, clang_defaultDiagnosticDisplayOptions());
                std::string msg = clang::toString(s);
                std::cerr << msg << "\n";
                clang_disposeDiagnostic(d);
            }
            return 1;
        }

        std::string targetFile = h.string();
        auto tuCursor = tu.cursor();
        tuCursor.visitChildren(
            [&](clang::Cursor cursor, clang::Cursor /* parent */)
            {
                CXCursorKind k = cursor.kind();
                if (k == CXCursor_VarDecl)
                {
                    if (cursor.hasGlobalStorage() != 1)
                    {
                        return CXChildVisit_Recurse;
                    }
                    std::string typeStr = cursor.typeSpelling();
                    if (typeStr != "WidgetClass")
                    {
                        return CXChildVisit_Recurse;
                    }
                    std::string name = cursor.spelling();
                    if (name.empty())
                    {
                        return CXChildVisit_Recurse;
                    }
                    std::string file = cursor.fileName();
                    if (file != targetFile)
                    {
                        return CXChildVisit_Recurse;
                    }
                    widgetClasses.emplace_back(name, file);
                }
                else if (k == CXCursor_FunctionDecl)
                {
                    if (cursor.linkage() != CXLinkage_External)
                    {
                        return CXChildVisit_Recurse;
                    }
                    std::string file = cursor.fileName();
                    if (file != targetFile)
                    {
                        return CXChildVisit_Recurse;
                    }
                    std::string name = cursor.spelling();
                    if (name.empty())
                    {
                        return CXChildVisit_Recurse;
                    }
                    functionsByFile[file].push_back(name);
                }
                return CXChildVisit_Recurse;
            });
    }

    /* Deduplicate widget classes by name and sort for stable output. */
    std::map<std::string, std::string> dedup;
    for (auto& p : widgetClasses)
    {
        if (dedup.find(p.first) == dedup.end())
        {
            dedup[p.first] = p.second;
        }
    }

    /* Deduplicate and sort functions per file. */
    for (auto& kv : functionsByFile)
    {
        auto& vec = kv.second;
        std::sort(vec.begin(), vec.end());
        vec.erase(std::unique(vec.begin(), vec.end()), vec.end());
    }

    /* Build pointer-to-symbol map for superclass resolution. */
    auto classPointerMap = buildClassPointerMap(dedup);

    if (!outputPath.empty())
    {
        nlohmann::json json = nlohmann::json::object();
        json["prefix"] = prefix;
        nlohmann::json widgets = nlohmann::json::object();
        /* Widgets keyed by classSymbol (symbol), each widget carries a
         * human-readable `name` field. Methods inside each widget are keyed by
         * full symbol and carry a `name` with the stripped human name. */
        std::map<std::string, std::string> seenHumanNames;
        for (auto& kv : dedup)
        {
            const std::string& classSymbol = kv.first;
            const std::string& include = kv.second;
            std::string humanName = computeHumanName(classSymbol, prefix);
            auto dupIt = seenHumanNames.find(humanName);
            if (dupIt != seenHumanNames.end())
            {
                std::cerr << "warning: duplicate humanName '" << humanName
                          << "' for classSymbol '" << classSymbol
                          << "' (already mapped to '" << dupIt->second
                          << "')\n";
            }
            else
            {
                seenHumanNames[humanName] = classSymbol;
            }
            nlohmann::json w = nlohmann::json::object();
            w["name"] = humanName;
            w["include"] = include;
            w["superclass"] = getSuperclassSymbol(classSymbol, classPointerMap);
            auto it = functionsByFile.find(include);
            if (it != functionsByFile.end())
            {
                auto methods = filterMethods(it->second, prefix, humanName);
                nlohmann::json m = nlohmann::json::object();
                for (auto& p : methods)
                {
                    m[p.first] = p.second;
                }
                w["methods"] = std::move(m);
            }
            else
            {
                w["methods"] = nlohmann::json::object();
            }
            {
                auto resources = getResources(classSymbol);
                nlohmann::json r = nlohmann::json::object();
                for (auto& p : resources)
                {
                    r[p.first] = p.second;
                }
                w["resources"] = std::move(r);
            }
            widgets[classSymbol] = std::move(w);
        }
        json["widgets"] = std::move(widgets);
        std::ofstream out(outputPath);
        if (!out)
        {
            std::cerr << "failed to open " << outputPath << "\n";
            return 1;
        }
        out << json.dump(2) << "\n";
        if (!out)
        {
            std::cerr << "failed to write " << outputPath << "\n";
            return 1;
        }
    }

    return 0;
}
