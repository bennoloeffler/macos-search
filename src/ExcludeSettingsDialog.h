#ifndef EXCLUDESETTINGSDIALOG_H
#define EXCLUDESETTINGSDIALOG_H

#include <QDialog>
#include <QMap>

class QVBoxLayout;
class QListWidget;
class QListWidgetItem;
class QPushButton;
class QLineEdit;
class QLabel;
class QTabWidget;
class ExcludeSettings;

// Two-tab editor: "Folders" patterns and "Files" patterns.
// Each tab edits its own list via the same ExcludeSettings API.
class ExcludeSettingsDialog : public QDialog
{
    Q_OBJECT

public:
    enum class Scope { Folders, Files };

    explicit ExcludeSettingsDialog(ExcludeSettings *settings, QWidget *parent = nullptr);

    void updateHitCounts(const QMap<QString, int> &counts);

    // Public for tests.
    Scope currentScope() const;
    void setCurrentScope(Scope scope);

private slots:
    void onItemChanged(QListWidgetItem *item);
    void onAddClicked();
    void onRemoveClicked();
    void onResetClicked();
    void onTabChanged(int index);

private:
    void setupUi();
    void refreshList();

    ExcludeSettings *m_settings;
    QTabWidget *m_tabs = nullptr;
    QListWidget *m_folderList = nullptr;
    QListWidget *m_fileList = nullptr;
    QListWidget *activeList() const;

    QLineEdit *m_addPatternEdit = nullptr;
    QPushButton *m_addButton = nullptr;
    QPushButton *m_removeButton = nullptr;
    QPushButton *m_resetButton = nullptr;
    QLabel *m_hintLabel = nullptr;

    QMap<QString, int> m_hitCounts;
    bool m_updatingList = false;
};

#endif // EXCLUDESETTINGSDIALOG_H
