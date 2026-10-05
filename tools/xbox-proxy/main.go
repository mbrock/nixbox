package main

import (
	"context"
	"crypto/tls"
	"encoding/json"
	"errors"
	"flag"
	"log"
	"net/http"
	"net/http/httputil"
	"net/url"
	"os"
	"os/signal"
	"path/filepath"
	"syscall"
	"time"

	"tailscale.com/tsnet"
)

func main() {
	state := flag.String("state", "", "private directory for durable Tailscale identity")
	backend := flag.String("backend", "https://temple.whale-justice.ts.net:8443", "Xbox portal URL")
	local := flag.String("local", "", "serve plain HTTP on this address instead of the Tailscale "+
		"node, forwarding without a login (for testing in front of another proxy)")
	flag.Parse()
	if *local != "" {
		serveLocally(*local, *backend)
		return
	}
	if *state == "" {
		log.Fatal("state is required")
	}
	target, err := url.Parse(*backend)
	if err != nil {
		log.Fatal(err)
	}
	if err := os.MkdirAll(*state, 0700); err != nil {
		log.Fatal(err)
	}
	var login struct{ Username, Password string }
	credentials, err := os.ReadFile(filepath.Join(filepath.Dir(*state), "xbox-login.json"))
	if err != nil {
		log.Fatal("read Xbox login: ", err)
	}
	if err := json.Unmarshal(credentials, &login); err != nil {
		log.Fatal("parse Xbox login: ", err)
	}
	if login.Username == "" || login.Password == "" {
		log.Fatal("Xbox login is incomplete")
	}
	node := &tsnet.Server{Dir: *state, Hostname: "xbox", AuthKey: os.Getenv("TS_AUTHKEY")}
	os.Unsetenv("TS_AUTHKEY")
	defer node.Close()
	listener, err := node.ListenTLS("tcp", ":443")
	if err != nil {
		log.Fatal(err)
	}
	defer listener.Close()
	proxy := &httputil.ReverseProxy{Rewrite: func(r *httputil.ProxyRequest) {
		r.SetURL(target)
		r.SetXForwarded()
		r.Out.SetBasicAuth(login.Username, login.Password)
		if origin := r.In.Header.Get("Origin"); origin != "" && origin == "https://"+r.In.Host {
			r.Out.Header.Set("Origin", target.String())
		}
	}}
	transport := http.DefaultTransport.(*http.Transport).Clone()
	transport.DialContext = node.Dial
	transport.Proxy = nil
	transport.TLSClientConfig = &tls.Config{MinVersion: tls.VersionTLS12}
	proxy.Transport = transport
	// Who is asking, by Tailscale login and machine, for lease records.
	tailnet, err := node.LocalClient()
	if err != nil {
		log.Fatal(err)
	}
	whoIs := func(r *http.Request) string {
		who, err := tailnet.WhoIs(r.Context(), r.RemoteAddr)
		if err != nil || who.Node == nil {
			return r.RemoteAddr
		}
		login := "?"
		if who.UserProfile != nil {
			login = who.UserProfile.LoginName
		}
		return login + " on " + who.Node.ComputedName
	}
	server := &http.Server{Handler: newLeases(whoIs).routes(proxy),
		ReadHeaderTimeout: 15 * time.Second}
	ctx, stop := signal.NotifyContext(context.Background(), os.Interrupt, syscall.SIGTERM)
	defer stop()
	go func() {
		<-ctx.Done()
		shutdown, cancel := context.WithTimeout(context.Background(), 10*time.Second)
		defer cancel()
		server.Shutdown(shutdown)
	}()
	log.Print("Xbox proxy serving on its private Tailscale identity, HTTPS port 443, with the console lease")
	if err := server.Serve(listener); err != nil && !errors.Is(err, http.ErrServerClosed) {
		log.Fatal(err)
	}
}

// serveLocally runs the lease and the proxy on a plain local address, for
// trying changes in front of the running proxy, which adds the login.
func serveLocally(address, backend string) {
	target, err := url.Parse(backend)
	if err != nil {
		log.Fatal(err)
	}
	proxy := &httputil.ReverseProxy{Rewrite: func(r *httputil.ProxyRequest) {
		r.SetURL(target)
		r.Out.Host = target.Host
	}}
	who := func(r *http.Request) string { return r.RemoteAddr }
	log.Printf("Xbox proxy with the console lease on http://%s, forwarding to %s", address, backend)
	log.Fatal(http.ListenAndServe(address, newLeases(who).routes(proxy)))
}
