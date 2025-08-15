# Ford Drive Mode Feature

## Overview

The Ford Drive Mode feature allows openpilot to automatically set and manage drive modes for Ford vehicles. This feature provides enhanced control over vehicle performance characteristics by automatically selecting appropriate drive modes based on user preferences and driving conditions.

## Features

### Automatic Drive Mode Selection
- **Default Drive Mode**: Set a preferred drive mode that will be automatically applied when the vehicle starts
- **Smart Mode Detection**: Automatically detects available drive modes from the vehicle's CAN bus
- **Mode Validation**: Ensures drive mode combinations are valid for the specific vehicle

### Supported Drive Modes

#### Powertrain Modes
- **Normal (0)**: Standard driving mode with balanced performance and efficiency
- **Sport (1)**: Enhanced performance with sportier throttle response and shift points
- **Economy (2)**: Optimized for fuel efficiency with relaxed throttle response
- **Tow Haul (3)**: Optimized for towing with enhanced engine braking and shift patterns
- **Grass/Gravel/Snow (5)**: Enhanced traction control for loose surfaces
- **Sand (7)**: Optimized for sand driving with reduced traction control
- **Mud/Ruts (8)**: Enhanced off-road performance for muddy conditions
- **Rock Crawl (9)**: Low-speed off-road mode for technical terrain
- **Sport Adaptive (14)**: Adaptive sport mode that adjusts to driving style
- **High Speed Desert/Baja (15)**: High-speed off-road mode for desert racing
- **Drag Mode (16)**: Optimized for drag racing with launch control
- **EV Now Mode (17)**: Electric vehicle mode prioritizing battery usage
- **EV Later Charger Mode (18)**: Electric vehicle mode preserving battery for later charging

#### Chassis Modes
- **Normal Adaptive (1)**: Adaptive suspension that adjusts to road conditions
- **Comfort (5)**: Smooth ride with softer suspension settings
- **Comfort Adaptive (6)**: Adaptive comfort mode that adjusts to road conditions
- **Low Mu Mode (7)**: Enhanced stability control for low friction surfaces
- **Mud and Ruts (8)**: Off-road suspension settings for challenging terrain
- **Track Mode (13)**: Firm suspension and enhanced handling for track use
- **Rough Road Mode (14)**: Enhanced suspension for rough road conditions
- **High Speed Desert (12)**: High-speed off-road suspension settings

#### AWD Modes
- **2WD (0)**: Two-wheel drive mode for improved fuel efficiency
- **4WD Auto (1)**: Automatic four-wheel drive that engages when needed
- **4WD High (2)**: Four-wheel drive high range for off-road use
- **4WD Low (3)**: Four-wheel drive low range for extreme off-road
- **Neutral (4)**: Neutral position for towing or maintenance

## Configuration

### Setting Default Drive Mode

1. Navigate to **BluePilot** → **Vehicle** → **Default Drive Mode**
2. Select your preferred drive mode from the dropdown
3. The selected mode will be automatically applied when the vehicle starts

### Parameter Configuration

The feature uses the following parameter:
- `FordDefaultDriveMode`: String parameter containing the default drive mode value

## Technical Implementation

### CAN Message Structure

Drive modes are controlled through the following CAN messages:

- **Message 1056 (SelectDriveModeData)**: Main drive mode selection message
  - `SelDrvMdePt_D_Rq`: Powertrain drive mode request
  - `SelDrvMdeChassis_D_Rq`: Chassis drive mode request
  - `SelDrvMdeAwd_D_Rq`: AWD drive mode request
  - `SelDrvMde_D_Stat`: Drive mode change status

- **Message 1054 (BrakeSysFeatures_3)**: Additional drive mode information
  - `SelDrvMdeChassis2_D_Rq`: Secondary chassis drive mode
  - `SelDrvMdeAwd_D_Rq`: AWD drive mode

### Code Structure

#### Car State Parser (`carstate.py`)
- Parses drive mode signals from CAN messages
- Updates car state with current drive mode information
- Provides drive mode data to the UI and controller

#### Car Controller (`carcontroller.py`)
- Manages drive mode change requests
- Sends drive mode CAN messages to the vehicle
- Handles drive mode validation and error handling

#### Drive Mode Utilities (`drive_mode_utils.py`)
- Provides drive mode definitions and metadata
- Validates drive mode combinations
- Offers utility functions for drive mode management

### Drive Mode Change Process

1. **Request Initiation**: User selects a new drive mode through the UI
2. **Validation**: System validates the requested mode combination
3. **CAN Message Creation**: Controller creates appropriate CAN messages
4. **Message Transmission**: Messages are sent to the vehicle at 10Hz
5. **Confirmation**: System monitors vehicle response and confirms mode change

