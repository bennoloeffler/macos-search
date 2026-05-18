# Qt Widgets: SwiftUI-like Design on macOS

Technical guidelines for making Qt 6 Widgets feel as close to SwiftUI as possible on macOS.

---

## Core Principle

**SwiftUI feel ≠ technology.**

It comes from:
- Layout discipline
- Typography & spacing hierarchy
- Motion + state transitions
- macOS Human Interface Guidelines compliance

Widgets can achieve this with strict discipline. No QML required.

---

## 1. Base Application Setup (MANDATORY)

### 1.1 Native macOS Style

```cpp
QApplication::setStyle("macos");
QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
QApplication::setAttribute(Qt::AA_DontShowIconsInMenus);
```

**Rules:**
- ❌ Never use `Fusion` on macOS
- ❌ Never use custom style emulators
- ❌ Never fight AppKit — let Qt's macOS style work

### 1.2 Application Font

```cpp
QFont base("SF Pro Text", 13);
QApplication::setFont(base);
```

If SF Pro is unavailable, fall back to system font:
```cpp
QFont base = QApplication::font();
base.setPointSize(13);
QApplication::setFont(base);
```

---

## 2. Layout Rules (SwiftUI Mental Model)

### 2.1 Only Stacks — No Grids Everywhere

SwiftUI = nested vertical/horizontal stacks. Widgets must follow the same pattern.

```cpp
auto *layout = new QVBoxLayout;
layout->setSpacing(8);
layout->setContentsMargins(16, 16, 16, 16);
```

**Allowed spacings:** `4 / 8 / 16 / 24` only (matching Apple HIG).

### 2.2 What Kills SwiftUI Feel

❌ `QGridLayout` everywhere
❌ Hardcoded sizes
❌ Overusing `QFrame`, `QGroupBox`
❌ Visible borders and separators

### 2.3 What To Do Instead

```cpp
// Vertical stacking (primary pattern)
auto *vbox = new QVBoxLayout;
vbox->setSpacing(8);
vbox->setContentsMargins(16, 16, 16, 16);

// Horizontal grouping
auto *hbox = new QHBoxLayout;
hbox->setSpacing(8);

// Stretch to push elements
hbox->addStretch();
hbox->addWidget(button);
```

### 2.4 No Hardcoded Sizes

❌ **Bad:**
```cpp
button->setFixedHeight(32);
widget->setFixedWidth(200);
```

✅ **Good:**
```cpp
button->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
widget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
```

---

## 3. Typography System (CRITICAL)

SwiftUI uses San Francisco with clear weight hierarchy. Replicate this exactly.

### 3.1 Base Font

```cpp
QFont base("SF Pro Text", 13);
QApplication::setFont(base);
```

### 3.2 Title Style

```cpp
QFont titleFont = label->font();
titleFont.setPointSize(20);
titleFont.setWeight(QFont::DemiBold);
label->setFont(titleFont);
```

### 3.3 Headline Style

```cpp
QFont headlineFont = label->font();
headlineFont.setPointSize(15);
headlineFont.setWeight(QFont::Medium);
label->setFont(headlineFont);
```

### 3.4 Caption Style

```cpp
QFont captionFont = label->font();
captionFont.setPointSize(11);
label->setFont(captionFont);
label->setStyleSheet("color: rgba(0,0,0,0.5);");  // Secondary color
```

### 3.5 Typography Rules

| Rule | Description |
|------|-------------|
| Weight over size | Use weight contrast, not size chaos |
| No bold body text | Body is always regular weight |
| Clear hierarchy | Title → Headline → Body → Caption |
| Monospace for code | Use system monospace for paths/code |

---

## 4. Buttons (SwiftUI-like)

### 4.1 Rules

- Flat (no raised appearance)
- Rounded corners
- Hover & pressed states
- No visible borders at rest

### 4.2 Implementation

```cpp
QPushButton *btn = new QPushButton("Continue");
btn->setFlat(true);
btn->setCursor(Qt::PointingHandCursor);
```

### 4.3 Style Sheet

