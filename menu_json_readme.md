 JSON Configuration Guide for ConfigDrivenPanel body { font-family: Arial, sans-serif; line-height: 1.6; margin: 20px; background-color: #f9f9f9; color: #333; } h1, h2, h3, h4 { color: #2c3e50; } table { width: 100%; border-collapse: collapse; margin-bottom: 20px; background-color: #fff; } table, th, td { border: 1px solid #bdc3c7; } th, td { padding: 10px; text-align: left; } th { background-color: #ecf0f1; } pre { background-color: #ecf0f1; padding: 10px; overflow-x: auto; } code { font-family: Consolas, monospace; background-color: #ecf0f1; padding: 2px 4px; border-radius: 4px; } blockquote { border-left: 4px solid #bdc3c7; padding-left: 16px; color: #7f8c8d; margin: 20px 0; }

# JSON Configuration Guide for ConfigDrivenPanel

This document provides a comprehensive overview of the JSON configuration format used to define panels, groups, and controls within the `ConfigDrivenPanel` system. Whether you're customizing vehicle settings, adding utility commands, or managing parameters, this guide will help you understand and utilize the available options effectively.

## Table of Contents

- [JSON Configuration Guide for ConfigDrivenPanel](#json-configuration-guide-for-configdrivenpanel)
  - [Table of Contents](#table-of-contents)
  - [Introduction](#introduction)
  - [Overall JSON Structure](#overall-json-structure)
    - [Top-Level Properties](#top-level-properties)
    - [Groups](#groups)
  - [Group Object](#group-object)
    - [Properties](#properties)
  - [Control Object](#control-object)
    - [Common Properties](#common-properties)
    - [Control Types](#control-types)
      - [Toggle Control](#toggle-control)
        - [Properties:](#properties-1)
      - [Float Control](#float-control)
        - [Properties:](#properties-2)
      - [Integer Control](#integer-control)
        - [Properties:](#properties-3)
      - [Selection Control](#selection-control)
        - [Properties:](#properties-4)
      - [Param Viewer Control](#param-viewer-control)
        - [Properties:](#properties-5)
      - [File Viewer Control](#file-viewer-control)
        - [Properties:](#properties-6)
      - [Command Button Control](#command-button-control)
        - [Properties:](#properties-7)
  - [Conditions](#conditions)
    - [Condition Types](#condition-types)
    - [Composite Conditions](#composite-conditions)
  - [Examples](#examples)
    - [Full Configuration Example](#full-configuration-example)
    - [Individual Control Examples](#individual-control-examples)
      - [Toggle Control with Conditions](#toggle-control-with-conditions)
      - [Float Control with Default Value](#float-control-with-default-value)
      - [Command Button Control with Confirmation](#command-button-control-with-confirmation)
  - [Optional vs. Required Properties](#optional-vs-required-properties)
    - [Required Properties](#required-properties)
    - [Optional Properties](#optional-properties)
  - [Notes and Best Practices](#notes-and-best-practices)
  - [Conclusion](#conclusion)

- - -

## Introduction

The `ConfigDrivenPanel` system leverages JSON files to dynamically generate user interface panels, organizing settings into groups and controls. This approach allows for flexible and scalable configuration management, enabling developers and advanced users to customize functionalities without altering the underlying codebase.

## Overall JSON Structure

A configuration JSON file defines the entire panel's structure, including its name, icon, description, and the groups and controls it contains.

### Top-Level Properties

| Property | Type | Description | Required | Default |
| --- | --- | --- | --- | --- |
| `menuName` | String | The name of the panel displayed in the UI. | Yes | \-  |
| `menuIcon` | String | The icon associated with the panel. | Yes | \-  |
| `menuDescription` | String | A brief description of the panel. | Yes | \-  |
| `persistentParams` | Array | List of parameter names that persist across sessions. | No  | `[]` |
| `clearOnManagerStartParams` | Array | Parameters to clear when the manager starts. | No  | `[]` |
| `clearOnOnroadTransitionParams` | Array | Parameters to clear during on-road transitions. | No  | `[]` |
| `clearOnOffroadTransitionParams` | Array | Parameters to clear during off-road transitions. | No  | `[]` |
| `groups` | Array | An array of group objects defining UI sections and controls. | Yes | \-  |

### Groups

The `groups` array contains objects that define distinct sections within the panel, each housing related controls.

## Group Object

Each group organizes related controls under a common title and can optionally include a reset button.

### Properties

| Property | Type | Description | Required | Default |
| --- | --- | --- | --- | --- |
| `groupName` | String | A unique identifier for the group. | Yes | \-  |
| `title` | String | The display title of the group in the UI. | Yes | \-  |
| `enableResetButton` | Boolean | Determines if a reset button is available for the group. | No  | `false` |
| `controls` | Array | An array of control objects within the group. | Yes | \-  |

> **Example:**
>
> ```
> {
>   "groupName": "systemPreferencesGroup",
>   "title": "System Preferences: (Tap item for desc)",
>   "enableResetButton": false,
>   "controls": [
>     // Control objects here
>   ]
> }
> ```

## Control Object

Controls are interactive elements within a group that allow users to view or modify settings. Each control type has specific properties.

### Common Properties

| Property | Type | Description | Required | Default |
| --- | --- | --- | --- | --- |
| `type` | String | The type of control (e.g., toggle, float, selection). | Yes | \-  |
| `param` | String | The parameter name associated with the control. | Yes | \-  |
| `title` | String | The display title of the control. | Yes | \-  |
| `desc` | String | A description or tooltip for the control. | Yes | \-  |
| `disable` | Boolean | Disables the control if set to `true`. | No  | `false` |
| `conditions` | Object | Defines conditions for the control's visibility and behavior. | No  | \-  |

### Control Types

#### Toggle Control

A switch that allows users to enable or disable a specific feature.

##### Properties:

| Property | Type | Description | Required | Default |
| --- | --- | --- | --- | --- |
| `type` | String | Must be `"toggle"`. | Yes | \-  |
| `param` | String | The parameter name to toggle. | Yes | \-  |
| `title` | String | The display title of the toggle. | Yes | \-  |
| `desc` | String | Description of what the toggle controls. | Yes | \-  |
| `disable` | Boolean | Disables the toggle if set to `true`. | No  | `false` |
| `conditions` | Object | Conditions to determine the toggle's availability. | No  | \-  |

**Example:**

```
{
  "type": "toggle",
  "param": "FordPrefEnableDebugLogs",
  "title": "Enable Debug Logging",
  "desc": "Enables additional debug output.",
  "disable": false,
  "conditions": {
    "git_remote": ["ford-op/sp-dev-c3"],
    "git_branch": []
  }
}
```

#### Float Control

Allows users to adjust a floating-point parameter within a specified range.

##### Properties:

| Property | Type | Description | Required | Default |
| --- | --- | --- | --- | --- |
| `type` | String | Must be `"float"`. | Yes | \-  |
| `param` | String | The parameter name to adjust. | Yes | \-  |
| `title` | String | The display title of the float control. | Yes | \-  |
| `desc` | String | Description of what the float control adjusts. | Yes | \-  |
| `disable` | Boolean | Disables the control if set to `true`. | No  | `false` |
| `min` | Number | The minimum allowable value. | Yes | \-  |
| `max` | Number | The maximum allowable value. | Yes | \-  |
| `increment` | Number | The step increment for adjusting the value. | Yes | \-  |
| `division` | Number | Division factor for displaying the value. | Yes | `1.0` |
| `conditions` | Object | Conditions to determine the control's availability. | No  | \-  |

**Example:**

```
{
  "type": "float",
  "param": "FordLatTuningLaneChgModifier",
  "title": "Lane Change Modifier",
  "desc": "Adjust lane change curvature aggressiveness (lower = slower).",
  "disable": false,
  "min": 0.0,
  "max": 1.0,
  "increment": 0.05,
  "division": 100.0,
  "conditions": {
    "git_remote": ["any"],
    "git_branch": []
  }
}
```

#### Integer Control

Allows users to adjust an integer parameter within a specified range.

##### Properties:

| Property | Type | Description | Required | Default |
| --- | --- | --- | --- | --- |
| `type` | String | Must be `"integer"`. | Yes | \-  |
| `param` | String | The parameter name to adjust. | Yes | \-  |
| `title` | String | The display title of the integer control. | Yes | \-  |
| `desc` | String | Description of what the integer control adjusts. | Yes | \-  |
| `disable` | Boolean | Disables the control if set to `true`. | No  | `false` |
| `min` | Number | The minimum allowable value. | Yes | \-  |
| `max` | Number | The maximum allowable value. | Yes | \-  |
| `increment` | Number | The step increment for adjusting the value. | Yes | \-  |
| `conditions` | Object | Conditions to determine the control's availability. | No  | \-  |

**Example:**

```
{
  "type": "integer",
  "param": "FordLatTuningRandomNumber",
  "title": "Random Number",
  "desc": "A random number between 0 and 10",
  "disable": false,
  "min": 0,
  "max": 10,
  "increment": 1,
  "conditions": { "git_remote": ["any"], "git_branch": [] }
}
```


#### Selection Control

Provides a dropdown or selection interface for choosing among predefined options.

##### Properties:

| Property | Type | Description | Required | Default |
| --- | --- | --- | --- | --- |
| `type` | String | Must be `"selection"`. | Yes | \-  |
| `param` | String | The parameter name to set based on selection. | Yes | \-  |
| `title` | String | The display title of the selection control. | Yes | \-  |
| `desc` | String | Description of what the selection controls. | Yes | \-  |
| `disable` | Boolean | Disables the control if set to `true`. | No  | `false` |
| `options` | Array | An array of option objects defining selectable items. | Yes | \-  |

**Option Object Properties:**

| Property | Type | Description | Required | Default |
| --- | --- | --- | --- | --- |
| `name` | String | The display name of the option. | Yes | \-  |
| `value` | String | The value to set when the option is selected. | Yes | \-  |

**Example:**

```
{
  "type": "selection",
  "param": "FordSelectedVehicleModel",
  "title": "Vehicle Model",
  "desc": "Select your vehicle model",
  "disable": false,
  "options": [
    { "name": "F-150 14th Gen (21-23)", "value": "FORD_F_150_MK14" },
    { "name": "F-150 Lightning 1st Gen", "value": "FORD_F_150_LIGHTNING_MK1" },
    { "name": "Mustang Mach-E 1st Gen", "value": "FORD_MUSTANG_MACH_E_MK1" }
  ]
}
```

#### Param Viewer Control

Displays the current value of a specified parameter in a read-only format.

##### Properties:

| Property | Type | Description | Required | Default |
| --- | --- | --- | --- | --- |
| `type` | String | Must be `"param_viewer"`. | Yes | \-  |
| `param` | String | The parameter name to view. | Yes | \-  |
| `title` | String | The display title of the parameter viewer. | Yes | \-  |
| `desc` | String | Description of what the viewer displays. | Yes | \-  |
| `disable` | Boolean | Disables the control if set to `true`. | No  | `false` |

**Example:**

```
{
  "type": "param_viewer",
  "param": "LiveParameters",
  "title": "Live Parameters",
  "desc": "View live parameters",
  "disable": false
}
```

#### File Viewer Control

Allows users to view the contents of a specified file within the application.

##### Properties:

| Property | Type | Description | Required | Default |
| --- | --- | --- | --- | --- |
| `type` | String | Must be `"file_viewer"`. | Yes | \-  |
| `path` | String | Relative path to the file to be viewed. | Yes | \-  |
| `title` | String | The display title of the file viewer. | Yes | \-  |
| `desc` | String | Description of what the file viewer does. | Yes | \-  |
| `header` | String | Optional header text for the dialog displaying the file. | No  | \-  |
| `disable` | Boolean | Disables the control if set to `true`. | No  | `false` |

**Example:**

```
{
  "type": "file_viewer",
  "path": "CHANGELOGS_BP.md",
  "title": "BluePilot Changelog",
  "desc": "View the BluePilot changelog",
  "header": "BluePilot Changelog",
  "disable": false
}
```

#### Command Button Control

Executes a specified command when clicked, optionally requiring user confirmation.

##### Properties:

| Property | Type | Description | Required | Default |
| --- | --- | --- | --- | --- |
| `type` | String | Must be `"command_button"`. | Yes | \-  |
| `command` | String | The command to execute. | Yes | \-  |
| `working_dir` | String | The working directory to execute the command in. | No  | Current directory |
| `title` | String | The display title of the command button. | Yes | \-  |
| `desc` | String | Description of what the command does. | Yes | \-  |
| `button_text` | String | The text displayed on the button. | No  | `"EXECUTE"` |
| `confirm` | Boolean | Whether to prompt for confirmation before executing the command. | No  | `false` |
| `confirm_text` | String | The confirmation message displayed to the user. | No  | Default confirmation message |
| `confirm_yes_text` | String | The text for the confirmation "Yes" button. | No  | `"Yes"` |
| `confirm_no_text` | String | The text for the confirmation "No" button. | No  | `"No"` |
| `disable` | Boolean | Disables the control if set to `true`. | No  | `false` |

**Example:**

```
{
  "type": "command_button",
  "command": "touch test.txt",
  "working_dir": "/data/openpilot",
  "title": "Create test file",
  "desc": "Create a test file in the openpilot directory",
  "button_text": "Create",
  "confirm": true,
  "confirm_text": "Are you sure you want to create a test file?",
  "confirm_yes_text": "Yes",
  "confirm_no_text": "No",
  "disable": false
}
```

## Conditions

Conditions determine the visibility and behavior of controls based on specific criteria. They enable dynamic and context-sensitive UI elements.

### Condition Types

*   **`paramValueEquals`:** Checks if a parameter equals a specified value.
    **Structure:**

    ```
    "paramValueEquals": {
      "ParameterName": "ExpectedValue"
    }
    ```

*   **`paramValueGreaterThan`:** Checks if a parameter's value is greater than a specified number.
    **Structure:**

    ```
    "paramValueGreaterThan": {
      "ParameterName": 70
    }
    ```

*   **`paramValueLessThan`:** Checks if a parameter's value is less than a specified number.
    **Structure:**

    ```
    "paramValueLessThan": {
      "ParameterName": 90
    }
    ```

*   **`git_remote`:** Validates against specified Git remote repositories.
    **Structure:**

    ```
    "git_remote": ["remote1", "remote2"]
    ```

*   **`git_branch`:** Validates against specified Git branches.
    **Structure:**

    ```
    "git_branch": ["branch1", "branch2"]
    ```

*   **`onlyWhenTheseParams`:** Ensures that certain parameters are enabled or true.
    **Structure:**

    ```
    "onlyWhenTheseParams": ["Param1", "Param2"]
    ```


### Composite Conditions

Conditions can be combined using logical operators to form more complex criteria.

*   **`anyConditionsTrue`:** The control is visible if **any** of the specified conditions are true.
    **Structure:**

    ```
    "anyConditionsTrue": [
      { "paramValueEquals": { "DriveMode": "sport" } },
      { "allConditionsTrue": [ /* Nested conditions */ ] }
    ]
    ```

*   **`allConditionsTrue`:** The control is visible only if **all** of the specified conditions are true.
    **Structure:**

    ```
    "allConditionsTrue": [
      { "git_remote": ["ford-op/sp-dev-c3"] },
      { "paramValueLessThan": { "Temperature": 90 } }
    ]
    ```


**Example of Composite Conditions:**

```
{
  "conditions": {
    "anyConditionsTrue": [
      { "paramValueEquals": { "DriveMode": "sport" } },
      {
        "allConditionsTrue": [
          { "paramValueGreaterThan": { "Speed": 70 } },
          { "onlyWhenTheseParams": ["AdvancedModeEnabled"] }
        ]
      }
    ],
    "allConditionsTrue": [
      { "git_remote": ["ford-op/sp-dev-c3"] },
      { "paramValueLessThan": { "Temperature": 90 } }
    ]
  }
}
```

## Examples

### Full Configuration Example

Below is a sample JSON configuration that defines two panels: "BluePilot" for Ford settings and "Utilities" for various utility commands.

```
{
  "_comment": "This configuration file defines all Ford settings panels, groups, and controls.",
  "menuName": "BluePilot",
  "menuIcon": "icon_ford.png",
  "menuDescription": "BluePilot Panel",
  "persistentParams": [
    "FordSelectedVehicleModel",
    "FordPrefQuietDrive",
    "FordPrefEnableDebugLogs",
    "FordPrefSendHandsFreeCanMsg",
    "FordPrefLaneDepartCanMsg",
    "FordPrefDriverMonitorCanMsg",
    "FordPrefEnablePathAngle",
    "FordPrefHumanTurnDetectionEnable",
    "FordPrefEnableCustomLatLogic",
    "FordLatTuningLaneChgModifier",
    "FordLatTuningCustomPathOffset",
    "FordLongTuningBrakeActuatorActivate",
    "FordLongTuningBrakeActuatorReleaseDelta",
    "FordLongTuningPrechargeActuatorTargetDelta",
    "FordLimitsCurvatureMax",
    "FordLimitsCurvatureError"
  ],
  "clearOnManagerStartParams": [],
  "clearOnOnroadTransitionParams": [],
  "clearOnOffroadTransitionParams": [],
  "groups": [
    {
      "groupName": "vehicleSelectorGroup",
      "title": "Vehicle Selection: (Tap item for desc)",
      "enableResetButton": false,
      "controls": [
        {
          "type": "selection",
          "param": "FordSelectedVehicleModel",
          "title": "Vehicle Model",
          "desc": "Select your vehicle model",
          "disable": false,
          "options": [
            { "name": "F-150 14th Gen (21-23)", "value": "FORD_F_150_MK14" },
            { "name": "F-150 Lightning 1st Gen", "value": "FORD_F_150_LIGHTNING_MK1" },
            { "name": "Mustang Mach-E 1st Gen", "value": "FORD_MUSTANG_MACH_E_MK1" }
          ]
        },
        {
          "type": "file_viewer",
          "path": "CHANGELOGS_BP.md",
          "title": "BluePilot Changelog",
          "desc": "View the BluePilot changelog",
          "header": "BluePilot Changelog",
          "disable": false
        }
      ]
    },
    // Additional groups here
  ]
}
```

### Individual Control Examples

#### Toggle Control with Conditions

```
{
  "type": "toggle",
  "param": "FordPrefEnableDebugLogs",
  "title": "Enable Debug Logging",
  "desc": "Enables additional debug output.",
  "disable": false,
  "conditions": {
    "git_remote": ["ford-op/sp-dev-c3"],
    "git_branch": []
  }
}
```

#### Float Control with Default Value

```
{
  "type": "float",
  "param": "FordLatTuningLaneChgModifier",
  "title": "Lane Change Modifier",
  "desc": "Adjust lane change curvature aggressiveness (lower = slower).",
  "disable": false,
  "min": 0.0,
  "max": 1.0,
  "increment": 0.05,
  "division": 100.0,
  "conditions": {
    "git_remote": ["any"],
    "git_branch": []
  }
}
```

#### Command Button Control with Confirmation

```
{
  "type": "command_button",
  "command": "touch test.txt",
  "working_dir": "/data/openpilot",
  "title": "Create test file",
  "desc": "Create a test file in the openpilot directory",
  "button_text": "Create",
  "confirm": true,
  "confirm_text": "Are you sure you want to create a test file?",
  "confirm_yes_text": "Yes",
  "confirm_no_text": "No",
  "disable": false
}
```

## Optional vs. Required Properties

Understanding which properties are mandatory and which are optional is crucial for creating valid configurations.

### Required Properties

*   **Top-Level:**
    *   `menuName`
    *   `menuIcon`
    *   `menuDescription`
    *   `groups`
*   **Group Object:**
    *   `groupName`
    *   `title`
    *   `controls`
*   **Control Object:**
    *   `type`
    *   `param`
    *   `title`
    *   `desc`

### Optional Properties

*   **Top-Level:**
    *   `persistentParams`
    *   `clearOnManagerStartParams`
    *   `clearOnOnroadTransitionParams`
    *   `clearOnOffroadTransitionParams`
*   **Group Object:**
    *   `enableResetButton`
*   **Control Object:**
    *   `disable`
    *   `conditions`
    *   Additional properties specific to control types (e.g., `min`, `max`, `options`).

## Notes and Best Practices

*   **Unique `groupName`:** Ensure each `groupName` is unique across the configuration to prevent conflicts.
*   **Consistent Parameter Naming:** Use clear and consistent naming conventions for parameters (`param`) to maintain readability and manageability.
*   **Condition Accuracy:** When defining conditions, ensure that parameter names and expected values match existing configurations and logic to prevent controls from being unintentionally hidden or disabled.
*   **Increment and Division Values:** For float controls, `increment` and `division` values should be chosen carefully to balance precision and usability.
*   **Command Safety:** When using `command_button` controls, ensure that commands are safe and tested to prevent unintended system behavior.
*   **Styling Consistency:** Leverage the available styling options to maintain a consistent and intuitive UI across all controls and groups.

## Conclusion

The JSON configuration system for `ConfigDrivenPanel` offers a flexible and powerful way to define and manage user interface panels, groups, and controls. By adhering to the outlined structure and best practices, you can create customized, conditionally-driven interfaces that enhance the user experience and streamline configuration management.

For further customization and advanced configurations, refer to the codebase and explore additional properties and control types as needed.
