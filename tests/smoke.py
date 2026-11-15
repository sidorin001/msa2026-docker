"""Read-only API checks. Python 3 standard library only."""
import argparse
import json
import time
import urllib.error
import urllib.request


def request(base, path, status=200, method="GET", json_body=True):
    req = urllib.request.Request(base + path, method=method)
    try:
        response = urllib.request.urlopen(req, timeout=15)
    except urllib.error.HTTPError as exc:
        response = exc
    with response:
        body = response.read()
        assert response.code == status, (method, path, response.code, status, body[:300])
        if method == "HEAD":
            assert not body
            return None
        if json_body:
            assert response.headers.get_content_type() == "application/json", path
            data = json.loads(body)
            if status >= 400:
                assert isinstance(data["error"]["code"], str)
                assert isinstance(data["error"]["message"], str)
            return data
        return body


def wait_ready(base, timeout=120):
    deadline = time.monotonic() + timeout
    while True:
        try:
            request(base, "/api/v1/groups")
            request(base, "/api/v1/items")
            return
        except (OSError, AssertionError, ValueError):
            if time.monotonic() >= deadline:
                raise
            time.sleep(1)


def run(base):
    wait_ready(base)
    groups = [{"id":i, "name":f"Group {i}"} for i in range(1, 5)]
    group_ids = [1, 1, 2, 2, 2, 3, 3, 3, 3, 3]
    items = [{"id":i, "group_id":g, "name":f"Item {i}"}
             for i, g in enumerate(group_ids, 1)]
    assert request(base, "/api/v1/groups") == groups
    assert request(base, "/api/v1/items") == items
    for group in groups:
        gid = group["id"]
        assert request(base, f"/api/v1/groups/{gid}") == group
        expected = [item for item in items if item["group_id"] == gid]
        assert request(base, f"/api/v1/items?group_id={gid}") == expected
        assert request(base, f"/api/v1/groups/{gid}/items") == expected
    for item in items:
        assert request(base, f"/api/v1/items/{item['id']}") == item
    for path in ["/api/v1/groups/999", "/api/v1/items/999", "/api/v1/groups/999/items"]:
        request(base, path, 404)
    assert request(base, "/api/v1/items?group_id=999") == []
    for bad in ["0", "-1", "abc", "2147483648", "999999999999999999999", "%2B1", "1%27"]:
        for path in [f"/api/v1/groups/{bad}", f"/api/v1/groups/{bad}/items",
                     f"/api/v1/items/{bad}", f"/api/v1/items?group_id={bad}"]:
            request(base, path, 400)
    request(base, "/api/v1/items?group_id=", 400)
    request(base, "/api/v1/items?group_id=1&group_id=2", 400)
    for path in ["/api/groups", "/context/groups/1/items", "/api/v1/context/groups/1/items",
                 "/api/v2/groups", "/api/v1/groups-extra", "/api/v1/items-extra",
                 "/api/v1/groups/1/items/extra"]:
        request(base, path, 404, json_body=False)
    for path in ["/api/v1/groups", "/api/v1/groups/1", "/api/v1/groups/1/items",
                 "/api/v1/items", "/api/v1/items/1"]:
        for method in ["POST", "PUT", "PATCH", "DELETE", "OPTIONS"]:
            req = urllib.request.Request(base + path, method=method)
            try:
                urllib.request.urlopen(req, timeout=15)
                raise AssertionError((method, path, "unexpected success"))
            except urllib.error.HTTPError as exc:
                assert exc.code == 405, (method, path, exc.code)
                assert "GET" in exc.headers.get("Allow", "")
        request(base, path, method="HEAD")
    print("PASS: seed, all routes, aggregation, validation, methods and errors")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--base-url", default="http://localhost:8080")
    run(parser.parse_args().base_url.rstrip("/"))