```css
QPushButton {
    padding: 6px 14px;
    border-radius: 6px;
    background: transparent;
    border: none;
}

QPushButton:hover {
    background: rgba(0, 0, 0, 0.06);
}

QPushButton:pressed {
    background: rgba(0, 0, 0, 0.12);
}

/* Primary action button */
QPushButton[primary="true"] {
    background: #9E38BE;
    color: white;
}

QPushButton[primary="true"]:hover {
    background: #8A2FA8;
}
```

---

## 5. State-Driven UI (SwiftUI Core Idea)

Widgets must react to state, not drive it directly.

### 5.1 State Object Pattern

```cpp
class ViewState : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY changed)
    Q_PROPERTY(QString content READ content WRITE setContent NOTIFY changed)

public:
    bool enabled() const { return m_enabled; }
    void setEnabled(bool v) {
        if (m_enabled != v) {
            m_enabled = v;
            emit changed();
        }
    }

signals:
    void changed();

private:
    bool m_enabled = false;
    QString m_content;
};
```

### 5.2 Binding Widgets to State

```cpp
// In view setup
connect(state, &ViewState::changed, this, &View::render);

void View::render() {
    m_submitButton->setEnabled(m_state->enabled());
    m_contentLabel->setText(m_state->content());
}
```

### 5.3 Rule

**Never mutate widgets directly in business logic.** Always go through state.

---

## 6. Animation (REQUIRED for SwiftUI Feel)

Without animation, it will never feel SwiftUI-like.

### 6.1 Use QPropertyAnimation

```cpp
auto *anim = new QPropertyAnimation(panel, "maximumHeight");
anim->setDuration(180);
anim->setEasingCurve(QEasingCurve::OutCubic);
anim->setStartValue(0);
anim->setEndValue(200);
anim->start(QAbstractAnimation::DeleteWhenStopped);
```

### 6.2 Animation Rules

| Rule | Value |
|------|-------|
| Duration | 120–220ms |
| Easing | `OutCubic` or `OutQuart` |
| Trigger | State changes only |

### 6.3 Common Animation Patterns

**Expand/Collapse:**
```cpp
void togglePanel(QWidget *panel, bool show) {
    auto *anim = new QPropertyAnimation(panel, "maximumHeight");
    anim->setDuration(180);
    anim->setEasingCurve(QEasingCurve::OutCubic);
    anim->setStartValue(panel->maximumHeight());
    anim->setEndValue(show ? 200 : 0);
    anim->start(QAbstractAnimation::DeleteWhenStopped);
}
```

**Fade In/Out:**
```cpp
auto *effect = new QGraphicsOpacityEffect(widget);
widget->setGraphicsEffect(effect);

auto *anim = new QPropertyAnimation(effect, "opacity");
anim->setDuration(150);
anim->setStartValue(0.0);
anim->setEndValue(1.0);
anim->start(QAbstractAnimation::DeleteWhenStopped);
```

**Hover State:**
```cpp
void Widget::enterEvent(QEnterEvent *event) {
    animateBackgroundColor(QColor(0, 0, 0, 15));  // 6% opacity
}

void Widget::leaveEvent(QEvent *event) {
    animateBackgroundColor(Qt::transparent);
}
```

---

## 7. Spacing System

Use consistent spacing values matching Apple HIG.

### 7.1 Standard Values

| Value | Usage |
|-------|-------|
| `4` | Tight (icon gaps, inline elements) |
| `8` | Standard small (list items, padding) |
| `16` | Large (sections, outer margins) |
| `24` | Extra large (major sections) |

### 7.2 Implementation

```cpp
// Card padding
layout->setContentsMargins(12, 8, 12, 8);

// Section spacing
sectionLayout->setSpacing(16);

// Inline elements
hbox->setSpacing(4);
```

---

## 8. Corner Radius

### 8.1 Radius Scale

| Radius | Usage |
|--------|-------|
| `4` | Small badges, inline elements |
| `6` | Buttons, list items |
| `8` | Cards, inputs |
| `10` | Large cards with borders |
| `12` | Hero sections |

### 8.2 Implementation

```cpp
// Via style sheet
widget->setStyleSheet("border-radius: 8px;");

// Or QPainterPath for custom painting
QPainterPath path;
path.addRoundedRect(rect, 8, 8);
painter.setClipPath(path);
```

