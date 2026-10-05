#include "simulation.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace {
constexpr float kPi = 3.14159265358979323846f;
SDL_FColor color(float r, float g, float b, float a = 1.0f) { return {r, g, b, a}; }
void line(SDL_Renderer* r, float x1, float y1, float x2, float y2, SDL_FColor c, float width = 1) {
    SDL_SetRenderDrawColorFloat(r, c.r, c.g, c.b, c.a);
    for (int n = 0; n < static_cast<int>(width); ++n) SDL_RenderLine(r, x1, y1 + n, x2, y2 + n);
}
void circle(SDL_Renderer* r, float x, float y, float radius, SDL_FColor c) {
    SDL_SetRenderDrawColorFloat(r, c.r, c.g, c.b, c.a);
    for (int dy = -static_cast<int>(radius); dy <= static_cast<int>(radius); ++dy) {
        const float dx = std::sqrt(std::max(0.0f, radius * radius - dy * dy));
        SDL_RenderLine(r, x - dx, y + dy, x + dx, y + dy);
    }
}
void fill(SDL_Renderer* r, SDL_FRect rect, SDL_FColor c) {
    SDL_SetRenderDrawColorFloat(r, c.r, c.g, c.b, c.a); SDL_RenderFillRect(r, &rect);
}
}

