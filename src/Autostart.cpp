#include "Autostart.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSettings>
#include <QStringList>

#include <optional>

namespace {

constexpr const char *kBundleId = "de.v-und-s.macos-search";
constexpr const char *kSettingsAutostart = "autostart";
constexpr const char *kSettingsFirstRun = "firstRunCompleted";

std::optional<bool> g_prodOverride;
bool g_dryRun = false;
bool g_lastOsRequest = false;
bool g_osCalled = false;

QSettings folderBrowserSettings()
{
    return QSettings("Maude", "FolderBrowser");
}

QString launchAgentPlistPath()
{
    return QDir::homePath()
        + QStringLiteral("/Library/LaunchAgents/")
        + QString::fromLatin1(kBundleId)
        + QStringLiteral(".plist");
}

bool envFlag(const char *name)
{
    const QByteArray v = qgetenv(name);
    return v == "1" || v.compare("true", Qt::CaseInsensitive) == 0;
}

bool dryRunEffective()
{
    return g_dryRun || envFlag("MACOS_SEARCH_DRY_RUN_AUTOSTART");
}

}  // namespace

bool Autostart::isProductionBuild()
{
    if (g_prodOverride.has_value()) return *g_prodOverride;
    if (envFlag("MACOS_SEARCH_FORCE_PROD")) return true;

    const QString dir = QCoreApplication::applicationDirPath();

    // Dev: anywhere in a build* tree (build/, build-benno/, build-cmake/, ...).
    if (dir.contains(QStringLiteral("/build")) ||
        dir.contains(QStringLiteral("/cmake-build"))) {
        return false;
    }

    // Prod: living inside /Applications or ~/Applications/ as a .app.
    const QString systemApps = QStringLiteral("/Applications/");
    const QString userApps = QDir::homePath() + QStringLiteral("/Applications/");
    if (dir.startsWith(systemApps) || dir.startsWith(userApps)) {
        return true;
    }

    return false;
}

bool Autostart::firstRunNeedsPrompt()
{
    if (!isProductionBuild()) return false;
    return !folderBrowserSettings()
                .value(QString::fromLatin1(kSettingsFirstRun), false)
                .toBool();
}

void Autostart::markFirstRunCompleted()
{
    QSettings s = folderBrowserSettings();
    s.setValue(QString::fromLatin1(kSettingsFirstRun), true);
    s.sync();
}

bool Autostart::isEnabled()
{
    return folderBrowserSettings()
        .value(QString::fromLatin1(kSettingsAutostart), false)
        .toBool();
}

void Autostart::setEnabled(bool enabled)
{
    QSettings s = folderBrowserSettings();
    s.setValue(QString::fromLatin1(kSettingsAutostart), enabled);
    s.sync();

    g_lastOsRequest = enabled;
    g_osCalled = true;

    if (dryRunEffective()) return;
    if (!isProductionBuild()) return;

    const QString plistPath = launchAgentPlistPath();
    QDir().mkpath(QFileInfo(plistPath).absolutePath());

    if (enabled) {
        const QString execPath = QCoreApplication::applicationFilePath();
        QFile file(plistPath);
        if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            const QString plist = QStringLiteral(
                "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
                "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
                "<plist version=\"1.0\">\n"
                "<dict>\n"
                "  <key>Label</key>\n"
                "  <string>%1</string>\n"
                "  <key>ProgramArguments</key>\n"
                "  <array>\n"
                "    <string>%2</string>\n"
                "  </array>\n"
                "  <key>RunAtLoad</key>\n"
                "  <true/>\n"
                "  <key>KeepAlive</key>\n"
                "  <false/>\n"
                "  <key>ProcessType</key>\n"
                "  <string>Interactive</string>\n"
                "</dict>\n"
                "</plist>\n").arg(QString::fromLatin1(kBundleId), execPath);
            file.write(plist.toUtf8());
            file.close();
        }
        QProcess::execute(QStringLiteral("launchctl"),
                          { QStringLiteral("load"),
                            QStringLiteral("-w"),
                            plistPath });
    } else {
        QProcess::execute(QStringLiteral("launchctl"),
                          { QStringLiteral("unload"),
                            QStringLiteral("-w"),
                            plistPath });
        QFile::remove(plistPath);
    }
}

void Autostart::applyFirstRunChoice(bool enableAutostart)
{
    markFirstRunCompleted();
    setEnabled(enableAutostart);
}

void Autostart::Testing::overrideProductionBuild(bool prod) { g_prodOverride = prod; }
void Autostart::Testing::clearProductionBuildOverride() { g_prodOverride.reset(); }
void Autostart::Testing::setDryRun(bool dryRun) { g_dryRun = dryRun; }
bool Autostart::Testing::dryRun() { return g_dryRun; }
bool Autostart::Testing::lastOsRegistrationRequest() { return g_lastOsRequest; }
bool Autostart::Testing::osRegistrationCalled() { return g_osCalled; }
void Autostart::Testing::resetCallTracking()
{
    g_lastOsRequest = false;
    g_osCalled = false;
}
