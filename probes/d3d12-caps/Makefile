# Inside `nix develop`: the `xbox` cross file targets the console.
#   make          build into build/
#   make deploy   package build/install and deploy it to $UWP_DEVICE_URL
.PHONY: all install deploy clean

all: build/build.ninja
	meson compile -C build

build/build.ninja:
	meson setup build --cross-file xbox --buildtype debugoptimized --prefix $(CURDIR)/build/install

install: all
	meson install -C build --quiet

deploy: install
	xbox-deploy --screenshot build/screenshot.png

clean:
	rm -rf build
