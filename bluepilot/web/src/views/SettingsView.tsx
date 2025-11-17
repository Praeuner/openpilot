/**
 * Settings View
 * Main settings page with tabbed panel interface
 */

import { useEffect, useState } from 'react'
import { Header } from '@/components/layout/Header'
import { LoadingSpinner, Icon } from '@/components/common'
import { PanelGroup } from '@/components/settings/PanelGroup'
import { BackupRestore } from '@/components/settings/BackupRestore'
import { FavoritesPanel } from '@/components/settings/FavoritesPanel'
import { usePanelsStore } from '@/stores/usePanelsStore'
import { usePanelStateStore } from '@/stores/usePanelStateStore'
import { useParamsStore } from '@/stores/useParamsStore'
import './SettingsView.css'

interface SettingsViewProps {
  deviceStatus: 'online' | 'onroad' | 'offline' | 'checking'
}

type PanelIconKey =
  | 'favorites'
  | 'bp_device_panel'
  | 'bp_display_panel'
  | 'bp_visuals_panel'
  | 'bp_vehicle_panel'
  | 'bp_cruise_panel'
  | 'bp_toggles_panel'
  | 'bp_steering_panel'
  | 'bp_developer_panel'
  | 'default'

const panelIcons: Record<PanelIconKey, () => JSX.Element> = {
  favorites: () => <Icon name="star" />,
  bp_device_panel: () => <Icon name="devices" />,
  bp_display_panel: () => <Icon name="monitor" />,
  bp_visuals_panel: () => <Icon name="visibility" />,
  bp_vehicle_panel: () => <Icon name="directions_car" />,
  bp_cruise_panel: () => <Icon name="speed" />,
  bp_toggles_panel: () => <Icon name="toggle_on" />,
  bp_steering_panel: () => <Icon name="trip_origin" />,
  bp_developer_panel: () => <Icon name="code" />,
  default: () => <Icon name="dashboard" />,
}

const getPanelIcon = (panelId?: string) => {
  if (!panelId) return panelIcons.default()
  const icon = panelIcons[panelId as PanelIconKey]
  return icon ? icon() : panelIcons.default()
}

