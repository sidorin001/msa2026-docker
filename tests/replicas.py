"""Destructive acceptance for a disposable seven-container Compose environment."""
import http.client
from datetime import datetime, timezone
import json
from pathlib import Path
import time
from integration import BASE, compose, expect_one_of, sql
from smoke import request, wait_ready

REPLICAS = ["service2-v1-a", "service2-v1-b"]


def get(path="/api/v1/items"):
    # One connection also lets the RR check verify HTTP-level selection with keep-alive.
    conn = http.client.HTTPConnection("localhost", 8080, timeout=15)
    try:
        conn.request("GET", path)
        response = conn.getresponse()
        data = json.loads(response.read()) if response.status == 200 else response.read()
        return response.status, response.getheader("X-Service2-Instance"), data
    finally:
        conn.close()


def eventually(check, description, timeout=30):
    end = time.monotonic() + timeout
    while True:
        try:
            check()
            return
        except (AssertionError, OSError, ValueError, http.client.HTTPException):
            if time.monotonic() >= end:
                raise AssertionError("Timed out: " + description)
            time.sleep(0.5)


def rr(path="/api/v1/items"):
    conn = http.client.HTTPConnection("localhost", 8080, timeout=15)
    identifiers = []
    try:
        for _ in range(20):
            conn.request("GET", path)
            response = conn.getresponse()
            data = json.loads(response.read())
            assert response.status == 200 and isinstance(data, list)
            identifiers.append(response.getheader("X-Service2-Instance"))
    finally:
        conn.close()
    assert set(identifiers) == set(REPLICAS), identifiers
    assert all(a != b for a, b in zip(identifiers, identifiers[1:])), identifiers
    assert all(identifiers.count(name) == 10 for name in REPLICAS), identifiers


def only(name):
    for _ in range(4):
        status, instance, data = get()
        assert status == 200 and instance == name and len(data) == 10, (status, instance)
    status, instance, data = get("/api/v1/groups/2/items")
    assert status == 200 and instance == name and len(data) == 3


def main():
    eventually(rr, "both replicas ready and alternating")
    rr("/api/v1/groups/2/items")
    for stopped in REPLICAS:
        sibling = next(name for name in REPLICAS if name != stopped)
        try:
            compose("stop", stopped)
            eventually(lambda: only(sibling), "stopped replica excluded")
        finally:
            compose("start", stopped)
            eventually(rr, "restarted replica readmitted after successful checks")
        try:
            compose("pause", stopped)
            request(BASE, "/api/v1/groups")
            eventually(lambda: only(sibling), "timed-out healthcheck excludes paused replica")
        finally:
            compose("unpause", stopped)
            eventually(rr, "unpaused replica readmitted")
    try:
        compose("stop", *REPLICAS)
        request(BASE, "/api/v1/groups")
        request(BASE, "/api/v1/groups/999/items", 404)
        expect_one_of("/api/v1/items", {502, 503})
        expect_one_of("/api/v1/groups/1/items", {502, 504})
    finally:
        compose("start", *REPLICAS)
        eventually(rr, "both replicas recover")
    # Override only the replica entrypoints in a disposable environment.
    state = Path(".local/readiness")
    state.mkdir(parents=True, exist_ok=True)
    for flag in state.iterdir():
        if flag.name in ("a", "b"):
            flag.unlink()
    override = ("-f", "compose.yaml", "-f", "tests/compose.readiness-test.yaml")
    try:
        compose(*override, "up", "-d", "--no-deps", "--force-recreate", *REPLICAS)
        eventually(rr, "test fixture replicas healthy")
        for letter, faulty in zip(("a", "b"), REPLICAS):
            sibling = next(name for name in REPLICAS if name != faulty)
            flag = state / letter
            flag.touch()
            try:
                eventually(lambda: only(sibling), "HTTP 503 readiness excludes a running replica")
                # Process and API remain available: only readiness is failing.
                direct = compose("exec", "-T", faulty, "curl", "-fsS", "http://localhost:8080/api/v1/items")
                assert len(json.loads(direct.stdout)) == 10
                end = time.monotonic() + 11  # More than two healthcheck intervals.
                while time.monotonic() < end:
                    only(sibling)
                    time.sleep(0.5)
            finally:
                flag.unlink(missing_ok=True)
                eventually(rr, "HTTP readiness recovery readmits replica")
    finally:
        compose("up", "-d", "--no-deps", "--force-recreate", *REPLICAS)
        eventually(rr, "normal replica entrypoints restored")
    # Both readiness endpoints depend on the shared DB; its failure removes both.
    since = datetime.now(timezone.utc).isoformat()
    try:
        compose("stop", "db2")
        def no_replicas():
            status, _, _ = get()
            assert status in (502, 503)
            logs = compose("logs", "--no-color", "--since", since, "gateway").stdout
            assert "no upstreams available" in logs
        eventually(no_replicas, "shared DB failure removes both replicas")
        request(BASE, "/api/v1/groups")
    finally:
        compose("start", "db2")
        eventually(rr, "DB recovery restores both replicas")
    assert sql("db2", "SELECT count(*) FROM items", app=True).stdout.strip() == "10"
    wait_ready(BASE)
    print("PASS: RR (external and aggregated), stop/pause/HTTP-unready, gated recovery, shared DB and seed")


if __name__ == "__main__":
    main()
