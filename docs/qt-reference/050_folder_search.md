# Folder Search Architecture

## Overview

Maude uses an in-memory folder path cache for instant project folder search. The cache is built at startup by scanning `$HOME` and kept in sync with the filesystem.

---

## Current Implementation

### PathCacheManager

Singleton that manages the folder cache:
- **Initial scan**: BFS traversal of `$HOME` in background thread
- **In-memory storage**: `QStringList` + `QSet` for O(1) lookup
- **Search**: Case-insensitive, multi-term (space = AND), excludes subfolders of matches
- **Real-time updates**: QFileSystemWatcher (limited to ~8k directories)

### Search Algorithm

```
Input: "mau docs"
Terms: ["mau", "docs"]

For each cached path:
  1. Check ALL terms match (AND logic)
  2. Skip if subfolder of already-matched result
  3. Return up to maxResults matches
```

### Highlighting

Multi-term highlighting with merged overlapping ranges:
- Purple background (#e1bee7)
- Dark purple text (#6a1b9a)
- Bold weight

---

## Platform-Specific Filesystem Watching

### macOS: FSEvents

```cpp
#include <CoreServices/CoreServices.h>

FSEventStreamRef stream = FSEventStreamCreate(
    NULL,                          // allocator
    &callback,                     // callback function
    &context,                      // context
    pathsToWatch,                  // CFArrayRef of paths
    kFSEventStreamEventIdSinceNow, // start from now
    0.5,                           // latency (seconds)
    kFSEventStreamCreateFlagFileEvents
);

FSEventStreamScheduleWithRunLoop(stream, CFRunLoopGetMain(), kCFRunLoopDefaultMode);
FSEventStreamStart(stream);
```

**Pros:**
- Watch entire `$HOME` with single call
- No directory limit
- Low overhead
- Events batched efficiently

**Cons:**
- macOS only
- Requires CoreServices framework
- Callback on arbitrary thread

### Linux: inotify

```cpp
#include <sys/inotify.h>

int fd = inotify_init1(IN_NONBLOCK);
int wd = inotify_add_watch(fd, path, IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO);

// Read events
char buf[4096];
ssize_t len = read(fd, buf, sizeof(buf));
```

**Pros:**
- Efficient kernel-level watching
- Low latency

**Cons:**
- Default limit: 8192 watches (tunable via `/proc/sys/fs/inotify/max_user_watches`)
- Must add each directory individually
- No recursive watching built-in

**Workaround for limit:**
```bash
# Increase limit (requires root)
echo 524288 | sudo tee /proc/sys/fs/inotify/max_user_watches
```

### Windows: ReadDirectoryChangesW

```cpp
#include <windows.h>

HANDLE hDir = CreateFileW(
    path,
    FILE_LIST_DIRECTORY,
    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
    NULL,
    OPEN_EXISTING,
    FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
    NULL
);

ReadDirectoryChangesW(
    hDir,
    buffer,
    bufferSize,
    TRUE,  // bWatchSubtree - CAN watch recursively!
    FILE_NOTIFY_CHANGE_DIR_NAME,
    &bytesReturned,
    &overlapped,
    NULL
);
```

**Pros:**
- Can watch entire subtree with single call (`bWatchSubtree = TRUE`)
- No explicit limit
- Built into Windows API

**Cons:**
- Windows only
- Async I/O complexity
- May miss events under heavy load

### Cross-Platform Summary

| Platform | API | Recursive | Limit | Complexity |
|----------|-----|-----------|-------|------------|
| macOS | FSEvents | ✅ Yes | None | Medium |
| Linux | inotify | ❌ No | ~8k default | Medium |
| Windows | ReadDirectoryChangesW | ✅ Yes | None | High |
| All | QFileSystemWatcher | ❌ No | ~8k | Low |

### Recommended Implementation

```cpp
class FileSystemMonitor : public QObject {
    Q_OBJECT
signals:
    void directoryCreated(const QString &path);
    void directoryDeleted(const QString &path);
    void directoryRenamed(const QString &oldPath, const QString &newPath);
};

#ifdef Q_OS_MACOS
class MacOSFileSystemMonitor : public FileSystemMonitor {
    // Use FSEvents
    FSEventStreamRef m_stream;
};
#elif defined(Q_OS_LINUX)
class LinuxFileSystemMonitor : public FileSystemMonitor {
    // Use inotify with fallback polling for overflow
    int m_inotifyFd;
    QSocketNotifier *m_notifier;
};
#elif defined(Q_OS_WIN)
class WindowsFileSystemMonitor : public FileSystemMonitor {
    // Use ReadDirectoryChangesW
    HANDLE m_dirHandle;
};
#else
class FallbackFileSystemMonitor : public FileSystemMonitor {
    // Use QFileSystemWatcher (limited)
    QFileSystemWatcher *m_watcher;
};
#endif
```

---

## Performance Optimization

### Current Bottleneck

Sequential BFS - single thread enumerates directories one at a time.

### Parallel BFS Strategy

```
┌─────────────────────────────────────────────────────────────┐
│                    Shared Queue                              │
│  [/Users/benno/Documents, /Users/benno/Downloads, ...]      │
└─────────────────────────────────────────────────────────────┘
        │           │           │           │
        ▼           ▼           ▼           ▼
   ┌────────┐  ┌────────┐  ┌────────┐  ┌────────┐
   │Worker 1│  │Worker 2│  │Worker 3│  │Worker 4│
   └────────┘  └────────┘  └────────┘  └────────┘
        │           │           │           │
        └───────────┴───────────┴───────────┘
                        │
                        ▼
                 ┌─────────────┐
                 │ Cache + Set │
                 └─────────────┘
```

**Algorithm:**
1. Start with `$HOME` in queue
2. Spawn N worker threads (N = CPU cores)
3. Each worker:
   - Dequeue directory
   - Enumerate children
   - Add children to cache
   - Enqueue children for further processing
4. Workers exit when queue empty and all workers idle

**Thread-safe structures needed:**
- `QMutex` for queue access
- `QMutex` for cache access
- `QWaitCondition` for worker coordination
- `QAtomicInt` for active worker count

### Expected Speedup

With 8 cores:
- Sequential: ~30 seconds for 50k folders
- Parallel: ~5-8 seconds (limited by disk I/O, not CPU)

---

## Exclude Patterns

Default excludes (configurable):
- `.git`
- `node_modules`
- `.cache`
- `__pycache__`
- `.Trash`
- `Library` (macOS system folder)

Stored in `~/.maude/settings.ini` under `[ExcludeSettings]`.

---

## Future Improvements

1. **Platform-native watching**: FSEvents (macOS), inotify (Linux), ReadDirectoryChangesW (Windows)
2. **Incremental updates**: Only scan changed directories
3. **Persistent cache**: Save/load cache to disk for faster startup
4. **Fuzzy matching**: Support typos and partial matches
5. **Frecency ranking**: Prioritize frequently/recently accessed folders
6. **Background refresh**: Periodic rescan of unwatched deep directories

---

## Scoring Algorithm

### Overview

Search results are ranked using a **compactness × completeness × basename_bonus** formula. This rewards:
1. Matches where characters are close together (compact)
2. Matches that cover more of the **relative path** from search root (complete)
3. Matches in the basename (folder name) over deep path matches

### Formula

```
score = compactness × completeness × basename_bonus × 100
```

Where each component is a value between 0.0 and 1.0.

### Key Concept: Relative Path

**Scoring uses the RELATIVE path from the search root, not the full absolute path.**

```
Search root: /Users/benno/projects/ai
Full path:   /Users/benno/projects/ai/maude-cp-v3
Relative:    maude-cp-v3  ← scoring uses THIS
```

This ensures that matches close to the search root score higher.

### Component Definitions

#### 1. Compactness

How tightly packed the matching characters are in the relative path.

```
compactness = pattern_size / len_of_hit
```

- **pattern_size**: Total characters in query (excluding spaces)
- **len_of_hit**: Distance from first matched char to last matched char + 1

**Example:**
```
Query: "mau"
Root:  "/Users/benno/projects/ai"
Path:  "/Users/benno/projects/ai/maude"
Relative: "maude"
           ^^^
Match positions: [0, 1, 2] (m, a, u)
len_of_hit = 2 - 0 + 1 = 3
compactness = 3 / 3 = 1.0  (perfect - all chars consecutive)
```

#### 2. Completeness

How much of the **relative path** is covered by the match pattern.

```
completeness = pattern_size / relative_path_length
```

- **pattern_size**: Total characters in query (excluding spaces)
- **relative_path_length**: Length of path relative to search root

**Example - Perfect match:**
```
Query: "maude"
Root:  "/Users/benno/projects/ai"
Path:  "/Users/benno/projects/ai/maude"
Relative: "maude" (5 chars)
completeness = 5 / 5 = 1.0  ← PERFECT!
```

**Example - Partial match:**
```
Query: "mau"
Root:  "/Users/benno/projects/ai"
Path:  "/Users/benno/projects/ai/maude-cp-v3"
Relative: "maude-cp-v3" (11 chars)
completeness = 3 / 11 = 0.27
```

#### 3. Basename Bonus

Rewards matches in the folder name (basename) over matches deeper in the path.

```
basename_bonus = max(0.1, matched_chars_in_basename / basename_length)
```

- **matched_chars_in_basename**: How many query characters match in basename
- **basename_length**: Length of the folder name
- **Minimum**: 0.1 (even if no basename match, still shows result)

**Example - Full basename match:**
```
Query: "maude"
Path:  "/Users/benno/projects/ai/maude"
Basename: "maude" (5 chars)
Matched in basename: 5 chars
basename_bonus = 5 / 5 = 1.0
```

### Scoring Examples

With root `/Users/benno/projects/ai`:

| Query | Full Path | Relative Path | Compact | Complete | Basename | Score |
|-------|-----------|---------------|---------|----------|----------|-------|
| `maude` | `.../ai/maude` | `maude` | 1.0 | 1.0 | 1.0 | **100** |
| `mau` | `.../ai/maude` | `maude` | 1.0 | 0.6 | 0.6 | **36** |
| `mau` | `.../ai/maude-cp-v3` | `maude-cp-v3` | 1.0 | 0.27 | 0.27 | **7** |
| `maude` | `.../ai/maude-cp-v3` | `maude-cp-v3` | 1.0 | 0.45 | 0.45 | **20** |

### Implementation

Located in `src/FolderSearchWorker.cpp`:

```cpp
int FolderSearchWorker::fuzzyScore(const QString &path, const QString &query, const QString &rootPath)
{
    // 1. Calculate RELATIVE path from root
    QString relativePath = path;
    if (!rootPath.isEmpty() && path.startsWith(rootPath)) {
        relativePath = path.mid(rootPath.length());
        if (relativePath.startsWith('/')) relativePath = relativePath.mid(1);
    }

    // 2. Parse query into terms, calculate pattern_size
    QStringList terms = query.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
    int patternSize = sum of term lengths;

    // 3. Find all character match positions in RELATIVE path
    QList<int> matchPositions;

    // 4. Calculate components
    int lenOfHit = max(matchPositions) - min(matchPositions) + 1;
    double compactness = patternSize / lenOfHit;
    double completeness = patternSize / relativePath.length();  // ← RELATIVE!
    double basenameBonus = matchedCharsInBasename / basename.length();

    // 5. Final score
    return compactness * completeness * basenameBonus * 100;
}
```

### Sorting

Results are sorted by:
1. **Primary**: Score (descending) - highest scores first
2. **Secondary**: Path length (ascending) - shorter paths preferred as tiebreaker

### Display

Scores are shown as badges (0-100 scale) in the search results UI.
