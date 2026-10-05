# libssh2 on Xbox

`pkgsXbox.libssh2` is a static MSVC-ABI/UWP build of Nixpkgs' libssh2 1.11.1
with its pinned security backports. It uses the existing UWP OpenSSL libcrypto
and zlib, not libssl or WinCNG. Desktop Pageant/named-pipe authentication agents
are disabled. Applications own socket creation and host-key policy.

```sh
./build libssh2-xbox
./build checks.x86_64-linux.libssh2-consumer
./build ssh-probe
nix run .#deploy-ssh-probe
```

The isolated consumer uses only libssh2 in `buildInputs`. It checks both
`pkg-config --cflags --libs --static libssh2` and CMake's
`find_package(Libssh2 REQUIRED)` / `Libssh2::libssh2`, including propagated
crypto/zlib dependencies. Its `ssh-probe.exe` and `ssh-cmake.exe` are portable
CLIs, not Xbox entry points. The `ssh-probe` app packages the same source in
an SDL3 host, with `internetClient` and `privateNetworkClientServer` capabilities.

## Disposable SSH fixture

Run `fixture.py` with Python and Paramiko (for example in a Nix shell with
`python3.withPackages (p: [ p.paramiko ])`):

```sh
python fixture.py --directory /private/ssh-fixture --bind 127.0.0.1
ssh-probe.exe 127.0.0.1 22222 /private/ssh-fixture/known_hosts /private/ssh-fixture/id_rsa fixture
```

Use Windows paths under Wine. For Xbox, run the fixture on an IPv4 address
reachable from the console, use `--bind SERVER_IP --host SERVER_IP`, and ensure
its port is already permitted by the machine's firewall. A runner's tailnet reachability does **not**
imply the Xbox can reach tailnet addresses. The fixture accepts only its fresh
test key/user, implements a fixed command response and an in-memory SFTP file,
and never executes incoming commands. Run it in the foreground or through the
environment's supervised service manager.

The probe expects exact stdout, separate stderr and exit status 7, then uploads,
downloads and deletes a binary SFTP payload containing NUL and 0xff. Re-run
with `--file-auth` to exercise file-backed PEM parsing against no-stdio
libcrypto. Repeat with `wrong_hosts` and `unknown_hosts`: both must exit 2 with
`REJECTED host key before authentication`, with **no new `AUTH` lines** in the
server log. A correct key exits 0. Other errors exit 1.

## Runtime provisioning on Xbox

The app reads `probe-args.txt` from the directory returned by
`SDL_GetPrefPath("nixbox", "ssh-probe")`, beneath its package LocalState. Read
`ssh.txt` there for the report. Provision one argument per line:

```text
SERVER_IP
22222
ABSOLUTE_LOCALSTATE_PATH\known_hosts
ABSOLUTE_LOCALSTATE_PATH\id_rsa
fixture
```

Append `--file-auth` for that mode; replace the host file path for rejection
tests. Provision keys/host files through Device Portal at runtime, **never**
in the package or Nix inputs. Keep temporary private keys mode 0600 in private
0700 directories. Delete test keys from both the console and fixture machine,
and stop the fixture when verification is finished.

Matching memory- and file-backed RSA authentication, exact command results and
binary SFTP passed on real Xbox Series X on 2026-10-05 against a disposable
fixture on routed public IPv4. Wrong and unknown host keys were rejected with
no new server authentication events. The fixture was stopped and its keys
removed from the runner and console. This does not verify private-LAN routing.

## Boundaries

The probe uses blocking calls on a worker thread, a 15-second libssh2 session
timeout and numeric IPv4;
it is not an SSH terminal app or a coroutine-runtime integration. libssh2's
nonblocking API is available, but requires an explicit readiness/IOCP adapter
before it can be driven by NXT. Host-key verification must precede any
authentication in real consumers too. Suspend/resume, long sessions, PTYs,
interactive shells and controller input are not covered by these smoke tests.
