#ifndef FILESYSTEMADAPTER_H
#define FILESYSTEMADAPTER_H

#include <QObject>
#include <QString>
#include <QStringList>

/**
 * @brief Mockable interface for filesystem operations.
 *
 * Provides path validation, directory listing, and completion filtering.
 * Can be subclassed for testing with mock filesystem.
 *
 * The filterCompletions() method implements contains-match feature:
 * typing "/Users/benno/s" will match "/Users/benno/Documents" and
 * "/Users/benno/Downloads" because they contain "s".
 */
class FileSystemAdapter : public QObject
{
    Q_OBJECT

public:
    explicit FileSystemAdapter(QObject *parent = nullptr);
    ~FileSystemAdapter() override = default;

    /**
     * @brief Set whether hidden directories should be included in listings.
     * @param show true to include hidden directories.
     */
    void setShowHidden(bool show) { m_showHidden = show; }

    /**
     * @brief Check if hidden directories are included in listings.
     */
    bool showHidden() const { return m_showHidden; }

    /**
     * @brief Check if path is a valid directory.
     * @param path The path to check (tilde will be expanded).
     * @return true if path exists and is a directory.
     */
    virtual bool isValidDirectory(const QString &path) const;

    /**
     * @brief List immediate subdirectories of a path.
     * @param path The parent directory path.
     * @return List of subdirectory names (not full paths), sorted alphabetically.
     */
    virtual QStringList listSubdirectories(const QString &path) const;

    /**
     * @brief Expand tilde to home directory path.
     * @param path Path that may start with ~
     * @return Path with ~ expanded to home directory, or original path if no tilde.
     */
    virtual QString expandTilde(const QString &path) const;

    /**
     * @brief Get the user's home directory path.
     * @return Home directory path (e.g., "/Users/benno").
     */
    virtual QString homePath() const;

    /**
     * @brief Filter completions with prefix and contains matching.
     *
     * Returns subdirectories of basePath that match the prefix.
     * Matching is done in two passes:
     * 1. Prefix matches (highest priority) - directories starting with prefix
     * 2. Contains matches - directories containing prefix anywhere
     *
     * Results are deduplicated and returned as full paths.
     *
     * @param basePath The parent directory to search in.
     * @param prefix The text to match against directory names (case-insensitive).
     * @return List of matching full paths, prefix matches first.
     */
    virtual QStringList filterCompletions(const QString &basePath,
                                          const QString &prefix) const;

    /**
     * @brief Create a directory (and all parent directories) at the given path.
     * @param path The directory path to create.
     * @return true if the directory was created or already exists.
     */
    virtual bool createDirectory(const QString &path) const;

    /**
     * @brief Parse a path into base directory and partial name.
     *
     * Given "/Users/benno/Doc", returns:
     * - basePath: "/Users/benno"
     * - partialName: "Doc"
     *
     * Given "/Users/benno/", returns:
     * - basePath: "/Users/benno"
     * - partialName: ""
     *
     * @param fullPath The full path to parse.
     * @param basePath Output: the parent directory.
     * @param partialName Output: the partial name after last slash.
     */
    virtual void parsePath(const QString &fullPath,
                           QString &basePath,
                           QString &partialName) const;

private:
    bool m_showHidden = false;
};

#endif // FILESYSTEMADAPTER_H
