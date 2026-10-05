# Expected to fail at SBCL's Linux-host OS autodetection. See sbcl-cross.txt.
# nix build --impure --file nix/experiments/sbcl-cross.nix --keep-failed -L
let
  flake = builtins.getFlake (toString ../..);
  pkgs = flake.lib.x86_64-linux.pkgs;
  pkgsXbox = flake.legacyPackages.x86_64-linux.pkgsXbox;
  sbcl = pkgsXbox.sbcl.override {
    bootstrapLisp = "${pkgs.sbcl}/bin/sbcl --disable-debugger --no-userinit --no-sysinit";
    coreutils = pkgs.coreutils;
  };
in
sbcl.overrideAttrs (old: {
  # Avoid unrelated zstd/grep/PCRE2 cross builds while probing SBCL itself.
  coreCompression = false;
  # Permit this one experiment, without declaring Windows support globally.
  meta = old.meta // { platforms = [ "x86_64-windows" ]; };
})
