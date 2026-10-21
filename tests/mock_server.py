import http.server
import json
import sys
import time
import urllib.parse

PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 8765
BALANCES = ["100.00", "99.58", "99.58", "99.58"]
balance_counter = 0


class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, fmt, *args):
        sys.stderr.write("mock: " + (fmt % args) + "\n")

    def _send_json(self, obj, status=200):
        body = json.dumps(obj).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        global balance_counter
        parsed = urllib.parse.urlparse(self.path)
        path = parsed.path

        if path == "/reset":
            balance_counter = 0
            self._send_json({"ok": True})
            return

        if path.endswith("/user/balance"):
            if "/bal401/" in path:
                self._send_json({"error": {"message": "invalid key"}}, 401)
                return
            index = min(balance_counter, len(BALANCES) - 1)
            total = BALANCES[index]
            balance_counter += 1
            self._send_json({
                "is_available": True,
                "balance_infos": [
                    {
                        "currency": "CNY",
                        "total_balance": total,
                        "granted_balance": "0.00",
                        "topped_up_balance": total,
                    },
                    {
                        "currency": "USD",
                        "total_balance": "5.00",
                        "granted_balance": "0.00",
                        "topped_up_balance": "5.00",
                    },
                ],
            })
            return

        if path.endswith("/api/v0/usage/by_api_key/cost"):
            if "/authfail/" in path:
                self._send_json({"code": 40003, "msg": "Authorization Failed"})
                return

            sign = -1.0 if "/negative/" in path else 1.0
            now = int(time.time())
            cny_buckets = [
                {"time": now - 10, "cost": {"value": sign * 0.200000}},
                {"time": now - 5, "cost": {"value": sign * 0.150000}},
            ]
            usd_buckets = [{"time": now - 5, "cost": {"value": 9.99}}]
            self._send_json({
                "code": 0,
                "msg": "",
                "data": {
                    "biz_code": 0,
                    "biz_msg": "",
                    "biz_data": {
                        "data": [
                            {
                                "currency": "CNY",
                                "series": [
                                    {
                                        "api_key": {"name": "main", "tracking_id": "k1"},
                                        "model": "deepseek-chat",
                                        "buckets": cny_buckets,
                                    }
                                ],
                            },
                            {
                                "currency": "USD",
                                "series": [
                                    {
                                        "api_key": "other",
                                        "model": "deepseek-chat",
                                        "buckets": usd_buckets,
                                    }
                                ],
                            },
                        ]
                    },
                },
            })
            return

        self._send_json({"error": "unknown path", "path": path}, 404)


if __name__ == "__main__":
    server = http.server.ThreadingHTTPServer(("127.0.0.1", PORT), Handler)
    print("mock server on %d" % PORT, flush=True)
    server.serve_forever()
