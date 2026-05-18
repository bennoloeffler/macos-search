#ifndef SWIFTUISTYLE_H
#define SWIFTUISTYLE_H

#include <QColor>
#include <QString>
#include <QFont>

class SwiftUIStyle
{
public:
    // Spacing values (Apple HIG compliant)
    static constexpr int SpacingTight = 4;   // Icon gaps, inline elements
    static constexpr int SpacingSmall = 8;   // List items, padding
    static constexpr int SpacingMedium = 16; // Sections, outer margins
    static constexpr int SpacingLarge = 24;  // Major sections
    static constexpr int SpacingXLarge = 32; // Hero sections, window edges

    // Corner radius values
    static constexpr int CornerRadiusSmall = 4;   // Small badges
    static constexpr int CornerRadiusButton = 8;  // Buttons, list items
    static constexpr int CornerRadiusCard = 8;    // Cards, inputs
    static constexpr int CornerRadiusLarge = 12;  // Hero sections

    // Brand color (same in light & dark)
    static const QString BrandColor;
    static const QString BrandColorHover;
    static const QString BrandColorPressed;

    // Semantic colors for status/actions (same in light & dark)
    static const QString ErrorColor;      // #DC3545
    static const QString SuccessColor;    // #27AE60
    static const QString WarningColor;    // #E67E22

    // --- Theme-aware text colors (CSS strings for stylesheets) ---
    static QString primaryTextColor();
    static QString secondaryTextColor();
    static QString tertiaryTextColor();

    // --- Theme-aware text colors (QColor for QPainter / icon tinting) ---
    static QColor primaryTextQColor();
    static QColor secondaryTextQColor();
    static QColor tertiaryTextQColor();

    // --- Theme-aware semantic surface colors ---
    static QString cardBackground();       // card / input background
    static QString secondaryBackground();  // secondary button bg
    static QString subtleBorder();         // 1px borders
    static QString hoverBackground();      // general hover
    static QString pressedBackground();    // general pressed
    static QString strongPressedBackground();
    static QString deepPressedBackground();
    static QString closeButtonBackground();
    static QString chipBackground();       // chip default bg

    // Chip inactive colors
    static QString chipInactiveBorder();
    static QString chipInactiveText();
    static QString chipInactiveHoverBackground();
    static QString chipInactiveHoverText();
    static QString chipInactivePressedBackground();

    // Content area gradient
    static QString contentAreaGradient();

    // Opacity values (kept for backwards compat where used directly)
    static constexpr double HoverOpacity = 0.06;
    static constexpr double PressedOpacity = 0.12;
    static constexpr double SelectedOpacity = 0.15;
    static constexpr double CardBackgroundOpacity = 0.03;
    static constexpr double BorderOpacity = 0.1;

    // Typography helpers (Apple HIG compliant)
    static QFont titleFont();      // 20pt DemiBold
    static QFont headlineFont();   // 15pt Medium
    static QFont bodyFont();       // 13pt Regular
    static QFont captionFont();    // 11pt Regular

    // Pre-built stylesheets (all theme-aware)
    static QString primaryButtonStyleSheet();
    static QString secondaryButtonStyleSheet();
    static QString closeButtonStyleSheet();
    static QString destructiveButtonStyleSheet();
    static QString cardStyleSheet();
    static QString inputStyleSheet();
    static QString listStyleSheet();
    static QString treeViewStyleSheet();
    static QString contentAreaGradientStyleSheet();
    static QString chipButtonStyleSheet();
    static QString chipActiveStyleSheet();
    static QString chipInactiveStyleSheet();

private:
    SwiftUIStyle() = delete;
};

#endif // SWIFTUISTYLE_H
