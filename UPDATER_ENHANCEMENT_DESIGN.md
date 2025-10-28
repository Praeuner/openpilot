# Enhanced Updater Design - Advanced Operations with Real-Time UI Feedback

## Problem Statement

The current `bp_software_panel` implements advanced git operations (manual update, repair, reset) using:
- Direct `QProcess` execution in the Qt UI
- `QtConcurrent` for threading
- Complex command output dialogs
- Runs as the UI user (may have permission issues)

**Issues:**
1. `.venv` ownership problems when build runs with elevated privileges
2. Duplicate code between Qt and Python
3. Complex threading and process management in Qt
4. No benefit from updater's safe overlay system

## Solution: Enhanced Python Updater

Move all git operations to `updated.py` and use a **command/response pattern** via params.

---

## Architecture Overview

```
┌─────────────────────────┐
│   Qt UI (bp_software_   │
│       panel.cc)         │
│                         │
│  - Write command to     │
│    UpdaterCommand param │
│  - Read status/output   │
│    from params          │
│  - Display in UI        │
└───────────┬─────────────┘
            │
            │ Params
            │
┌───────────▼─────────────┐
│  Python Updater         │
│   (updated.py)          │
│                         │
│  - Read UpdaterCommand  │
│  - Execute operation    │
│  - Stream output to     │
│    UpdaterOutput param  │
│  - Update status        │
└─────────────────────────┘
```

---

## New Params Interface

### Command Params (UI → Updater)

| Param Name | Type | Description | Example |
|------------|------|-------------|---------|
| `UpdaterCommand` | JSON string | Command to execute | `{"cmd": "manual_update", "args": {}}` |
| `UpdaterCommandID` | string | Unique ID for command tracking | `"cmd_1698765432123"` |

### Status Params (Updater → UI)

| Param Name | Type | Description | Example |
|------------|------|-------------|---------|
| `UpdaterCommandStatus` | string | Current command status | `"running"`, `"completed"`, `"failed"` |
| `UpdaterCommandProgress` | int | Progress percentage (0-100) | `75` |
| `UpdaterCommandOutput` | string | Real-time command output (rolling buffer) | Last 1000 lines of output |
| `UpdaterCommandResult` | JSON string | Final result with exit code, errors | `{"exit_code": 0, "duration": 123.4}` |

---

## Supported Commands

### 1. Manual Update
**Command:** `manual_update`
**Operations:**
```bash
git reset --hard HEAD
git clean -xdff          # Removes .venv and all ignored files
rm -f .git/index.lock
git fetch origin $BRANCH
git pull
git submodule update --init --recursive
scons -j$(nproc)
```

**Streams:**
- Git command output
- Build progress (from scons)

### 2. Repair Repository
**Command:** `repair_repo`
**Operations:**
```bash
git reset --hard HEAD
git clean -xdff
git submodule foreach --recursive git reset --hard
git submodule foreach --recursive git clean -xdff
```

### 3. Reset Changes
**Command:** `reset_changes`
**Operations:**
```bash
git reset --hard HEAD
```

### 4. Get Commit History
**Command:** `get_history`
**Args:** `{"limit": 30, "branch": "all"}`
**Returns:** JSON array of commits
```json
{
  "commits": [
    {
      "hash": "abc1234",
      "short_hash": "abc1234",
      "message": "Commit message",
      "author": "Author Name",
      "date": "2025-01-15 10:30:00",
      "date_relative": "2 days ago"
    }
  ]
}
```

### 5. Checkout Commit
**Command:** `checkout_commit`
**Args:** `{"commit": "abc1234"}`
**Operations:**
```bash
git checkout -f $COMMIT
git reset --hard
git clean -xdff
```

### 6. Get Repository Status
**Command:** `get_repo_status`
**Returns:** JSON with current status
```json
{
  "branch": "bp-5.0",
  "commit": "9b9ef22f82",
  "commit_message": "Added support for bluepilot remote",
  "commit_date": "2025-01-15 10:30:00",
  "has_local_changes": false,
  "uncommitted_files": [],
  "is_dirty": false
}
```

