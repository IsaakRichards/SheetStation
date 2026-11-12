// SheetStation - the app's brain, exposed to QML as "station".
// Owns the sheet polling loop, the WebRTC links (ss::Peer) and the transfer list.
#pragma once

#include <QElapsedTimer>
#include <QEvent>
#include <QHash>
#include <QObject>
#include <QSet>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <functional>
#include <memory>

#include "peer.h"

class SheetClient;

class Station : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString scriptUrl READ scriptUrl WRITE setScriptUrl NOTIFY settingsChanged)
    Q_PROPERTY(QString deviceName READ deviceName WRITE setDeviceName NOTIFY settingsChanged)
    Q_PROPERTY(QString shareDir READ shareDir WRITE setShareDir NOTIFY settingsChanged)
    Q_PROPERTY(QString downloadDir READ downloadDir WRITE setDownloadDir NOTIFY settingsChanged)
    Q_PROPERTY(QString stunServers READ stunServers WRITE setStunServers NOTIFY settingsChanged)
    Q_PROPERTY(QString deviceId READ deviceId CONSTANT)
    Q_PROPERTY(QString platform READ platform CONSTANT)
    Q_PROPERTY(bool configured READ configured NOTIFY settingsChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(bool sheetOk READ sheetOk NOTIFY statusChanged)
    Q_PROPERTY(QVariantList peers READ peers NOTIFY peersChanged)
    Q_PROPERTY(QVariantList myFiles READ myFiles NOTIFY myFilesChanged)
    Q_PROPERTY(QVariantList transfers READ transfers NOTIFY transfersChanged)
    Q_PROPERTY(int activeTransfers READ activeTransfers NOTIFY transfersChanged)

public:
    explicit Station(QObject *parent = nullptr);
    ~Station() override;

    QString scriptUrl() const { return m_scriptUrl; }
    QString deviceName() const { return m_deviceName; }
    QString shareDir() const { return m_shareDir; }
    QString downloadDir() const { return m_downloadDir; }
    QString stunServers() const { return m_stunServers; }
    QString deviceId() const { return m_deviceId; }
    QString platform() const;
    bool configured() const { return !m_scriptUrl.trimmed().isEmpty(); }
    QString status() const { return m_status; }
    bool sheetOk() const { return m_sheetOk; }
    QVariantList peers() const { return m_peersList; }
    QVariantList myFiles() const { return m_myFiles; }
    QVariantList transfers() const;
    int activeTransfers() const;

    void setScriptUrl(const QString &v);
    void setDeviceName(const QString &v);
    void setShareDir(const QString &v);
    void setDownloadDir(const QString &v);
    void setStunServers(const QString &v);

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void download(const QString &peerId, const QString &fileName);
    Q_INVOKABLE void clearFinished();
    Q_INVOKABLE bool openPath(const QString &path);
    Q_INVOKABLE QString formatSize(double bytes) const;

signals:
    void settingsChanged();
    void statusChanged();
    void peersChanged();
    void myFilesChanged();
    void transfersChanged();
    void notify(const QString &message);

protected:
    void customEvent(QEvent *e) override;

private:
    struct Link {
        std::shared_ptr<ss::Peer> peer;
        quint64 gen = 0;
        QElapsedTimer age;
        QList<QPair<quint64, QString>> requested; // uid, file name (for glare hand-over)
    };

    void post(std::function<void()> fn); // run fn on the GUI thread
    void loadSettings();
    void saveSettings();
    void setStatus(const QString &s, bool ok);

    void sendHello();
    void fetchPeers();
    void poll();
    void housekeeping();
    void updatePollInterval();

    void handleMessage(const QString &from, const QString &type, const QString &payload);
    Link &createLink(const QString &peerId, ss::Peer::Role role);
    void dropLink(const QString &peerId);
    void onLinkState(const QString &peerId, quint64 gen, ss::LinkState st);
    void onTransfer(const ss::TransferEvent &e);
    void rebuildPeersList();
    void scheduleTransfersChanged();
    QString peerName(const QString &peerId) const;

    SheetClient *m_sheet;
    QTimer m_pollTimer, m_helloTimer, m_peersTimer, m_houseTimer, m_transfersDirty;
    bool m_pollBusy = false;
    QElapsedTimer m_lastSignal;

    QString m_scriptUrl, m_deviceName, m_shareDir, m_downloadDir, m_stunServers, m_deviceId;
    QString m_status;
    bool m_sheetOk = false;

    QVariantList m_rawPeers;  // as returned by the sheet
    QVariantList m_peersList; // decorated for QML
    QVariantList m_myFiles;
    QHash<QString, Link> m_links;
    quint64 m_nextGen = 1;

    QList<quint64> m_transferOrder;
    QHash<quint64, QVariantMap> m_transfers;
    QSet<quint64> m_suppressed;
};
