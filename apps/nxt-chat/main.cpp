#include "view.hpp"
#include <nxtui/sdl.hpp>
#include <nxtai/agent.hpp>
#include <nxtai/responses_transport.hpp>
#include <nxtrt/arch.hpp>

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <atomic>
#include <cstdio>
#include <fstream>
#include <mutex>
#include <optional>
#include <thread>

namespace {
using namespace std::chrono_literals;
namespace chat = nxtui::chat;

void check(bool ok)
{
    if (!ok) throw std::runtime_error{SDL_GetError()};
}

std::string read_key(const std::string & path)
{
    std::ifstream file{path, std::ios::binary};
    std::string key{std::istreambuf_iterator<char>{file}, {}};
    while (!key.empty() && (key.back() == '\n' || key.back() == '\r'))
        key.pop_back();
    return key;
}

// Networking owns its deck and canonical model history on a worker. Only
// deltas/status cross the inbox; all SDL, shaping and view state stay on UI.
class Session
{
    std::string key;
    nxtai::responses_transport transport;
    std::ofstream log;
    std::thread worker;
    std::atomic_bool stop_requested{false};
    std::mutex mutex;
    std::vector<std::string> deltas;
    std::optional<std::pair<chat::Status, std::string>> completion;
    std::vector<nxtai::openai::raw_json> history, completed;
    std::size_t delta_count = 0, turn_count = 0;

    nxtrt::task<void> wait_for_stop()
    {
        while (!stop_requested.load())
            co_await nxtrt::op::timeout::after(5ms);
    }

    nxtrt::task<void> turn(std::string prompt)
    {
        auto request = nxtai::responses::openai_responses_request{
            .api_key = key, .model = "gpt-6-luna", .input = std::move(prompt),
            .include = {"reasoning.encrypted_content"}};
        request.input_items = history;
        // Build the new user item with NXT's JSON writer, including escaping.
        auto user = nxtai::responses::input_items_from_request(
            {.input = request.input});
        request.input_items.push_back(std::move(user.front()));
        const nxtai::tools::tool_registry tools{};
        co_await nxtai::run_agent(std::move(request), tools, *this, *this);
        history = std::move(completed);
    }

    nxtrt::task<void> controlled_turn(std::string prompt)
    {
        auto [response, stopper] = co_await nxtrt::settle(
            std::tuple{turn(std::move(prompt)), wait_for_stop()},
            nxtrt::first_completion_group{});
        if (!response) nxtrt::rethrow(response.error());
    }

public:
    Session(std::string key, std::string ca, std::string report)
        : key(std::move(key)), transport({.ca_file = std::move(ca)}),
          log(std::move(report))
    {
        log << "NXT Xbox graphical chat / gpt-6-luna\n" << std::flush;
    }

    ~Session()
    {
        stop();
        if (worker.joinable()) worker.join();
    }

    bool has_key() const { return !key.empty(); }
    void stop() { stop_requested.store(true); }

    nxtrt::task<void> text(std::string delta)
    {
        ++delta_count;
        std::lock_guard lock{mutex};
        deltas.push_back(std::move(delta));
        co_return;
    }

    nxtrt::task<void> tool_started(const nxtai::tools::function_call &)
    { co_return; }
    nxtrt::task<void> tool_finished(const nxtai::tools::function_call_result &)
    { co_return; }

    nxtrt::task<nxtai::responses::response_result> operator()(
        nxtai::responses::openai_responses_request request, Session & observer)
    {
        auto response = co_await transport(request, observer);
        completed = nxtai::responses::input_items_from_request(request);
        for (const auto & item : response.output_items)
            completed.push_back(item); // Includes opaque reasoning, not only text.
        co_return response;
    }

    void send(chat::State & state)
    {
        if (state.status == chat::Status::streaming || state.composer.empty()) return;
        if (!has_key()) {
            state.status = chat::Status::error;
            state.detail = "Add openai.key to LocalState and restart";
            return;
        }
        if (worker.joinable()) worker.join();
        stop_requested.store(false);
        delta_count = 0;
        completed.clear();
        auto prompt = std::exchange(state.composer, {});
        state.messages.push_back({false, prompt});
        state.messages.push_back({true, {}});
        state.status = chat::Status::streaming;
        state.detail = "GPT-6 Luna · connecting / streaming";
        state.scroll_from_bottom = {};
        worker = std::thread{[this, prompt = std::move(prompt)] {
            auto status = chat::Status::ready;
            std::string detail = "GPT-6 Luna · connected";
            log << "TURN " << ++turn_count << ": begin; history items "
                << history.size() << '\n' << std::flush;
            try {
                auto wand = nxtrt::arch::wand{};
                auto deck = nxtrt::deck{&wand};
                auto root = nxtrt::root_task{deck, [&] {
                    return nxtrt::with_timeout(120s, controlled_turn(prompt));
                }};
                root.start();
                wand.run_until_done(deck, root.inner());
                std::move(root.inner()).result();
            } catch (const nxtrt::operation_cancelled &) {
                status = chat::Status::stopped;
                detail = "Stopped · unfinished turn not retained by model";
            } catch (const nxtai::responses_http_error & error) {
                status = chat::Status::error;
                detail = "OpenAI HTTP " + std::to_string(error.status);
            } catch (const std::exception & error) {
                status = chat::Status::error;
                detail = error.what();
            }
            log << "TURN " << turn_count << ": "
                << (status == chat::Status::ready ? "completed" :
                    status == chat::Status::stopped ? "cancelled and drained" : "error")
                << "; deltas " << delta_count << "; history items "
                << history.size() << '\n' << std::flush;
            std::lock_guard lock{mutex};
            completion = {status, std::move(detail)};
        }};
    }

