# Inside `nix develop`: CMake already targets the Xbox (CMAKE_TOOLCHAIN_FILE).
#   make          build into build/
#   make deploy   package build/install and deploy it to $UWP_DEVICE_URL
.PHONY: all install deploy clean

all: build/build.ninja
	cmake --build build

build/build.ninja:
	cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo

install: all
	cmake --install build --prefix build/install

deploy: install
	xbox-deploy --screenshot build/screenshot.png

clean:
	rm -rf build
