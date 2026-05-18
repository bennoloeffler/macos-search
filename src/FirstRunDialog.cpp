#include "FirstRunDialog.h"

#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

FirstRunDialog::FirstRunDialog(QWidget *parent) : QDialog(parent)
{
    setObjectName("firstRunDialog");
    setWindowTitle(tr("Welcome to macos-search"));
    setModal(true);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 24, 24, 20);
    layout->setSpacing(16);

    auto *title = new QLabel(
        tr("Start macos-search automatically when you log in?"), this);
    title->setObjectName("firstRunTitle");
    QFont f = title->font();
    f.setPointSize(f.pointSize() + 2);
    f.setBold(true);
    title->setFont(f);
    title->setWordWrap(true);
    layout->addWidget(title);

    auto *body = new QLabel(
        tr("Keeping the app running in the background means the folder "
           "index is already built when you need to search — no waiting.\n\n"
           "You can change this anytime in Preferences."),
        this);
    body->setObjectName("firstRunBody");
    body->setWordWrap(true);
    layout->addWidget(body);

    auto *buttonRow = new QHBoxLayout;
    buttonRow->addStretch(1);

    m_skipButton = new QPushButton(tr("Skip"), this);
    m_skipButton->setObjectName("firstRunSkip");
    m_skipButton->setAutoDefault(false);
    connect(m_skipButton, &QPushButton::clicked, this, &QDialog::reject);
    buttonRow->addWidget(m_skipButton);

    m_enableButton = new QPushButton(tr("Yes, enable autostart"), this);
    m_enableButton->setObjectName("firstRunEnable");
    m_enableButton->setDefault(true);
    m_enableButton->setAutoDefault(true);
    connect(m_enableButton, &QPushButton::clicked, this, &QDialog::accept);
    buttonRow->addWidget(m_enableButton);

    layout->addLayout(buttonRow);

    setMinimumWidth(440);
    adjustSize();

    m_enableButton->setFocus();
}
