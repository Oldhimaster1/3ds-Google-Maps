# 3DS Google Maps - Copilot Instructions

<!-- Use this file to provide workspace-specific custom instructions to Copilot. For more details, visit https://code.visualstudio.com/docs/copilot/copilot-customization#_use-a-githubcopilotinstructionsmd-file -->

## Project Overview
This is a Nintendo 3DS homebrew application that provides interactive Google Maps functionality on the dual screens with touch navigation and WiFi connectivity.

## Technical Stack
- **Platform**: Nintendo 3DS homebrew
- **Framework**: libctru (3DS system library)
- **Graphics**: citro2d/citro3d for hardware-accelerated 2D/3D graphics
- **Networking**: 3DS HTTP client for fetching map tiles
- **Map Provider**: OpenStreetMap tiles (free alternative to Google Maps)

## Architecture
- `main.c`: Main application loop, input handling, screen management
- `map_tiles.c/h`: Map tile management, caching, coordinate conversion
- `network.c/h`: WiFi connectivity, HTTP requests for downloading tiles
- `Makefile`: devkitARM build configuration for 3DS homebrew

## Key Features
- Dual-screen map display (top: main map, bottom: continuation/controls)
- Touch screen panning and D-pad navigation
- Zoom in/out with L/R shoulder buttons
- Tile caching system for performance
- Real-time tile downloading over WiFi
- Coordinate system conversion (lat/lon ↔ tile coordinates)

## Development Guidelines
- Use 3DS-specific APIs (libctru) for system functions
- Memory management is critical - always free allocated buffers
- Network operations should be non-blocking where possible
- Graphics rendering uses hardware acceleration via citro2d
- Follow 3DS screen dimensions: Top (400x240), Bottom (320x240)
- Tile size standard: 256x256 pixels

## Build Requirements
- devkitARM toolchain
- libctru development libraries
- citro2d/citro3d graphics libraries
- 3DS homebrew development environment

## API Usage Notes
- Always check Result codes from 3DS system calls
- Use proper error handling for network operations
- Implement tile caching to reduce network requests
- Handle touch input states properly (start/continue/end dragging)
