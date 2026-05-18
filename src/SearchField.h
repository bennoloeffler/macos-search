#ifndef SEARCHFIELD_H
#define SEARCHFIELD_H

#include <QWidget>

class QLineEdit;
class QTimer;

// A reusable search field widget with built-in debouncing.
// Emits searchTriggered after a configurable delay (default 150ms)
// when the user stops typing.
class SearchField : public QWidget
{
    Q_OBJECT

public:
    explicit SearchField(QWidget *parent = nullptr);
    ~SearchField() override;

    // Text access
    QString text() const;
    void setText(const QString &text);
    void clear();

    // Placeholder text
    QString placeholderText() const;
    void setPlaceholderText(const QString &text);

    // Debounce configuration
    int debounceDelay() const;
    void setDebounceDelay(int milliseconds);

signals:
    // Emitted after debounce delay when user stops typing
    void searchTriggered(const QString &text);

    // Emitted immediately when text changes (no debounce)
    void textChanged(const QString &text);

private slots:
    void onTextChanged(const QString &text);
    void onDebounceTimeout();

private:
    void setupUi();

    QLineEdit *m_lineEdit = nullptr;
    QTimer *m_debounceTimer = nullptr;
    int m_debounceDelay = 150; // Default 150ms
};

#endif // SEARCHFIELD_H
