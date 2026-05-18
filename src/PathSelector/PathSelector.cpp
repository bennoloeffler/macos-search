#include "PathSelector.h"
#include "FileSystemAdapter.h"
#include "PathSelectorState.h"
#include "PathSelectorUI.h"
#include <QVBoxLayout>
#include <QDir>

PathSelector::PathSelector(QWidget *parent)
    : QWidget(parent)
    , m_fs(nullptr)
    , m_state(nullptr)
    , m_ui(nullptr)
    , m_ownsFs(true)
{
    init(new FileSystemAdapter(this));
}

PathSelector::PathSelector(FileSystemAdapter *fs, QWidget *parent)
    : QWidget(parent)
    , m_fs(nullptr)
    , m_state(nullptr)
    , m_ui(nullptr)
    , m_ownsFs(false)
{
    init(fs);
}

PathSelector::~PathSelector()
{
    // If we don't own the filesystem adapter, don't delete it
    // (Qt parenting will handle owned objects)
}

void PathSelector::init(FileSystemAdapter *fs)
{
    m_fs = fs;
    if (m_ownsFs) {
        m_fs->setParent(this);
    }

    m_state = new PathSelectorState(m_fs, this);
    m_ui = new PathSelectorUI(m_state, this);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_ui);

    setupConnections();

    // Initialize with home directory as default
    m_state->initialize(QDir::homePath());
}

void PathSelector::setupConnections()
{
    // Forward signals from state
    connect(m_state, &PathSelectorState::pathAccepted,
            this, &PathSelector::pathChanged);
    connect(m_state, &PathSelectorState::pathReverted,
            this, &PathSelector::pathCancelled);

    // Forward focus traversal from UI
    connect(m_ui, &PathSelectorUI::focusTraversalRequested,
            this, &PathSelector::focusTraversalRequested);
}

QString PathSelector::path() const
{
    return m_state->lastValidPath();
}

void PathSelector::setPath(const QString &path)
{
    QString expandedPath = m_fs->expandTilde(path);

    if (m_fs->isValidDirectory(expandedPath)) {
        m_state->initialize(expandedPath);
    }
}

void PathSelector::focusPathField()
{
    m_ui->focusLineEdit();
}
