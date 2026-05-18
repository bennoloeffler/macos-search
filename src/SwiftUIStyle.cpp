#include "SwiftUIStyle.h"
#include "ThemeManager.h"

// Brand colors (identical in light & dark)
const QString SwiftUIStyle::BrandColor = QStringLiteral("#9E38BE");
const QString SwiftUIStyle::BrandColorHover = QStringLiteral("#8A2FA8");
const QString SwiftUIStyle::BrandColorPressed = QStringLiteral("#762890");

// Semantic status colors (identical in light & dark)
const QString SwiftUIStyle::ErrorColor = QStringLiteral("#DC3545");
const QString SwiftUIStyle::SuccessColor = QStringLiteral("#27AE60");
const QString SwiftUIStyle::WarningColor = QStringLiteral("#E67E22");

// ---------------------------------------------------------------------------
// Theme-aware text colors
// ---------------------------------------------------------------------------

QString SwiftUIStyle::primaryTextColor()
{
    return ThemeManager::instance()->isDark()
        ? QStringLiteral("#E0E0E0")
        : QStringLiteral("#333333");
}

QString SwiftUIStyle::secondaryTextColor()
{
    return ThemeManager::instance()->isDark()
        ? QStringLiteral("rgba(255, 255, 255, 0.55)")
        : QStringLiteral("rgba(0, 0, 0, 0.5)");
}

QString SwiftUIStyle::tertiaryTextColor()
{
    return ThemeManager::instance()->isDark()
        ? QStringLiteral("rgba(255, 255, 255, 0.4)")
        : QStringLiteral("rgba(0, 0, 0, 0.4)");
}

// ---------------------------------------------------------------------------
// Theme-aware text colors — QColor variants for QPainter / icon tinting
// (CSS rgba() strings above cannot be parsed by QColor constructor)
// ---------------------------------------------------------------------------

QColor SwiftUIStyle::primaryTextQColor()
{
    return ThemeManager::instance()->isDark()
        ? QColor(0xE0, 0xE0, 0xE0)
        : QColor(0x33, 0x33, 0x33);
}

QColor SwiftUIStyle::secondaryTextQColor()
{
    return ThemeManager::instance()->isDark()
        ? QColor(255, 255, 255, 140)   // ~55% opacity
        : QColor(0, 0, 0, 128);        // ~50% opacity
}

QColor SwiftUIStyle::tertiaryTextQColor()
{
    return ThemeManager::instance()->isDark()
        ? QColor(255, 255, 255, 102)   // ~40% opacity
        : QColor(0, 0, 0, 102);        // ~40% opacity
}

// ---------------------------------------------------------------------------
// Theme-aware surface / background colors
// ---------------------------------------------------------------------------

QString SwiftUIStyle::cardBackground()
{
    return ThemeManager::instance()->isDark()
        ? QStringLiteral("rgba(255, 255, 255, 0.06)")
        : QStringLiteral("rgba(0, 0, 0, 0.03)");
}

QString SwiftUIStyle::secondaryBackground()
{
    return ThemeManager::instance()->isDark()
        ? QStringLiteral("rgba(255, 255, 255, 0.07)")
        : QStringLiteral("rgba(0, 0, 0, 0.04)");
}

QString SwiftUIStyle::subtleBorder()
{
    return ThemeManager::instance()->isDark()
        ? QStringLiteral("rgba(255, 255, 255, 0.12)")
        : QStringLiteral("rgba(0, 0, 0, 0.1)");
}

QString SwiftUIStyle::hoverBackground()
{
    return ThemeManager::instance()->isDark()
        ? QStringLiteral("rgba(255, 255, 255, 0.10)")
        : QStringLiteral("rgba(0, 0, 0, 0.08)");
}

QString SwiftUIStyle::pressedBackground()
{
    return ThemeManager::instance()->isDark()
        ? QStringLiteral("rgba(255, 255, 255, 0.15)")
        : QStringLiteral("rgba(0, 0, 0, 0.12)");
}

QString SwiftUIStyle::strongPressedBackground()
{
    return ThemeManager::instance()->isDark()
        ? QStringLiteral("rgba(255, 255, 255, 0.18)")
        : QStringLiteral("rgba(0, 0, 0, 0.14)");
}

QString SwiftUIStyle::deepPressedBackground()
{
    return ThemeManager::instance()->isDark()
        ? QStringLiteral("rgba(255, 255, 255, 0.22)")
        : QStringLiteral("rgba(0, 0, 0, 0.18)");
}

