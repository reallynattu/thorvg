# ThorVG Build Configurator ⚡

A visual UI for creating optimized custom ThorVG builds.

![Build Configurator UI](https://raw.githubusercontent.com/thorvg/thorvg/main/tools/build-ui/screenshot.png)

## Features

- 🎛️ **Visual Configuration** - Toggle features with a clean, modern UI
- 📦 **Presets** - Quick configs for common use cases (Minimal, Lottie, SVG, Full)
- 📊 **Size Estimates** - See approximate build size as you configure
- ⚡ **Live Building** - Run builds directly from the browser
- 📁 **Output Tracking** - See actual file sizes after build

## Quick Start

```bash
# Navigate to build-ui directory
cd tools/build-ui

# Start the server
node server.js

# Open http://localhost:8080
```

## Usage

1. **Select Features** - Click on options to toggle them
2. **Use Presets** - Click preset buttons for common configurations:
   - 🪶 **Minimal** - Smallest possible build (~150KB)
   - 🎬 **Lottie Only** - Just Lottie support (~800KB)
   - 📐 **SVG Only** - Just SVG support (~430KB)
   - 🎯 **Full Featured** - Everything enabled (~1.4MB)
   - 🌐 **WebAssembly** - Optimized for WASM builds
3. **Build** - Click "Build Now" to run the build
4. **Copy Command** - Or copy the meson command to run manually

## Requirements

- Node.js 14+
- meson & ninja installed
- ThorVG repository cloned

## API Endpoints

- `GET /` - Serve the UI
- `POST /api/build` - Run a build with the given command
- `GET /api/build/status` - Get the last build status and file sizes

## Configuration Options

### Engines
- **Software (sw)** - CPU-based rendering (always recommended)
- **OpenGL (gl)** - GPU rendering via OpenGL
- **WebGPU (wg)** - Modern GPU API

### Loaders
- **SVG** - Scalable Vector Graphics
- **Lottie** - Lottie/Bodymovin animations
- **PNG** - Portable Network Graphics
- **JPEG** - JPEG images
- **WebP** - WebP images
- **TTF** - TrueType fonts

### Savers
- **GIF** - Animated GIF export

### Build Options
- **Multi-threading** - Parallel task scheduler
- **Partial Rendering** - Incremental rendering
- **SIMD** - CPU vectorization
- **File I/O** - File system access
- **Static Linking** - Force static modules
- **Debug Logging** - Enable log messages

## License

MIT - Same as ThorVG
