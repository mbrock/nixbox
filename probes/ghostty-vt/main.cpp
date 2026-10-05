#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <cstdio>
#include <fstream>
#include <string>

extern "C" int ghostty_vt_probe_main(void);

int main(int, char **)
{
    if (!SDL_Init(SDL_INIT_VIDEO)) return 1;
    SDL_Window *window = nullptr;
    SDL_Renderer *renderer = nullptr;
    if (!SDL_CreateWindowAndRenderer("Ghostty VT on Xbox", 1920, 1080,
            SDL_WINDOW_FULLSCREEN, &window, &renderer)) return 1;
    SDL_SetRenderVSync(renderer, 1);
    char *pref = SDL_GetPrefPath("nixbox", "ghostty-vt-probe");
    if (!pref) return 1;
    const std::string report = std::string{pref} + "vt.txt";
    SDL_free(pref);
    if (!std::freopen(report.c_str(), "w", stdout)) return 1;
    const int result = ghostty_vt_probe_main();
    std::fflush(stdout);
    for (bool running = true; running;) {
        SDL_Event event;
        while (SDL_PollEvent(&event))
            if (event.type == SDL_EVENT_QUIT) running = false;
        SDL_SetRenderDrawColorFloat(renderer, 0.04f, 0.06f, 0.09f, 1);
        SDL_RenderClear(renderer);
        SDL_SetRenderScale(renderer, 3, 3);
        SDL_SetRenderDrawColorFloat(renderer, 0.4f, 0.8f, 1, 1);
        SDL_RenderDebugText(renderer, 32, 24, "Ghostty VT / Zig + MSVC UWP / actual terminal engine checks");
        SDL_RenderDebugText(renderer, 32, 40, result == 0 ? "PASSED" : "FAILED - see vt.txt");
        SDL_SetRenderDrawColorFloat(renderer, 0.9f, 0.93f, 0.96f, 1);
        std::ifstream log{report};
        float y = 64;
        for (std::string line; std::getline(log, line); y += 16)
            SDL_RenderDebugText(renderer, 32, y, line.c_str());
        SDL_SetRenderScale(renderer, 1, 1);
        SDL_RenderPresent(renderer);
    }
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return result;
}
