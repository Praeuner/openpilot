import { useEffect, useState } from 'react'
import { systemAPI } from '@/services/api'
import './StatusOverlay.css'

interface StatusOverlayProps {
  type: 'onroad' | 'offline'
  onRetry?: () => void
}

interface DetailedStatus {
  connection: string
  rateLimit: string
  lastUpdate: string
}

export const StatusOverlay = ({ type, onRetry }: StatusOverlayProps) => {
  const [details, setDetails] = useState<DetailedStatus | null>(null)
  const [loading, setLoading] = useState(false)

  useEffect(() => {
    loadDetailedStatus()
  }, [type])

  const loadDetailedStatus = async () => {
    try {
      const response = await systemAPI.getDetailedStatus()
      setDetails({
        connection: response.connection || 'Unknown',
        rateLimit: response.rate_limit || 'Normal',
        lastUpdate: response.last_update || new Date().toLocaleTimeString(),
      })
    } catch (error) {
      console.error('Failed to load detailed status:', error)
    }
  }

  const handleRetry = async () => {
    setLoading(true)
    try {
      await new Promise(resolve => setTimeout(resolve, 1000))
      if (onRetry) onRetry()
    } finally {
      setLoading(false)
    }
  }

  const config = type === 'onroad'
    ? {
        icon: (
          <svg width="80" height="80" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2">
            <circle cx="12" cy="12" r="10" />
            <path d="M12 6v6l4 2" />
          </svg>
        ),
        title: 'Device Onroad',
        message: 'The device is currently driving. Web access is limited to prevent distractions.',
      }
    : {
        icon: (
          <svg width="80" height="80" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2">
            <circle cx="12" cy="12" r="10" />
            <line x1="12" y1="8" x2="12" y2="12" />
            <line x1="12" y1="16" x2="12.01" y2="16" />
          </svg>
        ),
        title: 'Device Offline',
        message: 'Cannot connect to the device. Please check your network connection.',
      }

  return (
    <div className={`status-overlay status-${type}`}>
      <div className="status-overlay-content">
        <div className="status-icon">
          {config.icon}
        </div>
        <h2>{config.title}</h2>
        <p>{config.message}</p>

        {details && (
          <div className="status-details">
            <div className="status-detail-item">
              <span className="detail-label">Connection:</span>
              <span className="detail-value">{details.connection}</span>
            </div>
            <div className="status-detail-item">
              <span className="detail-label">Rate Limit:</span>
              <span className="detail-value">{details.rateLimit}</span>
            </div>
            <div className="status-detail-item">
              <span className="detail-label">Last Update:</span>
              <span className="detail-value">{details.lastUpdate}</span>
            </div>
          </div>
        )}

        {type === 'offline' && (
          <button
            type="button"
            className="btn btn-primary"
            onClick={handleRetry}
            disabled={loading}
          >
            {loading ? (
              <div className="spinner-small" />
            ) : (
              'Retry Connection'
            )}
          </button>
        )}
      </div>
    </div>
  )
}
