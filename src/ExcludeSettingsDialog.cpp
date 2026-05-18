#include "ExcludeSettingsDialog.h"
#include "ExcludeSettings.h"
#include "SwiftUIStyle.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPushButton>
#include <QLineEdit>
#include <QLabel>

ExcludeSettingsDialog::ExcludeSettingsDialog(ExcludeSettings *settings, QWidget *parent)
    : QDialog(parent)
    , m_settings(settings)
{
    setObjectName("ExcludeSettingsDialog");
    setWindowTitle(tr("Exclude Folders"));
    setMinimumSize(350, 400);
    resize(400, 500);
    setupUi();
    refreshList();
}

void ExcludeSettingsDialog::updateHitCounts(const QMap<QString, int> &counts)
{
    m_hitCounts = counts;
    refreshList();
}

void ExcludeSettingsDialog::setupUi()
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(SwiftUIStyle::SpacingMedium);
    mainLayout->setContentsMargins(SwiftUIStyle::SpacingMedium,
                                    SwiftUIStyle::SpacingMedium,
                                    SwiftUIStyle::SpacingMedium,
                                    SwiftUIStyle::SpacingMedium);

    // Title
    auto *titleLabel = new QLabel(tr("Exclude Patterns"), this);
    titleLabel->setObjectName("titleLabel");
    titleLabel->setFont(SwiftUIStyle::titleFont());
    mainLayout->addWidget(titleLabel);

    // Hint
    m_hintLabel = new QLabel(tr("Checked patterns will be excluded from search.\nUse * for wildcards (e.g., *.egg-info)"), this);
    m_hintLabel->setObjectName("hintLabel");
    m_hintLabel->setStyleSheet(QString("color: %1;").arg(SwiftUIStyle::secondaryTextColor()));
    QFont hintFont = m_hintLabel->font();
    hintFont.setPointSize(11);
    m_hintLabel->setFont(hintFont);
    m_hintLabel->setWordWrap(true);
    mainLayout->addWidget(m_hintLabel);

    // Pattern list
    m_patternList = new QListWidget(this);
    m_patternList->setObjectName("patternList");
    m_patternList->setStyleSheet(SwiftUIStyle::listStyleSheet());
    mainLayout->addWidget(m_patternList);

    // Add pattern row
    auto *addLayout = new QHBoxLayout();
    addLayout->setSpacing(SwiftUIStyle::SpacingSmall);

    m_addPatternEdit = new QLineEdit(this);
    m_addPatternEdit->setObjectName("addPatternEdit");
    m_addPatternEdit->setPlaceholderText(tr("Add pattern..."));
    m_addPatternEdit->setStyleSheet(SwiftUIStyle::inputStyleSheet());
    addLayout->addWidget(m_addPatternEdit);

    m_addButton = new QPushButton(tr("Add"), this);
    m_addButton->setObjectName("addButton");
    m_addButton->setFlat(true);
    m_addButton->setCursor(Qt::PointingHandCursor);
    m_addButton->setStyleSheet(SwiftUIStyle::primaryButtonStyleSheet());
    addLayout->addWidget(m_addButton);

    mainLayout->addLayout(addLayout);

    // Button row
    auto *buttonLayout = new QHBoxLayout();
    buttonLayout->setSpacing(SwiftUIStyle::SpacingSmall);

    m_removeButton = new QPushButton(tr("Remove Selected"), this);
    m_removeButton->setObjectName("removeButton");
    m_removeButton->setFlat(true);
    m_removeButton->setCursor(Qt::PointingHandCursor);
    m_removeButton->setStyleSheet(SwiftUIStyle::secondaryButtonStyleSheet());

    m_resetButton = new QPushButton(tr("Reset to Defaults"), this);
    m_resetButton->setObjectName("resetButton");
    m_resetButton->setFlat(true);
    m_resetButton->setCursor(Qt::PointingHandCursor);
    m_resetButton->setStyleSheet(SwiftUIStyle::secondaryButtonStyleSheet());

    buttonLayout->addWidget(m_removeButton);
    buttonLayout->addStretch();
    buttonLayout->addWidget(m_resetButton);

    mainLayout->addLayout(buttonLayout);

    // Close button
    auto *closeLayout = new QHBoxLayout();
    closeLayout->addStretch();

    auto *closeButton = new QPushButton(tr("Done"), this);
    closeButton->setObjectName("closeButton");
    closeButton->setFlat(true);
    closeButton->setCursor(Qt::PointingHandCursor);
    closeButton->setStyleSheet(SwiftUIStyle::closeButtonStyleSheet());
    closeLayout->addWidget(closeButton);

    mainLayout->addLayout(closeLayout);

    // Connections
    connect(m_patternList, &QListWidget::itemChanged,
            this, &ExcludeSettingsDialog::onItemChanged);
    connect(m_addButton, &QPushButton::clicked,
            this, &ExcludeSettingsDialog::onAddClicked);
    connect(m_addPatternEdit, &QLineEdit::returnPressed,
            this, &ExcludeSettingsDialog::onAddClicked);
    connect(m_removeButton, &QPushButton::clicked,
            this, &ExcludeSettingsDialog::onRemoveClicked);
    connect(m_resetButton, &QPushButton::clicked,
            this, &ExcludeSettingsDialog::onResetClicked);
    connect(closeButton, &QPushButton::clicked,
            this, &QDialog::accept);
}

void ExcludeSettingsDialog::refreshList()
{
    m_updatingList = true;
    m_patternList->clear();

    QStringList patterns = m_settings->allPatterns();
    for (const QString &pattern : patterns) {
        auto *item = new QListWidgetItem(m_patternList);

        // Show hit count if available
        QString displayText = pattern;
        if (m_hitCounts.contains(pattern) && m_hitCounts[pattern] > 0) {
            displayText += QString(" (%1 excluded)").arg(m_hitCounts[pattern]);
        }

        item->setText(displayText);
        item->setData(Qt::UserRole, pattern);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(m_settings->isPatternEnabled(pattern) ? Qt::Checked : Qt::Unchecked);
    }

    m_updatingList = false;
}

void ExcludeSettingsDialog::onItemChanged(QListWidgetItem *item)
{
    if (m_updatingList || !item) {
        return;
    }

    QString pattern = item->data(Qt::UserRole).toString();
    bool enabled = (item->checkState() == Qt::Checked);
    m_settings->setPatternEnabled(pattern, enabled);
}

void ExcludeSettingsDialog::onAddClicked()
{
    QString pattern = m_addPatternEdit->text().trimmed();
    if (!pattern.isEmpty()) {
        m_settings->addPattern(pattern);
        m_addPatternEdit->clear();
        refreshList();
    }
}

void ExcludeSettingsDialog::onRemoveClicked()
{
    QListWidgetItem *item = m_patternList->currentItem();
    if (item) {
        QString pattern = item->data(Qt::UserRole).toString();
        m_settings->removePattern(pattern);
        refreshList();
    }
}

void ExcludeSettingsDialog::onResetClicked()
{
    m_settings->resetToDefaults();
    m_hitCounts.clear();
    refreshList();
}
