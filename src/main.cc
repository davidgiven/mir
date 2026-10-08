/* mir -- Motif introspection / reflection. */

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <getopt.h>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include <clang/AST/Decl.h>
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/Tooling/Tooling.h>

#include <X11/Intrinsic.h>
#include <X11/IntrinsicP.h>
#include <Xm/Xm.h>

#include <dlfcn.h>
#include <sys/wait.h>
#include <unistd.h>

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
        for (auto& entry : fs::recursive_directory_iterator(dir))
        {
            if (!entry.is_regular_file())
            {
                continue;
            }
            auto ext = entry.path().extension().string();
            if (ext == ".h" || ext == ".H")
            {
                headers.push_back(entry.path());
            }
        }
    }

    std::sort(headers.begin(), headers.end());

    return headers;
}

/* Visitor that collects VarDecl of type WidgetClass and extern
 * FunctionDecls grouped by defining file. */
struct Visitor : public clang::RecursiveASTVisitor<Visitor>
{
    clang::SourceManager* _sm = nullptr;
    std::vector<std::pair<std::string, std::string>>* _widgetClasses = nullptr;
    std::map<std::string, std::vector<std::string>>* _functionsByFile = nullptr;

    bool VisitVarDecl(clang::VarDecl* decl)
    {
        if (clang::isa<clang::ParmVarDecl>(decl))
        {
            return true;
        }
        if (!decl->hasGlobalStorage())
        {
            return true;
        }
        std::string typeStr = decl->getType().getAsString();
        if (typeStr != "WidgetClass")
        {
            return true;
        }
        std::string name = decl->getNameAsString();
        if (name.empty())
        {
            return true;
        }
        clang::SourceLocation loc = decl->getLocation();
        if (!_sm->isInMainFile(loc))
        {
            return true;
        }
        std::string file = _sm->getFilename(_sm->getFileLoc(loc)).str();
        if (file.empty())
        {
            return true;
        }
        _widgetClasses->emplace_back(name, file);
        return true;
    }

    bool VisitFunctionDecl(clang::FunctionDecl* decl)
    {
        if (!decl->hasExternalFormalLinkage())
        {
            return true;
        }
        clang::SourceLocation loc = decl->getLocation();
        if (!_sm->isInMainFile(loc))
        {
            return true;
        }
        std::string file = _sm->getFilename(_sm->getFileLoc(loc)).str();
        if (file.empty())
        {
            return true;
        }
        std::string name = decl->getNameAsString();
        if (name.empty())
        {
            return true;
        }
        (*_functionsByFile)[file].push_back(name);
        return true;
    }
};

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
 * name to produce the method name. Returns a sorted map from method name to
 * an empty JSON object. */
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
            result.emplace(methodName, nlohmann::json::object());
        }
    }
    return result;
}

/* Build a map from WidgetClass pointer value to its classSymbol string.
 * This is used to resolve superclass pointers back to their symbolic names.
 * The map is seeded from the discovered dedup set plus a hard-coded list of
 * Xt Intrinsics symbols that are common superclasses but live outside Xm. */
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

    static const std::vector<std::string> xtSymbols = {
        "coreWidgetClass",
        "widgetClass",
        "compositeWidgetClass",
        "constraintWidgetClass",
        "objectClass",
        "rectObjClass",
        "shellWidgetClass",
        "overrideShellWidgetClass",
        "wmShellWidgetClass",
        "transientShellWidgetClass",
        "topLevelShellWidgetClass",
        "applicationShellWidgetClass",
        "sessionShellWidgetClass",
        "vendorShellWidgetClass",
        "hookObjectClass",
    };
    for (auto& s : xtSymbols)
    {
        tryAdd(s);
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

/* Look up `classSymbol` via dlsym(), initialise the class and return its
 * resource names as a sorted map to empty JSON objects. Uses
 * XtInitializeWidgetClass() + XtGetResourceList() as required by the spec.
 * Each widget is handled in a forked child so a segfault in
 * XtInitializeWidgetClass (observed for e.g. xmDesktopClass after gadget
 * initialisation) does not crash the whole tool. */
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

    /* Flush streams before fork to avoid duplicate buffered output. */
    std::cout.flush();
    std::cerr.flush();
    fflush(nullptr);

    int pipefd[2];
    if (pipe(pipefd) != 0)
    {
        std::cerr << "warning: pipe failed for '" << classSymbol << "'\n";
        return result;
    }

    pid_t pid = fork();
    if (pid < 0)
    {
        std::cerr << "warning: fork failed for '" << classSymbol << "'\n";
        close(pipefd[0]);
        close(pipefd[1]);
        return result;
    }

    if (pid == 0)
    {
        /* Child -- perform the Xt work and write resource names, one per
         * line, to the pipe. */
        close(pipefd[0]);

        XtInitializeWidgetClass(wc);

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
            /* Use dprintf / write to avoid iostream buffering issues after
             * fork. */
            dprintf(pipefd[1], "%s\n", name);
        }
        if (resList != nullptr)
        {
            XtFree((char*)resList);
        }
        close(pipefd[1]);
        _exit(0);
    }

    /* Parent -- read resource names from child. */
    close(pipefd[1]);

    std::string data;
    char buf[4096];
    ssize_t n = 0;
    while ((n = read(pipefd[0], buf, sizeof(buf))) > 0)
    {
        data.append(buf, static_cast<size_t>(n));
    }
    close(pipefd[0]);

    int status = 0;
    if (waitpid(pid, &status, 0) < 0)
    {
        std::cerr << "warning: waitpid failed for '" << classSymbol << "'\n";
        return result;
    }
    if (WIFSIGNALED(status))
    {
        std::cerr << "warning: XtInitializeWidgetClass crashed for '"
                  << classSymbol << "' (signal " << WTERMSIG(status)
                  << ") -- skipping resources\n";
        return result;
    }
    if (WIFEXITED(status) && WEXITSTATUS(status) != 0)
    {
        std::cerr << "warning: child failed for '" << classSymbol << "' (exit "
                  << WEXITSTATUS(status) << ")\n";
        return result;
    }

    /* Parse newline-separated names into the map. */
    size_t pos = 0;
    while (pos < data.size())
    {
        size_t nl = data.find('\n', pos);
        std::string name;
        if (nl == std::string::npos)
        {
            name = data.substr(pos);
            pos = data.size();
        }
        else
        {
            name = data.substr(pos, nl - pos);
            pos = nl + 1;
        }
        if (!name.empty())
        {
            result.emplace(name, nlohmann::json::object());
        }
    }

    return result;
}

