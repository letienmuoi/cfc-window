/// cfc-screen-receiver
/// Receives files encoded as cimbar barcodes by capturing the Windows desktop.
///
/// Usage:
///   cfc-screen-receiver.exe [output_dir] [--region x,y,w,h]
///
/// Keys while running:
///   ESC  - quit
///   R    - toggle region selection (shows a small preview window)

#include "ScreenCapturer.h"
#include "DecoderThread.h"

#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>

#include <Windows.h>

#include <chrono>
#include <filesystem>
#include <iostream>
#include <set>
#include <string>
#include <thread>

namespace fs = std::filesystem;

static void printUsage()
{
    std::cout << "Usage: cfc-screen-receiver [output_dir] [--region x,y,w,h]\n"
              << "  output_dir : directory for decoded files (default: ./cfc_received)\n"
              << "  --region   : capture a specific screen region\n"
              << "\nKeys:\n"
              << "  ESC        : quit\n"
              << std::endl;
}

static void parseRegion(const std::string& regionStr, int& x, int& y, int& w, int& h)
{
    // Expected format: "x,y,w,h"
    if (sscanf(regionStr.c_str(), "%d,%d,%d,%d", &x, &y, &w, &h) != 4)
    {
        std::cerr << "[WARN] Invalid region format. Use: x,y,w,h\n";
        x = y = 0;
        w = GetSystemMetrics(SM_CXSCREEN);
        h = GetSystemMetrics(SM_CYSCREEN);
    }
}

int main(int argc, char** argv)
{
    std::string outputDir = "./cfc_received";
    int regionX = 0, regionY = 0;
    int regionW = GetSystemMetrics(SM_CXSCREEN);
    int regionH = GetSystemMetrics(SM_CYSCREEN);
    bool hasRegion = false;

    // Parse command line
    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h")
        {
            printUsage();
            return 0;
        }
        else if (arg == "--region" && i + 1 < argc)
        {
            parseRegion(argv[++i], regionX, regionY, regionW, regionH);
            hasRegion = true;
        }
        else
        {
            outputDir = arg;
        }
    }

    // Ensure output directory exists
    fs::create_directories(outputDir);
    std::cout << "[cfc-screen-receiver] Output directory: "
              << fs::absolute(outputDir).string() << std::endl;

    // Initialize screen capturer
    ScreenCapturer capturer;
    if (hasRegion)
        capturer.setRegion(regionX, regionY, regionW, regionH);

    std::cout << "[cfc-screen-receiver] Capture area: "
              << capturer.captureWidth() << "x" << capturer.captureHeight()
              << " at (" << regionX << "," << regionY << ")" << std::endl;

    // Initialize decoder  (mode 0 = auto-detect)
    DecoderThread decoder(outputDir, /*modeVal=*/0);

    // Create a preview window
    const std::string winName = "cfc-screen-receiver (ESC=quit)";
    cv::namedWindow(winName, cv::WINDOW_NORMAL);
    cv::resizeWindow(winName, 640, 360);

    std::set<std::string> completedFiles;
    cv::Mat frame;

    const unsigned targetFps = 30;
    const unsigned delayMs = 1000 / targetFps;

    std::cout << "[cfc-screen-receiver] Running at ~" << targetFps
              << " FPS. Press ESC to stop.\n" << std::endl;

    auto lastStatTime = std::chrono::steady_clock::now();

    while (true)
    {
        auto frameStart = std::chrono::high_resolution_clock::now();

        // Capture screen
        if (!capturer.grabFrame(frame))
        {
            std::cerr << "[ERROR] Screen capture failed.\n";
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        // Feed to decoder
        decoder.addFrame(frame);

        // Show a scaled-down preview
        cv::Mat preview;
        double scale = 640.0 / frame.cols;
        cv::resize(frame, preview, cv::Size(), scale, scale, cv::INTER_AREA);

        // Draw stats overlay
        auto progress = decoder.getProgress();
        std::string statsLine = "Scan:" + std::to_string(DecoderThread::scanCount.load())
                              + " Dec:" + std::to_string(DecoderThread::decodeCount.load())
                              + " OK:" + std::to_string(DecoderThread::perfectCount.load())
                              + " Files:" + std::to_string(decoder.filesDecoded());
        cv::putText(preview, statsLine, cv::Point(10, 25),
                    cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(0, 255, 0), 1);

        // Show progress bars if any
        if (!progress.empty())
        {
            int barX = 10;
            for (double p : progress)
            {
                int barH = static_cast<int>(100 * p);
                cv::rectangle(preview,
                              cv::Point(barX, preview.rows - barH - 10),
                              cv::Point(barX + 8, preview.rows - 10),
                              cv::Scalar(0, 255, 255), -1);
                barX += 14;
            }
        }

        cv::imshow(winName, preview);

        // Check for decoded files
        auto done = decoder.getDone();
        for (auto& f : done)
        {
            if (completedFiles.find(f) == completedFiles.end())
            {
                completedFiles.insert(f);
                std::cout << "\n>>> FILE RECEIVED: " << f << " <<<\n" << std::endl;
            }
        }

        // Print periodic stats
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::seconds>(now - lastStatTime).count() >= 5)
        {
            lastStatTime = now;
            std::cout << "[STATS] Scanned:" << DecoderThread::scanCount.load()
                      << "  Decoded:" << DecoderThread::decodeCount.load()
                      << "  Perfect:" << DecoderThread::perfectCount.load()
                      << "  Bytes:" << DecoderThread::totalBytes.load()
                      << "  Files:" << decoder.filesDecoded()
                      << "  InFlight:" << decoder.filesInFlight()
                      << std::endl;
        }

        // Frame rate limiting
        int key = cv::waitKey(1);
        if (key == 27) // ESC
            break;

        auto frameEnd = std::chrono::high_resolution_clock::now();
        unsigned elapsed = static_cast<unsigned>(
            std::chrono::duration_cast<std::chrono::milliseconds>(frameEnd - frameStart).count());
        if (elapsed < delayMs)
            std::this_thread::sleep_for(std::chrono::milliseconds(delayMs - elapsed));
    }

    std::cout << "\n[cfc-screen-receiver] Shutting down...\n";
    decoder.stop();
    cv::destroyAllWindows();

    std::cout << "[FINAL] Scanned:" << DecoderThread::scanCount.load()
              << "  Decoded:" << DecoderThread::decodeCount.load()
              << "  Perfect:" << DecoderThread::perfectCount.load()
              << "  Total bytes:" << DecoderThread::totalBytes.load()
              << "  Files received:" << decoder.filesDecoded()
              << std::endl;

    return 0;
}
