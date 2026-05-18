#pragma once

#include <QString>

class QWidget;

/// Autostart-at-login support for macos-search.
///
/// Two layers:
///   - QSettings layer (firstRunCompleted + autostart keys under
///     ("Maude", "FolderBrowser") to match the favorites store).
///   - OS layer (LaunchAgent plist at ~/Library/LaunchAgents/<bundle>.plist
///     loaded with `launchctl load -w`).
///
/// Hard rule: the prompt + OS layer are no-ops in dev mode and during
/// tests. "Dev mode" = applicationDirPath() lives under a /build* tree
/// (or anywhere outside /Applications and ~/Applications). This stops
/// dev runs from fighting prod over which binary launches at login —
/// which was the user's explicit concern.
///
/// Manual prod-path smoke testing from a dev binary:
///   MACOS_SEARCH_FORCE_PROD=1 MACOS_SEARCH_DRY_RUN_AUTOSTART=1 \
///     ./build-benno/macos-search.app/Contents/MacOS/macos-search
namespace Autostart {

bool isProductionBuild();

bool firstRunNeedsPrompt();
void markFirstRunCompleted();

bool isEnabled();
void setEnabled(bool enabled);

/// Convenience: markFirstRunCompleted() + setEnabled(enableAutostart).
/// Called by main() after the FirstRunDialog returns its result.
void applyFirstRunChoice(bool enableAutostart);

namespace Testing {

void overrideProductionBuild(bool prod);
void clearProductionBuildOverride();

void setDryRun(bool dryRun);
bool dryRun();

bool lastOsRegistrationRequest();
bool osRegistrationCalled();
void resetCallTracking();

}  // namespace Testing

}  // namespace Autostart
