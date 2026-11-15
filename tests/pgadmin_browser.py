"""Browser-level reverse proxy, login and both database connections (disposable CI)."""
from pathlib import Path
from playwright.sync_api import sync_playwright, expect


def main():
    # The generated CI .env contains hex passwords. This simple reader intentionally
    # does not implement general dotenv quoting; it is used only by this test.
    config = dict(line.split("=", 1) for line in Path(".env").read_text().splitlines()
                  if line and not line.startswith("#"))
    Path("test-results").mkdir(exist_ok=True)
    with sync_playwright() as p:
        browser = p.chromium.launch()
        page = browser.new_page(viewport={"width": 1440, "height": 1000})
        page.set_default_timeout(30000)
        failures = []
        page.on("response", lambda r: failures.append(r.url) if r.status >= 400 and
                r.request.resource_type in ("script", "stylesheet") else None)
        try:
            response = page.goto("http://localhost:8080/pgadmin", wait_until="networkidle", timeout=90000)
            assert response.ok
            assert "/pgadmin/" in page.url
            page.locator('input[name="email"]').fill(config["PGADMIN_DEFAULT_EMAIL"])
            page.locator('input[name="password"]').fill(config["PGADMIN_DEFAULT_PASSWORD"])
            page.get_by_role("button", name="Login", exact=True).click()
            page.wait_for_url("**/pgadmin/browser/**", timeout=60000)
            # Dismiss optional introductory modal before interacting with the tree.
            close = page.get_by_role("button", name="Close", exact=True)
            if close.count() and close.first.is_visible():
                close.first.click()
            group = page.get_by_text("MSA", exact=True)
            if not group.count():
                page.get_by_text("Servers", exact=True).first.dblclick()
            group.first.wait_for(state="visible")
            group.first.dblclick()
            for name, password, database in [
                ("DB1 — Groups", config["DB1_ADMIN_PASSWORD"], config["DB1_NAME"]),
                ("DB2 — Items", config["DB2_ADMIN_PASSWORD"], config["DB2_NAME"]),
            ]:
                page.get_by_text(name, exact=True).first.dblclick()
                password_input = page.locator('input[type="password"]:visible')
                password_input.last.fill(password)
                page.get_by_role("button", name="OK", exact=True).last.click()
                # The browser tree must display the live database list after connection.
                page.get_by_text("Databases", exact=False).filter(has_text="Databases").first.wait_for()
                # Expand the selected server's Databases node using its tree ancestry.
                server_label = page.get_by_text(name, exact=True).first
                server_node = server_label.locator('xpath=ancestor::*[@role="treeitem"][1]')
                if server_node.count():
                    databases = server_node.get_by_text("Databases", exact=False).first
                else:
                    databases = page.get_by_text("Databases", exact=False).last
                databases.dblclick()
                expect(page.get_by_text(database, exact=True).first).to_be_visible()
                page.get_by_text(database, exact=True).first.click()
                page.screenshot(path=f"test-results/{database}.png", full_page=True)
            assert not failures, failures
            assert "/pgadmin/" in page.url
            print("PASS: pgAdmin proxy, login, static assets and both live databases")
        finally:
            page.screenshot(path="test-results/pgadmin-final.png", full_page=True)
            browser.close()


if __name__ == "__main__":
    main()
