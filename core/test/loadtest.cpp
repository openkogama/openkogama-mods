#include <cstdio>
#include <windows.h>

int main(int argc, char **argv)
{
    if (argc < 2) { std::printf("usage: loadtest <dll>\n"); return 2; }
    SetLastError(0);

    HMODULE h = LoadLibraryExA(argv[1], nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!h) { std::printf("LoadLibrary failed: %lu\n", GetLastError()); return 1; }
    std::printf("loaded ok\n");
    return 0;
}
