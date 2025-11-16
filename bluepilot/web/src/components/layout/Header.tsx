import { useNavigate, useLocation } from 'react-router-dom'
import { useWebSocketStore } from '@/stores/useWebSocketStore'
import './Header.css'

interface HeaderProps {
  deviceStatus?: 'online' | 'onroad' | 'offline' | 'checking'
  onMetricsClick?: () => void
  subtitle?: string
}

export const Header = ({ deviceStatus = 'checking', onMetricsClick, subtitle }: HeaderProps = {}) => {
  const navigate = useNavigate()
  const location = useLocation()
  const { connected } = useWebSocketStore()
  const isHome = location.pathname === '/'
  const isRoutesPage = location.pathname.startsWith('/routes')

  const getTitle = () => {
    if (location.pathname === '/') return 'BluePilot'
    if (isRoutesPage) return 'Routes'
    if (location.pathname.startsWith('/parameters')) return 'Parameters'
    if (location.pathname.startsWith('/logs')) return 'System Logs'
    if (location.pathname.startsWith('/settings')) return 'Settings'
    return 'BluePilot'
  }

  const isParametersPage = location.pathname.startsWith('/parameters')
  const isLogsPage = location.pathname.startsWith('/logs')

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
      <div className="header-title-wrapper">
        <h1 className="header-title">{getTitle()}</h1>
        {subtitle && <p className="header-subtitle">{subtitle}</p>}
      </div>
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
        {isLogsPage && (
          <button
            type="button"
            className="icon-btn"
            title="Parameters"
            onClick={() => navigate('/parameters')}
          >
            <svg
              width="24"
              height="24"
              viewBox="0 0 24 24"
              fill="none"
              stroke="currentColor"
              strokeWidth="2"
            >
              <circle cx="12" cy="12" r="3" />
              <path d="M12 1v6m0 6v6m8.66-7.5l-5.2 3M8.54 14l-5.2 3m10.4-15l-5.2 9m5.2 3l-5.2 9" />
            </svg>
          </button>
        )}
        {isParametersPage && (
          <button
            type="button"
            className="icon-btn"
            title="System Logs"
            onClick={() => navigate('/logs')}
          >
            <svg
              width="24"
              height="24"
              viewBox="0 0 24 24"
              fill="none"
              stroke="currentColor"
              strokeWidth="2"
            >
              <path d="M14 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V8z" />
              <polyline points="14 2 14 8 20 8" />
              <line x1="16" y1="13" x2="8" y2="13" />
              <line x1="16" y1="17" x2="8" y2="17" />
              <polyline points="10 9 9 9 8 9" />
            </svg>
          </button>
        )}
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
