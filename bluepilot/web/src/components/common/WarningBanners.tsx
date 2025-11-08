import { useState, useEffect } from 'react'
import './WarningBanners.css'

export const WarningBanners = () => {
  const [showCellular, setShowCellular] = useState(false)
  const [showFirefox, setShowFirefox] = useState(false)

  useEffect(() => {
    // Check if Firefox
    const isFirefox = navigator.userAgent.toLowerCase().includes('firefox')
    if (isFirefox) {
      setShowFirefox(true)
    }

    // Check for cellular connection (placeholder - would need backend support)
    // setShowCellular(checkCellularStatus())
  }, [])

  if (!showCellular && !showFirefox) {
    return null
  }

  return (
    <>
      {showCellular && (
        <div className="cellular-warning">
          <div className="cellular-warning-content">
            <svg width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2">
              <path d="M10.29 3.86L1.82 18a2 2 0 0 0 1.71 3h16.94a2 2 0 0 0 1.71-3L13.71 3.86a2 2 0 0 0-3.42 0z" />
              <line x1="12" y1="9" x2="12" y2="13" />
              <line x1="12" y1="17" x2="12.01" y2="17" />
            </svg>
            <div className="cellular-warning-text">
              <strong>Cellular Access Enabled</strong>
              <span>Server accessible over cellular network</span>
            </div>
            <button className="cellular-warning-close" onClick={() => setShowCellular(false)} title="Dismiss">
              ×
            </button>
          </div>
        </div>
      )}

      {showFirefox && (
        <div className="firefox-warning">
          <div className="firefox-warning-content">
            <svg width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2">
              <circle cx="12" cy="12" r="10" />
              <line x1="12" y1="8" x2="12" y2="12" />
              <line x1="12" y1="16" x2="12.01" y2="16" />
            </svg>
            <div className="firefox-warning-text">
              <strong>Limited Functionality on Firefox</strong>
              <span>
                HEVC video playback is not supported. Only the LQ (H.264) camera will be available for route viewing. For
                full functionality, please use Safari, Chrome, or Edge.
              </span>
            </div>
            <button className="firefox-warning-close" onClick={() => setShowFirefox(false)} title="Dismiss">
              ×
            </button>
          </div>
        </div>
      )}
    </>
  )
}
