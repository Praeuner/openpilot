/**
 * Favorites Store
 * Manages bookmarked/favorited settings
 */

import { create } from 'zustand'
import { persist } from 'zustand/middleware'

export interface FavoriteControl {
  panelId: string
  groupName: string
  controlTitle: string
  param?: string
  timestamp: number
}

interface FavoritesState {
  favorites: FavoriteControl[]

  // Actions
  addFavorite: (favorite: Omit<FavoriteControl, 'timestamp'>) => void
  removeFavorite: (panelId: string, groupName: string, controlTitle: string) => void
  isFavorite: (panelId: string, groupName: string, controlTitle: string) => boolean
  clearFavorites: () => void
}

export const useFavoritesStore = create<FavoritesState>()(
  persist(
    (set, get) => ({
      favorites: [],

      addFavorite: (favorite) => {
        const newFavorite = {
          ...favorite,
          timestamp: Date.now(),
        }
        set((state) => ({
          favorites: [...state.favorites, newFavorite],
        }))
      },

      removeFavorite: (panelId, groupName, controlTitle) => {
        set((state) => ({
          favorites: state.favorites.filter(
            (fav) =>
              !(
                fav.panelId === panelId &&
                fav.groupName === groupName &&
                fav.controlTitle === controlTitle
              )
          ),
        }))
      },

      isFavorite: (panelId, groupName, controlTitle) => {
        return get().favorites.some(
          (fav) =>
            fav.panelId === panelId &&
            fav.groupName === groupName &&
            fav.controlTitle === controlTitle
        )
      },

      clearFavorites: () => set({ favorites: [] }),
    }),
    {
      name: 'bluepilot-favorites', // localStorage key
    }
  )
)
