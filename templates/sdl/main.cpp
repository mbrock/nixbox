// An SDL3 game for Xbox: SDL_Renderer on Direct3D, gamepad input, plain main().
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <algorithm>
#include <cmath>

int main(int, char **)
{
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return 1;
    }
    SDL_Window *window = nullptr;
    SDL_Renderer *renderer = nullptr;
    if (!SDL_CreateWindowAndRenderer("nixbox SDL", 1920, 1080, SDL_WINDOW_FULLSCREEN, &window, &renderer)) {
        SDL_Log("Window or renderer failed: %s", SDL_GetError());
        return 1;
    }
    SDL_SetRenderVSync(renderer, 1);

    SDL_Gamepad *gamepad = nullptr;
    float x = 0.5f, y = 0.5f;
    Uint64 last = SDL_GetTicksNS();
    for (bool running = true; running;) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                running = false;
            } else if (event.type == SDL_EVENT_GAMEPAD_ADDED && !gamepad) {
                gamepad = SDL_OpenGamepad(event.gdevice.which);
            } else if (event.type == SDL_EVENT_GAMEPAD_REMOVED && gamepad &&
                       event.gdevice.which == SDL_GetGamepadID(gamepad)) {
                SDL_CloseGamepad(gamepad);
                gamepad = nullptr;
            }
        }

        const Uint64 now = SDL_GetTicksNS();
        const float seconds = (now - last) / 1e9f;
        last = now;
        if (gamepad) {
            const auto axis = [&](SDL_GamepadAxis a) {
                const float v = SDL_GetGamepadAxis(gamepad, a) / 32767.0f;
                return std::fabs(v) < 0.15f ? 0.0f : v;
            };
            x = std::clamp(x + axis(SDL_GAMEPAD_AXIS_LEFTX) * seconds * 0.5f, 0.0f, 1.0f);
            y = std::clamp(y + axis(SDL_GAMEPAD_AXIS_LEFTY) * seconds * 0.5f, 0.0f, 1.0f);
        }

        int w = 0, h = 0;
        SDL_GetRenderOutputSize(renderer, &w, &h);
        const float t = now / 1e9f;
        SDL_SetRenderDrawColorFloat(renderer, 0.05f, 0.07f, 0.11f, 1);
        SDL_RenderClear(renderer);
        for (int i = 0; i < 12; ++i) {
            const float a = t * 0.6f + i * 0.5236f;
            const SDL_FRect r{w / 2 + std::cos(a) * h * 0.35f - 20, h / 2 + std::sin(a) * h * 0.35f - 20, 40, 40};
            SDL_SetRenderDrawColorFloat(renderer, 0.25f + 0.06f * i, 0.55f, 1.0f - 0.06f * i, 1);
            SDL_RenderFillRect(renderer, &r);
        }
        const float size = h * 0.08f;
        const SDL_FRect player{x * (w - size), y * (h - size), size, size};
        SDL_SetRenderDrawColorFloat(renderer, 1.0f, 0.8f, 0.3f, 1);
        SDL_RenderFillRect(renderer, &player);
        SDL_SetRenderDrawColorFloat(renderer, 1, 1, 1, 1);
        SDL_SetRenderScale(renderer, 3, 3);
        SDL_RenderDebugText(renderer, 20, 20, gamepad ? "SDL3 on Xbox - left stick moves the square" : "SDL3 on Xbox - connect a gamepad");
        int joysticks = 0, gamepads = 0;
        SDL_free(SDL_GetJoysticks(&joysticks));
        SDL_free(SDL_GetGamepads(&gamepads));
        SDL_RenderDebugTextFormat(renderer, 20, 34, "renderer: %s, joysticks: %d, gamepads: %d",
                                  SDL_GetRendererName(renderer), joysticks, gamepads);
        SDL_SetRenderScale(renderer, 1, 1);
        SDL_RenderPresent(renderer);
    }

    if (gamepad) {
        SDL_CloseGamepad(gamepad);
    }
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
