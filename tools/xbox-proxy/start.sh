#!/bin/sh
set -eu
state="$HOME/.local/state/xbox-proxy"
if [ -f "$state/bootstrap-auth" ]; then
  TS_AUTHKEY=$(cat "$state/bootstrap-auth")
  export TS_AUTHKEY
fi
exec "$HOME/.local/bin/xbox-proxy" \
  -state "$state/tsnet" \
  -backend https://temple.whale-justice.ts.net:8443
