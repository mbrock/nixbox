# Library-only package, promoted into the normal cross package set.
let
  flake = builtins.getFlake (toString ../..);
in
flake.legacyPackages.x86_64-linux.pkgsXbox.luau
