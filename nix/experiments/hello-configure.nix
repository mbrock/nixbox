# Run from the workspace root:
# nix build --impure --file nix/experiments/hello-configure.nix --keep-failed -L
# The linker setup and UWP source patches now live in the package set.
let
  flake = builtins.getFlake (toString ../..);
  pkgsXbox = flake.legacyPackages.x86_64-linux.pkgsXbox;
in
pkgsXbox.hello
