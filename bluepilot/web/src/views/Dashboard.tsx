import { useEffect } from 'react'
import { useNavigate } from 'react-router-dom'
import { Header } from '@/components/layout/Header'
import { useSystemStore } from '@/stores/useSystemStore'
import { useRoutesStore } from '@/stores/useRoutesStore'
import { useParamsStore } from '@/stores/useParamsStore'
import './Dashboard.css'

interface DashboardProps {
  deviceStatus?: 'online' | 'onroad' | 'offline' | 'checking'
}

export const Dashboard = ({ deviceStatus = 'checking' }: DashboardProps) => {
  const navigate = useNavigate()
  const { fetchStatus } = useSystemStore()
  const { routes, loading: routesLoading, fetchRoutes } = useRoutesStore()
  const { params, loading: paramsLoading, fetchParams } = useParamsStore()

  useEffect(() => {
    console.log('Dashboard mounted, fetching data...')
    fetchStatus()
    fetchRoutes(1)
    fetchParams()
  }, [fetchStatus, fetchRoutes, fetchParams])

  const paramCount = Object.keys(params).length

  return (
    <>
      <Header deviceStatus={deviceStatus} />
      <div className="landing-page">
        <div className="landing-header">
          <h1>BluePilot Web App</h1>
          <p>Manage your routes, monitor system health, and configure parameters</p>
        </div>

        <div className="landing-cards">
          {/* Routes Card */}
          <div className="landing-card" onClick={() => navigate('/routes')}>
            <div className="card-icon routes-icon">
              <svg
                width="48"
                height="48"
                viewBox="0 0 24 24"
                fill="none"
                stroke="currentColor"
                strokeWidth="2"
              >
                <path d="M21 10c0 7-9 13-9 13s-9-6-9-13a9 9 0 0 1 18 0z" />
                <circle cx="12" cy="10" r="3" />
              </svg>
            </div>
            <h2>Routes</h2>
            <p>Browse drives, view videos, and manage route data</p>
            <div className="card-stats">
              <span>{routesLoading ? 'Loading...' : `${routes.length} routes`}</span>
            </div>
          </div>

          {/* Parameters Card */}
          <div className="landing-card" onClick={() => navigate('/parameters')}>
            <div className="card-icon params-icon">
              <svg
                width="48"
                height="48"
                viewBox="0 0 24 24"
                fill="none"
                stroke="currentColor"
                strokeWidth="2"
              >
                <circle cx="12" cy="12" r="3" />
                <path d="M12 1v6m0 6v6M5.6 5.6l4.2 4.2m4.4 4.4l4.2 4.2M1 12h6m6 0h6M5.6 18.4l4.2-4.2m4.4-4.4l4.2-4.2" />
              </svg>
            </div>
            <h2>Parameters</h2>
            <p>View and manage openpilot configuration parameters</p>
            <div className="card-stats">
              <span>
                {paramsLoading
                  ? 'Loading...'
                  : paramCount > 0
                  ? `${paramCount} parameters`
                  : '0 parameters'}
              </span>
            </div>
          </div>
        </div>
      </div>
    </>
  )
}
