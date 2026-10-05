# NXT Agent chat on Xbox

A small live `gpt-6-luna` chat app built against the pinned
[NXT runtime and UI](https://github.com/mbrock/nxtui). It reuses NXT's plain
chat state/view, with an actual Responses transport in place of the upstream
demo's fixture controller.

```sh
./build nxt-chat
UWP_DEVICE_URL=https://your-xbox.example nix run .#deploy-nxt-chat
```

## Credentials and trust

The package contains a public Mozilla CA bundle, DejaVu Sans and its license,
and the `internetClient` capability. Certificate chain and hostname validation
are mandatory. No API key is read by Nix or embedded in the executable/MSIX.

The first launch without a key displays an Error state. Through Device Portal's
file explorer, upload a UTF-8 `openai.key` into this package's
`LocalState/nixbox/nxt-chat` directory, then restart the app. Protect the local
source file and delete disposable transfer copies; do not put it in the
repository, package assets, screenshots or logs. This is development-only
file provisioning, not a production credential-management system.

A provisioned launch sends a short greeting prompt automatically. Each launch
starts a new in-memory conversation; completed turns preserve the canonical
Responses input/output JSON, including opaque reasoning items. Nothing is
persisted as chat history, and requests use `store=false`.

## Controls and behavior

- Keyboard text entry; Enter sends, Shift+Enter adds a newline.
- Escape or the Stop button cancels the active request and waits for NXT to
  drain it before returning to Stopped. Send a new message to continue.
- Mouse wheel scrolls the transcript; click Send/Stop or the composer.
- Ctrl+V pastes; Backspace removes a UTF-8 code point (not a whole grapheme).

The UI and font shaping stay on the SDL thread. A worker owns its NXT deck,
network tasks and model history; a synchronized inbox publishes deltas and
completion status. Requests have a two-minute deadline. Stopped/failed turns
remain visible but are **not** added to the model's completed conversation
history. Closing requests cancellation and joins the drained worker.

This uses `run_agent` with an empty tool registry: real streamed chat and
multi-turn context, not yet filesystem/shell tools or a production agent
workspace. Gamepad navigation, on-screen text entry, suspend/resume and
long-running reliability are follow-ups.

## Diagnostics and build checks

`LocalState/nixbox/nxt-chat/chat-status.txt` records model, turn number,
delta counts, history-item counts and completion/cancellation/error status.
It does not log keys, prompts or replies.

The app is a CMake/pkg-config consumer with only `pkgsXbox.nxtui-sdl` as a
build input. Its runtime and static SDL/FreeType/HarfBuzz dependencies must
propagate from the libraries, not be listed privately in the application.

```sh
./build checks.x86_64-linux.nxtui-sdl-consumer
./build nxt-network
```

The first command cross-links a separate direct-pkg-config consumer, catching
archive naming and missing public dependencies that CMake's library lookup
can mask. It does not run that executable on the Linux builder.

The `nxt-network` SDL host runs NXT's installed-source probe unchanged. Its
default DNS/socket/cancellation tests need no credential. To run two real
Luna turns, provision `LocalState/nixbox/nxt-network/openai.key` and a
`probe-args.txt` containing just `--openai` plus newline, then restart. The
public CA is already packaged. Results appear on screen and in `network.txt`.
The diagnostic probe has passed those networking and real two-turn tests on
Xbox.

## Xbox verification

On Xbox Series X, the final pinned package passed a fresh streamed greeting
(9 deltas), keyboard/Enter submission and a context-dependent follow-up
answering **Xbox** (1 delta); completed model history grew from 0 to 3 to 6
items. Ready and missing-key Error screens were inspected.

The initial integration package additionally passed Backspace, pending-request
cancellation and mid-text cancellation after 115 deltas, with inspected Stopped
screens and successful requests after each drain. Cancelled turns left completed
history unchanged. These functional checks were **not rerun** on the final
package, whose upstream update only fixes static MSVC archive naming and a
neutral narrow-screen header.

![The final NXT chat package running on Xbox](../../docs/assets/nxt-chat.png)

Physical-controller navigation, mouse/touch actions, Unicode/IME input,
suspend/resume and soak testing remain unverified. Upstream's broader Linux
graphical Nix check also has known sandbox DNS/timing failures; the targeted
native UI tests and nixbox cross-build/consumer checks are separate evidence.
