#ifndef THEMEMANAGER_H
#define THEMEMANAGER_H

#include <QObject>

/**
 * @brief Singleton that detects system dark/light mode and emits themeChanged().
 *
 * On macOS, Qt adjusts the palette when the system appearance changes, firing
 * QEvent::ApplicationPaletteChange. ThemeManager installs itself as an
 * application event filter to detect this and re-polish top-level widgets.
 *
 * Usage:
 *   ThemeManager::instance()->initialize();   // once, after QApplication
 *   connect(ThemeManager::instance(), &ThemeManager::themeChanged, ...);
 *   if (ThemeManager::instance()->isDark()) { ... }
 */
class ThemeManager : public QObject
{
    Q_OBJECT

public:
    static ThemeManager *instance();

    /// Call once after QApplication is created.
    void initialize();

    /// Returns true when the system palette indicates dark mode.
    bool isDark() const;

signals:
    void themeChanged();

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;

private:
    explicit ThemeManager(QObject *parent = nullptr);

    void checkThemeChange();
    void refreshTopLevelWidgets();

    bool m_isDark = false;
};

#endif // THEMEMANAGER_H
