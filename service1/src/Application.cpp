#include "Application.h"
#include "Database.h"
#include "Reply.h"
#include "Validation.h"
#include <QFutureWatcher>
#include <QHttpServerRequest>
#include <QHttpServerResponder>
#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QTcpSocket>
#include <QTimer>
#include <QtConcurrent>
#include <limits>
#include <stdexcept>
#include <utility>

namespace msa {
void Application::aggregate(Client client, int id) {
    QUrl url = config.upstream;
    url.setPath("/api/v1/items");
    url.setQuery("group_id=" + QString::number(id));
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::ManualRedirectPolicy);
    auto* reply = network.get(request);
    reply->setReadBufferSize(1024 * 1024 + 1);
    auto* deadline = new QTimer(reply);
    deadline->setSingleShot(true);
    auto timedOut = std::make_shared<bool>(false);
    auto oversized = std::make_shared<bool>(false);
    auto body = std::make_shared<QByteArray>();
    connect(deadline, &QTimer::timeout, reply, [reply, timedOut] {
        *timedOut = true;
        reply->abort();
    });
    auto* connectionTimer = new QTimer(reply);
    connectionTimer->setSingleShot(true);
    connect(connectionTimer, &QTimer::timeout, reply, [reply, timedOut] {
        *timedOut = true;
        reply->abort();
    });
    connect(reply, &QNetworkReply::requestSent, connectionTimer, &QTimer::stop);
    connect(reply, &QNetworkReply::readyRead, reply, [reply, body, oversized] {
        body->append(reply->readAll());
        if (body->size() > 1024 * 1024) {
            *oversized = true;
            reply->abort();
        }
    });
    connect(reply, &QNetworkReply::finished, this, [reply, client, id, body, timedOut, oversized] {
        body->append(reply->readAll());
        auto result = error(502, "bad_gateway", "Service2 unavailable or invalid response");
        if (*timedOut)
            result = error(504, "upstream_timeout", "Service2 timeout");
        else if (!*oversized && body->size() <= 1024 * 1024 &&
                 reply->error() == QNetworkReply::NoError &&
                 reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 200) {
            auto document = QJsonDocument::fromJson(*body);
            bool valid = document.isArray();
            int previous = 0;
            if (valid)
                for (const auto& value : document.array()) {
                    const auto item = value.toObject();
                    double number = item.value("id").toDouble(-1);
                    if (!item.value("id").isDouble() || number <= previous ||
                        number > std::numeric_limits<int>::max() ||
                        number != static_cast<int>(number) || !item.value("group_id").isDouble() ||
                        item.value("group_id").toDouble() != id || !item.value("name").isString()) {
                        valid = false;
                        break;
                    }
                    previous = static_cast<int>(number);
                }
            if (valid)
                result = {200, document, reply->rawHeader("X-Service2-Instance")};
        }
        client.send(result);
        reply->deleteLater();
    });
    deadline->start(config.read);
    connectionTimer->start(config.connect);
}

void Application::dispatch(const QHttpServerRequest& request, QHttpServerResponder&& responder) {
    Client client{std::make_shared<QHttpServerResponder>(std::move(responder)), nullptr,
                  request.method() == QHttpServerRequest::Method::Head};
    client.socket = client.responder->socket();
    const auto path = request.url().path();
    static const QRegularExpression resource("^/api/v1/groups/([^/]+)(/items)?$");
    const auto match = resource.match(path);
    bool known =
        match.hasMatch() || path == "/api/v1/groups" || path == "/ready" || path == "/health";
    if (!known) {
        client.send(error(404, "not_found", "Route not found"));
        return;
    }
    if (request.method() != QHttpServerRequest::Method::Get && !client.head) {
        client.send(error(405, "method_not_allowed", "Only GET and HEAD are supported"));
        return;
    }
    if (path == "/health") {
        client.send({200, QJsonDocument(QJsonObject{{"status", "ok"}}), {}});
        return;
    }
    int id = 0;
    if (match.hasMatch()) {
        try {
            id = positive(match.captured(1));
        } catch (const std::invalid_argument&) {
            client.send(error(400, "invalid_id", "Expected a positive 32-bit integer"));
            return;
        }
    }
    const bool grouped = match.hasMatch() && !match.captured(2).isEmpty();
    auto* watcher = new QFutureWatcher<Reply>(this);
    connect(watcher, &QFutureWatcher<Reply>::finished, this, [this, watcher, client, id, grouped] {
        Reply result = watcher->result();
        watcher->deleteLater();
        if (grouped && result.status == 200)
            aggregate(client, id);
        else
            client.send(result);
    });
    watcher->setFuture(QtConcurrent::run(&workers, [config = config, path, id] {
        try {
            Database db(config);
            if (path == "/ready") {
                db.query("SELECT 1 AS ready");
                return Reply{200, QJsonDocument(QJsonObject{{"status", "ready"}}), {}};
            }
            auto rows = id ? db.query("SELECT id, name FROM groups WHERE id = ?", id)
                           : db.query("SELECT id, name FROM groups ORDER BY id");
            if (id && rows.isEmpty())
                return error(404, "not_found", "Group not found");
            return Reply{200, id ? QJsonDocument(rows[0].toObject()) : QJsonDocument(rows), {}};
        } catch (const std::exception&) {
            return error(503, "database_unavailable", "Database unavailable");
        }
    }));
}

Application::Application() {
    workers.setMaxThreadCount(8);
    network.setProxy(QNetworkProxy::NoProxy);
    if (!QSqlDatabase::isDriverAvailable("QPSQL"))
        throw std::runtime_error("QPSQL missing");
    server.setMissingHandler(
        [this](const auto& request, auto&& responder) { dispatch(request, std::move(responder)); });
    if (!server.listen(QHostAddress::AnyIPv4, config.httpPort))
        throw std::runtime_error("listen");
    qInfo("Service1: Qt %s, QPSQL, port %d", qVersion(), config.httpPort);
}
} // namespace msa
