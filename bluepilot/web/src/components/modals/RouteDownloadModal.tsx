import { useEffect, useState } from 'react'
import { Modal } from '@/components/common'
import { useToastStore } from '@/stores/useToastStore'
import type { RouteDetails } from '@/types'
import './RouteDownloadModal.css'

interface RouteDownloadModalProps {
  isOpen: boolean
  onClose: () => void
  route: RouteDetails | null
}

interface CameraSizes {
  front?: number
  wide?: number
  driver?: number
  lq?: number
  qlog?: number
  rlog?: number
}

interface ExportProgress {
  camera: string
  status: 'pending' | 'processing' | 'ready' | 'error'
  progress: number
  message: string
}

export const RouteDownloadModal = ({ isOpen, onClose, route }: RouteDownloadModalProps) => {
  const { addToast } = useToastStore()
  const [cameraSizes, setCameraSizes] = useState<CameraSizes>({})
  const [exportProgress, setExportProgress] = useState<Map<string, ExportProgress>>(new Map())

  useEffect(() => {
    if (isOpen && route) {
      loadCameraSizes()
      setExportProgress(new Map())
    }
  }, [isOpen, route])

  const loadCameraSizes = async () => {
    if (!route) return
    try {
      const response = await fetch(`/api/route/${route.baseName}/camera-sizes`)
      if (response.ok) {
        const data = await response.json()
        setCameraSizes(data)
      }
    } catch (error) {
      console.error('Error loading camera sizes:', error)
    }
  }

  const formatSize = (bytes?: number): string => {
    if (!bytes) return '--'
    const mb = bytes / (1024 * 1024)
    if (mb < 1024) return `${mb.toFixed(1)} MB`
    return `${(mb / 1024).toFixed(2)} GB`
  }

  const downloadVideo = async (camera: string) => {
    if (!route) return

    const routeId = route.baseName || route.id || ''
    if (!routeId) {
      addToast('Invalid route ID', 'error')
      return
    }

    // Start export process
    setExportProgress(prev => {
      const newMap = new Map(prev)
      newMap.set(camera, {
        camera,
        status: 'processing',
        progress: 0,
        message: 'Starting export...'
      })
      return newMap
    })

    try {
      // Trigger export generation on backend
      const response = await fetch(`/api/route-export/${routeId}/${camera}`, {
        method: 'POST'
      })

      if (!response.ok) {
        throw new Error('Failed to start export')
      }

      // Poll for status
      const pollInterval = setInterval(async () => {
        try {
          const statusResponse = await fetch(`/api/route-export/${routeId}/${camera}`)
          const status = await statusResponse.json()

          setExportProgress(prev => {
            const newMap = new Map(prev)
            newMap.set(camera, {
              camera,
              status: status.status === 'ready' ? 'ready' : 'processing',
              progress: status.progressPercent || 0,
              message: status.message || 'Processing...'
            })
            return newMap
          })

          if (status.status === 'ready') {
            clearInterval(pollInterval)
            // Trigger download using anchor element for better browser compatibility
            const downloadUrl = `/api/download/route/${routeId}/${camera}`
            const link = document.createElement('a')
            link.href = downloadUrl
            link.download = '' // Let server specify filename via Content-Disposition
            document.body.appendChild(link)
            link.click()
            document.body.removeChild(link)
            addToast(`${camera} camera video download started`, 'success')

            // Clear progress after delay
            setTimeout(() => {
              setExportProgress(prev => {
                const newMap = new Map(prev)
                newMap.delete(camera)
                return newMap
              })
            }, 3000)
          } else if (status.status === 'error') {
            clearInterval(pollInterval)
            addToast(`Failed to export ${camera} video`, 'error')
            setExportProgress(prev => {
              const newMap = new Map(prev)
              newMap.delete(camera)
              return newMap
            })
          }
        } catch (error) {
          clearInterval(pollInterval)
          console.error('Status poll error:', error)
          addToast('Failed to check export status', 'error')
          setExportProgress(prev => {
            const newMap = new Map(prev)
            newMap.delete(camera)
            return newMap
          })
        }
      }, 1000)

      // Timeout after 10 minutes
      setTimeout(() => {
        clearInterval(pollInterval)
        addToast('Video export timed out', 'error')
        setExportProgress(prev => {
          const newMap = new Map(prev)
          newMap.delete(camera)
          return newMap
        })
      }, 600000)
    } catch (error: any) {
      console.error('Export error:', error)
      addToast(error?.message || 'Export failed', 'error')
      setExportProgress(prev => {
        const newMap = new Map(prev)
        newMap.delete(camera)
        return newMap
      })
    }
  }

  const downloadLog = (logType: 'qlog' | 'rlog') => {
    if (!route) return
    const routeId = route.baseName || route.id || ''
    const url = `/api/download/${logType}/${routeId}`
    // Trigger download using anchor element for better browser compatibility
    const link = document.createElement('a')
    link.href = url
    link.download = '' // Let server specify filename via Content-Disposition
    document.body.appendChild(link)
    link.click()
    document.body.removeChild(link)
    addToast(`${logType.toUpperCase()} download started`, 'success')
  }

  if (!route) return null

  const segmentCount = route.segments?.length || 0
  const frontProgress = exportProgress.get('front')
  const wideProgress = exportProgress.get('wide')
  const driverProgress = exportProgress.get('driver')

  return (
    <Modal isOpen={isOpen} onClose={onClose} title="Export Videos & Logs">
      <div className="route-download-modal">
        {/* Route Info */}
        <div className="download-route-info">
          <h3>Route: {route.baseName || route.date}</h3>
          <div className="download-route-meta">
            <span>{segmentCount} segments</span>
            <span>{route.duration || '--'}</span>
          </div>
        </div>

        {/* Video Downloads Section */}
        <div className="download-section">
          <h3 className="download-section-title">
            <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2">
              <polygon points="23 7 16 12 23 17 23 7" />
              <rect x="1" y="5" width="15" height="14" rx="2" ry="2" />
            </svg>
            Camera Videos
          </h3>
          <p className="download-section-description">
            Download full-route videos for each camera view. Videos are high-quality MP4 files.
          </p>

          <div className="camera-downloads">
            <div className="camera-download-item">
              <div className="camera-info">
                <strong>Front Camera</strong>
                <span className="camera-size">{formatSize(cameraSizes.front)}</span>
              </div>
              <button
                type="button"
                className="btn btn-primary btn-sm"
                onClick={() => downloadVideo('front')}
                disabled={frontProgress?.status === 'processing'}
              >
                {frontProgress?.status === 'processing' ? 'Exporting...' : 'Download'}
              </button>
            </div>
            {frontProgress && frontProgress.status === 'processing' && (
              <div className="export-progress">
                <div className="export-progress-bar">
                  <div className="export-progress-fill" style={{ width: `${frontProgress.progress}%` }} />
                </div>
                <div className="export-progress-message">{frontProgress.message}</div>
              </div>
            )}

            <div className="camera-download-item">
              <div className="camera-info">
                <strong>Wide Camera</strong>
                <span className="camera-size">{formatSize(cameraSizes.wide)}</span>
              </div>
              <button
                type="button"
                className="btn btn-primary btn-sm"
                onClick={() => downloadVideo('wide')}
                disabled={wideProgress?.status === 'processing'}
              >
                {wideProgress?.status === 'processing' ? 'Exporting...' : 'Download'}
              </button>
            </div>
            {wideProgress && wideProgress.status === 'processing' && (
              <div className="export-progress">
                <div className="export-progress-bar">
                  <div className="export-progress-fill" style={{ width: `${wideProgress.progress}%` }} />
                </div>
                <div className="export-progress-message">{wideProgress.message}</div>
              </div>
            )}

            <div className="camera-download-item">
              <div className="camera-info">
                <strong>Driver Camera</strong>
                <span className="camera-size">{formatSize(cameraSizes.driver)}</span>
              </div>
              <button
                type="button"
                className="btn btn-primary btn-sm"
                onClick={() => downloadVideo('driver')}
                disabled={driverProgress?.status === 'processing'}
              >
                {driverProgress?.status === 'processing' ? 'Exporting...' : 'Download'}
              </button>
            </div>
            {driverProgress && driverProgress.status === 'processing' && (
              <div className="export-progress">
                <div className="export-progress-bar">
                  <div className="export-progress-fill" style={{ width: `${driverProgress.progress}%` }} />
                </div>
                <div className="export-progress-message">{driverProgress.message}</div>
              </div>
            )}
          </div>
        </div>

        {/* Log Downloads Section */}
        <div className="download-section">
          <h3 className="download-section-title">
            <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2">
              <path d="M14 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V8z" />
              <polyline points="14 2 14 8 20 8" />
              <line x1="16" y1="13" x2="8" y2="13" />
              <line x1="16" y1="17" x2="8" y2="17" />
              <polyline points="10 9 9 9 8 9" />
            </svg>
            Log Files
          </h3>
          <p className="download-section-description">
            Download compressed log files for route analysis, debugging, and data review. These files contain timestamped telemetry data in cereal format.
          </p>

          <div className="log-downloads">
            <div className="log-download-item">
              <div className="log-info">
                <strong>qlog (Controls/CAN)</strong>
                <span className="log-description">Control commands, steering angles, gas/brake signals, CAN bus messages, lateral/longitudinal plans, and vehicle state data</span>
                <span className="log-size">{formatSize(cameraSizes.qlog)}</span>
              </div>
              <button
                type="button"
                className="btn btn-secondary btn-sm"
                onClick={() => downloadLog('qlog')}
              >
                Download qlog
              </button>
            </div>

            <div className="log-download-item">
              <div className="log-info">
                <strong>rlog (Raw Data)</strong>
                <span className="log-description">Camera frame metadata, driving model predictions, radar tracks, IMU data, GPS coordinates, and full sensor suite outputs</span>
                <span className="log-size">{formatSize(cameraSizes.rlog)}</span>
              </div>
              <button
                type="button"
                className="btn btn-secondary btn-sm"
                onClick={() => downloadLog('rlog')}
              >
                Download rlog
              </button>
            </div>
          </div>
        </div>
      </div>
    </Modal>
  )
}
