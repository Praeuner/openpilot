import { useState, useRef } from 'react'
import { Modal } from '@/components/common'
import './ExportBackupModal.css' // Reuse export modal styles

interface ImportBackupModalProps {
  isOpen: boolean
  onClose: () => void
  onImportComplete?: () => void
}

interface ImportStatus {
  visible: boolean
  message: string
  progress: number
  status: 'active' | 'success' | 'error'
}

export const ImportBackupModal = ({ isOpen, onClose, onImportComplete }: ImportBackupModalProps) => {
  const [selectedFile, setSelectedFile] = useState<File | null>(null)
  const [importStatus, setImportStatus] = useState<ImportStatus | null>(null)
  const [loading, setLoading] = useState(false)
  const fileInputRef = useRef<HTMLInputElement>(null)

  const handleFileSelect = (e: React.ChangeEvent<HTMLInputElement>) => {
    const file = e.target.files?.[0]
    if (file) {
      setSelectedFile(file)
      setImportStatus(null) // Clear any previous status
    }
  }

  const handleSelectButtonClick = () => {
    fileInputRef.current?.click()
  }

  const handleImport = async () => {
    if (!selectedFile) return

    setLoading(true)
    setImportStatus({
      visible: true,
      message: 'Uploading backup file...',
      progress: 0,
      status: 'active'
    })

    try {
      const formData = new FormData()
      formData.append('backup', selectedFile)

      const response = await fetch('/api/route-import', {
        method: 'POST',
        body: formData
      })

      const result = await response.json()

      if (!response.ok) {
        throw new Error(result.error || 'Failed to import backup')
      }

      // Poll for import completion
      await pollImportStatus(result.importId)
    } catch (error) {
      console.error('Error importing backup:', error)
      setImportStatus({
        visible: true,
        message: `Error: ${error instanceof Error ? error.message : 'Import failed'}`,
        progress: 0,
        status: 'error'
      })
      setLoading(false)
    }
  }

  const pollImportStatus = async (importId: string) => {
    const maxAttempts = 360 // 30 minutes (5s intervals)
    let attempts = 0

    const poll = async () => {
      if (attempts >= maxAttempts) {
        setImportStatus({
          visible: true,
          message: 'Import timed out',
          progress: 0,
          status: 'error'
        })
        setLoading(false)
        return
      }

      attempts++

      try {
        const response = await fetch(`/api/route-import/${importId}/status`)
        const status = await response.json()

        if (status.status === 'completed') {
          setImportStatus({
            visible: true,
            message: `Import successful! Route restored: ${status.routeName || 'Route'}`,
            progress: 100,
            status: 'success'
          })
          setLoading(false)

          // Notify parent and close modal after delay
          setTimeout(() => {
            onImportComplete?.()
            handleClose()
          }, 2000)
          return
        } else if (status.status === 'error') {
          throw new Error(status.message || 'Import failed')
        } else {
          // Still processing
          const progress = Math.round((status.progress || 0) * 100)
          setImportStatus({
            visible: true,
            message: `${status.message || 'Importing...'} (${progress}%)`,
            progress,
            status: 'active'
          })

          // Poll again after 5 seconds
          setTimeout(poll, 5000)
        }
      } catch (error) {
        console.error('Polling error:', error)
        setImportStatus({
          visible: true,
          message: `Error: ${error instanceof Error ? error.message : 'Import failed'}`,
          progress: 0,
          status: 'error'
        })
        setLoading(false)
      }
    }

    // Start polling
    setTimeout(poll, 5000)
  }

  const handleClose = () => {
    if (!loading) {
      setSelectedFile(null)
      setImportStatus(null)
      if (fileInputRef.current) {
        fileInputRef.current.value = ''
      }
      onClose()
    }
  }

  return (
    <Modal isOpen={isOpen} onClose={handleClose}>
      <div className="modal-content import-modal">
        <div className="modal-header">
          <h2>Import Route Backup</h2>
          <button
            type="button"
            className="icon-btn"
            onClick={handleClose}
            disabled={loading}
            title="Close"
          >
            <svg width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2">
              <line x1="18" y1="6" x2="6" y2="18" />
              <line x1="6" y1="6" x2="18" y2="18" />
            </svg>
          </button>
        </div>

        <div className="modal-body">
          <p className="import-description">
            Restore a previously backed up route. The route will be imported to your routes directory and automatically preserved.
          </p>

          <div className="import-info-box">
            <strong>Supported formats:</strong>
            <ul>
              <li>.zip - BluePilot backup archives</li>
              <li>.bpbackup - BluePilot backup files</li>
            </ul>
          </div>

          <div className="import-file-area">
            <input
              ref={fileInputRef}
              type="file"
              accept=".zip,.bpbackup"
              style={{ display: 'none' }}
              onChange={handleFileSelect}
              disabled={loading}
            />
            <button
              type="button"
              className="btn btn-secondary"
              onClick={handleSelectButtonClick}
              disabled={loading}
            >
              <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2">
                <path d="M3 15v4c0 1.1.9 2 2 2h14a2 2 0 0 0 2-2v-4M17 9l-5-5-5 5M12 4v12" />
              </svg>
              Select Backup File
            </button>
            <span className="import-file-name">
              {selectedFile ? selectedFile.name : 'No file selected'}
            </span>
          </div>

          <div className="modal-footer">
            <button
              type="button"
              className="btn btn-primary"
              onClick={handleImport}
              disabled={!selectedFile || loading}
            >
              <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2">
                <path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4" />
                <polyline points="17 8 12 3 7 8" />
                <line x1="12" y1="3" x2="12" y2="15" />
              </svg>
              Import Backup
            </button>
          </div>

          {importStatus?.visible && (
            <div className={`export-status ${importStatus.status}`}>
              <div className="export-status-message">{importStatus.message}</div>
              <div className="export-progress-bar">
                <div
                  className="export-progress-fill"
                  style={{ width: `${importStatus.progress}%` }}
                />
              </div>
            </div>
          )}
        </div>
      </div>
    </Modal>
  )
}
