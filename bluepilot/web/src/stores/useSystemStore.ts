import { create } from 'zustand'
import { systemAPI } from '@/services/api'
import type { SystemMetrics, ServerStatus, DiskSpace, VehicleInfo } from '@/types'

interface SystemState {
  metrics: SystemMetrics | null
  status: ServerStatus | null
  diskSpace: DiskSpace | null
  vehicleInfo: VehicleInfo | null
  loading: boolean
  error: string | null

  // Actions
  fetchMetrics: () => Promise<void>
  fetchStatus: () => Promise<void>
  fetchDiskSpace: () => Promise<void>
  fetchVehicleInfo: () => Promise<void>
  startPolling: (interval?: number) => void
  stopPolling: () => void
}

let pollingTimer: number | null = null

export const useSystemStore = create<SystemState>((set) => ({
  metrics: null,
  status: null,
  diskSpace: null,
  vehicleInfo: null,
  loading: false,
  error: null,

  fetchMetrics: async () => {
    try {
      const metrics = await systemAPI.getMetrics()
      set({ metrics, error: null })
    } catch (error) {
      set({
        error: error instanceof Error ? error.message : 'Failed to fetch metrics',
      })
    }
  },

  fetchStatus: async () => {
    try {
      const status = await systemAPI.getStatus()
      set({ status, error: null })
    } catch (error) {
      set({
        error: error instanceof Error ? error.message : 'Failed to fetch status',
      })
    }
  },

  fetchDiskSpace: async () => {
    try {
      const diskSpace = await systemAPI.getDiskSpace()
      set({ diskSpace, error: null })
    } catch (error) {
      set({
        error: error instanceof Error ? error.message : 'Failed to fetch disk space',
      })
    }
  },

  fetchVehicleInfo: async () => {
    try {
      const vehicleInfo = await systemAPI.getVehicleInfo()
      set({ vehicleInfo, error: null })
    } catch (error) {
      console.error('Failed to fetch vehicle info:', error)
      // Don't set error state for vehicle info as it's optional
    }
  },

  startPolling: (interval = 5000) => {
    if (pollingTimer !== null) {
      return // Already polling
    }

    const poll = async () => {
      const store = useSystemStore.getState()
      await Promise.all([
        store.fetchMetrics(),
        store.fetchStatus(),
        store.fetchDiskSpace(),
      ])
    }

    // Initial fetch
    poll()

    // Set up interval
    pollingTimer = window.setInterval(poll, interval)
  },

  stopPolling: () => {
    if (pollingTimer !== null) {
      clearInterval(pollingTimer)
      pollingTimer = null
    }
  },
}))
