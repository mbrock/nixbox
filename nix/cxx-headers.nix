# SDK C++ header adaptations shared by cross-built packages. This overlay
# changes headers only when explicitly included; it must not force declarations
# into configure probes or restore APIs hidden by the UWP partition.
{ pkgs, sdk }:
pkgs.runCommand "xbox-cxx-headers" { } ''
  mkdir -p "$out/include"
  cp ${sdk}/crt/include/cstdlib "$out/include/cstdlib"
  chmod u+w "$out/include/cstdlib"
  substituteInPlace "$out/include/cstdlib" \
    --replace-fail '_EXPORT_STD using _CSTD getenv;' '#ifdef _CRT_USE_WINAPI_FAMILY_DESKTOP_APP
  _EXPORT_STD using _CSTD getenv;
  #endif' \
    --replace-fail '_EXPORT_STD using _CSTD system;' '#ifdef _CRT_USE_WINAPI_FAMILY_DESKTOP_APP
  _EXPORT_STD using _CSTD system;
  #endif'
''
