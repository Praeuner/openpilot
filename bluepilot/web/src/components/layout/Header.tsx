import { useNavigate, useLocation } from 'react-router-dom'
import { useWebSocketStore } from '@/stores/useWebSocketStore'
import './Header.css'

interface HeaderProps {
  deviceStatus?: 'online' | 'onroad' | 'offline' | 'checking'
  onMetricsClick?: () => void
}

export const Header = ({ deviceStatus = 'checking', onMetricsClick }: HeaderProps = {}) => {
  const navigate = useNavigate()
  const location = useLocation()
  const { connected } = useWebSocketStore()
  const isHome = location.pathname === '/'
  const isRoutesPage = location.pathname.startsWith('/routes')

  const getTitle = () => {
    if (isRoutesPage) return 'BluePilot Routes'
    if (location.pathname.startsWith('/parameters')) return 'Parameters'
    return 'BluePilot Web App'
  }

  const statusTexts = {
    online: 'Online',
    onroad: 'Onroad',
    offline: 'Offline',
    checking: 'Checking...',
  }

  return (
    <header className="header">
      {!isHome && (
        <button
          className="icon-btn home-btn"
          onClick={() => navigate('/')}
          title="Home"
          type="button"
        >
          <svg
            width="24"
            height="24"
            viewBox="0 0 24 24"
            fill="none"
            stroke="currentColor"
            strokeWidth="2"
          >
            <path d="M3 9l9-7 9 7v11a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2z" />
            <polyline points="9 22 9 12 15 12 15 22" />
          </svg>
        </button>
      )}
      <h1 className="header-title">{getTitle()}</h1>
      <div className="header-stats">
        <div className={`device-status ${deviceStatus} ${connected ? 'websocket-active' : ''}`} title="Device status">
          <span className="status-indicator"></span>
          <span id="status-text">{statusTexts[deviceStatus]}</span>
          {connected && (
            <svg
              className="websocket-icon"
              width="14"
              height="14"
              viewBox="0 0 24 24"
              fill="none"
              stroke="currentColor"
              strokeWidth="2"
            >
              <path d="M13 2L3 14h9l-1 8 10-12h-9l1-8z" />
            </svg>
          )}
        </div>
      </div>
      <div className="header-actions">
        {isRoutesPage && (
          <button
            id="header-metrics-btn"
            type="button"
            className="icon-btn"
            title="System metrics"
            onClick={onMetricsClick}
          >
            <svg
              width="24"
              height="24"
              viewBox="0 0 24 24"
              fill="none"
              stroke="currentColor"
              strokeWidth="2"
            >
              <path d="M3 3v18h18" />
              <path d="M18 17V9" />
              <path d="M13 17V5" />
              <path d="M8 17v-3" />
            </svg>
          </button>
        )}
      </div>
    </header>
  )
}
