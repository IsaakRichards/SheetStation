#include "sheetclient.h"

#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

namespace {
QNetworkRequest makeRequest(const QUrl &url) {
    QNetworkRequest req(url);
#if QT_VERSION >= QT_VERSION_CHECK(5, 9, 0)
    // Apps Script answers a POST with a 302 to a GET-only result URL; follow it by hand
    // so every Qt version behaves the same.
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
#endif
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("SheetStation/0.1"));
    return req;
}
} // namespace

SheetClient::SheetClient(QObject *parent) : QObject(parent), m_nam(new QNetworkAccessManager(this)) {}

void SheetClient::call(const QString &action, QJsonObject args, Callback cb) {
    if (!m_url.isValid() || m_url.isEmpty()) {
        cb(false, QJsonObject(), tr("No Apps Script URL set"));
        return;
    }
    args.insert(QStringLiteral("action"), action);
    QNetworkRequest req = makeRequest(m_url);
    // text/plain avoids a CORS-style preflight and is what Apps Script expects in postData
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("text/plain;charset=utf-8"));
    QNetworkReply *reply = m_nam->post(req, QJsonDocument(args).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply, cb]() { handle(reply, 5, cb); });
}

void SheetClient::handle(QNetworkReply *reply, int redirectsLeft, Callback cb) {
    reply->deleteLater();
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status >= 300 && status < 400) {
        QUrl next = reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl();
        if (next.isEmpty()) next = QUrl(QString::fromUtf8(reply->rawHeader("Location")));
        next = reply->url().resolved(next);
        if (redirectsLeft <= 0 || next.isEmpty()) {
            cb(false, QJsonObject(), tr("Too many redirects"));
            return;
        }
        QNetworkReply *r2 = m_nam->get(makeRequest(next));
        connect(r2, &QNetworkReply::finished, this,
                [this, r2, redirectsLeft, cb]() { handle(r2, redirectsLeft - 1, cb); });
        return;
    }
    if (reply->error() != QNetworkReply::NoError) {
        cb(false, QJsonObject(), reply->errorString());
        return;
    }
    QJsonParseError perr;
    QJsonDocument doc = QJsonDocument::fromJson(reply->readAll(), &perr);
    if (perr.error != QJsonParseError::NoError || !doc.isObject()) {
        cb(false, QJsonObject(),
           tr("Unexpected reply from the sheet (is the web app deployed for \"Anyone\"?)"));
        return;
    }
    QJsonObject obj = doc.object();
    if (!obj.value(QStringLiteral("ok")).toBool()) {
        cb(false, obj, obj.value(QStringLiteral("error")).toString());
        return;
    }
    cb(true, obj, QString());
}
