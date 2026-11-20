#include "mainwindow.h"

#include <QAction>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QProcessEnvironment>
#include <QPushButton>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <QVersionNumber>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {

constexpr auto releaseEndpoint = "repos/GitterRalf/tr4131-viewer/releases/latest";
constexpr int updateTimeout = 10000;

QString githubCli()
{
    for (const auto *key : {"LOCALAPPDATA", "ProgramFiles", "ProgramW6432", "ProgramFiles(x86)"}) {
        const auto base = qEnvironmentVariable(key);
        if (base.isEmpty()) {
            continue;
        }
        const auto folder = QString::fromLatin1(key) == QStringLiteral("LOCALAPPDATA")
            ? QStringLiteral("Programs/GitHub CLI") : QStringLiteral("GitHub CLI");
        const auto executable = QDir(base).filePath(folder + QStringLiteral("/gh.exe"));
        if (QFileInfo(executable).isFile()) {
            return executable;
        }
    }
    return QStandardPaths::findExecutable(QStringLiteral("gh.exe"));
}

} // namespace

void MainWindow::checkForUpdates()
{
    if (updateCheckInProgress) {
        return;
    }
    updateCheckInProgress = true;
    updateAction->setEnabled(false);
    auto *network = new QNetworkAccessManager(this);
    QNetworkRequest request{QUrl(QStringLiteral("https://api.github.com/") + QLatin1String(releaseEndpoint))};
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setRawHeader("User-Agent", "TR4131-Viewer");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::SameOriginRedirectPolicy);
    auto *reply = network->get(request);
    QTimer::singleShot(updateTimeout, reply, &QNetworkReply::abort);

    connect(reply, &QNetworkReply::finished, this, [this, network, reply] {
        const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const auto data = reply->readAll();
        const auto error = reply->error();
        network->deleteLater();
        if (status == 404) {
            checkPrivateRelease();
        } else if (error == QNetworkReply::NoError && status == 200) {
            finishUpdateCheck(data);
        } else {
            finishUpdateCheck({}, tr("GitHub konnte nicht abgefragt werden. Prüfe die Internetverbindung und versuche es erneut."));
        }
    });
}

void MainWindow::checkPrivateRelease()
{
    const auto executable = githubCli();
    if (executable.isEmpty()) {
        finishUpdateCheck({}, tr("GitHub liefert keine öffentliche Version. Für das private Repository ist eine Anmeldung über die GitHub CLI erforderlich."));
        return;
    }

    auto *process = new QProcess(this);
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("GH_PROMPT_DISABLED"), QStringLiteral("1"));
    environment.insert(QStringLiteral("GH_NO_UPDATE_NOTIFIER"), QStringLiteral("1"));
    environment.insert(QStringLiteral("GH_PAGER"), QStringLiteral("cat"));
    process->setProcessEnvironment(environment);
    process->setStandardErrorFile(QProcess::nullDevice());
#ifdef Q_OS_WIN
    process->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) {
        args->flags |= CREATE_NO_WINDOW;
    });
#endif
    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            process->deleteLater();
            finishUpdateCheck({}, tr("Die GitHub CLI konnte nicht gestartet werden."));
        }
    });
    connect(process, &QProcess::finished, this, [this, process](int exitCode, QProcess::ExitStatus status) {
        const auto data = process->readAllStandardOutput();
        process->deleteLater();
        if (status == QProcess::NormalExit && exitCode == 0) {
            finishUpdateCheck(data);
        } else {
            finishUpdateCheck({}, tr("Das GitHub-Release ist nicht erreichbar. Prüfe die Internetverbindung und ob du mit „gh auth login“ bei einem Konto mit Zugriff angemeldet bist."));
        }
    });
    QTimer::singleShot(updateTimeout, process, [process] {
        if (process->state() != QProcess::NotRunning) {
            process->kill();
        }
    });
    process->start(executable, {QStringLiteral("api"), QStringLiteral("--hostname"), QStringLiteral("github.com"),
                                QStringLiteral("--method"), QStringLiteral("GET"), QLatin1String(releaseEndpoint)});
}

void MainWindow::finishUpdateCheck(const QByteArray &data, const QString &error)
{
    updateCheckInProgress = false;
    updateAction->setEnabled(true);
    const auto title = tr("Nach Updates suchen");
    if (!error.isEmpty()) {
        QMessageBox::warning(this, title, error);
        return;
    }

    const auto release = QJsonDocument::fromJson(data).object();
    const auto tag = release.value(QStringLiteral("tag_name")).toString();
    static const QRegularExpression versionTag(QStringLiteral("^v?([0-9]{1,6}\\.[0-9]{1,6}\\.[0-9]{1,6})$"));
    const auto match = versionTag.match(tag);
    const auto installed = QCoreApplication::applicationVersion();
    const auto installedVersion = QVersionNumber::fromString(installed);
    if (!match.hasMatch() || installedVersion.isNull() ||
        release.value(QStringLiteral("draft")).toBool() || release.value(QStringLiteral("prerelease")).toBool()) {
        QMessageBox::warning(this, title, tr("GitHub hat keine gültige Release-Version geliefert."));
        return;
    }
    const auto available = match.captured(1);
    if (QVersionNumber::fromString(available) <= installedVersion) {
        QMessageBox::information(this, title, tr("Keine neuere Version verfügbar.\nInstalliert: %1\nAuf GitHub: %2").arg(installed, available));
        return;
    }

    QMessageBox dialog(QMessageBox::Information, title,
                       tr("Version %1 ist verfügbar. Installiert ist Version %2.").arg(available, installed),
                       QMessageBox::Close, this);
    auto *openRelease = dialog.addButton(tr("Release auf GitHub öffnen"), QMessageBox::ActionRole);
    dialog.exec();
    if (dialog.clickedButton() == openRelease) {
        QDesktopServices::openUrl(QUrl(QStringLiteral("https://github.com/GitterRalf/tr4131-viewer/releases/tag/") + tag));
    }
}
