#pragma once

#include <opencv2/core.hpp>
#include <memory>

/// Captures the desktop via Windows Graphics Capture (WinRT + D3D11).
/// By default captures the entire primary monitor.
class ScreenCapturer
{
public:
    ScreenCapturer();
    ~ScreenCapturer();

    /// Set a capture region (in screen coordinates).
    /// If never called, captures the full primary screen.
    void setRegion(int x, int y, int width, int height);

    /// Grab one frame from the screen.
    /// Returns true on success, fills `out` with a BGR cv::Mat.
    bool grabFrame(cv::Mat& out);

    int captureWidth()  const { return _width; }
    int captureHeight() const { return _height; }

private:
    struct Impl;
    void initCapture();
    void releaseCapture();
    void clampRegionToBounds();

    int _srcX = 0;
    int _srcY = 0;
    int _width = 0;
    int _height = 0;
    int _fullWidth = 0;
    int _fullHeight = 0;

    std::unique_ptr<Impl> _impl;
};
