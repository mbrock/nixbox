#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <cstdio>
#include <fstream>
#include <string>

extern "C" int sbcl_platform_probe(FILE *report);

int main(int, char **)
{
    if (!SDL_Init(SDL_INIT_VIDEO)) return 1;
    SDL_Window *window = nullptr;
    SDL_Renderer *renderer = nullptr;
    if (!SDL_CreateWindowAndRenderer("SBCL platform probe", 1920, 1080,
            SDL_WINDOW_FULLSCREEN, &window, &renderer)) return 1;
    SDL_SetRenderVSync(renderer, 1);
    char *pref = SDL_GetPrefPath("nixbox", "sbcl-platform-probe");
    if (!pref) return 1;
    const std::string path = std::string{pref} + "platform.txt";
    SDL_free(pref);
    FILE *report = std::fopen(path.c_str(), "w");
    if (!report) return 1;
    const int failures = sbcl_platform_probe(report);
    std::fclose(report);
    for (bool running = true; running;) {
        SDL_Event event;
        while (SDL_PollEvent(&event))
            if (event.type == SDL_EVENT_QUIT) running = false;
        SDL_SetRenderDrawColorFloat(renderer, 0.04f, 0.06f, 0.09f, 1);
        SDL_RenderClear(renderer);
        SDL_SetRenderScale(renderer, 2, 2);
        SDL_SetRenderDrawColorFloat(renderer, 0.4f, 0.8f, 1, 1);
        SDL_RenderDebugText(renderer, 24, 20, "SBCL / Xbox runtime primitives (not a Lisp port yet)");
        SDL_SetRenderDrawColorFloat(renderer, 0.9f, 0.93f, 0.96f, 1);
        SDL_SetRenderScale(renderer, 1.5f, 1.5f);
        std::ifstream log{path};
        int row = 0;
        for (std::string line; std::getline(log, line); ++row)
            SDL_RenderDebugText(renderer, 32 + (row / 34) * 640, 64 + (row % 34) * 18, line.c_str());
        SDL_SetRenderScale(renderer, 1, 1);
        SDL_RenderPresent(renderer);
    }
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return failures ? 1 : 0;
}