---

## Implementation Plan

### Phase 1: Core Command System (updated.py)

**File:** `system/updated/updated.py`

#### 1.1 Add Command Handler Infrastructure

```python
class UpdaterCommand:
    MANUAL_UPDATE = "manual_update"
    REPAIR_REPO = "repair_repo"
    RESET_CHANGES = "reset_changes"
    GET_HISTORY = "get_history"
    CHECKOUT_COMMIT = "checkout_commit"
    GET_REPO_STATUS = "get_repo_status"

class CommandExecutor:
    def __init__(self, params: Params):
        self.params = params
        self.current_command = None
        self.output_buffer = []

    def read_command(self) -> dict | None:
        """Read and parse UpdaterCommand param"""

    def write_status(self, status: str, progress: int = 0):
        """Update status params"""

    def stream_output(self, line: str):
        """Stream output line to UI (rolling buffer)"""

    def execute_command(self, cmd: dict):
        """Main command dispatcher"""
```

#### 1.2 Add Output Streaming

```python
def run_streaming(cmd: list[str], cwd: str,
                  output_callback: callable) -> tuple[int, str]:
    """
    Run command and stream output line-by-line
    Returns: (exit_code, full_output)
    """
    process = subprocess.Popen(
        cmd,
        cwd=cwd,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        bufsize=1,
        universal_newlines=True
    )

    output_lines = []
    for line in iter(process.stdout.readline, ''):
        output_lines.append(line.rstrip())
        output_callback(line.rstrip())

    exit_code = process.wait()
    return exit_code, '\n'.join(output_lines)
```

#### 1.3 Implement Command Handlers

```python
def handle_manual_update(self, args: dict) -> dict:
    """Execute manual update with build"""

def handle_repair_repo(self, args: dict) -> dict:
    """Repair repository"""

def handle_reset_changes(self, args: dict) -> dict:
    """Reset uncommitted changes"""

def handle_get_history(self, args: dict) -> dict:
    """Get commit history"""

def handle_checkout_commit(self, args: dict) -> dict:
    """Checkout specific commit"""

def handle_get_repo_status(self, args: dict) -> dict:
    """Get current repository status"""
```

#### 1.4 Integrate Into Main Loop

```python
def main() -> None:
    # ... existing code ...

    updater = Updater()
    command_executor = CommandExecutor(params)

    while True:
        # Check for commands
        cmd = command_executor.read_command()
        if cmd:
            try:
                result = command_executor.execute_command(cmd)
                command_executor.write_result(result)
            except Exception as e:
                command_executor.write_error(str(e))

        # ... existing update logic ...
```

---

### Phase 2: Qt UI Simplification (bp_software_panel)

**File:** `selfdrive/ui/bluepilot/qt/offroad/panels/bp_software_panel.cc`

#### 2.1 Remove Complex Command Execution

**Delete:**
- `showCommandOutputDialog()` - entire dialog system
- All `QtConcurrent::run()` calls
- `BPGitManager::executeCommand()` direct calls
- Command output dialog with timers, buttons, etc.

**Replace with:**
- `sendCommand(cmd, args)` - write to param
- `watchCommandStatus()` - poll status params
- Simple progress display

#### 2.2 New Simple Command Interface

```cpp
class BPUpdaterClient : public QObject {
    Q_OBJECT
public:
    // Send command to updater
    QString sendCommand(const QString &cmd, const QJsonObject &args = {});

    // Watch for command completion
    void watchCommand(const QString &commandID);

signals:
    void commandStatusChanged(QString status);
    void commandProgressChanged(int progress);
    void commandOutputReceived(QString output);
    void commandCompleted(QJsonObject result);
    void commandFailed(QString error);

private:
    Params params;
    QTimer *statusPollTimer;
    QString currentCommandID;
};
```

#### 2.3 Simplified Button Handlers

