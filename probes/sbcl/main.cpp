#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <windows.h>
#include <objbase.h>
#include <process.h>
#include <io.h>
#include <cerrno>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

extern "C" {
int initialize_lisp(int argc, char **argv, char **envp);
extern wchar_t **nixbox_lisp_argv;
// Deliberate Developer Mode import, omitted from the SDK's App partition.
__declspec(dllimport) BOOL WINAPI SetStdHandle(DWORD, HANDLE);
}

struct LispStartup {
    std::vector<std::string> args;
    std::string diagnostics;
};

static std::ofstream startup_log;

static unsigned __stdcall run_lisp(void *opaque)
{
    auto &startup = *static_cast<LispStartup *>(opaque);
    const std::string input = startup.diagnostics + ".stdin";
    if (!std::freopen(startup.diagnostics.c_str(), "w", stdout)) return 1;
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::fprintf(stdout, "Initial CRT descriptors: stdout=%d stderr=%d stdin=%d\n",
                 _fileno(stdout), _fileno(stderr), _fileno(stdin));
    // UWP has no console: unopened streams can have descriptor -2, which
    // _dup2 cannot accept as a destination. Bind stderr before duplicating.
    if (!std::freopen(startup.diagnostics.c_str(), "a", stderr) ||
        _dup2(_fileno(stdout), _fileno(stderr)) != 0 ||
        !std::freopen(input.c_str(), "w+", stdin)) {
        std::fprintf(stdout, "ERROR: CRT stream setup failed, errno=%d\n", errno);
        return 1;
    }
    std::setvbuf(stderr, nullptr, _IONBF, 0);
    if (!SetStdHandle(STD_OUTPUT_HANDLE, reinterpret_cast<HANDLE>(_get_osfhandle(_fileno(stdout)))) ||
        !SetStdHandle(STD_ERROR_HANDLE, reinterpret_cast<HANDLE>(_get_osfhandle(_fileno(stderr)))) ||
        !SetStdHandle(STD_INPUT_HANDLE, reinterpret_cast<HANDLE>(_get_osfhandle(_fileno(stdin))))) {
        std::fprintf(stdout, "ERROR: SetStdHandle failed, Win32 error=%lu\n", GetLastError());
        return 1;
    }
    std::fprintf(stderr, "Starting embedded SBCL on a CRT thread.\n");
    std::vector<char *> argv;
    std::vector<std::wstring> wide;
    for (auto &arg : startup.args) {
        argv.push_back(arg.data());
        const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                             arg.c_str(), -1, nullptr, 0);
        if (!count) return 1;
        std::wstring text(count, L'\0');
        if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, arg.c_str(), -1,
                                text.data(), count)) return 1;
        wide.push_back(std::move(text));
    }
    argv.push_back(nullptr);
    std::vector<wchar_t *> wargv;
    for (auto &arg : wide) wargv.push_back(arg.data());
    wargv.push_back(nullptr);
    nixbox_lisp_argv = wargv.data();
    char *envp[] = {nullptr};
    initialize_lisp(static_cast<int>(startup.args.size()), argv.data(), envp);
    std::fprintf(stderr, "ERROR: initialize_lisp unexpectedly returned.\n");
    return 1;
}

static int app_main(int, char **)
{
    startup_log << "Entered SDL app main.\n" << std::flush;
    if (!SDL_Init(SDL_INIT_VIDEO)) return 1;
    startup_log << "SDL video initialized.\n" << std::flush;
    SDL_Window *window = nullptr;
    SDL_Renderer *renderer = nullptr;
    if (!SDL_CreateWindowAndRenderer("Common Lisp on Xbox", 1920, 1080,
            SDL_WINDOW_FULLSCREEN, &window, &renderer)) return 1;
    startup_log << "Window and renderer created.\n" << std::flush;
    SDL_SetRenderVSync(renderer, 1);
    char *pref = SDL_GetPrefPath("nixbox", "sbcl-probe");
    const char *base = SDL_GetBasePath();
    if (!pref || !base) return 1;
    const std::string report = std::string{pref} + "lisp.txt";
    LispStartup startup;
    startup.diagnostics = std::string{pref} + "runtime.txt";
    SDL_free(pref);
    std::ostringstream form;
    form << "(progn (nixbox-sbcl:run " << std::quoted(report)
         << ") (loop (sleep 60)))";
    startup.args = {"sbcl-probe", "--core", std::string{base} + "sbcl.core",
        "--dynamic-space-size", "128", "--disable-ldb", "--disable-debugger",
        "--no-userinit", "--no-sysinit", "--load", std::string{base} + "probe.lisp",
        "--eval", form.str()};
    // Clear any old result so a failed relaunch cannot display a stale PASS.
    { std::ofstream initial{report}; initial << "Starting SBCL / waiting for Lisp...\n"; }
    const uintptr_t worker = _beginthreadex(nullptr, 16 * 1024 * 1024, run_lisp,
        &startup, STACK_SIZE_PARAM_IS_A_RESERVATION, nullptr);
    if (!worker) return 1;
    startup_log << "SBCL worker created.\n" << std::flush;
    for (bool running = true; running;) {
        SDL_Event event;
        while (SDL_PollEvent(&event))
            if (event.type == SDL_EVENT_QUIT) running = false;
        SDL_SetRenderDrawColorFloat(renderer, 0.04f, 0.06f, 0.09f, 1);
        SDL_RenderClear(renderer);
        SDL_SetRenderScale(renderer, 2, 2);
        SDL_SetRenderDrawColorFloat(renderer, 0.4f, 0.8f, 1, 1);
        SDL_RenderDebugText(renderer, 24, 20, "Common Lisp on Xbox / SBCL");
        SDL_SetRenderScale(renderer, 1.5f, 1.5f);
        std::ifstream log{report};
        int row = 0;
        for (std::string line; std::getline(log, line);) {
            if (line.starts_with("RUN ")) continue;
            if (line.starts_with("PASS ")) SDL_SetRenderDrawColorFloat(renderer, 0.45f, 0.9f, 0.65f, 1);
            else if (line.starts_with("FAIL ")) SDL_SetRenderDrawColorFloat(renderer, 1, 0.4f, 0.35f, 1);
            else SDL_SetRenderDrawColorFloat(renderer, 0.9f, 0.93f, 0.96f, 1);
            SDL_RenderDebugText(renderer, 32, 64 + row++ * 24, line.c_str());
        }
        if (WaitForSingleObject(reinterpret_cast<HANDLE>(worker), 0) == WAIT_OBJECT_0) {
            SDL_SetRenderDrawColorFloat(renderer, 1, 0.4f, 0.35f, 1);
            SDL_RenderDebugText(renderer, 32, 64 + row * 24, "Runtime thread stopped: see LocalState/runtime.txt");
        }
        SDL_SetRenderScale(renderer, 1, 1);
        SDL_RenderPresent(renderer);
    }
    CloseHandle(reinterpret_cast<HANDLE>(worker));
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
    // Log before CoreApplication.Run/activation, not only after SDL calls main.
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    char *pref = SDL_GetPrefPath("nixbox", "sbcl-probe");
    if (pref) {
        startup_log.open(std::string{pref} + "startup.txt");
        SDL_free(pref);
    }
    startup_log << "Entered WinMain; COM initialization: 0x" << std::hex
                << initialized << ". Calling SDL_RunApp.\n" << std::flush;
    const int result = SDL_RunApp(0, nullptr, app_main, nullptr);
    startup_log << "SDL_RunApp returned " << result << ": " << SDL_GetError()
                << '\n' << std::flush;
    if (SUCCEEDED(initialized)) CoUninitialize();
    return result;
}
