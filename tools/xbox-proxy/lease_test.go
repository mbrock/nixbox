package main

import (
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"testing"
	"time"
)

// A fake portal that records which changing requests reached it.
func portal(reached *[]string) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		*reached = append(*reached, r.Method+" "+r.URL.Path+" "+r.Header.Get(leaseHeader))
		w.WriteHeader(http.StatusOK)
	})
}

func call(t *testing.T, h http.Handler, method, target, token string) (int, map[string]any) {
	t.Helper()
	r := httptest.NewRequest(method, target, nil)
	if token != "" {
		r.Header.Set(leaseHeader, token)
	}
	w := httptest.NewRecorder()
	h.ServeHTTP(w, r)
	var body map[string]any
	json.Unmarshal(w.Body.Bytes(), &body)
	return w.Code, body
}

func TestLeaseQueue(t *testing.T) {
	var reached []string
	l := newLeases(func(*http.Request) string { return "tester" })
	h := l.routes(portal(&reached))

	// Even a free console refuses changes without a lease.
	if code, _ := call(t, h, "POST", "/api/taskmanager/app", ""); code != 428 {
		t.Fatalf("unleased change on a free console: %d", code)
	}
	// A takes the lease at once.
	code, a := call(t, h, "POST", "/lease/acquire?label=a&wait=0", "")
	if code != 200 || a["token"] == "" {
		t.Fatalf("first acquire: %d %v", code, a)
	}
	tokenA := a["token"].(string)
	// B queues behind A; B's changes are refused, A's pass without the header.
	code, b := call(t, h, "POST", "/lease/acquire?label=b&wait=0", "")
	if code != 202 || b["position"].(float64) != 1 {
		t.Fatalf("second acquire should queue: %d %v", code, b)
	}
	if code, body := call(t, h, "DELETE", "/api/taskmanager/app", ""); code != 423 {
		t.Fatalf("change under someone else's lease: %d %v", code, body)
	}
	if code, _ := call(t, h, "GET", "/ext/screenshot", ""); code != 200 {
		t.Fatalf("reads stay open: %d", code)
	}
	if code, _ := call(t, h, "POST", "/api/app/packagemanager/package", tokenA); code != 200 {
		t.Fatalf("holder's change: %d", code)
	}
	if last := reached[len(reached)-1]; last != "POST /api/app/packagemanager/package " {
		t.Fatalf("the lease header must not reach the portal: %q", last)
	}
	// B waits; A releases; B is granted on its next poll.
	done := make(chan map[string]any)
	go func() {
		_, granted := call(t, h, "POST", "/lease/acquire?label=b&wait=5&ticket="+b["ticket"].(string), "")
		done <- granted
	}()
	time.Sleep(50 * time.Millisecond)
	call(t, h, "POST", "/lease/release?token="+tokenA, "")
	select {
	case granted := <-done:
		if granted["token"] == nil || granted["label"] != "b" {
			t.Fatalf("B should hold the lease: %v", granted)
		}
		if code, _ := call(t, h, "POST", "/api/taskmanager/app", tokenA); code != 423 {
			t.Fatalf("A's old token must no longer pass: %d", code)
		}
	case <-time.After(3 * time.Second):
		t.Fatal("B was not granted after A released")
	}
}

func TestLeaseExpires(t *testing.T) {
	var reached []string
	l := newLeases(func(*http.Request) string { return "tester" })
	now := time.Now()
	l.now = func() time.Time { return now }
	h := l.routes(portal(&reached))
	call(t, h, "POST", "/lease/acquire?label=a&ttl=60&wait=0", "")
	if code, _ := call(t, h, "POST", "/api/control/restart", ""); code != 423 {
		t.Fatalf("held: %d", code)
	}
	now = now.Add(61 * time.Second)
	if code, _ := call(t, h, "POST", "/api/control/restart", ""); code != 428 {
		t.Fatalf("expired lease should free the console for the next lease: %d", code)
	}
	if code, body := call(t, h, "POST", "/lease/acquire?label=b&wait=0", ""); code != 200 {
		t.Fatalf("the next client should get the expired lease: %d %v", code, body)
	}
}
