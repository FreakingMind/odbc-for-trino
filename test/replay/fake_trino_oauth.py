#!/usr/bin/env python3
"""
A minimal stand-in for a Trino coordinator with External Authentication, to
test how the driver renews tokens without a browser or identity provider.

Like Trino with OAuth 2.0 refresh tokens, it hands out tokens that aren't
readable JWTs (five dot-separated parts, like a JWE) and answers a request
without a valid token with HTTP 401 and a WWW-Authenticate challenge that
names only a token server (no x_redirect_server: no browser sign-in).

Every query returns one row with one varchar column: "token-<n>", the
number of the token the result page was fetched with.

Control endpoints for tests:
  POST /control/revoke             every token issued so far becomes invalid
  POST /control/revoke-after-post  same, right after the next query is posted
                                   (its result pages then get HTTP 401)
  POST /control/reject-all         no token is ever accepted again
  POST /control/reset              back to the initial state
  GET  /control/stats              {"issued": n, "rejected": n}

  python3 fake_trino_oauth.py run -- <command...>
      starts the server on a free port, runs the command with
      FAKE_TRINO_PORT set, and exits with the command's exit code.

Python 3.9+, standard library only.
"""

import json
import os
import subprocess
import sys
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer


# A column the way Trino describes an unbounded varchar.
VARCHAR_COLUMN = {
    "name": "token",
    "type": "varchar",
    "typeSignature": {
        "rawType": "varchar",
        "arguments": [{"kind": "LONG", "value": 2147483647}],
    },
}


class State:
    def __init__(self):
        self.lock = threading.Lock()
        self.reset()

    def reset(self):
        self.valid = {}  # token -> number
        self.issued = 0
        self.rejected = 0
        self.revoke_after_post = False
        self.reject_all = False
        self.queries = 0


def make_handler(state):
    class Handler(BaseHTTPRequestHandler):
        protocol_version = "HTTP/1.1"

        def log_message(self, *args):
            pass

        def base(self):
            return "http://%s" % self.headers.get("Host")

        def send_json(self, status, document, headers=None):
            payload = json.dumps(document).encode("utf-8")
            self.send_response(status)
            self.send_header("Content-Type", "application/json")
            for name, value in (headers or {}).items():
                self.send_header(name, value)
            self.send_header("Content-Length", str(len(payload)))
            self.end_headers()
            self.wfile.write(payload)

        def read_body(self):
            length = int(self.headers.get("Content-Length") or 0)
            return self.rfile.read(length) if length else b""

        def token_number(self):
            # The number of a valid bearer token, or None.
            value = self.headers.get("Authorization", "")
            if not value.startswith("Bearer "):
                return None
            with state.lock:
                if state.reject_all:
                    return None
                return state.valid.get(value[len("Bearer "):])

        def challenge(self):
            with state.lock:
                state.rejected += 1
                ticket = state.issued + 1
            header = 'Bearer x_token_server="%s/oauth2/token/%d"' % (self.base(), ticket)
            self.send_json(401, {"message": "Unauthorized"},
                           {"WWW-Authenticate": header})

        def do_POST(self):
            body = self.read_body()
            if self.path == "/control/revoke":
                with state.lock:
                    state.valid.clear()
                return self.send_json(200, {})
            if self.path == "/control/revoke-after-post":
                with state.lock:
                    state.revoke_after_post = True
                return self.send_json(200, {})
            if self.path == "/control/reset":
                with state.lock:
                    state.reset()
                return self.send_json(200, {})
            if self.path == "/control/reject-all":
                with state.lock:
                    state.reject_all = True
                    state.valid.clear()
                return self.send_json(200, {})
            if self.path != "/v1/statement":
                return self.send_json(404, {})
            if self.token_number() is None:
                return self.challenge()
            with state.lock:
                state.queries += 1
                query_id = "q%d" % state.queries
                if state.revoke_after_post:
                    state.revoke_after_post = False
                    state.valid.clear()
            self.send_json(200, {
                "id": query_id,
                "nextUri": "%s/v1/statement/executing/%s/1" % (self.base(), query_id),
                "stats": {"state": "QUEUED"},
            })

        def do_GET(self):
            if self.path == "/control/stats":
                with state.lock:
                    return self.send_json(200, {"issued": state.issued,
                                                "rejected": state.rejected})
            if self.path.startswith("/oauth2/token/"):
                with state.lock:
                    state.issued += 1
                    number = state.issued
                    # Five parts, like an encrypted JWE: no readable claims.
                    token = "eyJhbGciOiJkaXIifQ..iv%d.ciphertext%d.tag%d" % (
                        number, number, number)
                    state.valid[token] = number
                return self.send_json(200, {"token": token})
            if not self.path.startswith("/v1/statement/executing/"):
                return self.send_json(404, {})
            number = self.token_number()
            if number is None:
                return self.challenge()
            query_id, page = self.path.split("/")[-2:]
            if page == "1":
                return self.send_json(200, {
                    "id": query_id,
                    "nextUri": "%s/v1/statement/executing/%s/2" % (self.base(), query_id),
                    "columns": [VARCHAR_COLUMN],
                    "data": [["token-%d" % number]],
                    "stats": {"state": "RUNNING"},
                })
            return self.send_json(200, {
                "id": query_id,
                "columns": [VARCHAR_COLUMN],
                "stats": {"state": "FINISHED"},
            })

        def do_DELETE(self):
            self.send_response(204)
            self.send_header("Content-Length", "0")
            self.end_headers()

    return Handler


def main():
    if len(sys.argv) < 4 or sys.argv[1] != "run" or sys.argv[2] != "--":
        print(__doc__, file=sys.stderr)
        return 2
    server = ThreadingHTTPServer(("127.0.0.1", 0), make_handler(State()))
    threading.Thread(target=server.serve_forever, daemon=True).start()
    env = dict(os.environ, FAKE_TRINO_PORT=str(server.server_address[1]))
    try:
        return subprocess.call(sys.argv[3:], env=env)
    finally:
        server.shutdown()


if __name__ == "__main__":
    sys.exit(main())
