"""Check Qt HTTP responses directly, before Caddy can normalize HEAD handling."""
import json
from integration import compose

PROBE = r'''
import json
import socket

def exchange(method, path):
    with socket.create_connection(("service1-v1", 8080), timeout=15) as connection:
        connection.settimeout(15)
        connection.sendall((method + " " + path + " HTTP/1.1\r\nHost: service1-v1\r\nConnection: close\r\n\r\n").encode())
        chunks = []
        while True:
            chunk = connection.recv(65536)
            if not chunk:
                break
            chunks.append(chunk)
    headers, body = b"".join(chunks).split(b"\r\n\r\n", 1)
    lines = headers.decode("ascii").split("\r\n")
    values = {}
    for line in lines[1:]:
        key, value = line.split(":", 1)
        key = key.lower()
        assert key not in values, ("duplicate header", key)
        values[key] = value.strip()
    return int(lines[0].split()[1]), values, body

for path, status in [("/api/v1/groups", 200), ("/api/v1/groups/1", 200),
                     ("/api/v1/groups/2/items", 200), ("/api/v1/groups/999", 404),
                     ("/api/v1/groups/0", 400), ("/missing", 404)]:
    code, headers, body = exchange("GET", path)
    assert code == status and headers["content-type"] == "application/json"
    assert int(headers["content-length"]) == len(body)
    json.loads(body)
    head_code, head_headers, head_body = exchange("HEAD", path)
    assert head_code == status and head_body == b"", (path, head_code, head_body)
    assert head_headers["content-type"] == headers["content-type"]
    assert int(head_headers["content-length"]) == len(body), path
    if path.endswith("/items") and status == 200:
        assert head_headers["x-service2-instance"] in ("service2-v1-a", "service2-v1-b")
code, headers, body = exchange("POST", "/api/v1/groups")
assert code == 405 and headers["allow"] == "GET, HEAD"
assert json.loads(body)["error"]["code"] == "method_not_allowed"
print(json.dumps({"status": "passed"}))
'''


if __name__ == "__main__":
    result = compose("exec", "-T", "service2-v1-a", "/opt/venv/bin/python", "-", input=PROBE)
    assert json.loads(result.stdout)["status"] == "passed"
    print("PASS: direct Qt HTTP responses, JSON, application headers and HEAD framing")
