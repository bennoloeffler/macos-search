#include "ExcludeSettingsDialog.h"
#include "ExcludeSettings.h"
#include "SwiftUIStyle.h"
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPushButton>
#include <QTabWidget>
#include <QVBoxLayout>

ExcludeSettingsDialog::ExcludeSettingsDialog(ExcludeSettings *settings, QWidget *parent)
    : QDialog(parent)
    , m_settings(settings)
{
    setObjectName("ExcludeSettingsDialog");
    setWindowTitle(tr("Exclude Patterns"));
    setMinimumSize(420, 460);
    resize(480, 560);
    setupUi();
    refreshList();
}

void ExcludeSettingsDialog::updateHitCounts(const QMap<QString, int> &counts)
{
    m_hitCounts = counts;
    refreshList();
}

ExcludeSettingsDialog::Scope ExcludeSettingsDialog::currentScope() const
{
    return m_tabs && m_tabs->currentIndex() == 1 ? Scope::Files : Scope::Folders;
}

void ExcludeSettingsDialog::setCurrentScope(Scope scope)
{
    if (m_tabs) m_tabs->setCurrentIndex(scope == Scope::Files ? 1 : 0);
}

QListWidget *ExcludeSettingsDialog::activeList() const
{
    return currentScope() == Scope::Files ? m_fileList : m_folderList;
}

void ExcludeSettingsDialog::setupUi()
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(SwiftUIStyle::SpacingMedium);
    mainLayout->setContentsMargins(SwiftUIStyle::SpacingMedium,
                                    SwiftUIStyle::SpacingMedium,
                                    SwiftUIStyle::SpacingMedium,
                                    SwiftUIStyle::SpacingMedium);

    auto *titleLabel = new QLabel(tr("Exclude Patterns"), this);
    titleLabel->setObjectName("titleLabel");
    titleLabel->setFont(SwiftUIStyle::titleFont());
    mainLayout->addWidget(titleLabel);

    m_hintLabel = new QLabel(
        tr("Folder patterns skip whole directories from the scan.\n"
           "File patterns drop matching files from the file cache.\n"
           "Use * for wildcards (e.g., *.bak)."),
        this);
    m_hintLabel->setObjectName("hintLabel");
    m_hintLabel->setStyleSheet(QString("color: %1;").arg(SwiftUIStyle::secondaryTextColor()));
    QFont hintFont = m_hintLabel->font();
    hintFont.setPointSize(11);
    m_hintLabel->setFont(hintFont);
    m_hintLabel->setWordWrap(true);
    mainLayout->addWidget(m_hintLabel);

    m_tabs = new QTabWidget(this);
    m_tabs->setObjectName("excludeTabs");

    m_folderList = new QListWidget();
    m_folderList->setObjectName("folderPatternList");
    m_folderList->setStyleSheet(SwiftUIStyle::listStyleSheet());

    m_fileList = new QListWidget();
    m_fileList->setObjectName("filePatternList");
    m_fileList->setStyleSheet(SwiftUIStyle::listStyleSheet());

    m_tabs->addTab(m_folderList, tr("Folders"));
    m_tabs->addTab(m_fileList, tr("Files"));
    mainLayout->addWidget(m_tabs, 1);

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

    auto *closeLayout = new QHBoxLayout();
    closeLayout->addStretch();

    auto *closeButton = new QPushButton(tr("Done"), this);
    closeButton->setObjectName("closeButton");
    closeButton->setFlat(true);
    closeButton->setCursor(Qt::PointingHandCursor);
    closeButton->setStyleSheet(SwiftUIStyle::closeButtonStyleSheet());
    closeLayout->addWidget(closeButton);

    mainLayout->addLayout(closeLayout);

    connect(m_folderList, &QListWidget::itemChanged,
            this, &ExcludeSettingsDialog::onItemChanged);
    connect(m_fileList, &QListWidget::itemChanged,
            this, &ExcludeSettingsDialog::onItemChanged);
    connect(m_addButton, &QPushButton::clicked,
            this, &ExcludeSettingsDialog::onAddClicked);
    connect(m_addPatternEdit, &QLineEdit::returnPressed,
            this, &ExcludeSettingsDialog::onAddClicked);
    connect(m_removeButton, &QPushButton::clicked,
            this, &ExcludeSettingsDialog::onRemoveClicked);
    connect(m_resetButton, &QPushButton::clicked,
            this, &ExcludeSettingsDialog::onResetClicked);
    connect(m_tabs, &QTabWidget::currentChanged,
            this, &ExcludeSettingsDialog::onTabChanged);
    connect(closeButton, &QPushButton::clicked,
            this, &QDialog::accept);
}

void ExcludeSettingsDialog::refreshList()
{
    m_updatingList = true;
    m_folderList->clear();
    m_fileList->clear();

    auto fill = [this](QListWidget *list, const QStringList &patterns,
                       auto enabledPred) {
        for (const QString &pattern : patterns) {
            auto *item = new QListWidgetItem(list);
            QString displayText = pattern;
            if (m_hitCounts.contains(pattern) && m_hitCounts[pattern] > 0) {
                displayText += QString(" (%1 excluded)").arg(m_hitCounts[pattern]);
            }
            item->setText(displayText);
            item->setData(Qt::UserRole, pattern);
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
            item->setCheckState(enabledPred(pattern) ? Qt::Checked : Qt::Unchecked);
        }
    };

    fill(m_folderList, m_settings->allPatterns(),
         [this](const QString &p) { return m_settings->isPatternEnabled(p); });
    fill(m_fileList, m_settings->allFilePatterns(),
         [this](const QString &p) { return m_settings->isFilePatternEnabled(p); });

    m_updatingList = false;
}

void ExcludeSettingsDialog::onItemChanged(QListWidgetItem *item)
{
    if (m_updatingList || !item) return;
    const QString pattern = item->data(Qt::UserRole).toString();
    const bool enabled = (item->checkState() == Qt::Checked);
    if (currentScope() == Scope::Files) {
        m_settings->setFilePatternEnabled(pattern, enabled);
    } else {
        m_settings->setPatternEnabled(pattern, enabled);
    }
}

void ExcludeSettingsDialog::onAddClicked()
{
    const QString pattern = m_addPatternEdit->text().trimmed();
    if (pattern.isEmpty()) return;
    if (currentScope() == Scope::Files) {
        m_settings->addFilePattern(pattern);
    } else {
        m_settings->addPattern(pattern);
    }
    m_addPatternEdit->clear();
    refreshList();
}

void ExcludeSettingsDialog::onRemoveClicked()
{
    QListWidgetItem *item = activeList()->currentItem();
    if (!item) return;
    const QString pattern = item->data(Qt::UserRole).toString();
    if (currentScope() == Scope::Files) {
        m_settings->removeFilePattern(pattern);
    } else {
        m_settings->removePattern(pattern);
    }
    refreshList();
}

void ExcludeSettingsDialog::onResetClicked()
{
    if (currentScope() == Scope::Files) {
        m_settings->resetFilePatternsToDefaults();
    } else {
        m_settings->resetToDefaults();
    }
    m_hitCounts.clear();
    refreshList();
}

void ExcludeSettingsDialog::onTabChanged(int /*index*/)
{
    // Item-changed signals are sourced from whichever list the user clicks,
    // so changing tabs doesn't need to rewire anything; just clear the entry
    // field so it doesn't carry a folder pattern into the files tab.
    m_addPatternEdit->clear();
}
