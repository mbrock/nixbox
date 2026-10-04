# Optional binary cache

[Back to the README](../README.md)

Source builds work without cache credentials. The maintainer's authenticated
cache lets authorized machines reuse the extracted SDK, toolchain, libraries,
and app builds:

```text
https://nix.swa.sh/xbox-cache/
Signing key: xbox-uwp-1:YMEqS8rfsKIOyXplusBbWtNUuKTNikZqw7X/BtW3ijQ=
```

The endpoint uses HTTPS and Basic Auth, with directory listing disabled. Nix
verifies signatures against the public key pinned in the flake. SDK outputs
are kept separate from public caches and are not uploaded to Cachix.

## Using the cache

Keep the supplied HTTP credentials in a netrc file outside the repository:

```text
${XDG_CONFIG_HOME:-$HOME/.config}/xbox-nix-cache/netrc
```

`./build`, `./env`, and `./cache-publish` use it when present. Set
`UWP_NIX_CACHE_NETRC` to override the path. Without that file, the wrappers
build from source with normal Nix inputs.

On a cloud agent, supply netrc as a secret file with mode `0600`:

```sh
UWP_NIX_CACHE_NETRC=/run/secrets/xbox-cache.netrc ./build
UWP_NIX_CACHE_NETRC=/run/secrets/xbox-cache.netrc ./env
```

For a direct Nix command:

```sh
nix build --accept-flake-config \
  --option netrc-file /run/secrets/xbox-cache.netrc .#hello-uwp
```

Cloud agents need only the HTTP credential. They do not need the cache's signing
private key or an application signing key to substitute build outputs.

## Publishing from the cache host

```sh
./cache-publish
```

The publisher builds `cache-roots`, signs the complete runtime closures, and
copies them to the cache directory. Application signing keys and Wine prefixes
are excluded.

| Setting | Default |
| --- | --- |
| `UWP_NIX_CACHE_DIR` | `/var/www/xbox-nix-cache` |
| `UWP_NIX_CACHE_KEY` | `${XDG_CONFIG_HOME:-$HOME/.config}/xbox-nix-cache/cache.secret` |

Keep the private cache signing key outside the repository and Nix store.