int main(int argc, char** argv) {
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) { SDL_Log("SDL init: %s", SDL_GetError()); return 1; }
    SDL_Window* window = nullptr; SDL_Renderer* renderer = nullptr;
    if (!SDL_CreateWindowAndRenderer("RICOCHET // CANNON ARENA", 1280, 720, SDL_WINDOW_RESIZABLE, &window, &renderer)) {
        SDL_Log("SDL window: %s", SDL_GetError()); SDL_Quit(); return 1;
    }
    SDL_SetRenderVSync(renderer, 1);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    IMGUI_CHECKVERSION(); ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    ImFontConfig font;
    font.SizePixels = 18;
    ImGui::GetIO().Fonts->AddFontDefault(&font);
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 10; style.FrameRounding = 6; style.GrabRounding = 6;
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.035f, 0.055f, 0.085f, 0.96f);
    style.Colors[ImGuiCol_Button] = ImVec4(0.13f, 0.42f, 0.50f, 1);
    style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.18f, 0.62f, 0.66f, 1);
    ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer3_Init(renderer);

    ricochet::Simulation game;
    SDL_Gamepad* pad = nullptr;
    float aim = 0, chargeTime = 0;
    bool charging = false, showHelp = false, running = true;
    Uint64 last = SDL_GetTicksNS();
    double accumulator = 0;
    bool demo = false;
    bool demoRound = false;
    bool screenshotSaved = false;
    std::string screenshotPath;
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--demo") demo = true;
        else if (std::string(argv[i]) == "--demo-round") { demo = true; demoRound = true; }
        else if (std::string(argv[i]) == "--show-help") showHelp = true;
        else if (std::string(argv[i]) == "--screenshot" && i + 1 < argc) screenshotPath = argv[++i];
    }
    int demoTicks = 0;
    const auto reset = [&] {
        game.reset();
        aim = 0;
        charging = false;
        chargeTime = 0;
    };
    const auto releaseShot = [&] {
        if (!charging) return;
        game.setAim(aim);
        game.setCharge(std::clamp(0.18f + chargeTime * 0.68f, 0.15f, 1.0f));
        game.fire();
        charging = false;
    };

    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            ImGui_ImplSDL3_ProcessEvent(&e);
            if (e.type == SDL_EVENT_QUIT) running = false;
            else if (e.type == SDL_EVENT_GAMEPAD_ADDED && !pad) pad = SDL_OpenGamepad(e.gdevice.which);
            else if (e.type == SDL_EVENT_GAMEPAD_REMOVED && pad && e.gdevice.which == SDL_GetGamepadID(pad)) { SDL_CloseGamepad(pad); pad = nullptr; charging = false; }
            else if (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN && e.gbutton.button == SDL_GAMEPAD_BUTTON_SOUTH) { charging = true; chargeTime = 0; }
            else if (e.type == SDL_EVENT_GAMEPAD_BUTTON_UP && e.gbutton.button == SDL_GAMEPAD_BUTTON_SOUTH) releaseShot();
            else if (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN && e.gbutton.button == SDL_GAMEPAD_BUTTON_EAST) reset();
            else if (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN && e.gbutton.button == SDL_GAMEPAD_BUTTON_START) showHelp = !showHelp;
            else if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE) running = false;
            else if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_R) reset();
            else if (e.type == SDL_EVENT_KEY_DOWN && (e.key.key == SDLK_UP || e.key.key == SDLK_RIGHT)) aim = std::min(aim + 0.06f, 0.82f);
            else if (e.type == SDL_EVENT_KEY_DOWN && (e.key.key == SDLK_DOWN || e.key.key == SDLK_LEFT)) aim = std::max(aim - 0.06f, -0.82f);
            else if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_SPACE && !e.key.repeat) { charging = true; chargeTime = 0; }
            else if (e.type == SDL_EVENT_KEY_UP && e.key.key == SDLK_SPACE) releaseShot();
        }
        const Uint64 now = SDL_GetTicksNS();
        const double elapsed = demoRound ? ricochet::kStep * 4 : std::min(0.1, (now - last) / 1.0e9); last = now; accumulator += elapsed;
        if (pad) {
            auto axis = [&](SDL_GamepadAxis a) { float v = SDL_GetGamepadAxis(pad, a) / 32767.0f; return std::abs(v) < 0.17f ? 0.0f : v; };
            aim = std::clamp(aim - axis(SDL_GAMEPAD_AXIS_LEFTY) * static_cast<float>(elapsed) * 1.7f, -0.82f, 0.82f);
        }
        game.setAim(aim);
        if (charging) chargeTime += static_cast<float>(elapsed);
        while (accumulator >= ricochet::kStep) {
            if (demo && (demoTicks == 24 || (demoRound && demoTicks % 60 == 24))) { game.setAim(0); game.setCharge(1); game.fire(); }
            game.tick(); accumulator -= ricochet::kStep; ++demoTicks;
        }

        ImGui_ImplSDL3_NewFrame(); ImGui_ImplSDLRenderer3_NewFrame(); ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(24, 20), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(410, 152), ImGuiCond_Always);
        ImGui::Begin("RICOCHET  /  RANGE 01", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove);
        ImGui::TextColored(ImVec4(0.31f, 0.93f, 0.83f, 1), "CANNONBALL CONTROL");
        ImGui::Text("SCORE   %05d       SHOTS   %02d", game.score(), game.shotsLeft());
        ImGui::Text("TARGETS IN GOAL   %02d / 15", game.targetsScored());
        ImGui::Text("%s", pad ? "GAMEPAD READY" : "KEYBOARD / GAMEPAD READY");
        ImGui::End();
        ImGui::SetNextWindowPos(ImVec2(24, 186), ImGuiCond_Always); ImGui::SetNextWindowSize(ImVec2(270, 110), ImGuiCond_Always);
        ImGui::Begin("SHOT POWER", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove);
        ImGui::ProgressBar(charging ? std::clamp(0.18f + chargeTime * 0.68f, 0.15f, 1.0f) : 0.0f, ImVec2(-1, 20), charging ? "CHARGING" : "HOLD TO CHARGE");
        ImGui::Text("AIM   %+.0f DEG", aim * 180 / kPi); ImGui::Text("FIRE  [SPACE]  /  [A]"); ImGui::End();
        if (showHelp) {
            ImGui::SetNextWindowPos(ImVec2(460, 20), ImGuiCond_Always); ImGui::SetNextWindowSize(ImVec2(400, 146), ImGuiCond_Always);
            ImGui::Begin("CONTROLS", &showHelp, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove);
            ImGui::Text("Arrows / left stick: aim"); ImGui::Text("Hold SPACE / A, release to fire"); ImGui::Text("R / B: restart the 12-shot round"); ImGui::Text("Menu button: toggle these controls"); ImGui::End();
        }
        ImGui::SetNextWindowPos(ImVec2(24, 624), ImGuiCond_Always); ImGui::SetNextWindowSize(ImVec2(500, 75), ImGuiCond_Always);
        ImGui::Begin("ACTIONS", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove);
        if (ImGui::Button("NEW ROUND  [R / B]", ImVec2(225, 32))) reset(); ImGui::SameLine();
        if (ImGui::Button(showHelp ? "HIDE CONTROLS" : "HOW TO PLAY", ImVec2(205, 32))) showHelp = !showHelp;
        ImGui::End();
        if (game.roundOver()) {
            ImGui::SetNextWindowPos(ImVec2(860, 600), ImGuiCond_Always); ImGui::SetNextWindowSize(ImVec2(390, 100), ImGuiCond_Always);
            ImGui::Begin("ROUND COMPLETE", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove);
            ImGui::Text("ROUND COMPLETE  //  %d POINTS", game.score()); ImGui::Text("Press R or choose NEW ROUND"); ImGui::End();
        }
        ImGui::Render();

        int w = 0, h = 0; SDL_GetRenderOutputSize(renderer, &w, &h);
        const float scale = std::min(w / 30.0f, h / 13.0f);
        const float ox = (w - 30 * scale) * 0.5f, oy = (h - 13 * scale) * 0.5f;
        auto sx = [&](float x) { return ox + x * scale; };
        auto sy = [&](float y) { return oy + (13.0f - y) * scale; };
        SDL_SetRenderDrawColorFloat(renderer, 0.018f, 0.032f, 0.055f, 1); SDL_RenderClear(renderer);
        fill(renderer, {sx(0.35f), sy(12.65f), 29.3f * scale, 12.3f * scale}, color(0.055f, 0.085f, 0.12f));
        for (int gx = 1; gx < 30; ++gx) line(renderer, sx(gx), sy(0.4f), sx(gx), sy(12.6f), color(0.12f, 0.17f, 0.20f, 0.45f));
        for (int gy = 1; gy < 13; ++gy) line(renderer, sx(0.4f), sy(gy), sx(29.6f), sy(gy), color(0.12f, 0.17f, 0.20f, 0.45f));
        fill(renderer, {sx(27.55f), sy(11.35f), 1.75f * scale, 8.7f * scale}, color(0.07f, 0.35f, 0.30f, 0.32f));
        line(renderer, sx(27.5f), sy(11.3f), sx(27.5f), sy(1.7f), color(0.21f, 0.92f, 0.70f), 3);
        fill(renderer, {sx(21.25f), sy(4.35f), 0.9f * scale, 2.3f * scale}, color(0.24f, 0.35f, 0.42f));
        fill(renderer, {sx(21.25f), sy(10.95f), 0.9f * scale, 2.3f * scale}, color(0.24f, 0.35f, 0.42f));
        for (const auto& t : game.targets()) {
            const float c = std::cos(t.angle), s = std::sin(t.angle);
            const SDL_FColor tint = t.scored ? color(0.18f, 0.95f, 0.69f) : color(0.92f, 0.58f, 0.25f);
            const b2Vec2 corners[] = {{-.29f, -.29f}, {.29f, -.29f}, {.29f, .29f}, {-.29f, .29f}};
            SDL_Vertex vertices[4];
            for (int i = 0; i < 4; ++i) {
                auto p = corners[i];
                vertices[i] = {{sx(t.position.x + p.x*c - p.y*s), sy(t.position.y + p.x*s + p.y*c)}, tint, {0, 0}};
            }
            const int indices[] = {0, 1, 2, 0, 2, 3};
            SDL_RenderGeometry(renderer, nullptr, vertices, 4, indices, 6);
        }
        for (b2Vec2 p : game.balls()) circle(renderer, sx(p.x), sy(p.y), scale * .29f, color(0.36f, 0.88f, 1));
        const float cx = sx(4.3f), cy = sy(6.5f), barrel = scale * .95f;
        line(renderer, cx, cy, cx + std::cos(aim) * barrel, cy - std::sin(aim) * barrel, color(0.8f, .91f, .94f), 8);
        circle(renderer, cx, cy, scale*.55f, color(.18f, .72f, .76f));
        line(renderer, cx, cy, cx + std::cos(aim)*scale*3, cy - std::sin(aim)*scale*3, color(.29f, .95f, .87f, .7f), 2);
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
        const bool demoFinished = demoRound ? game.roundOver() : demoTicks >= 240;
        if (demo && !screenshotPath.empty() && (demoRound ? demoFinished : demoTicks >= 105) && !screenshotSaved) {
            SDL_Surface* surface = SDL_RenderReadPixels(renderer, nullptr);
            if (surface) screenshotSaved = SDL_SaveBMP(surface, screenshotPath.c_str());
            SDL_DestroySurface(surface);
        }
        SDL_RenderPresent(renderer);
        if (demo && demoFinished) running = false;
    }
    if (pad) SDL_CloseGamepad(pad);
    ImGui_ImplSDLRenderer3_Shutdown(); ImGui_ImplSDL3_Shutdown(); ImGui::DestroyContext();
    SDL_DestroyRenderer(renderer); SDL_DestroyWindow(window); SDL_Quit();
    if (!screenshotPath.empty() && !screenshotSaved) { std::fprintf(stderr, "Screenshot was not saved\n"); return 1; }
    return 0;
}
