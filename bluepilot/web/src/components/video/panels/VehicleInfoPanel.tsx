import { useEffect, useState } from 'react'
import type { RouteDetails } from '@/types'
import './Panels.css'

interface VehicleInfoPanelProps {
  route: RouteDetails | null
}

interface FirmwareVersion {
  ecu: string
  version: string
}

interface VehicleFingerprint {
  carFingerprint?: string
  carVin?: string
  brand?: string
  fingerprintSource?: string
  firmwareVersions?: FirmwareVersion[]
}

export const VehicleInfoPanel = ({ route }: VehicleInfoPanelProps) => {
  const [isOpen, setIsOpen] = useState(true)
  const [vehicleData, setVehicleData] = useState<VehicleFingerprint | null>(null)

  useEffect(() => {
    if (route && route.fingerprint) {
      setVehicleData(route.fingerprint as VehicleFingerprint)
    } else {
      setVehicleData(null)
    }
  }, [route])

  if (!vehicleData) {
    return null
  }

  // Ford part number format: PL3A-14C204-XK or PL3A-14C204-XKK
  // Pattern: [4 alphanumeric]-[5-6 alphanumeric]-[2-3 alphanumeric]
  const fordPartNumberRegex = /^[A-Z0-9]{4}-[A-Z0-9]{5,6}-[A-Z0-9]{2,3}$/i

  // Filter and deduplicate firmware versions
  const filteredFirmware = vehicleData.firmwareVersions
    ? vehicleData.firmwareVersions.filter((fw, index, self) => {
        // Only show Ford part number formats
        if (!fordPartNumberRegex.test(fw.version)) return false

        // Deduplicate based on ECU+version combination
        return self.findIndex(f => f.ecu === fw.ecu && f.version === fw.version) === index
      })
    : []

  return (
    <div className="bottom-panel" id="vehicle-panel">
      <div
        className="panel-header"
        id="vehicle-panel-header"
        onClick={() => setIsOpen(!isOpen)}
        style={{ cursor: 'pointer' }}
      >
        <div className="panel-title">
          <svg
            className="panel-icon"
            width="20"
            height="20"
            viewBox="0 0 24 24"
            fill="none"
            stroke="currentColor"
            strokeWidth="2"
          >
            <path d="M18 6H5a2 2 0 0 0-2 2v3a2 2 0 0 0 2 2h13l4-3.5L18 6Z" />
            <path d="M5 11V6" />
            <circle cx="7" cy="15" r="2" />
            <circle cx="17" cy="15" r="2" />
          </svg>
          <div className="panel-title-text">
            <span className="panel-title-label">Vehicle Information</span>
            <span className="panel-title-subtitle">Platform details & firmware</span>
          </div>
        </div>
        <div className="panel-header-actions">
          <span className="panel-collapse-indicator">
            <svg
              width="16"
              height="16"
              viewBox="0 0 24 24"
              fill="none"
              stroke="currentColor"
              strokeWidth="2"
              style={{
                transform: isOpen ? 'rotate(0deg)' : 'rotate(-90deg)',
                transition: 'transform 0.2s'
              }}
            >
              <polyline points="6 9 12 15 18 9" />
            </svg>
          </span>
        </div>
      </div>
      {isOpen && (
        <div className="panel-content" id="vehicle-panel-content">
          <div className="vehicle-info-grid">
            <div className="vehicle-info-card">
              <div className="vehicle-info-label">Platform</div>
              <div className="vehicle-info-value">
                {vehicleData.carFingerprint || '--'}
              </div>
            </div>
            <div className="vehicle-info-card">
              <div className="vehicle-info-label">VIN</div>
              <div className="vehicle-info-value">{vehicleData.carVin || '--'}</div>
            </div>
            <div className="vehicle-info-card">
              <div className="vehicle-info-label">Brand</div>
              <div className="vehicle-info-value">{vehicleData.brand || '--'}</div>
            </div>
            <div className="vehicle-info-card">
              <div className="vehicle-info-label">Fingerprint Source</div>
              <div className="vehicle-info-value">
                {vehicleData.fingerprintSource?.toUpperCase() || '--'}
              </div>
            </div>
          </div>

          {filteredFirmware.length > 0 && (
            <div className="firmware-section">
              <h4 className="firmware-section-title">ECU Firmware Versions</h4>
              <div className="firmware-table-wrapper">
                <table className="firmware-table">
                  <thead>
                    <tr>
                      <th>ECU</th>
                      <th>Firmware Version</th>
                    </tr>
                  </thead>
                  <tbody>
                    {filteredFirmware.map((fw, index) => (
                      <tr key={index}>
                        <td>{fw.ecu}</td>
                        <td>{fw.version}</td>
                      </tr>
                    ))}
                  </tbody>
                </table>
              </div>
            </div>
          )}
        </div>
      )}
    </div>
  )
}
