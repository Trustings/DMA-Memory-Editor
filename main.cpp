#include "config.hpp"
#include "core.hpp"
#include "list.hpp"
#include "memory.hpp"
#include "render.hpp"

#include <cstdio>

static void RenderFrame() {
    // The menu draws itself from Render::RenderLoop; nothing extra per frame.
}

int main(int argc, char** argv)
{
    // Keep stdout in order with stderr when output is redirected to a file or
    // a pipe; block-buffered stdout otherwise arrives after unbuffered stderr,
    // which makes the log of a failed run read back-to-front.
    //
    // The two CRTs disagree here. glibc takes a zero size for _IOLBF as "pick
    // a suitable buffer", but the MSVC CRT requires size >= 2 for _IOLBF and
    // _IOFBF -- passing 0 trips the invalid parameter handler and fast-fails
    // (0xC0000409) before main() prints anything. MSVC also maps _IOLBF onto
    // full buffering, so unbuffered is what actually preserves ordering there.
#ifdef _WIN32
    setvbuf(stdout, nullptr, _IONBF, 0);
#else
    setvbuf(stdout, nullptr, _IOLBF, 0);
#endif

    Config_LoadDefaultFile(argc > 0 ? argv[0] : nullptr);

    switch (Config_ParseArgs(argc, argv)) {
    case ConfigParse::ExitSuccess: return 0;   // --help
    case ConfigParse::ExitFailure: return 1;
    case ConfigParse::Ok:          break;
    }

    Config_Print();

    // Every one of these used to be ignored. Initialize() returning false left
    // hVMM null and the app carried on issuing VMM calls against it; a failed
    // Vulkan init printed a message, called system("pause") -- which is not a
    // command on Linux -- and then entered the render loop anyway.
    if (!Initialize()) {
        fprintf(stderr, "[!] Failed to initialize the memory backend. Exiting.\n");
        return 1;
    }

    if (!Render::InitVulkan()) {
        fprintf(stderr, "[!] Failed to initialize Vulkan. Exiting.\n");
        Shutdown();
        return 1;
    }

    Core_Start();

    Render::RenderLoop(RenderFrame);

    Core_Stop();
    Render::Cleanup();
    Shutdown();

    return 0;
}
