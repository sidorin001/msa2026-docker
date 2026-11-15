#include "runtime.hpp"

int main() {
    try {
        auto signals = msa::termination_signals();
        msa::Config config;
        auto base_url = msa::env("SERVICE2_BASE_URL", "http://service2-v1:8080");
        if (!std::regex_match(base_url, std::regex(R"(^http://[a-zA-Z0-9.-]+:[0-9]+$)")))
            throw std::runtime_error("Invalid SERVICE2_BASE_URL");
        int connect_ms = msa::setting("HTTP_CONNECT_TIMEOUT_MS", "2000");
        int read_ms = msa::setting("HTTP_READ_TIMEOUT_MS", "5000");
        httplib::Server server;
        msa::configure(server, config, true);
        server.Get("/api/v1/groups", msa::guarded([&](const auto&, auto& res) {
            msa::Database db(config);
            msa::reply(res, db.query("SELECT id, name FROM groups ORDER BY id"));
        }));
        server.Get(R"(/api/v1/groups/([^/]+))", msa::guarded([&](const auto& req, auto& res) {
            int id = msa::positive(req.matches[1].str());
            msa::Database db(config);
            auto rows = db.query("SELECT id, name FROM groups WHERE id = $1", {std::to_string(id)});
            if (rows.empty()) msa::error(res, 404, "not_found", "Group not found");
            else msa::reply(res, rows[0]);
        }));
        server.Get(R"(/api/v1/groups/([^/]+)/items)", msa::guarded([&](const auto& req, auto& res) {
            int id = msa::positive(req.matches[1].str());
            {
                msa::Database db(config);
                if (db.query("SELECT id FROM groups WHERE id = $1", {std::to_string(id)}).empty()) {
                    msa::error(res, 404, "not_found", "Group not found");
                    return;
                }
            }
            httplib::Client client(base_url);
            client.set_connection_timeout(std::chrono::milliseconds(connect_ms));
            client.set_read_timeout(std::chrono::milliseconds(read_ms));
            client.set_write_timeout(std::chrono::milliseconds(read_ms));
            auto started = std::chrono::steady_clock::now();
            std::string body;
            bool oversized = false;
            // Bound both memory and total response duration, including a slow streaming peer.
            auto result = client.Get(("/api/v1/items?group_id=" + std::to_string(id)).c_str(),
                [&](const char* data, size_t size) {
                    if (body.size() + size > 1024 * 1024) { oversized = true; return false; }
                    body.append(data, size);
                    return std::chrono::steady_clock::now() - started < std::chrono::milliseconds(connect_ms + read_ms);
                });
            if (!result) {
                bool timeout = !oversized && (result.error() == httplib::Error::ConnectionTimeout ||
                    std::chrono::steady_clock::now() - started >= std::chrono::milliseconds(read_ms));
                msa::error(res, timeout ? 504 : 502, timeout ? "upstream_timeout" : "bad_gateway",
                           timeout ? "Service2 timeout" : "Service2 unavailable");
                return;
            }
            auto items = msa::json::parse(body, nullptr, false);
            bool valid = result->status == 200 && items.is_array();
            int previous = 0;
            if (valid) for (const auto& item : items) {
                if (!item.is_object() || !item.contains("id") || !item["id"].is_number_integer() ||
                    !item.contains("group_id") || item["group_id"] != id ||
                    !item.contains("name") || !item["name"].is_string() || item["id"] <= previous) {
                    valid = false; break;
                }
                previous = item["id"].get<int>();
            }
            if (!valid) msa::error(res, 502, "bad_gateway", "Invalid Service2 response");
            else msa::reply(res, items);
        }));
        return msa::serve(server, config.http_port, signals);
    } catch (const std::exception&) {
        std::cerr << "Service1 startup failed; check environment configuration" << std::endl;
        return 1;
    }
}