int main(int argc, char* argv[])
{
    /* Initialise Xt/Motif -- creates an application context needed for
     * any subsequent Xt/Motif calls. Dynamic reflection will later use
     * this context to instantiate widget hierarchies. */
    XtAppContext appContext = XtCreateApplicationContext();
    (void)appContext;

    std::vector<std::string> headerDirs;
    std::string outputPath;
    std::string prefix;

    static struct option longOptions[] = {
        {"headers", required_argument, nullptr, 'H'},
        {"output",  required_argument, nullptr, 'o'},
        {"prefix",  required_argument, nullptr, 'p'},
        {nullptr,   0,                 nullptr, 0  },
    };

    int opt = 0;
    int longIndex = 0;
    while ((opt = getopt_long(argc, argv, "o:", longOptions, &longIndex)) != -1)
    {
        switch (opt)
        {
            case 'H':
                headerDirs.emplace_back(optarg);
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
     * unresolved. Use the C++ Tooling API (buildASTFromCodeWithArgs)
     * rather than the C API. */
    std::vector<std::string> args;

    for (auto& h : headers)
    {
        std::ifstream in(h);
        if (!in)
        {
            std::cerr << "warning: failed to open " << h << "\n";
            continue;
        }
        std::stringstream buffer;
        buffer << in.rdbuf();
        std::string code = buffer.str();

        auto ast = clang::tooling::buildASTFromCodeWithArgs(
            code, args, h.string(), "/usr/bin/clang");
        if (!ast)
        {
            std::cerr << "warning: failed to parse " << h << "\n";
            continue;
        }

        Visitor visitor;
        visitor._sm = &ast->getSourceManager();
        visitor._widgetClasses = &widgetClasses;
        visitor._functionsByFile = &functionsByFile;
        visitor.TraverseDecl(ast->getASTContext().getTranslationUnitDecl());
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
        /* Map humanName -> (classSymbol, include) sorted by humanName for
         * stable output keyed by human name. */
        std::map<std::string, std::pair<std::string, std::string>> humanMap;
        for (auto& kv : dedup)
        {
            std::string humanName = computeHumanName(kv.first, prefix);
            if (humanMap.find(humanName) != humanMap.end())
            {
                std::cerr << "warning: duplicate humanName '" << humanName
                          << "' for classSymbol '" << kv.first
                          << "' (already mapped to '"
                          << humanMap[humanName].first << "') -- skipping\n";
                continue;
            }
            humanMap[humanName] = {kv.first, kv.second};
        }
        for (auto& kv : humanMap)
        {
            const std::string& humanName = kv.first;
            const std::string& classSymbol = kv.second.first;
            const std::string& include = kv.second.second;
            nlohmann::json w = nlohmann::json::object();
            w["include"] = include;
            w["classSymbol"] = classSymbol;
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
            widgets[humanName] = std::move(w);
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
