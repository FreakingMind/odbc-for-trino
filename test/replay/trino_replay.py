#!/usr/bin/env python3
"""
Records the HTTP conversation between the driver tests and a Trino server,
and replays it later without Trino.

Windows CI can't run Trino (Trino supports only Linux and macOS, and GitHub's
Windows runners have no Linux containers), so the driver's integration tests
run there against recordings of real Trino responses:

  record   a proxy in front of a real Trino (the Docker test environment):
           python3 trino_replay.py record --upstream http://localhost:8080 \
               --port 18080 --out recordings.json.gz
  replay   a stand-in server that answers from the recordings:
           python3 trino_replay.py replay --recordings recordings.json.gz \
               --port 18080

A statement is identified by its SQL text and the prepared statements the
client sends (X-Trino-Prepared-Statement). Prepared statement names are
random per run, so they are normalized for matching, and the names in the
recorded X-Trino-Added-Prepare / X-Trino-Deallocated-Prepare headers are
replaced with the live ones. When the same statement was recorded several
times, the recordings are replayed in order.

Python 3.9+, standard library only.
"""

import argparse
import gzip
import http.client
import json
import re
import signal
import sys
import threading
import urllib.parse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

STATEMENT_PATH = "/v1/statement"
NAME_PATTERN = re.compile(r'(PREPARE|EXECUTE|DEALLOCATE PREPARE)\s+"([A-Za-z0-9_]+)"')
PREPARED_HEADER = "X-Trino-Prepared-Statement"
NAMED_RESPONSE_HEADERS = ("x-trino-added-prepare", "x-trino-deallocated-prepare")
URI_FIELDS = ("nextUri", "infoUri", "partialCancelUri")


def normalize_sql(sql):
    return NAME_PATTERN.sub(lambda m: '%s "<name>"' % m.group(1), sql)


def statement_names(sql):
    return [m.group(2) for m in NAME_PATTERN.finditer(sql)]


def normalize_prepared_header(value):
    # "name1=statement1,name2=statement2": the names are random.
    if not value:
        return ""
    statements = [part.split("=", 1)[-1] for part in value.split(",")]
    return ",".join(sorted(statements))


def statement_key(sql, prepared_header):
    return json.dumps([normalize_sql(sql), normalize_prepared_header(prepared_header)])


def trino_headers(headers):
    return {k.lower(): v for k, v in headers.items() if k.lower().startswith("x-trino-")}


def uri_path(uri):
    parsed = urllib.parse.urlsplit(uri)
    return parsed.path + ("?" + parsed.query if parsed.query else "")


def log(message):
    print(message, file=sys.stderr, flush=True)


# --------------------------------------------------------------------------
# Recording
# --------------------------------------------------------------------------


class Recorder:
    def __init__(self, upstream, out):
        parsed = urllib.parse.urlsplit(upstream)
        self.upstream_host = parsed.hostname
        self.upstream_port = parsed.port or 80
        self.out = out
        self.lock = threading.Lock()
        self.chains = []  # {"key", "sql", "responses": [...]}
        self.chain_by_path = {}  # nextUri path -> chain index
        self.gets = {}  # path -> response (e.g. /v1/info)

    def forward(self, method, path, headers, body):
        connection = http.client.HTTPConnection(self.upstream_host, self.upstream_port, timeout=600)
        forwarded = {k: v for k, v in headers.items()
                     if k.lower() not in ("host", "content-length", "accept-encoding", "connection")}
        forwarded["Accept-Encoding"] = "identity"
        connection.request(method, path, body=body, headers=forwarded)
        response = connection.getresponse()
        data = response.read()
        result = (response.status, dict(response.getheaders()), data)
        connection.close()
        return result

    def save(self):
        with self.lock:
            document = {"chains": self.chains, "gets": self.gets}
            with gzip.open(self.out, "wt", encoding="utf-8") as f:
                json.dump(document, f, separators=(",", ":"))
        log("Saved %d statements to %s" % (len(self.chains), self.out))


def make_record_handler(recorder):
    class Handler(BaseHTTPRequestHandler):
        protocol_version = "HTTP/1.1"

        def log_message(self, *args):
            pass

        def proxy(self, method):
            length = int(self.headers.get("Content-Length") or 0)
            body = self.rfile.read(length) if length else None
            status, headers, data = recorder.forward(method, self.path, self.headers, body)
            text = data.decode("utf-8", errors="replace")
            # Make the client come back here instead of going to Trino.
            own = "http://%s" % self.headers.get("Host")
            upstream = "http://%s:%d" % (recorder.upstream_host, recorder.upstream_port)
            text = text.replace(upstream, own)
            response = {"status": status, "headers": trino_headers(headers), "body": text}
            with recorder.lock:
                if method == "POST" and self.path.startswith(STATEMENT_PATH):
                    sql = (body or b"").decode("utf-8")
                    chain = {"key": statement_key(sql, self.headers.get(PREPARED_HEADER)),
                             "sql": sql, "responses": [response]}
                    recorder.chains.append(chain)
                    index = len(recorder.chains) - 1
                elif method == "GET" and self.path in recorder.chain_by_path:
                    index = recorder.chain_by_path[self.path]
                    recorder.chains[index]["responses"].append(response)
                else:
                    index = None
                    if method == "GET" and self.path not in recorder.gets:
                        recorder.gets[self.path] = response
                if index is not None:
                    try:
                        next_uri = json.loads(text).get("nextUri")
                    except ValueError:
                        next_uri = None
                    if next_uri:
                        recorder.chain_by_path[uri_path(next_uri)] = index
            payload = text.encode("utf-8")
            self.send_response(status)
            for name, value in headers.items():
                if name.lower() not in ("content-length", "transfer-encoding", "connection",
                                        "content-encoding"):
                    self.send_header(name, value)
            self.send_header("Content-Length", str(len(payload)))
            self.end_headers()
            self.wfile.write(payload)

        def do_GET(self):
            self.proxy("GET")

        def do_POST(self):
            self.proxy("POST")

        def do_DELETE(self):
            self.proxy("DELETE")

    return Handler


