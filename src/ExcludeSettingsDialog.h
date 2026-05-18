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
class ExcludeSettings;

class ExcludeSettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ExcludeSettingsDialog(ExcludeSettings *settings, QWidget *parent = nullptr);

    // Update hit counts for patterns (called during search)
    void updateHitCounts(const QMap<QString, int> &counts);

private slots:
    void onItemChanged(QListWidgetItem *item);
    void onAddClicked();
    void onRemoveClicked();
    void onResetClicked();

private:
    void setupUi();
    void refreshList();

    ExcludeSettings *m_settings;
    QListWidget *m_patternList;
    QLineEdit *m_addPatternEdit;
    QPushButton *m_addButton;
    QPushButton *m_removeButton;
    QPushButton *m_resetButton;
    QLabel *m_hintLabel;

    QMap<QString, int> m_hitCounts;
    bool m_updatingList = false;
};

#endif // EXCLUDESETTINGSDIALOG_H