```cpp
void BPSoftwarePanel::onManualUpdateClicked() {
    if (!showBPConfirmation(...)) return;

    QString cmdID = updaterClient->sendCommand("manual_update");

    // Show progress dialog
    showCommandProgress(cmdID, tr("Manual Update"));
}

void BPSoftwarePanel::showCommandProgress(const QString &cmdID,
                                          const QString &title) {
    BPCommandProgressDialog *dialog = new BPCommandProgressDialog(title, this);

    connect(updaterClient, &BPUpdaterClient::commandOutputReceived,
            dialog, &BPCommandProgressDialog::appendOutput);
    connect(updaterClient, &BPUpdaterClient::commandProgressChanged,
            dialog, &BPCommandProgressDialog::setProgress);
    connect(updaterClient, &BPUpdaterClient::commandCompleted,
            dialog, &BPCommandProgressDialog::onSuccess);

    updaterClient->watchCommand(cmdID);
    dialog->exec();
}
```

---

## Benefits

### 1. Eliminates Permission Issues
- All operations run as `comma` user via updater
- No more `.venv` ownership problems
- Consistent with how stock updates work

### 2. Simpler Qt Code
- **Before:** ~600 lines of complex command execution code
- **After:** ~150 lines of simple param read/write
- No threading complexity
- No process management

### 3. Unified Codebase
- All git operations in one place (Python)
- Easier to test and debug
- Can leverage updater's overlay system for safety

### 4. Better UX
- Real-time progress updates
- Consistent UI for all operations
- Can show actual command output (not just "running...")

### 5. Extensibility
- Easy to add new commands
- Can add safety checks in Python
- Can integrate with updater's logging

---

## Migration Strategy

### Step 1: Implement Core System
1. Add command system to `updated.py`
2. Add basic commands (manual_update, repair, reset)
3. Test via direct param writes

### Step 2: Create Qt Client Library
1. Implement `BPUpdaterClient` class
2. Create simplified progress dialog
3. Test with one command (e.g., reset)

### Step 3: Migrate Commands One-by-One
1. Start with simplest (reset_changes)
2. Then repair_repo
3. Finally manual_update (most complex)
4. Remove old code as we go

### Step 4: Add Advanced Features
1. Commit history
2. Checkout commit
3. Repository status polling

---

## Testing Plan

### Unit Tests (Python)
```python
def test_manual_update_command():
    """Test manual update command execution"""

def test_output_streaming():
    """Test real-time output streaming"""

def test_error_handling():
    """Test error cases and cleanup"""
```

### Integration Tests (Qt)
```cpp
void testCommandExecution() {
    // Test basic command execution
}

void testCommandCancellation() {
    // Test canceling running command
}

void testMultipleCommands() {
    // Test command queue/rejection
}
```

---

## Open Questions

1. **Command Queueing**: Should we queue commands or reject if one is running?
   - **Recommendation**: Reject with error "Command already running"

2. **Cancellation**: How to cancel a running command?
   - **Recommendation**: Add `UpdaterCommandCancel` param, updater kills process

3. **Output Buffer Size**: How much output to keep?
   - **Recommendation**: Last 1000 lines, ~100KB max

4. **Backward Compatibility**: Support old signal-based system?
   - **Recommendation**: Yes, keep SIGUSR1/SIGHUP for backward compat

5. **Build Progress**: How to parse scons progress?
   - **Recommendation**: Parse "progress: N" lines from scons stderr

---

## Next Steps

1. Review and approve design
2. Create feature branch: `feature/enhanced-updater`
3. Implement Phase 1 (Python updater)
4. Implement Phase 2 (Qt client)
5. Test thoroughly on device
6. Merge and release

---

## Files to Modify

### Python
- `system/updated/updated.py` - Add command system
- Add unit tests

### C++
- `selfdrive/ui/bluepilot/qt/offroad/panels/bp_software_panel.cc` - Simplify
- `selfdrive/ui/bluepilot/qt/offroad/panels/bp_software_panel.h` - Update interface
- Add `selfdrive/ui/bluepilot/qt/widgets/bp_updater_client.h/cc` - New client class
- Add `selfdrive/ui/bluepilot/qt/widgets/bp_command_progress_dialog.h/cc` - New dialog

### Documentation
- Update CLAUDE.md with new architecture
- Add developer guide for adding commands
