package main

// The console lease: one client at a time may change what runs on the Xbox.
//
// Agents share one console, and an install or launch from one replaces the
// app another is measuring. A lease names the client allowed to send
// changing requests (anything but GET, HEAD, and OPTIONS) to the portal;
// others wait in a first-come queue. Reads stay open to everyone, and while
// no one holds the lease, requests pass as before.
//
//	POST /lease/acquire?label=L&ttl=SECONDS[&ticket=T][&wait=SECONDS]
//	  200 {"token", "label", "who", "expires"} once granted, or
//	  202 {"ticket", "position", "holder"} while queued: poll again with the
//	  ticket within 45 seconds to keep the place.
//	POST /lease/renew?token=T[&ttl=SECONDS]
//	POST /lease/release?token=T
//	GET  /lease  the holder and the queue
//
// A leased request carries the token in the X-Xbox-Lease header, which the
// proxy removes before forwarding. Leases last ttl seconds (default 600,
// at most 1800) unless renewed. State lives in memory.

import (
	"crypto/rand"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"net/http"
	"strconv"
	"sync"
	"time"
)

const (
	leaseHeader     = "X-Xbox-Lease"
	defaultTTL      = 10 * time.Minute
	maximumTTL      = 30 * time.Minute
	abandonedTicket = 45 * time.Second
	maximumWait     = 25 * time.Second
)

type lease struct {
	Token   string    `json:"token,omitempty"`
	Label   string    `json:"label"`
	Who     string    `json:"who"`
	Since   time.Time `json:"since"`
	Expires time.Time `json:"expires"`
}

type ticket struct {
	ID       string        `json:"ticket"`
	Label    string        `json:"label"`
	Who      string        `json:"who"`
	Since    time.Time     `json:"since"`
	ttl      time.Duration // requested lease length
	lastSeen time.Time
}

type leases struct {
	mu      sync.Mutex
	holder  *lease
	queue   []*ticket
	changed chan struct{} // closed and replaced on every change
	whoIs   func(*http.Request) string
	now     func() time.Time
}

func newLeases(whoIs func(*http.Request) string) *leases {
	l := &leases{changed: make(chan struct{}), queue: []*ticket{},
		whoIs: whoIs, now: time.Now}
	go func() {
		for range time.Tick(time.Second) {
			l.mu.Lock()
			l.settle()
			l.mu.Unlock()
		}
	}()
	return l
}

func randomID() string {
	b := make([]byte, 16)
	rand.Read(b)
	return hex.EncodeToString(b)
}

// settle expires the holder and abandoned tickets and grants the lease to
// the first ticket when the console is free. Callers hold mu.
func (l *leases) settle() {
	now := l.now()
	changed := false
	if l.holder != nil && now.After(l.holder.Expires) {
		l.holder = nil
		changed = true
	}
	kept := l.queue[:0]
	for _, t := range l.queue {
		if now.Sub(t.lastSeen) < abandonedTicket {
			kept = append(kept, t)
		} else {
			changed = true
		}
	}
	l.queue = kept
	if changed {
		close(l.changed)
		l.changed = make(chan struct{})
	}
}

func (l *leases) notify() {
	close(l.changed)
	l.changed = make(chan struct{})
}

func ttlFrom(r *http.Request) time.Duration {
	seconds, err := strconv.Atoi(r.URL.Query().Get("ttl"))
	if err != nil || seconds <= 0 {
		return defaultTTL
	}
	return min(time.Duration(seconds)*time.Second, maximumTTL)
}

// grant makes the ticket's client the holder. Callers hold mu.
func (l *leases) grant(t *ticket) *lease {
	now := l.now()
	l.holder = &lease{Token: randomID(), Label: t.Label, Who: t.Who,
		Since: now, Expires: now.Add(t.ttl)}
	for i, q := range l.queue {
		if q == t {
			l.queue = append(l.queue[:i], l.queue[i+1:]...)
			break
		}
	}
	l.notify()
	return l.holder
}

func (l *leases) position(t *ticket) int {
	for i, q := range l.queue {
		if q == t {
			return i + 1
		}
	}
	return 0
}

func (l *leases) find(id string) *ticket {
	for _, t := range l.queue {
		if t.ID == id {
			return t
		}
	}
	return nil
}

func (l *leases) public(h *lease) *lease {
	if h == nil {
		return nil
	}
	c := *h
	c.Token = ""
	return &c
}

