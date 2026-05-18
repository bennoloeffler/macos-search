#pragma once

#include <QColor>
#include <QIcon>
#include <QSize>
#include <QString>

/**
 * Global icon registry — single source of truth for all icon lookups.
 *
 * Provides icons for categories, tools, scopes, UI elements, and keywords.
 * Case-insensitive. Normalizes hyphens, underscores, and spaces.
 *
 * Lookup order:
 *   1. Exact match (after normalization)
 *   2. Keyword match (substring matching against keyword table)
 *   3. Empty string (caller decides fallback)
 */
class IconRegistry {
public:
    /// Returns Qt resource path (e.g., ":/icons/mcp/brain.svg") for the given key.
    /// Key can be a category name, tool name, scope name, or UI element.
    /// Returns empty string if no match found.
    static QString iconPath(const QString &key);

    /// Returns true if the key resolves to a known icon.
    static bool hasIcon(const QString &key);

    /// Returns a QIcon tinted with the given color (preserves alpha).
    /// Useful for dark mode: tint icons with a light color for contrast.
    static QIcon coloredIcon(const QString &key, const QColor &color,
                             const QSize &size = QSize(16, 16));

private:
    /// Normalize key: lowercase, trim, replace underscores with hyphens.
    static QString normalize(const QString &key);

    /// Exact match against the full mapping table.
    static QString exactMatch(const QString &normalized);

    /// Keyword-based fallback matching.
    static QString keywordMatch(const QString &normalized);
};