export function SettingsView({ deviceStatus: _deviceStatus }: SettingsViewProps) {
  const { panels, loadedPanels, loading, error, fetchPanels, fetchPanel } = usePanelsStore()
  const { state, fetchState } = usePanelStateStore()
  const { fetchParams } = useParamsStore()
  const [selectedPanelId, setSelectedPanelId] = useState<string | null>(null)
  const [searchQuery, setSearchQuery] = useState('')

  // Fetch panels and state on mount
  useEffect(() => {
    fetchPanels()
    fetchState()
    fetchParams()
  }, [fetchPanels, fetchState, fetchParams])

  // Auto-select Favorites panel on mount
  useEffect(() => {
    if (!selectedPanelId) {
      setSelectedPanelId('favorites')
    }
  }, [selectedPanelId])

  // Load selected panel configuration
  useEffect(() => {
    // Skip fetching for special built-in panels
    if (selectedPanelId && selectedPanelId !== 'favorites' && !loadedPanels[selectedPanelId]) {
      fetchPanel(selectedPanelId)
    }
  }, [selectedPanelId, loadedPanels, fetchPanel])

  const selectedPanel = selectedPanelId ? loadedPanels[selectedPanelId] : null

  // Filter panel groups based on search query
  const filteredGroups = selectedPanel?.groups.map((group) => {
    if (!searchQuery.trim()) {
      return group // No filtering
    }

    const query = searchQuery.toLowerCase()
    const filteredControls = group.controls.filter((control) => {
      // Skip controls that are not web-supported
      if ('webSupported' in control && control.webSupported === false) {
        return false
      }

      const title = control.title?.toLowerCase() || ''
      const desc = control.desc?.toLowerCase() || ''
      return title.includes(query) || desc.includes(query)
    })

    return {
      ...group,
      controls: filteredControls,
    }
  }).filter((group) => group.controls.length > 0) // Remove empty groups

  if (loading && panels.length === 0) {
    return (
      <>
        <Header deviceStatus={_deviceStatus} subtitle="Configure BluePilot settings and behavior" />
        <div className="settings-view settings-view-centered">
          <LoadingSpinner message="Loading settings..." />
        </div>
      </>
    )
  }

  if (error) {
    return (
      <>
        <Header deviceStatus={_deviceStatus} subtitle="Configure BluePilot settings and behavior" />
        <div className="settings-view settings-view-centered">
          <div className="settings-error-card">
            <h2>Error Loading Settings</h2>
            <p>{error}</p>
          </div>
        </div>
      </>
    )
  }

  const favoritesMeta = {
    id: 'favorites',
    name: 'Favorites',
    description: 'Pinned controls from every panel for quick access',
  }

  const activeMetadata =
    selectedPanelId === 'favorites'
      ? favoritesMeta
      : panels.find((panel) => panel.id === selectedPanelId) || null

  const headerDescription =
    selectedPanelId === 'favorites'
      ? favoritesMeta.description
      : selectedPanel?.menuDescription || activeMetadata?.description || 'Configure BluePilot behavior offroad'

  return (
    <>
      <Header deviceStatus={_deviceStatus} subtitle="Configure BluePilot settings and behavior" />
      <div className="settings-view">
        <div className="settings-layout">
          <aside className="settings-sidebar">
            <div className="settings-nav">
              <button
                className={`settings-nav-btn ${selectedPanelId === 'favorites' ? 'active' : ''}`}
                onClick={() => setSelectedPanelId('favorites')}
              >
                <div className="settings-nav-icon">{getPanelIcon('favorites')}</div>
                <div className="settings-nav-copy">
                  <span className="settings-nav-label">Favorites</span>
                  <span className="settings-nav-desc">Starred controls</span>
                </div>
              </button>

              {panels.map((panel) => (
                <button
                  key={panel.id}
                  className={`settings-nav-btn ${selectedPanelId === panel.id ? 'active' : ''}`}
                  onClick={() => setSelectedPanelId(panel.id)}
                  data-panel-id={panel.id}
                >
                  <div className="settings-nav-icon">{getPanelIcon(panel.id)}</div>
                  <div className="settings-nav-copy">
                    <span className="settings-nav-label">{panel.name}</span>
                    <span className="settings-nav-desc">{panel.description || 'Panel controls'}</span>
                  </div>
                </button>
              ))}
            </div>
          </aside>

          <section className="settings-main">
            <div className="settings-panel-header">
              <div className="settings-panel-heading">
                <div className="settings-panel-icon">{getPanelIcon(selectedPanelId || activeMetadata?.id)}</div>
                <div>
                  <h1>{activeMetadata?.name || 'Settings'}</h1>
                  <p>{headerDescription}</p>
                </div>
              </div>
              <div className="settings-search">
                <input
                  type="text"
                  className="settings-search-input"
                  placeholder="Search settings..."
                  value={searchQuery}
                  onChange={(e) => setSearchQuery(e.target.value)}
                />
                {searchQuery && (
                  <button
                    className="settings-search-clear"
                    onClick={() => setSearchQuery('')}
                    aria-label="Clear search"
                  >
                    <Icon name="close" size={18} />
                  </button>
                )}
              </div>
            </div>

            <div className="settings-panel-content">
              {selectedPanelId === 'favorites' ? (
                <FavoritesPanel />
              ) : selectedPanel ? (
                <>
                  {!searchQuery && headerDescription && (
                    <div className="settings-panel-description">{headerDescription}</div>
                  )}

                  {selectedPanelId === 'bp_device_panel' && !searchQuery && (
                    <div className="panel-group panel-group-inline">
                      <div className="panel-group-header">
                        <h3 className="panel-group-title">Backup &amp; Restore</h3>
                      </div>
                      <BackupRestore />
                    </div>
                  )}

                  {filteredGroups && filteredGroups.length > 0 ? (
                    filteredGroups.map((group) => (
                      <PanelGroup
                        key={group.groupName}
                        group={group}
                        state={state}
                        panelId={selectedPanelId || undefined}
                      />
                    ))
                  ) : searchQuery ? (
                    <div className="settings-no-results">
                      <p>No settings found for "{searchQuery}"</p>
                      <button onClick={() => setSearchQuery('')}>Clear search</button>
                    </div>
                  ) : null}
                </>
              ) : (
                <div className="settings-panel-loading">
                  <LoadingSpinner message="Loading panel..." />
                </div>
              )}
            </div>
          </section>
        </div>
      </div>
    </>
  )
}
