# Check SDK header usability without contaminating Autoconf's negative probes.
{ pkgs, llvmPackages, compiler }:
pkgs.runCommand "xbox-compiler-check" { } ''
  cat > cpp.cpp <<'EOF'
  #include <cstdlib>
  #include <stdexcept>
  #include <vector>
  #ifdef _CRT_USE_WINAPI_FAMILY_DESKTOP_APP
  #error The UWP compiler must not reopen the desktop CRT partition
  #endif
  struct Guard {
    int& count;
    ~Guard() { ++count; }
  };
  __declspec(noinline) void throwThroughGuard(int& count) {
    Guard guard{count};
    throw std::runtime_error("SDK exception runtime");
  }
  int main() {
    std::vector<int> values{2, 3};
    int destroyed = 0;
    try {
      throwThroughGuard(destroyed);
      return 1;
    } catch (const std::runtime_error&) {
      return destroyed == 1 ? std::abs(-5) - values[0] - values[1] : 2;
    }
  }
  EOF
  ${compiler}/bin/x86_64-pc-windows-msvc-clang++ -std=c++17 cpp.cpp -o cpp.exe
  ${llvmPackages.llvm}/bin/llvm-readobj --file-headers cpp.exe > headers.txt
  grep -q IMAGE_FILE_MACHINE_AMD64 headers.txt

  printf 'int main(void) { (void) strchr; return 0; }\n' > undeclared.c
  for driver in clang clang++; do
    if ${compiler}/bin/x86_64-pc-windows-msvc-$driver -fno-builtin -c undeclared.c -o probe.obj 2> probe.log; then
      echo "Unexpected strchr declaration with $driver" >&2
      exit 1
    fi
    grep -q "undeclared identifier 'strchr'" probe.log
  done
  mkdir -p "$out"
  cp cpp.exe headers.txt "$out/"
''