---

## 9. Color System

### 9.1 Brand Color

```
#9E38BE (RGB: 158, 56, 190) - Maude Purple
```

Use for:
- Primary actions
- Selected states
- Brand accents

### 9.2 Semantic Colors

| Context | Color | Opacity |
|---------|-------|---------|
| Card background | Primary | 0.03 |
| Hover state | Black | 0.06 |
| Pressed state | Black | 0.12 |
| Selected | Purple | 0.10–0.15 |
| Success | Green | 0.15 |
| Warning | Orange | 0.10 |
| Error | Red | 0.10 |

### 9.3 Dark Mode Support

Use Qt's palette system for automatic adaptation:

```cpp
QPalette palette = QApplication::palette();
QColor textColor = palette.color(QPalette::WindowText);
QColor bgColor = palette.color(QPalette::Window);
```

Listen for system changes:
```cpp
connect(qApp, &QGuiApplication::paletteChanged, this, [this]() {
    updateColors();
});
```

---

## 10. Component Patterns

### 10.1 Card

```cpp
QFrame *card = new QFrame;
card->setStyleSheet(R"(
    QFrame {
        background: rgba(0, 0, 0, 0.03);
        border-radius: 8px;
        border: none;
    }
)");

auto *layout = new QVBoxLayout(card);
layout->setContentsMargins(12, 8, 12, 8);
layout->setSpacing(8);
```

### 10.2 Card with Border

```cpp
card->setStyleSheet(R"(
    QFrame {
        background: rgba(0, 0, 0, 0.03);
        border-radius: 10px;
        border: 1px solid rgba(0, 0, 0, 0.06);
    }
)");
```

### 10.3 Badge/Tag

```cpp
QLabel *badge = new QLabel("Label");
badge->setStyleSheet(R"(
    QLabel {
        font-size: 11px;
        padding: 2px 6px;
        background: rgba(158, 56, 190, 0.15);
        border-radius: 10px;
    }
)");
```

### 10.4 List Item with Hover

```cpp
class ListItem : public QWidget {
protected:
    void enterEvent(QEnterEvent *) override {
        setStyleSheet("background: rgba(158, 56, 190, 0.10); border-radius: 8px;");
    }
    void leaveEvent(QEvent *) override {
        setStyleSheet("background: transparent;");
    }
};
```

---

## 11. What to Avoid (ABSOLUTE)

| ❌ Avoid | Why |
|----------|-----|
| Grids everywhere | Breaks stack-based flow |
| Heavy frames/borders | Not SwiftUI aesthetic |
| Modal UI flows | Prefer inline disclosure |
| Pixel-perfect layouts | Use flexible layouts |
| Custom widget libraries | Fight native look |
| Shadows | Not part of current macOS design |
| Excessive gradients | SwiftUI uses flat colors |

---

## 12. Apple HIG Reference

Follow [Apple Human Interface Guidelines](https://developer.apple.com/design/human-interface-guidelines/).

SwiftUI looks good because HIG is enforced. Qt does not enforce it — you must.

Key sections:
- Spacing and layout
- Typography
- Color
- Controls
- Navigation

---

## 13. Realistic Expectations

How close can Widgets get to SwiftUI on macOS?

| Aspect | Achievable |
|--------|------------|
| Visual | ~85% |
| Motion | ~70% |
| Developer ergonomics | ~40% |

### Probability of Success

| Approach | SwiftUI Feel |
|----------|--------------|
| Widgets + strict HIG + animations | 65% |
| Widgets without animations | 30% |
| Widgets + random layouts | 10% |

---

## 14. Summary

**Widgets + SwiftUI feel is possible if and only if:**

1. Layout discipline is enforced (stacks, not grids)
2. State drives UI (not direct widget mutation)
3. Everything animates (120–220ms, OutCubic)
4. macOS HIG is followed strictly
5. Typography hierarchy is respected
6. Spacing values are consistent (4/8/16/24)

**One-sentence takeaway:** Qt Widgets can feel SwiftUI-like on macOS through strict adherence to layout stacks, state-driven rendering, consistent animations, and Apple HIG compliance — but it requires discipline.
