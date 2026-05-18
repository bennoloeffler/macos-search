#include "MaudeConfig.h"
#include <QDir>
#include <QFile>
#include <QSettings>
#include <QStandardPaths>

QString MaudeConfig::configDir()
{
    // Drift vs. maude-cp-v3: was ~/.maude. Standalone app stores under
    // ~/.macos-search so there's no collision with an installed maude.
    QString homeDir = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
    return homeDir + "/.macos-search";
}

QString MaudeConfig::settingsFilePath()
{
    return configDir() + "/settings.ini";
}

QString MaudeConfig::claudeConfigDir()
{
    return configDir() + "/claude-config";
}

bool MaudeConfig::ensureConfigDirExists()
{
    QString path = configDir();
    QDir dir(path);
    if (dir.exists()) {
        return true;
    }
    return dir.mkpath(".");
}

void MaudeConfig::migrateLegacySettings()
{
    QString newSettingsPath = settingsFilePath();

    // Check if new settings already exist
    if (QFile::exists(newSettingsPath)) {
        return;
    }

    // Check if legacy settings have any of our app-specific data
    // Note: On macOS, QSettings includes system settings from NSGlobalDomain,
    // so we can't use allKeys().isEmpty() - we must check our specific keys
    QSettings legacySettings;

    bool hasRecentProjects = legacySettings.contains("recentProjects");

    legacySettings.beginGroup("ExcludeSettings");
    bool hasExcludeSettings = legacySettings.contains("patterns");
    legacySettings.endGroup();

    if (!hasRecentProjects && !hasExcludeSettings) {
        return;
    }

    // Ensure config directory exists
    ensureConfigDirExists();

    // Create new settings and copy app-specific data only
    QSettings newSettings(newSettingsPath, QSettings::IniFormat);

    // Copy recentProjects if present
    if (hasRecentProjects) {
        newSettings.setValue("recentProjects", legacySettings.value("recentProjects"));
    }

    // Copy ExcludeSettings group if present
    if (hasExcludeSettings) {
        legacySettings.beginGroup("ExcludeSettings");
        newSettings.beginGroup("ExcludeSettings");

        for (const QString &key : legacySettings.childKeys()) {
            newSettings.setValue(key, legacySettings.value(key));
        }

        newSettings.endGroup();
        legacySettings.endGroup();
    }

    newSettings.sync();
}
