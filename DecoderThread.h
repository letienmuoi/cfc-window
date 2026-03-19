#pragma once

#include "compression/zstd_decompressor.h"
#include "encoder/Decoder.h"
#include "extractor/Anchor.h"
#include "extractor/Deskewer.h"
#include "extractor/Extractor.h"
#include "extractor/Scanner.h"
#include "fountain/concurrent_fountain_decoder_sink.h"
#include "cimb_translator/Config.h"
#include "fountain/FountainInit.h"

#include "concurrent/thread_pool.h"
#include <opencv2/opencv.hpp>

#include <atomic>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

/// Multi-threaded cimbar frame decoder.
/// Port of the Android MultiThreadedDecoder for Windows.
class DecoderThread
{
public:
    DecoderThread(const std::string& outputPath, int modeVal = 0);
    ~DecoderThread();

    /// Feed a captured frame. Returns true if the frame was enqueued.
    bool addFrame(const cv::Mat& frame);

    /// Stop all worker threads.
    void stop();

    // ─── Statistics (atomic, safe to read from any thread) ──────────
    inline static std::atomic<unsigned> scanCount{0};
    inline static std::atomic<unsigned> decodeCount{0};
    inline static std::atomic<unsigned> perfectCount{0};
    inline static std::atomic<unsigned> totalBytes{0};

    unsigned filesDecoded()    const;
    unsigned filesInFlight()   const;
    std::vector<std::string> getDone() const;
    std::vector<double>      getProgress() const;
    std::vector<cv::Rect>    getLastAnchors() const;

private:
    int doExtract(const cv::Mat& mat, cv::Mat& out);

    int _modeVal;
    Decoder _dec;
    unsigned _numThreads;
    turbo::thread_pool _pool;
    concurrent_fountain_decoder_sink _writer;
    std::string _outputPath;

    mutable std::mutex       _anchorMutex;
    std::vector<cv::Rect>    _lastAnchors;
};
