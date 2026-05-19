#ifndef EDITORLAUNCHER_H
#define EDITORLAUNCHER_H

#include <QString>

// Best-effort "open file at line N" for VS Code.
//
// Detection order:
//   1. EditorLauncher::setOverride() (test seam — pass binary path).
//   2. `code` on $PATH.
//   3. /Applications/Visual Studio Code.app/Contents/Resources/app/bin/code
//
// When no editor is found, openAtLine() falls back to plain `open file`.
namespace EditorLauncher {

// Returns the absolute path to the `code` binary, or empty string if not found.
QString findVsCode();

// Test seam: pretend the editor lives at `path`. Pass empty string to clear.
void setOverride(const QString &path);

// Open `file` at the given `line` (1-based). Returns true if an editor (or
// the plain `open` fallback) was launched successfully.
bool openAtLine(const QString &file, int line);

// True iff a VS Code binary is detectable right now.
bool isAvailable();

}  // namespace EditorLauncher

#endif // EDITORLAUNCHER_H
