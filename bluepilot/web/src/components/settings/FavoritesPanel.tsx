/**
 * Favorites Panel Component
 * Displays user's bookmarked settings for quick access
 */

import { useMemo } from 'react'
import { useFavoritesStore } from '@/stores/useFavoritesStore'
import { usePanelsStore } from '@/stores/usePanelsStore'
import { usePanelStateStore } from '@/stores/usePanelStateStore'
import { DynamicControl } from './DynamicControl'
import type { PanelControl } from '@/types/panels'
import './FavoritesPanel.css'

export function FavoritesPanel() {
  const favorites = useFavoritesStore((state) => state.favorites)
  const { loadedPanels } = usePanelsStore()
  const { state } = usePanelStateStore()

  // Build list of favorite controls with their full data
  const favoriteControls = useMemo(() => {
    const controls: Array<{
      favorite: (typeof favorites)[0]
      control: PanelControl
      panelName: string
    }> = []

    for (const fav of favorites) {
      const panel = loadedPanels[fav.panelId]
      if (!panel) continue

      for (const group of panel.groups) {
        if (group.groupName !== fav.groupName) continue

        const control = group.controls.find((c) => c.title === fav.controlTitle)
        if (control) {
          controls.push({
            favorite: fav,
            control,
            panelName: panel.menuName,
          })
        }
      }
    }

    return controls
  }, [favorites, loadedPanels])

  // Group favorites by panel
  const groupedByPanel = useMemo(() => {
    const groups: Record<string, typeof favoriteControls> = {}
    for (const item of favoriteControls) {
      if (!groups[item.panelName]) {
        groups[item.panelName] = []
      }
      groups[item.panelName].push(item)
    }
    return groups
  }, [favoriteControls])

  if (favorites.length === 0) {
    return (
      <div className="favorites-empty">
        <div className="favorites-empty-icon">⭐</div>
        <h3>No Favorites Yet</h3>
        <p>
          Star your frequently used settings to access them quickly here. Look for the star icon
          next to each setting.
        </p>
      </div>
    )
  }

  return (
    <div className="favorites-panel">
      <div className="favorites-info">
        <p>
          <strong>{favorites.length}</strong> favorite setting{favorites.length !== 1 ? 's' : ''}{' '}
          bookmarked for quick access
        </p>
      </div>

      {Object.entries(groupedByPanel).map(([panelName, items]) => (
        <div key={panelName} className="favorites-section">
          <h3 className="favorites-section-title">{panelName}</h3>
          <div className="favorites-controls">
            {items.map((item) => (
              <DynamicControl
                key={`${item.favorite.panelId}-${item.favorite.groupName}-${item.favorite.controlTitle}`}
                control={item.control}
                state={state}
              />
            ))}
          </div>
        </div>
      ))}
    </div>
  )
}
