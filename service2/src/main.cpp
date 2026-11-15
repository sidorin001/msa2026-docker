#include "runtime.hpp"

int main() {
    try {
        auto signals = msa::termination_signals();
        msa::Config config;
        httplib::Server server;
        msa::configure(server, config, false);
        server.Get("/api/v1/items", msa::guarded([&](const auto& req, auto& res) {
            // Validate before connecting, including duplicate query parameters.
            std::vector<std::string> params;
            if (req.has_param("group_id")) {
                if (req.get_param_value_count("group_id") != 1) throw std::invalid_argument("Duplicate group_id");
                params.push_back(std::to_string(msa::positive(req.get_param_value("group_id"))));
            }
            msa::Database db(config);
            msa::reply(res, params.empty()
                ? db.query("SELECT id, group_id, name FROM items ORDER BY id")
                : db.query("SELECT id, group_id, name FROM items WHERE group_id = $1 ORDER BY id", params));
        }));
        server.Get(R"(/api/v1/items/([^/]+))", msa::guarded([&](const auto& req, auto& res) {
            int id = msa::positive(req.matches[1].str());
            msa::Database db(config);
            auto rows = db.query("SELECT id, group_id, name FROM items WHERE id = $1", {std::to_string(id)});
            if (rows.empty()) msa::error(res, 404, "not_found", "Item not found");
            else msa::reply(res, rows[0]);
        }));
        return msa::serve(server, config.http_port, signals);
    } catch (const std::exception&) {
        std::cerr << "Service2 startup failed; check environment configuration" << std::endl;
        return 1;
    }
}
