#include "ThemeManager.h"
#include <QApplication>
#include <QEvent>
#include <QPalette>
#include <QStyle>
#include <QTimer>
#include <QWidget>

ThemeManager::ThemeManager(QObject *parent)
    : QObject(parent)
{
}

ThemeManager *ThemeManager::instance()
{
    static ThemeManager s_instance;
    return &s_instance;
}

void ThemeManager::initialize()
{
    m_isDark = isDark();
    qApp->installEventFilter(this);
}

bool ThemeManager::isDark() const
{
    const QColor windowColor = QApplication::palette().color(QPalette::Window);
    // HSL lightness < 0.5 means dark background
    return windowColor.lightnessF() < 0.5;
}

bool ThemeManager::eventFilter(QObject *obj, QEvent *event)
{
    if (event->type() == QEvent::ApplicationPaletteChange) {
        checkThemeChange();
    } else if (event->type() == QEvent::ThemeChange) {
        // On macOS, ThemeChange fires when system appearance changes.
        // Defer so the palette has time to update before we read it.
        QTimer::singleShot(0, this, &ThemeManager::checkThemeChange);
    }
    return QObject::eventFilter(obj, event);
}

void ThemeManager::checkThemeChange()
{
    bool nowDark = isDark();
    if (nowDark != m_isDark) {
        m_isDark = nowDark;
        refreshTopLevelWidgets();
        emit themeChanged();
    }
}

void ThemeManager::refreshTopLevelWidgets()
{
    // Re-polish all top-level widgets so native style picks up the new palette
    const auto topLevelWidgets = QApplication::topLevelWidgets();
    for (QWidget *w : topLevelWidgets) {
        w->style()->unpolish(w);
        w->style()->polish(w);
        w->update();
    }
}
