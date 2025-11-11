/**
 * Panel Group Component
 * Renders a group of controls within a panel
 */

import type { PanelGroup as PanelGroupType, PanelState } from '@/types/panels'
import { Button } from '@/components/common'
import { DynamicControl } from './DynamicControl'
import './PanelGroup.css'

interface PanelGroupProps {
  group: PanelGroupType
  state: PanelState
  panelId?: string
}

export function PanelGroup({ group, state, panelId }: PanelGroupProps) {
  // Skip hidden groups
  if (group.hidden) {
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
