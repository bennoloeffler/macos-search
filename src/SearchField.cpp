#include "SearchField.h"
#include "SwiftUIStyle.h"
#include <QLineEdit>
#include <QTimer>
#include <QVBoxLayout>

SearchField::SearchField(QWidget *parent)
    : QWidget(parent)
{
    setObjectName("searchField");
    setupUi();
}

SearchField::~SearchField()
{
    if (m_debounceTimer) {
        m_debounceTimer->stop();
    }
}

void SearchField::setupUi()
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_lineEdit = new QLineEdit(this);
    m_lineEdit->setObjectName("searchLineEdit");
    m_lineEdit->setPlaceholderText(tr("Search..."));
    m_lineEdit->setClearButtonEnabled(true);
    m_lineEdit->setStyleSheet(SwiftUIStyle::inputStyleSheet());
    layout->addWidget(m_lineEdit);

    // SearchField wraps a QLineEdit; forward focus so that calling
    // searchField->setFocus() actually lands in the editable widget,
    // not on the outer QWidget where keystrokes have nowhere to go.
    setFocusProxy(m_lineEdit);

    m_debounceTimer = new QTimer(this);
    m_debounceTimer->setSingleShot(true);
    m_debounceTimer->setInterval(m_debounceDelay);

    connect(m_lineEdit, &QLineEdit::textChanged,
            this, &SearchField::onTextChanged);
    connect(m_debounceTimer, &QTimer::timeout,
            this, &SearchField::onDebounceTimeout);
}

QString SearchField::text() const
{
    return m_lineEdit ? m_lineEdit->text() : QString();
}

void SearchField::setText(const QString &text)
{
    if (m_lineEdit) {
        m_lineEdit->setText(text);
    }
}

void SearchField::clear()
{
    if (m_lineEdit) {
        m_lineEdit->clear();
    }
}

QString SearchField::placeholderText() const
{
    return m_lineEdit ? m_lineEdit->placeholderText() : QString();
}

void SearchField::setPlaceholderText(const QString &text)
{
    if (m_lineEdit) {
        m_lineEdit->setPlaceholderText(text);
    }
}

int SearchField::debounceDelay() const
{
    return m_debounceDelay;
}

void SearchField::setDebounceDelay(int milliseconds)
{
    m_debounceDelay = milliseconds;
    if (m_debounceTimer) {
        m_debounceTimer->setInterval(milliseconds);
    }
}

void SearchField::onTextChanged(const QString &text)
{
    // Emit textChanged immediately
    emit textChanged(text);

    // Restart debounce timer
    m_debounceTimer->start();
}

void SearchField::onDebounceTimeout()
{
    if (m_lineEdit) {
        emit searchTriggered(m_lineEdit->text());
    }
}
