import { create } from 'zustand'
import { paramsAPI } from '@/services/api'
import type { Parameter } from '@/types'

interface ParamsState {
  params: Record<string, Parameter>
  loading: boolean
  error: string | null
  searchQuery: string

  // Actions
  fetchParams: () => Promise<void>
  updateParam: (key: string, value: string | number | boolean) => Promise<void>
  setSearchQuery: (query: string) => void
  getFilteredParams: () => Parameter[]
}

export const useParamsStore = create<ParamsState>((set, get) => ({
  params: {},
  loading: false,
  error: null,
  searchQuery: '',

  fetchParams: async () => {
    set({ loading: true, error: null })

    try {
      const params = await paramsAPI.getAll()
      const paramCount = Object.keys(params).length
      console.log('[BluePilot] Fetched params:', paramCount, 'parameters')

      // Debug: Log first param
      const firstParam = Object.values(params)[0]
      if (firstParam) {
        console.log('[BluePilot] Sample param:', firstParam)
        console.log('[BluePilot] Has type?', firstParam.type !== undefined)
        console.log('[BluePilot] Has last_modified?', firstParam.last_modified !== undefined)
      }

      set({ params, loading: false })
    } catch (error) {
      console.error('Failed to fetch params:', error)
      set({
        error: error instanceof Error ? error.message : 'Failed to fetch parameters',
        loading: false,
      })
    }
  },

  updateParam: async (key: string, value: string | number | boolean) => {
    try {
      await paramsAPI.update(key, value)

      set((state) => ({
        params: {
          ...state.params,
          [key]: {
            ...state.params[key],
            value,
          },
        },
      }))
    } catch (error) {
      set({
        error: error instanceof Error ? error.message : 'Failed to update parameter',
      })
    }
  },

  setSearchQuery: (query: string) => {
    set({ searchQuery: query })
  },

  getFilteredParams: () => {
    const { params, searchQuery } = get()

    if (!searchQuery) {
      return Object.values(params)
    }

    const query = searchQuery.toLowerCase()
    return Object.values(params).filter(
      (param) =>
        param.key.toLowerCase().includes(query) ||
        String(param.value).toLowerCase().includes(query) ||
        param.description?.toLowerCase().includes(query)
    )
  },
}))
