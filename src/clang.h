/* mir -- wrapper around the clang-c API. */

#pragma once

#include <clang-c/CXString.h>
#include <clang-c/Index.h>

#include <functional>
#include <string>
#include <vector>

namespace clang
{

    /* Convert a CXString to std::string and dispose the CXString. */
    extern std::string toString(CXString s);

    /* RAII wrapper for CXString. Useful when the string must live longer than a
     * single expression. */
    class String
    {
    public:
        explicit String(CXString s): _s(s) {}

        ~String()
        {
            clang_disposeString(_s);
        }

        String(const String&) = delete;
        String& operator=(const String&) = delete;

        String(String&& other) noexcept: _s(other._s)
        {
            other._s = CXString{nullptr, 0};
        }

        String& operator=(String&& other) noexcept
        {
            if (this != &other)
            {
                clang_disposeString(_s);
                _s = other._s;
                other._s = CXString{nullptr, 0};
            }
            return *this;
        }

        /* Return the string as std::string (copies). */
        std::string str() const
        {
            const char* c = clang_getCString(_s);
            return c != nullptr ? c : "";
        }

        const char* c_str() const
        {
            const char* c = clang_getCString(_s);
            return c != nullptr ? c : "";
        }

        CXString get() const
        {
            return _s;
        }

    private:
        CXString _s;
    };

    /* RAII wrapper for CXIndex. */
    class Index
    {
    public:
        explicit Index(
            int excludeDeclarationsFromPCH = 0, int displayDiagnostics = 0):
            _index(clang_createIndex(
                excludeDeclarationsFromPCH, displayDiagnostics))
        {
        }

        ~Index()
        {
            if (_index != nullptr)
            {
                clang_disposeIndex(_index);
            }
        }

        Index(const Index&) = delete;
        Index& operator=(const Index&) = delete;

        Index(Index&& other) noexcept: _index(other._index)
        {
            other._index = nullptr;
        }

        Index& operator=(Index&& other) noexcept
        {
            if (this != &other)
            {
                if (_index != nullptr)
                {
                    clang_disposeIndex(_index);
                }
                _index = other._index;
                other._index = nullptr;
            }
            return *this;
        }

        CXIndex get() const
        {
            return _index;
        }

    private:
        CXIndex _index;
    };

    /* Wrapper for CXType. */
    class Type
    {
    public:
        explicit Type(CXType t): _type(t) {}

        std::string spelling() const
        {
            return toString(clang_getTypeSpelling(_type));
        }

        CXType get() const
        {
            return _type;
        }

        CXTypeKind kind() const
        {
            return _type.kind;
        }

    private:
        CXType _type;
    };

    /* Wrapper for CXCursor. This type is lightweight (mirrors CXCursor) and
     * does not own resources. */
    class Cursor
    {
    public:
        explicit Cursor(CXCursor c): _cursor(c) {}

        CXCursor get() const
        {
            return _cursor;
        }

        CXCursorKind kind() const
        {
            return clang_getCursorKind(_cursor);
        }

        std::string spelling() const
        {
            return toString(clang_getCursorSpelling(_cursor));
        }

        std::string displayName() const
        {
            return toString(clang_getCursorDisplayName(_cursor));
        }

        Type type() const
        {
            return Type(clang_getCursorType(_cursor));
        }

        std::string typeSpelling() const
        {
            return toString(
                clang_getTypeSpelling(clang_getCursorType(_cursor)));
        }

        CXLinkageKind linkage() const
        {
            return clang_getCursorLinkage(_cursor);
        }

        int hasGlobalStorage() const
        {
            return clang_Cursor_hasVarDeclGlobalStorage(_cursor);
        }

        int hasExternalStorage() const
        {
            return clang_Cursor_hasVarDeclExternalStorage(_cursor);
        }

        bool isNull() const
        {
            return clang_Cursor_isNull(_cursor) != 0;
        }

        CXSourceLocation location() const
        {
            return clang_getCursorLocation(_cursor);
        }

        /* Return the presumed file name for the cursor's location. This matches
         * SourceManager::getFilename(getFileLoc(...)) used previously. */
        std::string fileName() const
        {
            CXSourceLocation loc = clang_getCursorLocation(_cursor);
            CXString fn;
            unsigned line = 0;
            unsigned col = 0;
            clang_getPresumedLocation(loc, &fn, &line, &col);
            return toString(fn);
        }

        /* Alternative using FileLocation (spelling). */
        std::string spellingFileName() const
        {
            CXSourceLocation loc = clang_getCursorLocation(_cursor);
            CXFile file = nullptr;
            unsigned line = 0;
            unsigned col = 0;
            unsigned off = 0;
            clang_getSpellingLocation(loc, &file, &line, &col, &off);
            if (file == nullptr)
            {
                return "";
            }
            return toString(clang_getFileName(file));
        }

        bool isDeclaration() const
        {
            return clang_isDeclaration(kind()) != 0;
        }

        bool isInvalid() const
        {
            return clang_isInvalid(kind()) != 0;
        }

        /* Visit direct children. The visitor returns a CXChildVisitResult. */
        using Visitor = std::function<CXChildVisitResult(Cursor, Cursor)>;

        unsigned visitChildren(Visitor visitor) const;

    private:
        CXCursor _cursor;
    };

    /* RAII wrapper for CXTranslationUnit. */
    class TranslationUnit
    {
    public:
        TranslationUnit() = default;

        explicit TranslationUnit(CXTranslationUnit tu): _tu(tu) {}

        ~TranslationUnit()
        {
            if (_tu != nullptr)
            {
                clang_disposeTranslationUnit(_tu);
            }
        }

        TranslationUnit(const TranslationUnit&) = delete;
        TranslationUnit& operator=(const TranslationUnit&) = delete;

        TranslationUnit(TranslationUnit&& other) noexcept: _tu(other._tu)
        {
            other._tu = nullptr;
        }

        TranslationUnit& operator=(TranslationUnit&& other) noexcept
        {
            if (this != &other)
            {
                if (_tu != nullptr)
                {
                    clang_disposeTranslationUnit(_tu);
                }
                _tu = other._tu;
                other._tu = nullptr;
            }
            return *this;
        }

        explicit operator bool() const
        {
            return _tu != nullptr;
        }

        CXTranslationUnit get() const
        {
            return _tu;
        }

        Cursor cursor() const
        {
            return Cursor(clang_getTranslationUnitCursor(_tu));
        }

        /* Return true if any diagnostic has severity Error or Fatal. */
        bool hasError() const;

        unsigned numDiagnostics() const
        {
            return _tu != nullptr ? clang_getNumDiagnostics(_tu) : 0;
        }

    private:
        CXTranslationUnit _tu = nullptr;
    };

    /* Parse a translation unit from an unsaved file. `fileName` is the name
     * used for the translation unit (typically dummy.c). The unsaved file
     * provides the actual source contents. `args` are command-line arguments
     * forwarded to the parser (e.g. -std=c99, -I...). `options` are
     * CXTranslationUnit flags.
     *
     * On failure returns an empty TranslationUnit (bool false) and sets
     * CXErrorCode in `error` if non-null. */
    extern TranslationUnit parse(Index& index,
        const std::string& fileName,
        const std::vector<std::string>& args,
        const std::string& unsavedFileName,
        const std::string& unsavedContents,
        unsigned options = CXTranslationUnit_None,
        CXErrorCode* error = nullptr);

} /* namespace clang */