QString SwiftUIStyle::closeButtonBackground()
{
    return ThemeManager::instance()->isDark()
        ? QStringLiteral("rgba(255, 255, 255, 0.08)")
        : QStringLiteral("rgba(0, 0, 0, 0.06)");
}

QString SwiftUIStyle::chipBackground()
{
    return ThemeManager::instance()->isDark()
        ? QStringLiteral("rgba(255, 255, 255, 0.08)")
        : QStringLiteral("rgba(0, 0, 0, 0.05)");
}

// Chip inactive variants
QString SwiftUIStyle::chipInactiveBorder()
{
    return subtleBorder();
}

QString SwiftUIStyle::chipInactiveText()
{
    return ThemeManager::instance()->isDark()
        ? QStringLiteral("rgba(255, 255, 255, 0.35)")
        : QStringLiteral("rgba(0, 0, 0, 0.35)");
}

QString SwiftUIStyle::chipInactiveHoverBackground()
{
    return ThemeManager::instance()->isDark()
        ? QStringLiteral("rgba(255, 255, 255, 0.06)")
        : QStringLiteral("rgba(0, 0, 0, 0.04)");
}

QString SwiftUIStyle::chipInactiveHoverText()
{
    return secondaryTextColor();
}

QString SwiftUIStyle::chipInactivePressedBackground()
{
    return hoverBackground();
}

QString SwiftUIStyle::contentAreaGradient()
{
    if (ThemeManager::instance()->isDark()) {
        return QStringLiteral(R"(
            background: qlineargradient(
                x1: 0, y1: 0, x2: 1, y2: 1,
                stop: 0 rgba(158, 56, 190, 0.06),
                stop: 1 rgba(158, 56, 190, 0.02)
            );
        )");
    }
    return QStringLiteral(R"(
        background: qlineargradient(
            x1: 0, y1: 0, x2: 1, y2: 1,
            stop: 0 rgba(158, 56, 190, 0.04),
            stop: 1 rgba(158, 56, 190, 0.01)
        );
    )");
}

// ---------------------------------------------------------------------------
// Typography
// ---------------------------------------------------------------------------

QFont SwiftUIStyle::titleFont()
{
    QFont font;
    font.setPointSize(20);
    font.setWeight(QFont::DemiBold);
    return font;
}

QFont SwiftUIStyle::headlineFont()
{
    QFont font;
    font.setPointSize(15);
    font.setWeight(QFont::Medium);
    return font;
}

QFont SwiftUIStyle::bodyFont()
{
    QFont font;
    font.setPointSize(13);
    font.setWeight(QFont::Normal);
    return font;
}

QFont SwiftUIStyle::captionFont()
{
    QFont font;
    font.setPointSize(11);
    font.setWeight(QFont::Normal);
    return font;
}

// ---------------------------------------------------------------------------
// Pre-built stylesheets
// ---------------------------------------------------------------------------

QString SwiftUIStyle::primaryButtonStyleSheet()
{
    return QStringLiteral(R"(
        QPushButton {
            padding: 8px 24px;
            border-radius: 8px;
            background: #9E38BE;
            color: white;
            font-weight: 500;
            border: none;
        }
        QPushButton:hover {
            background: #8A2FA8;
        }
        QPushButton:pressed {
            background: #762890;
        }
    )");
}

QString SwiftUIStyle::secondaryButtonStyleSheet()
{
    return QStringLiteral(
        "QPushButton {"
        "    padding: 8px 8px;"
        "    border-radius: 8px;"
        "    background: %1;"
        "    color: %2;"
        "    border: none;"
        "}"
        "QPushButton:hover {"
        "    background: %3;"
        "}"
        "QPushButton:pressed {"
        "    background: %4;"
        "}")
        .arg(secondaryBackground(),
             primaryTextColor(),
             hoverBackground(),
             strongPressedBackground());
}

QString SwiftUIStyle::closeButtonStyleSheet()
{
    return QStringLiteral(
        "QPushButton {"
        "    padding: 8px 24px;"
        "    border-radius: 8px;"
        "    background: %1;"
        "    color: %2;"
        "    font-weight: 500;"
        "    border: none;"
        "}"
        "QPushButton:hover {"
        "    background: %3;"
        "}"
        "QPushButton:pressed {"
        "    background: %4;"
        "}")
        .arg(closeButtonBackground(),
             primaryTextColor(),
             pressedBackground(),
             deepPressedBackground());
}

