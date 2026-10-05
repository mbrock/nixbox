#define main ssh_probe_main
#include "ssh-probe.cpp"
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
    if (!SDL_CreateWindowAndRenderer("Xbox SSH probe", 1920, 1080,
            SDL_WINDOW_FULLSCREEN, &window, &renderer)) return 1;
    SDL_SetRenderVSync(renderer, 1);
    char *pref = SDL_GetPrefPath("nixbox", "ssh-probe");
    if (!pref) return 1;
    const std::string directory{pref};
    SDL_free(pref);
    const std::string report = directory + "ssh.txt";
    if (!std::freopen(report.c_str(), "w", stdout)) return 1;
    std::puts("libssh2 Windows/UWP probe");
    std::puts("Runtime configuration: probe-args.txt (one argument per line)");
    std::fflush(stdout);
    std::vector<std::string> arguments{"ssh-probe"};
    std::ifstream config{directory + "probe-args.txt"};
    for (std::string argument; std::getline(config, argument);) {
        if (!argument.empty() && argument.back() == '\r') argument.pop_back();
        if (!argument.empty()) arguments.push_back(std::move(argument));
    }
    std::atomic<int> result{-1};
    std::thread work{[&] {
        std::vector<char *> argv;
        for (auto &argument : arguments) argv.push_back(argument.data());
        argv.push_back(nullptr);
        result.store(ssh_probe_main(static_cast<int>(arguments.size()), argv.data()));
    }};
    for (bool running = true; running;) {
        SDL_Event event;
        while (SDL_PollEvent(&event))
            if (event.type == SDL_EVENT_QUIT) running = false;
        SDL_SetRenderDrawColorFloat(renderer, 0.04f, 0.06f, 0.09f, 1);
        SDL_RenderClear(renderer);
        SDL_SetRenderScale(renderer, 3, 3);
        SDL_SetRenderDrawColorFloat(renderer, 0.4f, 0.8f, 1, 1);
        SDL_RenderDebugText(renderer, 32, 24, "libssh2 on Xbox / pinned host keys + public-key auth + SFTP");
        const int code = result.load();
        SDL_RenderDebugText(renderer, 32, 40, code < 0 ? "RUNNING" : code == 0 ? "PASSED" :
            code == 2 ? "REJECTED - untrusted host key" : "FAILED - see ssh.txt");
        SDL_SetRenderDrawColorFloat(renderer, 0.9f, 0.93f, 0.96f, 1);
        int width = 0, height = 0;
        SDL_GetRenderOutputSize(renderer, &width, &height);
        const auto columns = static_cast<size_t>(std::max(1, (width / 3 - 64) / 8));
        float y = 64;
        std::ifstream log{report};
        for (std::string line; std::getline(log, line) && y < height / 3 - 24;) {
            if (line.empty()) y += 12;
            for (size_t offset = 0; offset < line.size() && y < height / 3 - 24; offset += columns) {
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
