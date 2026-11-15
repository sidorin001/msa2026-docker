#pragma once
#include <httplib.h>
#include <nlohmann/json.hpp>
#include <libpq-fe.h>
#include <charconv>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <regex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include <pthread.h>

namespace msa {
using json = nlohmann::json;
using Request = httplib::Request;
using Response = httplib::Response;

inline std::string env(const char* key, const char* fallback = "") {
    const char* value = std::getenv(key);
    return value ? value : fallback;
}

inline int positive(const std::string& text) {
    int result = 0;
    auto parsed = std::from_chars(text.data(), text.data() + text.size(), result);
    if (text.empty() || parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || result <= 0)
        throw std::invalid_argument("Expected a positive 32-bit integer");
    return result;
}

inline int setting(const char* key, const char* fallback) {
    return positive(env(key, fallback));
}

struct Config {
    std::string host = env("DB_HOST");
    std::string port = env("DB_PORT", "5432");
    std::string database = env("DB_NAME");
    std::string user = env("DB_USER");
    std::string password = env("DB_PASSWORD");
    std::string connect_timeout = std::to_string(setting("DB_CONNECT_TIMEOUT_SECONDS", "2"));
    std::string options = "-c statement_timeout=" + std::to_string(setting("DB_STATEMENT_TIMEOUT_MS", "3000"));
    int http_port = setting("HTTP_PORT", "8080");
    Config() {
        if (host.empty() || database.empty() || user.empty() || password.empty() || http_port > 65535)
            throw std::runtime_error("Invalid environment configuration");
        if (positive(port) > 65535) throw std::runtime_error("Invalid database port");
    }
};

class DatabaseError : public std::runtime_error {
public:
    DatabaseError() : std::runtime_error("Database unavailable") {}
};

class Database {
    using Connection = std::unique_ptr<PGconn, decltype(&PQfinish)>;
    Connection connection_;
public:
    explicit Database(const Config& c) : connection_(nullptr, PQfinish) {
        const char* keys[] = {"host", "port", "dbname", "user", "password", "connect_timeout", "options", "client_encoding", nullptr};
        const char* values[] = {c.host.c_str(), c.port.c_str(), c.database.c_str(), c.user.c_str(),
                                c.password.c_str(), c.connect_timeout.c_str(), c.options.c_str(), "UTF8", nullptr};
        connection_.reset(PQconnectdbParams(keys, values, 0));
        if (!connection_ || PQstatus(connection_.get()) != CONNECTION_OK) throw DatabaseError();
    }
    json query(const char* sql, const std::vector<std::string>& params = {}) {
        std::vector<const char*> values;
        for (const auto& p : params) values.push_back(p.c_str());
        std::unique_ptr<PGresult, decltype(&PQclear)> result(
            PQexecParams(connection_.get(), sql, static_cast<int>(values.size()), nullptr,
                         values.empty() ? nullptr : values.data(), nullptr, nullptr, 0), PQclear);
        if (!result || PQresultStatus(result.get()) != PGRES_TUPLES_OK) throw DatabaseError();
        auto rows = json::array();
        for (int r = 0; r < PQntuples(result.get()); ++r) {
            json row = json::object();
            for (int col = 0; col < PQnfields(result.get()); ++col) {
                std::string key = PQfname(result.get(), col);
                const char* value = PQgetvalue(result.get(), r, col);
                if (PQgetisnull(result.get(), r, col)) row[key] = nullptr;
                else if (key == "id" || key == "group_id") row[key] = std::stoi(value);
                else row[key] = value;
            }
            rows.push_back(std::move(row));
        }
        return rows;
    }
};

inline void reply(Response& res, const json& body, int status = 200) {
    res.status = status;
    res.set_content(body.dump(), "application/json");
}
inline void error(Response& res, int status, const char* code, const char* message) {
    reply(res, {{"error", {{"code", code}, {"message", message}}}}, status);
}

template <class Handler>
auto guarded(Handler handler) {
    return [handler](const Request& req, Response& res) {
        try { handler(req, res); }
        catch (const std::invalid_argument&) { error(res, 400, "invalid_id", "Expected a positive 32-bit integer"); }
        catch (const DatabaseError&) { error(res, 503, "database_unavailable", "Database unavailable"); }
        catch (const std::exception&) { error(res, 500, "internal_error", "Internal server error"); }
    };
}

inline void configure(httplib::Server& server, const Config& config, bool groups) {
    // A separate exact matcher prevents treating unknown nested routes as resources.
    std::regex known(groups ? R"(^/api/v1/groups(/[^/]+(/items)?)?$)"
                            : R"(^/api/v1/items(/[^/]+)?$)");
    server.set_pre_routing_handler([known](const Request& req, Response& res) {
        bool exists = req.path == "/health" || req.path == "/ready" || std::regex_match(req.path, known);
        if (exists && req.method != "GET" && req.method != "HEAD") {
            error(res, 405, "method_not_allowed", "Only GET and HEAD are supported");
            res.set_header("Allow", "GET, HEAD");
            return httplib::Server::HandlerResponse::Handled;
        }
        return httplib::Server::HandlerResponse::Unhandled;
    });
    server.Get("/health", [](const Request&, Response& res) { reply(res, {{"status", "ok"}}); });
    server.Get("/ready", guarded([config](const Request&, Response& res) {
        Database db(config);
        db.query("SELECT 1 AS ready");
        reply(res, {{"status", "ready"}});
    }));
    server.set_error_handler([](const Request&, Response& res) {
        if (res.body.empty()) error(res, res.status, "not_found", "Route not found");
    });
    server.set_exception_handler([](const Request&, Response& res, std::exception_ptr) {
        error(res, 500, "internal_error", "Internal server error");
    });
    server.set_logger([](const Request& req, const Response& res) {
        // Do not log query strings, request bodies or connection credentials.
        std::cout << req.method << " status=" << res.status << std::endl;
    });
    server.new_task_queue = [] { return new httplib::ThreadPool(8); };
    server.set_read_timeout(10);
    server.set_write_timeout(10);
    server.set_payload_max_length(1024 * 1024);
}

// Block termination signals before worker creation; sigwait runs outside a signal handler.
inline sigset_t termination_signals() {
    sigset_t signals;
    sigemptyset(&signals);
    sigaddset(&signals, SIGTERM);
    sigaddset(&signals, SIGINT);
    if (pthread_sigmask(SIG_BLOCK, &signals, nullptr) != 0) throw std::runtime_error("Signal setup failed");
    return signals;
}
inline int serve(httplib::Server& server, int port, sigset_t signals) {
    if (!server.bind_to_port("0.0.0.0", port)) return 1;
    std::thread listener([&] { server.listen_after_bind(); });
    server.wait_until_ready();
    std::cout << "Listening on port " << port << std::endl;
    int signal = 0;
    sigwait(&signals, &signal);
    server.stop();
    listener.join();
    return 0;
}
} // namespace msa