QString SwiftUIStyle::destructiveButtonStyleSheet()
{
    return QStringLiteral(R"(
        QPushButton {
            background: %1;
            border: none;
            border-radius: %2px;
            padding: 8px 16px;
            font-size: 13px;
            font-weight: 500;
            color: white;
        }
        QPushButton:hover {
            background: #C82333;
        }
        QPushButton:pressed {
            background: #BD2130;
        }
    )").arg(ErrorColor, QString::number(CornerRadiusButton));
}

QString SwiftUIStyle::cardStyleSheet()
{
    return QStringLiteral(
        "QFrame {"
        "    background: %1;"
        "    border-radius: 8px;"
        "    border: none;"
        "}")
        .arg(cardBackground());
}

QString SwiftUIStyle::inputStyleSheet()
{
    return QStringLiteral(
        "QLineEdit {"
        "    padding: 8px;"
        "    border-radius: 8px;"
        "    border: 1px solid %1;"
        "    background: %2;"
        "}"
        "QLineEdit:focus {"
        "    border: 1px solid #9E38BE;"
        "}")
        .arg(subtleBorder(), cardBackground());
}

QString SwiftUIStyle::listStyleSheet()
{
    return QStringLiteral(
        "QListWidget {"
        "    border: 1px solid %1;"
        "    border-radius: 8px;"
        "    background: %2;"
        "}"
        "QListWidget::item {"
        "    padding: 8px;"
        "    border-radius: 8px;"
        "}"
        "QListWidget::item:hover {"
        "    background: rgba(158, 56, 190, 0.10);"
        "}"
        "QListWidget::item:selected {"
        "    background: rgba(158, 56, 190, 0.15);"
        "}")
        .arg(subtleBorder(), cardBackground());
}

QString SwiftUIStyle::treeViewStyleSheet()
{
    return QStringLiteral(
        "QTreeView {"
        "    border: 1px solid %1;"
        "    border-radius: 8px;"
        "    background: %2;"
        "    padding: 4px;"
        "}"
        "QTreeView::item {"
        "    padding: 4px 8px;"
        "    border-radius: 8px;"
        "}"
        "QTreeView::item:hover {"
        "    background: rgba(158, 56, 190, 0.10);"
        "}"
        "QTreeView::item:selected {"
        "    background: rgba(158, 56, 190, 0.15);"
        "}"
        "QTreeView::branch:has-children:!has-siblings:closed,"
        "QTreeView::branch:closed:has-children:has-siblings {"
        "    border-image: none;"
        "}"
        "QTreeView::branch:open:has-children:!has-siblings,"
        "QTreeView::branch:open:has-children:has-siblings {"
        "    border-image: none;"
        "}")
        .arg(subtleBorder(), cardBackground());
}

QString SwiftUIStyle::contentAreaGradientStyleSheet()
{
    return contentAreaGradient();
}

QString SwiftUIStyle::chipButtonStyleSheet()
{
    return QStringLiteral(
        "QPushButton {"
        "    padding: 4px 12px;"
        "    border-radius: 12px;"
        "    background: %1;"
        "    border: none;"
        "    color: %2;"
        "    font-size: 11px;"
        "    font-weight: 500;"
        "}"
        "QPushButton:hover {"
        "    background: %3;"
        "}"
        "QPushButton:pressed {"
        "    background: %4;"
        "}")
        .arg(chipBackground(),
             primaryTextColor(),
             hoverBackground(),
             pressedBackground());
}

QString SwiftUIStyle::chipActiveStyleSheet()
{
    return QStringLiteral(R"(
        QPushButton {
            padding: 4px 12px;
            border-radius: 12px;
            background: rgba(158, 56, 190, 0.15);
            border: none;
            color: #9E38BE;
            font-size: 11px;
            font-weight: 500;
        }
        QPushButton:hover {
            background: rgba(158, 56, 190, 0.22);
        }
        QPushButton:pressed {
            background: rgba(158, 56, 190, 0.28);
        }
    )");
}

QString SwiftUIStyle::chipInactiveStyleSheet()
{
    return QStringLiteral(
        "QPushButton {"
        "    padding: 4px 12px;"
        "    border-radius: 12px;"
        "    background: transparent;"
        "    border: 1px solid %1;"
        "    color: %2;"
        "    font-size: 11px;"
        "    font-weight: 500;"
        "}"
        "QPushButton:hover {"
        "    background: %3;"
        "    color: %4;"
        "}"
        "QPushButton:pressed {"
        "    background: %5;"
        "}")
        .arg(chipInactiveBorder(),
             chipInactiveText(),
             chipInactiveHoverBackground(),
             chipInactiveHoverText(),
             chipInactivePressedBackground());
}
