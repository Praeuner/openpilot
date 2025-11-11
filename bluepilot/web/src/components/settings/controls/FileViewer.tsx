/**
 * File Viewer Control Component
 * Displays file contents in a modal (logs, changelogs, etc.)
 */

import { useState } from 'react'
import type { FileViewerControl } from '@/types/panels'
import { Modal } from '@/components/common'
import './FileViewer.css'

interface FileViewerProps {
  control: FileViewerControl
  disabled?: boolean
}

export function FileViewer({ control, disabled }: FileViewerProps) {
  const [showModal, setShowModal] = useState(false)
  const [content, setContent] = useState<string>('')
  const [loading, setLoading] = useState(false)
  const [error, setError] = useState<string | null>(null)

  const handleOpen = async () => {
    setShowModal(true)
    setLoading(true)
    setError(null)

    try {
      // Fetch file content from backend
      const response = await fetch(`/api/file-content?path=${encodeURIComponent(control.path)}`)

      if (!response.ok) {
        throw new Error('Failed to load file')
      }

      const data = await response.json()

      if (data.success) {
        setContent(data.content || 'File is empty')
      } else {
        setError(data.error || 'Failed to load file')
      }
    } catch (err) {
      setError(err instanceof Error ? err.message : 'Failed to load file')
    } finally {
      setLoading(false)
    }
  }

  const handleClose = () => {
    setShowModal(false)
    setContent('')
    setError(null)
  }

  return (
    <>
      <div className="file-viewer-control">
        <div className="file-viewer-content">
          <h4 className="file-viewer-title">{control.title}</h4>
          {control.desc && (
            <p className="file-viewer-description">{control.desc}</p>
          )}
        </div>
        <button
          className="file-viewer-btn"
          onClick={handleOpen}
          disabled={disabled}
        >
          {control.button_text || 'VIEW'}
        </button>
      </div>

      {showModal && (
        <Modal
          isOpen={showModal}
          title={control.header || control.title}
          onClose={handleClose}
          maxWidth="800px"
        >
          <div className="file-viewer-modal-content">
            {loading && <div className="file-viewer-loading">Loading...</div>}

            {error && (
              <div className="file-viewer-error">
                <strong>Error:</strong> {error}
              </div>
            )}

            {!loading && !error && (
              <pre className="file-viewer-pre">{content}</pre>
            )}
          </div>
        </Modal>
      )}
    </>
  )
}
