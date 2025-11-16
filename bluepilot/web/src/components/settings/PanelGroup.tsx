/**
 * Panel Group Component
 * Renders a group of controls within a panel
 */

import { useMemo } from 'react'
import type { PanelGroup as PanelGroupType, PanelState } from '@/types/panels'
import { useParamsStore } from '@/stores/useParamsStore'
import { isControlVisible } from '@/utils/conditionalEvaluator'
import { Button } from '@/components/common'
import { DynamicControl } from './DynamicControl'
import './PanelGroup.css'

interface PanelGroupProps {
  group: PanelGroupType
  state: PanelState
  panelId?: string
}

export function PanelGroup({ group, state, panelId }: PanelGroupProps) {
  const params = useParamsStore((store) => store.params)

  // Skip hidden groups
  if (group.hidden) {
    return null
  }

  // Check if any controls in this group are visible
  const hasVisibleControls = useMemo(() => {
    return group.controls.some((control) => {
      // Check if control is hidden in web UI
      if ('webSupported' in control && control.webSupported === false) {
        return false
      }
      // Check visibility conditions
      return isControlVisible(control, state, params)
    })
  }, [group.controls, state, params])

  // Hide the entire group if no controls are visible
  if (!hasVisibleControls) {
    return null
  }

  return (
    <div className="panel-group">
      <div className="panel-group-header">
        <h3 className="panel-group-title">{group.title}</h3>
        {group.enableResetButton && (
          <Button
            variant="secondary"
            size="small"
            className="panel-group-reset"
            title="Reset to defaults"
            type="button"
          >
            Reset
          </Button>
        )}
      </div>

      <div className="panel-group-controls">
        {group.controls.map((control, index) => (
          <DynamicControl
            key={`${group.groupName}-${index}`}
            control={control}
            state={state}
            panelId={panelId}
            groupName={group.groupName}
          />
        ))}
      </div>
    </div>
  )
}
