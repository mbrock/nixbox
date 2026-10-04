# Turn an install prefix into a package directory: OUT/layout, OUT/<pname>.msix,
# and OUT/symbols for any PDBs. Shared by mkXboxApp's Nix build and the dev
# shell's xbox-package; both supply the package description through XBOX_*.
#
#   package.sh PREFIX OUT
#
# PREFIX holds bin/ and optionally share/<pname>/, which become the package
# root. An existing OUT is replaced only if it looks like an earlier package.
set -euo pipefail
prefix=$1
out=$2
: "${XBOX_PNAME:?}" "${XBOX_EXECUTABLE:?}" "${XBOX_MANIFEST:?}" "${XBOX_ASSETS:?}" "${XBOX_AUDIT:?}"

[[ -f "$prefix/bin/$XBOX_EXECUTABLE" ]] || {
  echo "No $prefix/bin/$XBOX_EXECUTABLE; install the build into $prefix first" >&2
  exit 1
}
if [[ -e "$out" ]]; then
  [[ -f "$out/layout/AppxManifest.xml" ]] || {
    echo "Refusing to replace $out, which is not an earlier package" >&2
    exit 1
  }
  rm -rf "$out"
fi

mkdir -p "$out/layout" "$out/symbols"
layout=$out/layout
cp -r "$prefix/bin/." "$layout/"
if [[ -d "$prefix/share/$XBOX_PNAME" ]]; then
  cp -r "$prefix/share/$XBOX_PNAME/." "$layout/"
fi
# Debug symbols stay out of the package, beside it for crash dumps.
find "$layout" -name '*.pdb' -exec mv -t "$out/symbols" {} +
rmdir --ignore-fail-on-non-empty "$out/symbols"
cp "$XBOX_MANIFEST" "$layout/AppxManifest.xml"
mkdir -p "$layout/Assets"
cp -r "$XBOX_ASSETS/." "$layout/Assets/"
chmod -R u+w "$layout"

# The app container wants the GUI subsystem at version 6.02 or later; set it
# here so the build may link an ordinary main() or wWinMain().
llvm-objcopy --subsystem windows:6.2 "$layout/$XBOX_EXECUTABLE"
bash "$XBOX_AUDIT" --allow-kernel32 "$layout/$XBOX_EXECUTABLE"
openappx validate --root "$layout"
openappx pack --root "$layout" --out "$out/$XBOX_PNAME.msix"
