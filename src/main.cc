/* mir -- Motif introspection / reflection. */

#include <clang-c/Index.h>

int main(int argc, char* argv[])
{
    (void)argc;
    (void)argv;

    CXIndex index = clang_createIndex(0, 0);
    clang_disposeIndex(index);
    return 0;
}
