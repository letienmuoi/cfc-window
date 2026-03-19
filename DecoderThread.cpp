#include "DecoderThread.h"
#include <algorithm>
#include <iostream>

static unsigned fountain_chunk_size(int modeVal)
{
    return cimbar::Config::temp_conf(modeVal).fountain_chunk_size();
}

DecoderThread::DecoderThread(const std::string& outputPath, int modeVal)
    : _modeVal(modeVal)
    , _dec(cimbar::Config::ecc_bytes(), cimbar::Config::color_bits())
    , _numThreads(std::max<int>(static_cast<int>(std::thread::hardware_concurrency()) / 2, 1))
    , _pool(_numThreads, 1)
    , _writer(fountain_chunk_size(modeVal),
              decompress_on_store<std::ofstream>(outputPath, true))
    , _outputPath(outputPath)
{
    FountainInit::init();
    _pool.start();
    std::cout << "[DecoderThread] Started with " << _numThreads
              << " worker thread(s), mode=" << _modeVal << std::endl;
}

DecoderThread::~DecoderThread()
{
    stop();
}

void DecoderThread::stop()
{
    _pool.stop();
}

int DecoderThread::doExtract(const cv::Mat& mat, cv::Mat& out)
{
    Scanner scanner(mat);
    std::vector<Anchor> anchors = scanner.scan();
    ++scanCount;

    {
        std::lock_guard<std::mutex> lock(_anchorMutex);
        _lastAnchors.clear();
        for (const auto& a : anchors)
            _lastAnchors.emplace_back(a.x(), a.y(), a.xmax() - a.x(), a.ymax() - a.y());
    }

    if (anchors.size() < 4)
        return Extractor::FAILURE;

    Corners corners(anchors);
    Deskewer de;
    out = de.deskew(mat, corners);

    return Extractor::SUCCESS;
}

bool DecoderThread::addFrame(const cv::Mat& frame)
{
    // Clone the frame so the worker thread owns its own copy
    cv::Mat mat = frame.clone();
    unsigned modeVal = _modeVal;

    // Auto-detect mode: cycle through modes
    static std::atomic<unsigned> frameCount{0};
    unsigned count = ++frameCount;
    if (modeVal == 0)
    {
        switch (count % 4)
        {
            case 1:  modeVal = 4;  break;
            case 2:  modeVal = 66; break;
            case 3:  modeVal = 67; break;
            default: modeVal = 68; break;
        }
    }

    return _pool.try_execute([this, mat, modeVal]()
    {
        cimbar::Config::update(modeVal);

        cv::Mat img;
        int res = doExtract(mat, img);
        if (res == Extractor::FAILURE)
            return;

        bool shouldPreprocess = (res == Extractor::NEEDS_SHARPEN);
        int colorCorrection = (modeVal == 4) ? 1 : 2;
        unsigned decodeRes = _dec.decode_fountain(img, _writer, shouldPreprocess, colorCorrection);

        totalBytes += decodeRes;
        ++decodeCount;

        if (decodeRes >= cimbar::Config::capacity(cimbar::Config::color_bits()) * 0.7)
            ++perfectCount;
    });
}

unsigned DecoderThread::filesDecoded() const
{
    return _writer.num_done();
}

unsigned DecoderThread::filesInFlight() const
{
    return _writer.num_streams();
}

std::vector<std::string> DecoderThread::getDone() const
{
    return _writer.get_done();
}

std::vector<double> DecoderThread::getProgress() const
{
    return _writer.get_progress();
}

std::vector<cv::Rect> DecoderThread::getLastAnchors() const
{
    std::lock_guard<std::mutex> lock(_anchorMutex);
    return _lastAnchors;
}
