# cfc-screen-receiver

A Windows desktop application that **captures the screen** and **decodes [cimbar](https://github.com/sz3/libcimbar) barcodes** in real time, saving the received files to disk.

Cimbar (Color Icon Matrix Barcode) is a high-density 2D barcode format that encodes data as animated frames on screen. This receiver is the Windows counterpart to cimbar sender apps (e.g., on Android).

---

## Features

- Real-time screen capture using **Windows Graphics Capture API** (WinRT + Direct3D 11)
- Multi-threaded cimbar frame decoding via **libcimbar**
- Optional capture region (`--region x,y,w,h`) to focus on a specific area
- Live preview window with decode statistics and progress bars
- Decoded files are automatically saved to an output directory

---

## Requirements

| Requirement | Version |
|---|---|
| Windows | 10 version 1903 (build 18362) or later |
| Visual Studio | 2022 (MSVC v143) |
| CMake | 3.20 or later |
| Windows SDK | 10.0.19041.0 or later |
| Internet connection | Required for first build (FetchContent downloads OpenCV and libcimbar) |

---

## Building

All dependencies (OpenCV 4.11.0 and libcimbar) are fetched automatically from GitHub via CMake `FetchContent`. **The first build takes a long time** because OpenCV is compiled from source.

```powershell
# Configure (from the project root)
cmake -S . -B build -G "Visual Studio 17 2022" -A x64

# Build (Release)
cmake --build build --config Release --target cfc-screen-receiver
```

The executable will be at:
```
build/Release/cfc-screen-receiver.exe
```

---

## Usage

```
cfc-screen-receiver.exe [output_dir] [--region x,y,w,h]
```

| Argument | Description | Default |
|---|---|---|
| `output_dir` | Directory where decoded files are saved | `./cfc_received` |
| `--region x,y,w,h` | Capture a specific screen region (pixels) | Full screen |

### Examples

```powershell
# Capture full screen, save to default output folder
cfc-screen-receiver.exe

# Save to a custom directory
cfc-screen-receiver.exe C:\Users\me\Downloads\received

# Capture only a 1280x720 region starting at (100, 50)
cfc-screen-receiver.exe --region 100,50,1280,720
```

### Keys (in the preview window)

| Key | Action |
|---|---|
| `ESC` | Quit |

---

## How It Works

1. **Screen Capture** — `ScreenCapturer` uses the Windows Graphics Capture API (`Windows.Graphics.Capture`) with a Direct3D 11 staging texture to grab frames from the primary monitor at ~30 FPS.
2. **Frame Decoding** — `DecoderThread` feeds each captured frame to libcimbar's extractor and decoder pipeline on a thread pool.
3. **File Reconstruction** — libcimbar's fountain decoder reassembles file chunks across multiple frames using Wirehair LDPC fountain codes, then decompresses with Zstandard.
4. **File Output** — Completed files are written to the configured output directory and logged to the console.

---

## Project Structure

```
cfc-qt-receiver/
├── main.cpp              # Entry point, capture loop, preview window
├── ScreenCapturer.h/.cpp # WinRT + D3D11 screen capture (pImpl pattern)
├── DecoderThread.h/.cpp  # Multi-threaded cimbar decoder wrapper
├── concurrent/           # Thread pool utilities
└── CMakeLists.txt        # Build configuration (FetchContent for all deps)
```

---

## Dependencies

| Library | Source | License |
|---|---|---|
| [libcimbar](https://github.com/sz3/libcimbar) | GitHub (fetched automatically) | MIT |
| [OpenCV 4.11.0](https://github.com/opencv/opencv) | GitHub (fetched automatically) | Apache 2.0 |
| Windows Graphics Capture API | Windows SDK (built-in) | — |
| Direct3D 11 / DXGI | Windows SDK (built-in) | — |

---

## Troubleshooting

**Build fails with `and`/`or`/`xor` keyword errors in libcimbar sources**

MSVC does not support C++ alternative tokens without `<iso646.h>`. The included build normalizes libcimbar source files automatically. If you see these errors after a clean fetch, run the normalization script:

```powershell
Get-ChildItem "build\_deps\libcimbar-src\src\lib" -Recurse -Include "*.cpp","*.h" |
  ForEach-Object {
    (Get-Content $_.FullName -Raw) `
      -replace '\band\b','&&' -replace '\bor\b','||' -replace '\bnot\b','!' `
      -replace '\bxor\b','^'  -replace '\bbitand\b','&' -replace '\bbitor\b','|' |
    Set-Content $_.FullName
  }
```

**The window says "Screen capture failed"**

- Ensure you are running Windows 10 1903 or later.
- The Windows Graphics Capture API requires the app to run with a valid user session (not as a service).

**No files are decoded after a long time**

- Make sure the cimbar sender is displaying on the captured region of your screen.
- Try running without `--region` first to capture the full screen.
- Check that the preview window shows the cimbar barcode clearly.
