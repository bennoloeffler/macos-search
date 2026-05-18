#ifndef MAUDECONFIG_H
#define MAUDECONFIG_H

#include <QString>

// Provides isolated config directory management for Maude
// All configuration is stored in ~/.maude/ directory
class MaudeConfig
{
public:
    // Returns the config directory path (~/.maude/)
    static QString configDir();

    // Returns the settings file path (~/.maude/settings.ini)
    static QString settingsFilePath();

    // Returns the Claude config directory path (~/.maude/claude-config)
    // This is used for CLAUDE_CONFIG_DIR environment variable
    static QString claudeConfigDir();

    // Creates the config directory if it doesn't exist
    // Returns true on success
    static bool ensureConfigDirExists();

    // Migrates legacy QSettings to new config directory
    // Only migrates if new settings don't exist yet
    static void migrateLegacySettings();

private:
    MaudeConfig() = delete;
};

#endif // MAUDECONFIG_H
