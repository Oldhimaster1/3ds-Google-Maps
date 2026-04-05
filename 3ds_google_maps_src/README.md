# 3DS Google Maps

An interactive Google Maps application for Nintendo 3DS homebrew that provides map viewing, navigation, and touch controls across both screens.

## Features

- **Dual Screen Display**: Utilizes both 3DS screens for an expanded map view
- **Touch Navigation**: Pan the map by dragging on the touch screen
- **D-Pad Controls**: Navigate using the directional pad
- **Zoom Controls**: Zoom in/out using L/R shoulder buttons
- **WiFi Connectivity**: Downloads map tiles in real-time over WiFi
- **Tile Caching**: Intelligent caching system for smooth performance
- **Cross-hair Indicator**: Red crosshair shows the center point on the map

## Controls

| Input | Action |
|-------|--------|
| D-Pad | Pan the map in any direction |
| L Button | Zoom in |
| R Button | Zoom out |
| Touch Screen | Drag to pan the map |
| START | Exit application |

## Technical Details

### Architecture
- **Main Loop**: Handles input, rendering, and system events
- **Map Tiles**: Downloads and caches 256x256 pixel map tiles
- **Network**: HTTP client for fetching tiles from OpenStreetMap
- **Graphics**: Hardware-accelerated rendering using citro2d/citro3d

### Map Provider
Currently uses OpenStreetMap tiles as a free alternative to Google Maps. The tile server URL format is:
```
https://tile.openstreetmap.org/{zoom}/{x}/{y}.png
```

### Coordinate System
- Uses standard Web Mercator projection (EPSG:3857)
- Converts between latitude/longitude and tile coordinates
- Supports zoom levels 1-18

## Building

### Requirements
- devkitARM toolchain
- libctru development libraries
- citro2d/citro3d graphics libraries
- Nintendo 3DS homebrew development environment

### Build Instructions
```bash
make clean
make
```

This will generate `3ds_google_maps.3dsx` which can be run on a homebrew-enabled 3DS.

## Installation

1. Copy `3ds_google_maps.3dsx` to your 3DS SD card's `/3ds/` folder
2. Launch using a homebrew launcher (such as the Homebrew Launcher)
3. Ensure WiFi is enabled and connected before starting

## Default Location

The application starts centered on New York City (40.7128°N, 74.0060°W) at zoom level 10.

## Performance Notes

- Tile caching reduces network requests for better performance
- Maximum of 64 tiles cached simultaneously
- Tiles are downloaded asynchronously to avoid blocking the UI
- Network timeout and error handling for unreliable connections

## Future Enhancements

- [ ] GPS location support (if available)
- [ ] Search functionality for addresses/places
- [ ] Custom markers and points of interest
- [ ] Offline map support
- [ ] Different map styles (satellite, terrain)
- [ ] Route planning and navigation

## License

This project is developed for educational and homebrew purposes. Map data is provided by OpenStreetMap contributors under the Open Database License.

## Disclaimer

This application requires an internet connection to download map tiles. Data usage applies based on your WiFi connection. The app is intended for homebrew/development purposes only.
