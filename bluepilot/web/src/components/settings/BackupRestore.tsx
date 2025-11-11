/**
 * Backup & Restore Component
 * Export/Import settings that have the BACKUP flag
 */

import { useState } from 'react'
import { Modal } from '@/components/common'
import './BackupRestore.css'

export function BackupRestore() {
  const [exporting, setExporting] = useState(false)
  const [importing, setImporting] = useState(false)
  const [result, setResult] = useState<{ success: boolean; message: string; details?: any } | null>(
    null
  )
  const [showResult, setShowResult] = useState(false)

  const handleExport = async () => {
    setExporting(true)
    setResult(null)

    try {
      const response = await fetch('/api/params/backup')
      const data = await response.json()

      if (data.success) {
        // Create backup file with metadata
        const backup = {
          version: '1.0',
          timestamp: new Date().toISOString(),
          device: 'BluePilot',
          params: data.params,
          count: data.count,
        }

        // Download as JSON file
        const blob = new Blob([JSON.stringify(backup, null, 2)], { type: 'application/json' })
        const url = URL.createObjectURL(blob)
        const link = document.createElement('a')
        link.href = url
        link.download = `bluepilot-backup-${new Date().toISOString().split('T')[0]}.json`
        document.body.appendChild(link)
        link.click()
        document.body.removeChild(link)
        URL.revokeObjectURL(url)

        setResult({
          success: true,
          message: `Successfully exported ${data.count} parameters with BACKUP flag`,
        })
        setShowResult(true)
      } else {
        setResult({
          success: false,
          message: data.error || 'Export failed',
        })
        setShowResult(true)
      }
    } catch (error) {
      setResult({
        success: false,
        message: error instanceof Error ? error.message : 'Export failed',
      })
      setShowResult(true)
    } finally {
      setExporting(false)
    }
  }

  const handleImport = async (event: React.ChangeEvent<HTMLInputElement>) => {
    const file = event.target.files?.[0]
    if (!file) return

    setImporting(true)
    setResult(null)

    try {
      const text = await file.text()
      const backup = JSON.parse(text)

      // Validate backup structure
      if (!backup.params || !backup.version) {
        throw new Error('Invalid backup file format')
      }

      // Send to backend
      const response = await fetch('/api/params/restore', {
        method: 'POST',
        headers: {
          'Content-Type': 'application/json',
        },
        body: JSON.stringify({
          params: backup.params,
        }),
      })

      const data = await response.json()

      if (data.success || data.restored?.length > 0) {
        setResult({
          success: true,
          message: `Successfully restored ${data.count} parameters`,
          details: {
            restored: data.restored?.length || 0,
            failed: data.failed?.length || 0,
            skipped: data.skipped?.length || 0,
          },
        })
      } else {
        setResult({
          success: false,
          message: data.error || 'Restore failed',
          details: data,
        })
      }
      setShowResult(true)
    } catch (error) {
      setResult({
        success: false,
        message: error instanceof Error ? error.message : 'Import failed',
      })
      setShowResult(true)
    } finally {
      setImporting(false)
      // Reset file input
      event.target.value = ''
    }
  }

  return (
    <>
      <div className="backup-restore">
        <div className="backup-restore-section">
          <div className="backup-restore-content">
            <h4>Export Settings</h4>
            <p>
              Download a backup of all settings with the BACKUP flag. This includes user preferences,
              configuration, and customizations that are safe to backup and restore.
            </p>
          </div>
          <button
            className="backup-restore-btn export-btn"
            onClick={handleExport}
            disabled={exporting}
          >
            {exporting ? 'Exporting...' : 'Export Backup'}
          </button>
        </div>

        <div className="backup-restore-section">
          <div className="backup-restore-content">
            <h4>Import Settings</h4>
            <p>
              Restore settings from a previously exported backup file. Only parameters with the
              BACKUP flag will be restored for safety.
            </p>
          </div>
          <label className="backup-restore-btn import-btn">
            {importing ? 'Importing...' : 'Import Backup'}
            <input
              type="file"
              accept=".json"
              onChange={handleImport}
              disabled={importing}
              style={{ display: 'none' }}
            />
          </label>
        </div>
      </div>

      {showResult && result && (
        <Modal
          isOpen={showResult}
          title={result.success ? 'Success' : 'Error'}
          onClose={() => setShowResult(false)}
        >
          <p>{result.message}</p>
          {result.details && (
            <div className="backup-result-details">
              {result.details.restored !== undefined && (
                <p>
                  <strong>Restored:</strong> {result.details.restored}
                </p>
              )}
              {result.details.failed !== undefined && result.details.failed > 0 && (
                <p>
                  <strong>Failed:</strong> {result.details.failed}
                </p>
              )}
              {result.details.skipped !== undefined && result.details.skipped > 0 && (
                <p>
                  <strong>Skipped (no BACKUP flag):</strong> {result.details.skipped}
                </p>
              )}
            </div>
          )}
          <div style={{ marginTop: '1rem', textAlign: 'right' }}>
            <button
              onClick={() => setShowResult(false)}
              style={{
                padding: '0.5rem 1.5rem',
                background: 'var(--primary-color)',
                color: 'white',
                border: 'none',
                borderRadius: '6px',
                cursor: 'pointer',
              }}
            >
              OK
            </button>
          </div>
        </Modal>
      )}
    </>
  )
}
