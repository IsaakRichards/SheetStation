// SheetStation - talks to the Apps Script web app bound to the Google Sheet.
// Written against the Qt 5.6 API subset so it also builds on Sailfish OS.
#pragma once

#include <QJsonObject>
#include <QObject>
#include <QUrl>
#include <functional>

class QNetworkAccessManager;
class QNetworkReply;

class SheetClient : public QObject {
    Q_OBJECT
public:
    using Callback = std::function<void(bool ok, const QJsonObject &reply, const QString &error)>;

    explicit SheetClient(QObject *parent = nullptr);
    void setUrl(const QUrl &url) { m_url = url; }
    QUrl url() const { return m_url; }

    // POSTs {"action": action, ...args} and calls cb with the parsed JSON reply.
    void call(const QString &action, QJsonObject args, Callback cb);

private:
    void handle(QNetworkReply *reply, int redirectsLeft, Callback cb);
    QNetworkAccessManager *m_nam;
    QUrl m_url;
};