    void tick(chat::State & state)
    {
        std::lock_guard lock{mutex};
        for (auto & delta : deltas) state.messages.back().text += delta;
        deltas.clear();
        if (completion) {
            state.status = completion->first;
            state.detail = std::move(completion->second);
            completion.reset();
        }
    }
};

void backspace(std::string & text)
{
    if (text.empty()) return;
    auto start = text.size() - 1;
    while (start > 0 && (static_cast<unsigned char>(text[start]) & 0xc0) == 0x80) --start;
    text.resize(start);
}

int run()
{
    auto window = std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)>{
        SDL_CreateWindow("NXT Agent chat", 1920, 1080, SDL_WINDOW_FULLSCREEN), SDL_DestroyWindow};
    check(window != nullptr);
    auto renderer = std::unique_ptr<SDL_Renderer, decltype(&SDL_DestroyRenderer)>{
        SDL_CreateRenderer(window.get(), nullptr), SDL_DestroyRenderer};
    check(renderer != nullptr);
    SDL_SetRenderVSync(renderer.get(), 1);
    auto pref = std::unique_ptr<char, decltype(&SDL_free)>{
        SDL_GetPrefPath("nixbox", "nxt-chat"), SDL_free};
    check(pref != nullptr);
    const std::string directory{pref.get()}, base{SDL_GetBasePath()};
    int width = 0, height = 0;
    check(SDL_GetRenderOutputSize(renderer.get(), &width, &height));
    nxtui::ui::SdlPainter painter{renderer.get(), base + "font.ttf", 30.f * height / 1080.f};
    Session session{read_key(directory + "openai.key"), base + "ca-bundle.pem", directory + "chat-status.txt"};
    chat::State state;
    state.detail = "GPT-6 Luna · runtime key required";
    state.composer = "Say hello from Xbox in two short sentences.";
    // A provisioned app opens with a genuine streamed greeting, never fixture text.
    if (session.has_key()) session.send(state);
    else {
        state.status = chat::Status::error;
        state.detail = "Add openai.key to LocalState and restart";
    }
    check(SDL_StartTextInput(window.get()));
    chat::Hits hits;
    for (bool running = true; running;) {
        SDL_Event event{};
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) running = false;
            else if (event.type == SDL_EVENT_TEXT_INPUT) {
                state.composer += event.text.text;
                state.preedit.clear();
            } else if (event.type == SDL_EVENT_TEXT_EDITING) state.preedit = event.edit.text;
            else if (event.type == SDL_EVENT_MOUSE_WHEEL)
                state.scroll_from_bottom = std::max(nxtui::ui::Height{},
                    state.scroll_from_bottom + event.wheel.y * 3 * nxtui::ui::ln);
            else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT) {
                const auto point = painter.position(event.button.x, event.button.y);
                if (hits.action.contains(point)) {
                    if (state.status == chat::Status::streaming) session.stop();
                    else session.send(state);
                } else if (hits.composer.contains(point)) check(SDL_StartTextInput(window.get()));
            } else if (event.type == SDL_EVENT_KEY_DOWN) {
                if (event.key.key == SDLK_ESCAPE) session.stop();
                else if (event.key.key == SDLK_BACKSPACE) backspace(state.composer);
                else if (event.key.key == SDLK_RETURN && state.preedit.empty()) {
                    if (event.key.mod & SDL_KMOD_SHIFT) state.composer += '\n';
                    else session.send(state);
                } else if (event.key.key == SDLK_V && (event.key.mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI))) {
                    auto clipboard = std::unique_ptr<char, decltype(&SDL_free)>{SDL_GetClipboardText(), SDL_free};
                    if (clipboard) state.composer += clipboard.get();
                }
            }
        }
        session.tick(state);
        auto frame = nxtui::ui::paint(chat::view(state, hits), painter, painter.viewport());
        painter.draw(frame);
        check(SDL_RenderPresent(renderer.get()));
        SDL_Delay(16);
    }
    return 0;
}
} // namespace

int main(int, char **)
{
    int result = 1;
    try {
        check(SDL_Init(SDL_INIT_VIDEO));
        check(TTF_Init());
        result = run();
    } catch (const std::exception & error) {
        std::fprintf(stderr, "NXT chat startup: %s\n", error.what());
    }
    TTF_Quit();
    SDL_Quit();
    return result;
}