func writeJSON(w http.ResponseWriter, status int, value any) {
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(status)
	json.NewEncoder(w).Encode(value)
}

func (l *leases) acquire(w http.ResponseWriter, r *http.Request) {
	query := r.URL.Query()
	label := query.Get("label")
	if label == "" {
		label = "unlabelled"
	}
	wait := maximumWait
	if seconds, err := strconv.Atoi(query.Get("wait")); err == nil && seconds >= 0 {
		wait = min(time.Duration(seconds)*time.Second, maximumWait)
	}
	deadline := time.NewTimer(wait)
	defer deadline.Stop()

	l.mu.Lock()
	l.settle()
	t := l.find(query.Get("ticket"))
	if t == nil {
		t = &ticket{ID: randomID(), Label: label, Who: l.whoIs(r),
			Since: l.now(), ttl: ttlFrom(r)}
		l.queue = append(l.queue, t)
		l.notify()
	}
	for {
		t.lastSeen = l.now()
		if l.holder == nil && l.position(t) == 1 {
			granted := *l.grant(t)
			l.mu.Unlock()
			writeJSON(w, http.StatusOK, granted)
			return
		}
		changed := l.changed
		l.mu.Unlock()
		select {
		case <-changed:
		case <-deadline.C:
			l.mu.Lock()
			t.lastSeen = l.now()
			response := map[string]any{"ticket": t.ID,
				"position": l.position(t), "holder": l.public(l.holder)}
			l.mu.Unlock()
			writeJSON(w, http.StatusAccepted, response)
			return
		case <-r.Context().Done():
			return
		}
		l.mu.Lock()
		l.settle()
		if l.position(t) == 0 { // abandoned meanwhile
			l.queue = append(l.queue, t)
		}
	}
}

func (l *leases) renew(w http.ResponseWriter, r *http.Request) {
	l.mu.Lock()
	defer l.mu.Unlock()
	l.settle()
	if l.holder == nil || l.holder.Token != r.URL.Query().Get("token") {
		writeJSON(w, http.StatusConflict, map[string]any{
			"error": "not the lease holder", "holder": l.public(l.holder)})
		return
	}
	l.holder.Expires = l.now().Add(ttlFrom(r))
	writeJSON(w, http.StatusOK, l.holder)
}

func (l *leases) release(w http.ResponseWriter, r *http.Request) {
	l.mu.Lock()
	defer l.mu.Unlock()
	if l.holder != nil && l.holder.Token == r.URL.Query().Get("token") {
		l.holder = nil
		l.notify()
	}
	writeJSON(w, http.StatusOK, map[string]any{"released": true})
}

func (l *leases) status(w http.ResponseWriter, r *http.Request) {
	l.mu.Lock()
	defer l.mu.Unlock()
	l.settle()
	writeJSON(w, http.StatusOK, map[string]any{
		"holder": l.public(l.holder), "queue": l.queue, "now": l.now()})
}

// gate lets a request through to the portal, or answers 423 when it would
// change the console under someone else's lease.
func (l *leases) gate(next http.Handler) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		token := r.Header.Get(leaseHeader)
		r.Header.Del(leaseHeader)
		switch r.Method {
		case http.MethodGet, http.MethodHead, http.MethodOptions:
			next.ServeHTTP(w, r)
			return
		}
		l.mu.Lock()
		l.settle()
		holder := l.holder
		var blocked map[string]any
		if holder != nil && holder.Token != token {
			blocked = map[string]any{
				"error": fmt.Sprintf("the Xbox is leased to %q (%s) until %s; "+
					"take a lease with POST /lease/acquire and send it in %s",
					holder.Label, holder.Who,
					holder.Expires.Format(time.RFC3339), leaseHeader),
				"holder": l.public(holder), "waiting": len(l.queue)}
		}
		l.mu.Unlock()
		if blocked != nil {
			writeJSON(w, http.StatusLocked, blocked)
			return
		}
		next.ServeHTTP(w, r)
	})
}

func (l *leases) routes(portal http.Handler) http.Handler {
	mux := http.NewServeMux()
	mux.HandleFunc("POST /lease/acquire", l.acquire)
	mux.HandleFunc("POST /lease/renew", l.renew)
	mux.HandleFunc("POST /lease/release", l.release)
	mux.HandleFunc("GET /lease", l.status)
	mux.Handle("/", l.gate(portal))
	return mux
}