## Vehicle Compatibility

### Supported Vehicles
- **F-150 (2021-2023)**: Full drive mode support
- **F-150 Lightning**: Full drive mode support with EV-specific modes
- **Mustang Mach-E**: Full drive mode support with EV-specific modes
- **Bronco Sport**: Full drive mode support with off-road modes
- **Explorer**: Full drive mode support
- **Edge**: Full drive mode support
- **Escape**: Full drive mode support
- **Expedition**: Full drive mode support
- **Ranger**: Full drive mode support with off-road modes

### Vehicle Type Detection
The system automatically detects vehicle type and filters available drive modes accordingly:
- **Car**: Basic powertrain and chassis modes
- **SUV**: Full range of modes including off-road
- **Truck**: Full range of modes including towing and off-road
- **EV/PHEV**: Electric vehicle specific modes
- **Performance**: Track and performance modes
- **Luxury**: Enhanced comfort and adaptive modes

## Safety Features

### Mode Validation
- Ensures requested drive mode combinations are valid
- Prevents incompatible mode selections
- Validates against vehicle capabilities

### Error Handling
- Graceful fallback to safe default modes
- Comprehensive error logging
- User notification of mode change failures

### Rate Limiting
- Limits drive mode change frequency to prevent CAN bus flooding
- Implements proper timing for mode change requests
- Monitors vehicle response to mode changes

## Usage Examples

### City Driving
- **Powertrain**: Normal or Economy
- **Chassis**: Normal Adaptive or Comfort
- **AWD**: 2WD for fuel efficiency

### Highway Driving
- **Powertrain**: Economy for fuel efficiency
- **Chassis**: Comfort for long-distance comfort
- **AWD**: 2WD for fuel efficiency

### Off-Road Driving
- **Powertrain**: Grass/Gravel/Snow or Mud/Ruts
- **Chassis**: Mud and Ruts or Rough Road
- **AWD**: 4WD Auto or 4WD High

### Towing
- **Powertrain**: Tow Haul
- **Chassis**: Normal Adaptive
- **AWD**: 4WD Auto

### Performance Driving
- **Powertrain**: Sport
- **Chassis**: Track Mode
- **AWD**: 2WD for maximum performance

## Troubleshooting

### Common Issues

#### Drive Mode Not Changing
1. Check if the vehicle supports the requested mode
2. Verify the vehicle is in a state that allows mode changes
3. Check for any error messages in the logs

#### Mode Combination Not Allowed
1. Ensure the requested modes are compatible
2. Check vehicle type restrictions
3. Use the validation functions to check compatibility

#### CAN Message Errors
1. Verify vehicle CAN bus configuration
2. Check for message frequency and timing issues
3. Review CAN message structure compatibility

### Debug Information

The system provides comprehensive logging for troubleshooting:
- Drive mode change requests and responses
- CAN message transmission status
- Vehicle compatibility information
- Error conditions and fallback actions

## Future Enhancements

### Planned Features
- **Automatic Mode Selection**: AI-driven mode selection based on driving conditions
- **Mode Profiles**: Save and recall custom drive mode combinations
- **Conditional Mode Changes**: Automatic mode changes based on speed, terrain, or load
- **Integration with Navigation**: Mode suggestions based on destination and route

### API Extensions
- **External Control**: Allow external systems to request drive mode changes
- **Telemetry Integration**: Log drive mode usage and performance metrics
- **Cloud Sync**: Sync drive mode preferences across devices

## Contributing

### Development Guidelines
- Follow the existing code style and patterns
- Add comprehensive error handling
- Include unit tests for new functionality
- Update documentation for any changes

### Testing
- Test with multiple vehicle types
- Verify CAN message compatibility
- Test error conditions and edge cases
- Validate mode combination logic

## References

### Technical Documentation
- [Ford CAN Bus Documentation](https://www.f150gen14.com/forum/threads/introducing-bluepilot-a-ford-specific-fork-for-comma3x-openpilot.24241/)
- [Openpilot CAN Protocol](https://github.com/commaai/openpilot/blob/master/docs/CAN.md)
- [Ford DBC Files](opendbc_repo/opendbc/dbc/ford_lincoln_base_pt.dbc)

### Related Features
- [BluePilot Ford Integration](../bluepilot/README.md)
- [Openpilot Vehicle Interfaces](../opendbc_repo/opendbc/car/README.md)
- [CAN Message Handling](../opendbc_repo/opendbc/README.md)
