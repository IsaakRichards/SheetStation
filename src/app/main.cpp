#include "station.h"

#include <QGuiApplication>
#include <QQmlContext>
#include <QUrl>

#ifdef SHEETSTATION_SAILFISH
#include <QQuickView>
#include <QScopedPointer>
#include <sailfishapp.h>

int main(int argc, char *argv[]) {
    QScopedPointer<QGuiApplication> app(SailfishApp::application(argc, argv));
    app->setOrganizationName(QStringLiteral("sheetstation"));
    app->setApplicationName(QStringLiteral("sheetstation"));
    Station station;
    QScopedPointer<QQuickView> view(SailfishApp::createView());
    view->rootContext()->setContextProperty(QStringLiteral("station"), &station);
    view->setSource(QUrl(QStringLiteral("qrc:/qml/sailfish/sheetstation.qml")));
    view->show();
    return app->exec();
}

#else
#include <QQmlApplicationEngine>

int main(int argc, char *argv[]) {
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
#endif
    QGuiApplication app(argc, argv);
    app.setOrganizationName(QStringLiteral("SheetStation"));
    app.setApplicationName(QStringLiteral("SheetStation"));
    app.setApplicationVersion(QStringLiteral(SHEETSTATION_VERSION));

    Station station;
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("station"), &station);
    engine.load(QUrl(QStringLiteral("qrc:/qml/generic/main.qml")));
    if (engine.rootObjects().isEmpty()) return 1;
    return app.exec();
}
#endif
