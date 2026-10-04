#include "pch.h"

#include "MainPage.h"
#include "D3DView.h"
#include <winrt/Windows.UI.Xaml.Controls.Primitives.h>
#include <zlib.h>
#include <array>
#include <cstring>
#include <string>
#include <memory>
#include <Luau/Compiler.h>
#include <lua.h>
#include <lualib.h>

using namespace winrt;
using namespace winrt::Windows::UI::Xaml;
using namespace winrt::Windows::UI::Xaml::Controls;
using namespace winrt::Windows::UI::Xaml::Media;

namespace hello {

namespace {
int applyDamage(lua_State* state) {
    auto* calls = static_cast<int*>(lua_touserdata(state, lua_upvalueindex(1)));
    ++*calls;
    lua_pushnumber(state, luaL_checknumber(state, 1) * 2);
    return 1;
}

struct ScriptResult { bool passed; std::string message; double health = 0; };

ScriptResult runGameplayScript() {
    int nativeCalls = 0;
    std::unique_ptr<lua_State, decltype(&lua_close)> state{luaL_newstate(), &lua_close};
    if (!state)
        return {false, "Luau: could not allocate VM"};
    luaL_openlibs(state.get());
    lua_pushlightuserdata(state.get(), &nativeCalls);
    lua_pushcclosure(state.get(), applyDamage, "apply_damage", 1);
    lua_setglobal(state.get(), "apply_damage");
    luaL_sandbox(state.get());
    luaL_sandboxthread(state.get());
    const auto bytecode = Luau::compile(R"(
        local health = 100
        for _, hit in {7, 11, 9} do
            health -= apply_damage(hit)
        end
        return health
    )");
    if (luau_load(state.get(), "xbox-gameplay", bytecode.data(), bytecode.size(), 0) != 0 ||
        lua_pcall(state.get(), 0, 1, 0) != 0) {
        const char* error = lua_tostring(state.get(), -1);
        return {false, std::string("Luau failed: ") + (error ? error : "unknown error")};
    }
    const double health = lua_tonumber(state.get(), -1);
    const bool passed = lua_type(state.get(), -1) == LUA_TNUMBER &&
                        health == 46 && nativeCalls == 3;
    return {passed, passed ? "Luau on Xbox: health = 46, native callbacks = 3 — PASS"
                           : "Luau on Xbox: gameplay check FAILED", health};
}
} // namespace

MainPage::~MainPage() = default;

MainPage::MainPage() {
    TextBlock title;
    title.Text(L"Built on Linux");
    title.FontSize(36);
    title.HorizontalAlignment(HorizontalAlignment::Center);
    title.Foreground(SolidColorBrush{winrt::Windows::UI::Colors::White()});

    TextBlock subtitle;
    subtitle.Text(L"clang-cl + lld-link, packaged and signed by openappx");
    subtitle.FontSize(16);
    subtitle.HorizontalAlignment(HorizontalAlignment::Center);
    subtitle.Foreground(SolidColorBrush{winrt::Windows::UI::Colors::Gray()});

    // Exercise the cross-built library on the console, including both paths
    // and a byte-for-byte comparison of the decompressed payload.
    constexpr char payload[] = "Nix cross packages on Xbox: compress, decompress, repeat!";
    std::array<Bytef, 256> compressed{};
    std::array<Bytef, sizeof(payload)> restored{};
    uLongf compressedSize = compressed.size();
    uLongf restoredSize = restored.size();
    const bool passed =
        compress2(compressed.data(), &compressedSize,
                  reinterpret_cast<Bytef const*>(payload), sizeof(payload), Z_BEST_COMPRESSION) == Z_OK &&
        uncompress(restored.data(), &restoredSize, compressed.data(), compressedSize) == Z_OK &&
        restoredSize == sizeof(payload) && std::memcmp(restored.data(), payload, sizeof(payload)) == 0;
    TextBlock result;
    const std::string message = std::string("pkgsXbox zlib ") + zlibVersion() +
        (passed ? ": compression round-trip passed" : ": compression round-trip FAILED");
    result.Text(winrt::to_hstring(message));
    result.FontSize(18);
    result.HorizontalAlignment(HorizontalAlignment::Center);
    result.Foreground(SolidColorBrush{passed ? winrt::Windows::UI::Colors::LightGreen()
                                             : winrt::Windows::UI::Colors::Red()});

    const auto script = runGameplayScript();
    TextBlock scriptResult;
    scriptResult.Text(winrt::to_hstring(script.message));
    scriptResult.FontSize(18);
    scriptResult.HorizontalAlignment(HorizontalAlignment::Center);
    scriptResult.Foreground(SolidColorBrush{script.passed ? winrt::Windows::UI::Colors::LightGreen()
                                                        : winrt::Windows::UI::Colors::Red()});
    ProgressBar health;
    health.Minimum(0);
    health.Maximum(100);
    health.Value(script.health);
    health.Width(520);
    health.HorizontalAlignment(HorizontalAlignment::Center);

    SwapChainPanel view;
    view.Width(520);
    view.Height(220);
    view.HorizontalAlignment(HorizontalAlignment::Center);
    TextBlock graphicsStatus;
    graphicsStatus.Text(L"Starting Direct3D…");
    graphicsStatus.FontSize(16);
    graphicsStatus.HorizontalAlignment(HorizontalAlignment::Center);
    m_graphics = std::make_unique<D3DView>(view, graphicsStatus);

    StackPanel panel;
    panel.VerticalAlignment(VerticalAlignment::Center);
    panel.Spacing(10);
    panel.Children().Append(title);
    panel.Children().Append(subtitle);
    panel.Children().Append(view);
    panel.Children().Append(graphicsStatus);
    panel.Children().Append(result);
    panel.Children().Append(scriptResult);
    panel.Children().Append(health);

    Grid root;
    root.Background(SolidColorBrush{winrt::Windows::UI::ColorHelper::FromArgb(255, 14, 17, 22)});
    root.Children().Append(panel);

    m_root = Page{};
    m_root.Content(root);
}

} // namespace hello
