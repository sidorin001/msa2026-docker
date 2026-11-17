#include <QCoreApplication>
#include <QHttpServer>
#include <QHttpServerRequest>
#include <QHttpServerResponder>
#include <QTcpSocket>
#include <QPointer>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QVariant>
#include <QUuid>
#include <QtConcurrent>
#include <QFutureWatcher>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkProxy>
#include <QRegularExpression>
#include <QTimer>
#include <QThreadPool>
#include <csignal>
#include <limits>
#include <memory>
#include <stdexcept>
#include <thread>
#include <pthread.h>

namespace {
QString env(const char* key, const char* fallback = "") {
    return qEnvironmentVariable(key, QString::fromUtf8(fallback));
}
int positive(const QString& value) {
    static const QRegularExpression digits("^[0-9]+$");
    bool ok = false;
    int id = value.toInt(&ok);
    if (!digits.match(value).hasMatch() || !ok || id <= 0) throw std::invalid_argument("id");
    return id;
}
int setting(const char* key, const char* fallback) { return positive(env(key, fallback)); }
struct Config {
    QString host = env("DB_HOST"), name = env("DB_NAME"), user = env("DB_USER"), password = env("DB_PASSWORD");
    int port = setting("DB_PORT", "5432"), httpPort = setting("HTTP_PORT", "8080");
    int dbConnect = setting("DB_CONNECT_TIMEOUT_SECONDS", "2"), statement = setting("DB_STATEMENT_TIMEOUT_MS", "3000");
    int connect = setting("HTTP_CONNECT_TIMEOUT_MS", "2000"), read = setting("HTTP_READ_TIMEOUT_MS", "10000");
    QUrl upstream{env("SERVICE2_BASE_URL", "http://gateway:80")};
    Config() {
        if (host.isEmpty() || name.isEmpty() || user.isEmpty() || password.isEmpty() || port > 65535 || httpPort > 65535 ||
            !upstream.isValid() || upstream.scheme() != "http" || upstream.host().isEmpty() ||
            !upstream.userInfo().isEmpty() || !upstream.query().isEmpty() || !upstream.fragment().isEmpty() ||
            (!upstream.path().isEmpty() && upstream.path() != "/")) throw std::runtime_error("configuration");
    }
};
struct Reply { int status = 200; QJsonDocument body; QByteArray instance; };
Reply error(int status, const char* code, const char* message) {
    QJsonObject detail{{"code", code}, {"message", message}};
    return {status, QJsonDocument(QJsonObject{{"error", detail}}), {}};
}
class Database {
    QString connectionName = QUuid::createUuid().toString();
    QSqlDatabase db;
public:
    explicit Database(const Config& c) : db(QSqlDatabase::addDatabase("QPSQL", connectionName)) {
        db.setHostName(c.host); db.setPort(c.port); db.setDatabaseName(c.name);
        db.setUserName(c.user); db.setPassword(c.password);
        db.setConnectOptions(QString("connect_timeout=%1;options='-c statement_timeout=%2'").arg(c.dbConnect).arg(c.statement));
    }
    ~Database() { db.close(); db = QSqlDatabase(); QSqlDatabase::removeDatabase(connectionName); }
    QJsonArray query(const QString& sql, int id = 0) {
        if (!db.isOpen() && !db.open()) throw std::runtime_error("database");
        QSqlQuery query(db);
        if (!query.prepare(sql)) throw std::runtime_error("database");
        if (id) query.addBindValue(id);
        if (!query.exec()) throw std::runtime_error("database");
        QJsonArray rows;
        while (query.next()) {
            QJsonObject row;
            const auto record = query.record();
            for (int i = 0; i < record.count(); ++i) {
                const auto field = record.fieldName(i);
                row.insert(field, field == "id" ? QJsonValue(query.value(i).toInt()) : QJsonValue(query.value(i).toString()));
            }
            rows.append(row);
        }
        return rows;
    }
};
struct Client {
    std::shared_ptr<QHttpServerResponder> responder;
    QPointer<QTcpSocket> socket;
    bool head;
    void send(const Reply& result) const {
        if (!socket || socket->state() != QAbstractSocket::ConnectedState) return;
        const QByteArray body = result.body.toJson(QJsonDocument::Compact);
        responder->writeStatusLine(static_cast<QHttpServerResponder::StatusCode>(result.status));
        responder->writeHeader("Content-Type", "application/json");
        responder->writeHeader("Content-Length", QByteArray::number(body.size()));
        responder->writeHeader("Connection", "close");
        if (result.status == 405) responder->writeHeader("Allow", "GET, HEAD");
        if (!result.instance.isEmpty()) responder->writeHeader("X-Service2-Instance", result.instance);
        responder->writeBody(head ? QByteArray() : body);
        socket->disconnectFromHost();
        qInfo("Service1 status=%d", result.status);
    }
};
class Application : public QObject {
    Config config;
    QHttpServer server;
    QNetworkAccessManager network;
    QThreadPool workers;
    void aggregate(Client client, int id) {
        QUrl url = config.upstream;
        url.setPath("/api/v1/items"); url.setQuery("group_id=" + QString::number(id));
        QNetworkRequest request(url);
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
        auto* reply = network.get(request);
        reply->setReadBufferSize(1024 * 1024 + 1);
        auto* deadline = new QTimer(reply);
        deadline->setSingleShot(true);
        auto timedOut = std::make_shared<bool>(false);
        auto oversized = std::make_shared<bool>(false);
        auto body = std::make_shared<QByteArray>();
        connect(deadline, &QTimer::timeout, reply, [reply, timedOut] { *timedOut = true; reply->abort(); });
        auto* connectionTimer = new QTimer(reply);
        connectionTimer->setSingleShot(true);
        connect(connectionTimer, &QTimer::timeout, reply, [reply, timedOut] { *timedOut = true; reply->abort(); });
        connect(reply, &QNetworkReply::requestSent, connectionTimer, &QTimer::stop);
        connect(reply, &QNetworkReply::readyRead, reply, [reply, body, oversized] {
            body->append(reply->readAll());
            if (body->size() > 1024 * 1024) { *oversized = true; reply->abort(); }
        });
        connect(reply, &QNetworkReply::finished, this, [reply, client, id, body, timedOut, oversized] {
            body->append(reply->readAll());
            auto result = error(502, "bad_gateway", "Service2 unavailable or invalid response");
            if (*timedOut) result = error(504, "upstream_timeout", "Service2 timeout");
            else if (!*oversized && body->size() <= 1024 * 1024 && reply->error() == QNetworkReply::NoError &&
                     reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 200) {
                auto document = QJsonDocument::fromJson(*body);
                bool valid = document.isArray();
                int previous = 0;
                if (valid) for (const auto& value : document.array()) {
                    const auto item = value.toObject();
                    double number = item.value("id").toDouble(-1);
                    if (!item.value("id").isDouble() || number <= previous || number > std::numeric_limits<int>::max() ||
                        number != static_cast<int>(number) || !item.value("group_id").isDouble() ||
                        item.value("group_id").toDouble() != id || !item.value("name").isString()) { valid = false; break; }
                    previous = static_cast<int>(number);
                }
                if (valid) result = {200, document, reply->rawHeader("X-Service2-Instance")};
            }
            client.send(result); reply->deleteLater();
        });
        deadline->start(config.read);
        connectionTimer->start(config.connect);
    }
    void dispatch(const QHttpServerRequest& request, QHttpServerResponder&& responder) {
        Client client{std::make_shared<QHttpServerResponder>(std::move(responder)), nullptr,
                      request.method() == QHttpServerRequest::Method::Head};
        client.socket = client.responder->socket();
        const auto path = request.url().path();
        static const QRegularExpression resource("^/api/v1/groups/([^/]+)(/items)?$");
        const auto match = resource.match(path);
        bool known = match.hasMatch() || path == "/api/v1/groups" || path == "/ready" || path == "/health";
        if (!known) { client.send(error(404, "not_found", "Route not found")); return; }
        if (request.method() != QHttpServerRequest::Method::Get && !client.head) {
            client.send(error(405, "method_not_allowed", "Only GET and HEAD are supported")); return;
        }
        if (path == "/health") { client.send({200, QJsonDocument(QJsonObject{{"status", "ok"}}), {}}); return; }
        int id = 0;
        if (match.hasMatch()) {
            try { id = positive(match.captured(1)); }
            catch (const std::invalid_argument&) { client.send(error(400, "invalid_id", "Expected a positive 32-bit integer")); return; }
        }
        const bool grouped = match.hasMatch() && !match.captured(2).isEmpty();
        auto* watcher = new QFutureWatcher<Reply>(this);
        connect(watcher, &QFutureWatcher<Reply>::finished, this, [this, watcher, client, id, grouped] {
            Reply result = watcher->result(); watcher->deleteLater();
            if (grouped && result.status == 200) aggregate(client, id);
            else client.send(result);
        });
        watcher->setFuture(QtConcurrent::run(&workers, [config = config, path, id] {
            try {
                Database db(config);
                if (path == "/ready") {
                    db.query("SELECT 1 AS ready"); return Reply{200, QJsonDocument(QJsonObject{{"status", "ready"}}), {}};
                }
                auto rows = id ? db.query("SELECT id, name FROM groups WHERE id = ?", id)
                               : db.query("SELECT id, name FROM groups ORDER BY id");
                if (id && rows.isEmpty()) return error(404, "not_found", "Group not found");
                return Reply{200, id ? QJsonDocument(rows[0].toObject()) : QJsonDocument(rows), {}};
            } catch (const std::exception&) { return error(503, "database_unavailable", "Database unavailable"); }
        }));
    }
public:
    Application() {
        workers.setMaxThreadCount(8);
        network.setProxy(QNetworkProxy::NoProxy);
        if (!QSqlDatabase::isDriverAvailable("QPSQL")) throw std::runtime_error("QPSQL missing");
        server.setMissingHandler([this](const auto& request, auto&& responder) { dispatch(request, std::move(responder)); });
        if (!server.listen(QHostAddress::AnyIPv4, config.httpPort)) throw std::runtime_error("listen");
        qInfo("Service1: Qt %s, QPSQL, port %d", qVersion(), config.httpPort);
    }
};
}
int main(int argc, char** argv) {
    sigset_t terminationSet;
    sigemptyset(&terminationSet); sigaddset(&terminationSet, SIGINT); sigaddset(&terminationSet, SIGTERM);
    if (pthread_sigmask(SIG_BLOCK, &terminationSet, nullptr) != 0) return 1;
    QCoreApplication app(argc, argv);
    try {
        Application service;
        std::thread terminator([&] {
            int signal = 0; sigwait(&terminationSet, &signal);
            QMetaObject::invokeMethod(&app, &QCoreApplication::quit, Qt::QueuedConnection);
        });
        int result = app.exec();
        pthread_kill(terminator.native_handle(), SIGTERM); terminator.join();
        return result;
    } catch (const std::exception&) {
        qCritical("Service1 startup failed; check configuration and Qt plugins"); return 1;
    }
}
