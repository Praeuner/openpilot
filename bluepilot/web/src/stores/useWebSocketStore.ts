import { create } from 'zustand'
import { getWebSocketService } from '@/services/websocket'
import type { WebSocketMessage } from '@/types'
import { useRoutesStore } from './useRoutesStore'
import { useParamsStore } from './useParamsStore'
import { useSystemStore } from './useSystemStore'

interface WebSocketState {
  connected: boolean
  connecting: boolean

  // Actions
  connect: () => void
  disconnect: () => void
  handleMessage: (message: WebSocketMessage) => void
}

export const useWebSocketStore = create<WebSocketState>((set) => {
  const ws = getWebSocketService()

  return {
    connected: false,
    connecting: false,

    connect: () => {
      set({ connecting: true })

      // Subscribe to WebSocket messages
      ws.subscribe((message: WebSocketMessage) => {
        useWebSocketStore.getState().handleMessage(message)
      })

      // Connect
      ws.connect()

      // Update connection status after a brief delay
      setTimeout(() => {
        set({
          connected: ws.isConnected(),
          connecting: false,
        })
      }, 1000)
    },

    disconnect: () => {
      ws.disconnect()
      set({ connected: false })
    },

    handleMessage: (message: WebSocketMessage) => {
      const { type, data } = message

      switch (type) {
        case 'connection':
          set({ connected: (data as { connected: boolean }).connected })
          break

        case 'route_updated':
        case 'route_deleted':
        case 'route_added':
          // Refresh routes
          useRoutesStore.getState().fetchRoutes(1)
          break

        case 'param_changed':
          // Update specific parameter
          if (data && typeof data === 'object' && 'key' in data && 'value' in data) {
            const { key, value } = data as { key: string; value: unknown }
            const paramsStore = useParamsStore.getState()
            if (paramsStore.params[key]) {
              paramsStore.params[key].value = value as string | number | boolean
            }
          }
          break

        case 'export_progress':
        case 'backup_progress':
          // These will be handled by export components
          console.log('Export/backup progress:', data)
          break

        case 'system_update':
          // Refresh system metrics
          useSystemStore.getState().fetchMetrics()
          break

        case 'heartbeat':
          // Respond to heartbeat with pong to keep connection alive
          const ws = getWebSocketService()
          ws.send({ type: 'pong', data: { timestamp: new Date().toISOString() } })
          break

        default:
          console.log('Unhandled WebSocket message:', type, data)
      }
    },
  }
})
