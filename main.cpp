#include "mainwindow.h"
#include "version.h"

#include <QApplication>
#include <QFont>
#include <QFontDatabase>
#include <QIcon>
#include <QSettings>
#include <QStyleFactory>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setOrganizationName(QStringLiteral("Ralf Tralow"));
    app.setApplicationName(QStringLiteral("TR4131-Viewer"));
    app.setWindowIcon(QIcon(QStringLiteral(":/device-icon.ico")));
    app.setApplicationVersion(QStringLiteral(TR4131_VIEWER_VERSION));
    app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

    QFont font = app.font();
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    const QFontDatabase fontDatabase;
    if (fontDatabase.hasFamily(QStringLiteral("Inter"))) {
#else
    if (QFontDatabase::hasFamily(QStringLiteral("Inter"))) {
#endif
        font.setFamily(QStringLiteral("Inter"));
    }
    font.setPointSize(10);
    app.setFont(font);

    MainWindow window;
    QSettings settings;
    const auto geometryKey = QStringLiteral("window/geometry");
    window.restoreGeometry(settings.value(geometryKey).toByteArray());
    window.show();

    const int exitCode = app.exec();
    settings.setValue(geometryKey, window.saveGeometry());
    return exitCode;
}
