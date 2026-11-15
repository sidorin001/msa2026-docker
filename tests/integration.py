"""Destructive lifecycle checks for a DISPOSABLE Compose environment."""
import json
import subprocess
import time
from smoke import request, run, wait_ready

BASE = "http://localhost:8080"


def compose(*args, check=True, **kwargs):
    return subprocess.run(["docker", "compose", *args], check=check,
                          capture_output=True, text=True, **kwargs)


def expect_one_of(path, statuses):
    import urllib.error
    import urllib.request
    start = time.monotonic()
    try:
        response = urllib.request.urlopen(BASE + path, timeout=20)
    except urllib.error.HTTPError as exc:
        response = exc
    with response:
        assert response.code in statuses, (path, response.code, statuses)
    assert time.monotonic() - start < 20


def sql(service, statement, app=False, check=True):
    # Environment expansion happens inside the DB container; never print credentials.
    script = ('export PGPASSWORD="$APP_PASSWORD"; psql -X -U "$APP_USER" -h 127.0.0.1 -d "$POSTGRES_DB" -v ON_ERROR_STOP=1 -tAc "$1"'
              if app else 'psql -X -U postgres -d "$POSTGRES_DB" -v ON_ERROR_STOP=1 -tAc "$1"')
    return compose("exec", "-T", service, "sh", "-c", script, "sh", statement, check=check)


def main():
    run(BASE)
    config = json.loads(compose("config", "--format", "json").stdout)
    services = config["services"]
    assert len(services) == 6
    assert all(not value.get("ports") for name, value in services.items() if name != "gateway")
    assert len(services["gateway"]["ports"]) == 1
    for api, own, other in [("service1-v1", "db1", "db2"), ("service2-v1", "db2", "db1")]:
        assert own + "-net" in services[api]["networks"]
        assert other + "-net" not in services[api]["networks"]
        assert services[api]["environment"]["DB_HOST"] == own
        compose("exec", "-T", api, "sh", "-c", 'test "$(id -u)" != 0 && ! command -v g++ && ! command -v cmake')
        probe = compose("exec", "-T", api, "curl", "--noproxy", "*", "--connect-timeout", "2", "--max-time", "3",
                        f"http://{other}:5432", check=False)
        assert probe.returncode != 0
    for db, table in [("db1", "groups"), ("db2", "items")]:
        assert sql(db, f"SELECT count(*) FROM {table}", app=True).stdout.strip() in ["4", "10"]
        for statement in [f"INSERT INTO {table} (id,name) VALUES (999,'denied')",
                          f"UPDATE {table} SET name='denied' WHERE id=1",
                          f"DELETE FROM {table} WHERE id=1", "CREATE TABLE denied (id int)"]:
            result = sql(db, statement, app=True, check=False)
            assert result.returncode != 0 and "permission denied" in result.stderr, statement
        assert sql(db, "SELECT rolsuper FROM pg_roles WHERE rolname=current_user", app=True).stdout.strip() == "f"
    try:
        compose("stop", "service2-v1")
        request(BASE, "/api/v1/groups")
        request(BASE, "/api/v1/groups/999/items", 404)
        expect_one_of("/api/v1/groups/1/items", {502, 504})
    finally:
        compose("start", "service2-v1")
        wait_ready(BASE)
    for db, affected, independent in [("db1", "groups", "items"), ("db2", "items", "groups")]:
        try:
            compose("stop", db)
            request(BASE, f"/api/v1/{affected}", 503)
            request(BASE, f"/api/v1/{independent}")
            if db == "db2":
                request(BASE, "/api/v1/groups/1/items", 502)
        finally:
            compose("start", db)
            wait_ready(BASE)
    # Verify persistence across removal/recreation, keeping named volumes.
    compose("down")
    compose("up", "-d", "--wait", "--wait-timeout", "180")
    run(BASE)
    print("PASS: privileges, topology, runtime, dependency failures, recovery and persistence")


if __name__ == "__main__":
    main()
