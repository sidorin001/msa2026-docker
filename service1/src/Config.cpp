#include "Config.h"
#include "Validation.h"
#include <stdexcept>

namespace msa {
namespace {
QString env(const char* key, const char* fallback = "") {
    return qEnvironmentVariable(key, QString::fromUtf8(fallback));
}

int setting(const char* key, const char* fallback) {
    return positive(env(key, fallback));
}
} // namespace

Config::Config()
    : host(env("DB_HOST")), name(env("DB_NAME")), user(env("DB_USER")),
      password(env("DB_PASSWORD")), port(setting("DB_PORT", "5432")),
      httpPort(setting("HTTP_PORT", "8080")), dbConnect(setting("DB_CONNECT_TIMEOUT_SECONDS", "2")),
      statement(setting("DB_STATEMENT_TIMEOUT_MS", "3000")),
      connect(setting("HTTP_CONNECT_TIMEOUT_MS", "2000")),
      read(setting("HTTP_READ_TIMEOUT_MS", "10000")),
      upstream(env("SERVICE2_BASE_URL", "http://gateway:80")) {
    if (host.isEmpty() || name.isEmpty() || user.isEmpty() || password.isEmpty() || port > 65535 ||
        httpPort > 65535 || !upstream.isValid() || upstream.scheme() != "http" ||
        upstream.host().isEmpty() || !upstream.userInfo().isEmpty() ||
        !upstream.query().isEmpty() || !upstream.fragment().isEmpty() ||
        (!upstream.path().isEmpty() && upstream.path() != "/"))
        throw std::runtime_error("configuration");
}
} // namespace msa