# --------------------------------------------------------------------------
# Replaying
# --------------------------------------------------------------------------


class Replayer:
    def __init__(self, recordings, verbose=False):
        self.verbose = verbose
        with gzip.open(recordings, "rt", encoding="utf-8") as f:
            document = json.load(f)
        self.chains_by_key = {}
        for chain in document["chains"]:
            self.chains_by_key.setdefault(chain["key"], []).append(chain)
        self.gets = document["gets"]
        self.lock = threading.Lock()
        self.uses = {}  # key -> how many times replayed
        self.sessions = []  # (chain, {recorded name: live name})
        self.missing = 0


def rename(value, names):
    for recorded, live in names.items():
        value = value.replace(recorded, live)
    return value


def make_replay_handler(replayer):
    class Handler(BaseHTTPRequestHandler):
        protocol_version = "HTTP/1.1"

        def log_message(self, *args):
            pass

        def send_json(self, status, body, headers=None):
            payload = body.encode("utf-8")
            self.send_response(status)
            self.send_header("Content-Type", "application/json")
            for name, value in (headers or {}).items():
                self.send_header(name, value)
            self.send_header("Content-Length", str(len(payload)))
            self.end_headers()
            self.wfile.write(payload)

        def send_step(self, session, step):
            chain, names = replayer.sessions[session]
            if step >= len(chain["responses"]):
                self.send_json(404, "{}")
                return
            response = chain["responses"][step]
            body = response["body"]
            try:
                document = json.loads(body)
            except ValueError:
                document = None
            if isinstance(document, dict):
                own = "http://%s" % self.headers.get("Host")
                for field in URI_FIELDS:
                    if field in document:
                        if field == "nextUri":
                            document[field] = "%s/replay/%d/%d" % (own, session, step + 1)
                        else:
                            document[field] = "%s/replay/info" % own
                body = json.dumps(document)
            headers = {name: rename(value, names) if name in NAMED_RESPONSE_HEADERS else value
                       for name, value in response["headers"].items()}
            self.send_json(response["status"], body, headers)

        def do_POST(self):
            length = int(self.headers.get("Content-Length") or 0)
            sql = self.rfile.read(length).decode("utf-8") if length else ""
            key = statement_key(sql, self.headers.get(PREPARED_HEADER))
            if replayer.verbose:
                log("POST %s found=%s sql=%r" % (self.path, key in replayer.chains_by_key,
                                                 sql[:120]))
            with replayer.lock:
                candidates = replayer.chains_by_key.get(key)
                if not candidates:
                    replayer.missing += 1
                    log("NO RECORDING for statement: %s" % sql.replace("\n", " ")[:300])
                    error = {"id": "replay", "stats": {"state": "FAILED"},
                             "error": {"message": "trino_replay: no recording for this statement; "
                                                  "re-record the fixtures", "errorCode": 0,
                                       "errorName": "REPLAY_MISSING", "errorType": "USER_ERROR"}}
                    self.send_json(200, json.dumps(error))
                    return
                use = replayer.uses.get(key, 0)
                replayer.uses[key] = use + 1
                chain = candidates[use % len(candidates)]
                names = dict(zip(statement_names(chain["sql"]), statement_names(sql)))
                replayer.sessions.append((chain, names))
                session = len(replayer.sessions) - 1
            self.send_step(session, 0)

        def do_GET(self):
            if replayer.verbose:
                log("GET %s" % self.path)
            match = re.match(r"^/replay/(\d+)/(\d+)$", self.path)
            if match:
                self.send_step(int(match.group(1)), int(match.group(2)))
                return
            response = replayer.gets.get(self.path)
            if response is None:
                self.send_json(404, "{}")
                return
            self.send_json(response["status"], response["body"], response["headers"])

        def do_DELETE(self):
            self.send_response(204)
            self.send_header("Content-Length", "0")
            self.end_headers()

    return Handler


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="mode", required=True)
    record = sub.add_parser("record")
    record.add_argument("--upstream", required=True)
    record.add_argument("--port", type=int, required=True)
    record.add_argument("--out", required=True)
    replay = sub.add_parser("replay")
    replay.add_argument("--recordings", required=True)
    replay.add_argument("--port", type=int, required=True)
    replay.add_argument("--verbose", action="store_true", help="log every request")
    args = parser.parse_args()

    # SIGTERM (and SIGINT) stop the server; a recorder then saves.
    def stop(signum, frame):
        threading.Thread(target=server.shutdown, daemon=True).start()

    if args.mode == "record":
        recorder = Recorder(args.upstream, args.out)
        server = ThreadingHTTPServer(("127.0.0.1", args.port), make_record_handler(recorder))
        signal.signal(signal.SIGTERM, stop)
        signal.signal(signal.SIGINT, stop)
        log("Recording on port %d, forwarding to %s" % (args.port, args.upstream))
        server.serve_forever()
        recorder.save()
    else:
        replayer = Replayer(args.recordings, args.verbose)
        server = ThreadingHTTPServer(("127.0.0.1", args.port), make_replay_handler(replayer))
        signal.signal(signal.SIGTERM, stop)
        signal.signal(signal.SIGINT, stop)
        log("Replaying %d statements on port %d" %
            (sum(len(c) for c in replayer.chains_by_key.values()), args.port))
        server.serve_forever()
        if replayer.missing:
            log("%d statements had no recording" % replayer.missing)


if __name__ == "__main__":
    main()
