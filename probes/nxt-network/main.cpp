// Give the portable upstream CLI a UWP entry point and a visible report.
// Credentials are read by the probe from LocalState, never from the package.
#define main nxt_network_probe_main
#include "network-probe.cpp"
#undef main

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <algorithm>
#include <atomic>
#include <thread>
#include <vector>

int main(int, char **)
{
    if (!SDL_Init(SDL_INIT_VIDEO)) return 1;
    SDL_Window *window = nullptr;
    SDL_Renderer *renderer = nullptr;
    if (!SDL_CreateWindowAndRenderer("NXT networking", 1920, 1080,
            SDL_WINDOW_FULLSCREEN, &window, &renderer)) return 1;
    SDL_SetRenderVSync(renderer, 1);

    char *pref = SDL_GetPrefPath("nixbox", "nxt-network");
    if (!pref) return 1;
    const std::string directory{pref};
    SDL_free(pref);
    const std::string report = directory + "network.txt";
    if (!std::freopen(report.c_str(), "w", stdout)) return 1;
    std::printf("NXT Windows/UWP network probe\n");
    std::printf("Runtime configuration: probe-args.txt beside network.txt\n");
    std::fflush(stdout);

    // One argument per line, including the mode; paths can contain spaces.
    // Only credential FILE paths belong here, never the credential itself.
    std::vector<std::string> arguments{"network-probe"};
    std::ifstream config{directory + "probe-args.txt"};
    for (std::string argument; std::getline(config, argument);) {
        if (!argument.empty() && argument.back() == '\r') argument.pop_back();
        if (!argument.empty()) arguments.push_back(std::move(argument));
    }
    if (arguments.size() == 2 && arguments[1] == "--openai") {
        arguments.push_back(directory + "openai.key");
        arguments.push_back(std::string{SDL_GetBasePath()} + "ca-bundle.pem");
    }
    std::atomic<int> result{-1};
    std::thread work{[&] {
        std::vector<char *> argv;
        for (auto &argument : arguments) argv.push_back(argument.data());
        argv.push_back(nullptr);
        const int code = nxt_network_probe_main(static_cast<int>(arguments.size()), argv.data());
        std::fflush(stdout);
        result.store(code);
    }};

    for (bool running = true; running;) {
        SDL_Event event;
        while (SDL_PollEvent(&event))
            if (event.type == SDL_EVENT_QUIT) running = false;
        SDL_SetRenderDrawColorFloat(renderer, 0.04f, 0.06f, 0.09f, 1);
        SDL_RenderClear(renderer);
        SDL_SetRenderScale(renderer, 3, 3);
        SDL_SetRenderDrawColorFloat(renderer, 0.4f, 0.8f, 1, 1);
        SDL_RenderDebugText(renderer, 32, 24, "NXT networking on Xbox / GPT-6 Luna");
        const int code = result.load();
        SDL_RenderDebugText(renderer, 32, 40,
            code < 0 ? "RUNNING" : code == 0 ? "PASSED" : "FAILED - see network.txt");
        SDL_SetRenderDrawColorFloat(renderer, 0.9f, 0.93f, 0.96f, 1);

        int width = 0, height = 0;
        SDL_GetRenderOutputSize(renderer, &width, &height);
        const std::size_t columns = static_cast<std::size_t>(std::max(1, (width / 3 - 64) / 8));
        float y = 64;
        std::ifstream log{report};
        for (std::string line; std::getline(log, line) && y < height / 3 - 24;) {
            if (line.empty()) y += 12;
            for (std::size_t offset = 0; offset < line.size() && y < height / 3 - 24; offset += columns) {
                SDL_RenderDebugText(renderer, 32, y, line.substr(offset, columns).c_str());
                y += 12;
            }
        }
        SDL_SetRenderScale(renderer, 1, 1);
        SDL_RenderPresent(renderer);
    }
    work.join();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return result.load();
}
