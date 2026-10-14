#include "JxlDecoder.hpp"

#include "DynLib.hpp"

#include <QDebug>
#include <QFile>
#include <QLibrary>
#include <algorithm>
#include <cstring>

// Only the few ABI details needed are declared here. Struct layouts are those
// of libjxl 0.7 to 0.12 (the C API keeps them across these).
namespace
{
enum : int
{
    DEC_SUCCESS         = 0,
    DEC_NEED_IMAGE_OUT  = 5,
    DEC_BASIC_INFO      = 0x40,
    DEC_COLOR_ENCODING  = 0x100,
    DEC_FRAME           = 0x400,
    DEC_FULL_IMAGE      = 0x1000,
};

struct PixelFormat
{
    uint32_t num_channels;
    int data_type; // JXL_TYPE_UINT8 = 2
    int endianness;
    size_t align;
};

// Raw JxlBasicInfo (204 bytes); fields are read at their offsets.
struct BasicInfo
{
    alignas(8) unsigned char raw[512];

    uint32_t u32(size_t off) const
    {
        uint32_t v;
        std::memcpy(&v, raw + off, sizeof v);
        return v;
    }
    uint32_t xsize() const { return u32(4); }
    uint32_t ysize() const { return u32(8); }
    bool haveAnimation() const { return u32(44) != 0; }
    uint32_t tpsNum() const { return u32(80); }
    uint32_t tpsDen() const { return u32(84); }
    uint32_t numLoops() const { return u32(88); }
};

// Raw JxlFrameHeader (56 bytes); the duration, in ticks, comes first.
struct FrameHeader
{
    alignas(8) unsigned char raw[256];
    uint32_t duration() const
    {
        uint32_t v;
        std::memcpy(&v, raw, sizeof v);
        return v;
    }
};

using PFN_create    = void *(*)(const void *);
using PFN_destroy   = void (*)(void *);
using PFN_subscribe = int (*)(void *, int);
using PFN_set_input = int (*)(void *, const uint8_t *, size_t);
using PFN_process   = int (*)(void *);
using PFN_info      = int (*)(const void *, BasicInfo *);
using PFN_frame     = int (*)(const void *, FrameHeader *);
using PFN_bufsize   = int (*)(const void *, const PixelFormat *, size_t *);
using PFN_setbuf    = int (*)(void *, const PixelFormat *, void *, size_t);
using PFN_srgb      = void (*)(void *, int);
using PFN_outprof   = int (*)(void *, const void *, const uint8_t *, size_t);
using PFN_runner_create  = void *(*)(const void *);
using PFN_runner_destroy = void (*)(void *);
using PFN_runner_suggest = uint32_t (*)(uint64_t, uint64_t);
using PFN_runner_set     = void (*)(void *, size_t);
using PFN_set_runner     = int (*)(void *, void *, void *);

struct Lib
{
    PFN_create create           = nullptr;
    PFN_destroy destroy         = nullptr;
    PFN_subscribe subscribe     = nullptr;
    PFN_set_input set_input     = nullptr;
    PFN_process process         = nullptr;
    PFN_info get_info           = nullptr;
    PFN_frame get_frame_header  = nullptr;
    PFN_bufsize buf_size        = nullptr;
    PFN_setbuf set_buf          = nullptr;
    // Optional: converting to sRGB needs libjxl >= 0.8
    PFN_srgb enc_srgb           = nullptr;
    PFN_outprof set_out_profile = nullptr;
    // Optional: multi-threaded decoding (libjxl_threads)
    PFN_set_runner set_runner          = nullptr;
    PFN_runner_create runner_create    = nullptr;
    PFN_runner_destroy runner_destroy  = nullptr;
    PFN_runner_suggest runner_suggest  = nullptr;
    PFN_runner_set runner_set          = nullptr;
    void *runner_fn                    = nullptr;
    bool ok                            = false;

    static Lib &get() noexcept
    {
        static Lib s;
        return s;
    }

private:
    QLibrary lib;
    QLibrary threads_lib;

    Lib() noexcept
    {
#if defined(Q_OS_WIN)
        lib.setFileName("libjxl");
        const bool loaded = lib.load();
#else
        // libjxl's soname follows its minor version, so try each known one.
        const bool loaded
            = DynLib::load(lib, "LEKTRA_LIB_JXL", "jxl",
                           {"0.12", "0.11", "0.10", "0.9", "0.8", "0.7"});
#endif
        if (!loaded)
        {
            qWarning()
                << "Unable to load libjxl; JPEG XL files can't be opened";
            return;
        }

#define LOADSYM(field, sym)                                                    \
    field = reinterpret_cast<decltype(field)>(lib.resolve(sym));               \
    if (!field)                                                                \
    {                                                                          \
        qWarning() << "Missing symbol" << sym;                                 \
        return;                                                                \
    }
        LOADSYM(create, "JxlDecoderCreate")
        LOADSYM(destroy, "JxlDecoderDestroy")
        LOADSYM(subscribe, "JxlDecoderSubscribeEvents")
        LOADSYM(set_input, "JxlDecoderSetInput")
        LOADSYM(process, "JxlDecoderProcessInput")
        LOADSYM(get_info, "JxlDecoderGetBasicInfo")
        LOADSYM(get_frame_header, "JxlDecoderGetFrameHeader")
        LOADSYM(buf_size, "JxlDecoderImageOutBufferSize")
        LOADSYM(set_buf, "JxlDecoderSetImageOutBuffer")
#undef LOADSYM

        enc_srgb = reinterpret_cast<PFN_srgb>(
            lib.resolve("JxlColorEncodingSetToSRGB"));
        set_out_profile = reinterpret_cast<PFN_outprof>(
            lib.resolve("JxlDecoderSetOutputColorProfile"));
        set_runner = reinterpret_cast<PFN_set_runner>(
            lib.resolve("JxlDecoderSetParallelRunner"));

#if defined(Q_OS_WIN)
        threads_lib.setFileName("libjxl_threads");
        const bool threads = threads_lib.load();
#else
        const bool threads
            = DynLib::load(threads_lib, "LEKTRA_LIB_JXL_THREADS", "jxl_threads",
                           {"0.12", "0.11", "0.10", "0.9", "0.8", "0.7"});
#endif
        if (threads)
        {
            runner_create = reinterpret_cast<PFN_runner_create>(
                threads_lib.resolve("JxlResizableParallelRunnerCreate"));
            runner_destroy = reinterpret_cast<PFN_runner_destroy>(
                threads_lib.resolve("JxlResizableParallelRunnerDestroy"));
            runner_suggest = reinterpret_cast<PFN_runner_suggest>(
                threads_lib.resolve(
                    "JxlResizableParallelRunnerSuggestThreads"));
            runner_set = reinterpret_cast<PFN_runner_set>(
                threads_lib.resolve("JxlResizableParallelRunnerSetThreads"));
            runner_fn = reinterpret_cast<void *>(
                threads_lib.resolve("JxlResizableParallelRunner"));
        }
        ok = true;
    }
};

constexpr PixelFormat kRgba8{4, 2 /*UINT8*/, 0 /*native*/, 0};
} // namespace

JxlAnimation::~JxlAnimation()
{
    stop();
}

void
JxlAnimation::stop() noexcept
{
    auto &jxl = Lib::get();
    if (m_dec)
        jxl.destroy(m_dec);
    if (m_runner)
        jxl.runner_destroy(m_runner);
    m_dec    = nullptr;
    m_runner = nullptr;
}

// (Re)starts decoding from the first frame.
bool
JxlAnimation::start() noexcept
{
    auto &jxl = Lib::get();
    stop();

    m_dec = jxl.create(nullptr);
    if (!m_dec)
        return false;

    if (jxl.set_runner && jxl.runner_create && jxl.runner_fn
        && jxl.runner_destroy)
    {
        m_runner = jxl.runner_create(nullptr);
        if (m_runner)
            jxl.set_runner(m_dec, jxl.runner_fn, m_runner);
    }

    if (jxl.subscribe(m_dec, DEC_BASIC_INFO | DEC_COLOR_ENCODING | DEC_FRAME
                                 | DEC_FULL_IMAGE)
            != DEC_SUCCESS
        || jxl.set_input(m_dec,
                         reinterpret_cast<const uint8_t *>(m_data.constData()),
                         static_cast<size_t>(m_data.size()))
               != DEC_SUCCESS)
    {
        stop();
        return false;
    }
    return true;
}

std::unique_ptr<JxlAnimation>
JxlAnimation::load(const QString &path)
{
    if (!Lib::get().ok)
        return nullptr;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return nullptr;

    std::unique_ptr<JxlAnimation> a(new JxlAnimation);
    a->m_data = file.readAll();
    if (a->m_data.isEmpty() || !a->start())
        return nullptr;
    return a;
}

bool
JxlAnimation::isAnimated(const QString &path)
{
    auto &jxl = Lib::get();
    if (!jxl.ok)
        return false;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    // The basic info is at the very start of the file.
    const QByteArray head = file.read(64 * 1024);
    if (head.isEmpty())
        return false;

    void *dec = jxl.create(nullptr);
    if (!dec)
        return false;

    bool animated = false;
    if (jxl.subscribe(dec, DEC_BASIC_INFO) == DEC_SUCCESS
        && jxl.set_input(dec, reinterpret_cast<const uint8_t *>(head.constData()),
                         static_cast<size_t>(head.size()))
               == DEC_SUCCESS
        && jxl.process(dec) == DEC_BASIC_INFO)
    {
        BasicInfo info{};
        if (jxl.get_info(dec, &info) == DEC_SUCCESS)
            animated = info.haveAnimation();
    }
    jxl.destroy(dec);
    return animated;
}

QImage
JxlAnimation::decodeFirstFrame(const QString &path)
{
    auto a = load(path);
    return a ? a->nextFrame() : QImage();
}

QImage
JxlAnimation::nextFrame(int *delayMs, bool *wrapped)
{
    auto &jxl = Lib::get();
    if (wrapped)
        *wrapped = false;
    if (!m_dec)
        return {};

    QImage img;
    uint32_t duration = 0;
    bool restarted    = false;

    for (;;)
    {
        const int status = jxl.process(m_dec);
        switch (status)
        {
            case DEC_BASIC_INFO:
            {
                BasicInfo info{};
                if (jxl.get_info(m_dec, &info) != DEC_SUCCESS || !info.xsize()
                    || !info.ysize())
                    return {};
                m_w = info.xsize();
                m_h = info.ysize();
                if (info.haveAnimation())
                {
                    m_tps_num = std::max<uint32_t>(1, info.tpsNum());
                    m_tps_den = std::max<uint32_t>(1, info.tpsDen());
                    m_loops   = static_cast<int>(info.numLoops());
                }
                if (m_runner && jxl.runner_suggest && jxl.runner_set)
                    jxl.runner_set(m_runner, jxl.runner_suggest(m_w, m_h));
                break;
            }
            case DEC_COLOR_ENCODING:
                // Ask for sRGB output so wide-gamut/linear files look right.
                if (jxl.enc_srgb && jxl.set_out_profile)
                {
                    alignas(8) unsigned char enc[512] = {};
                    jxl.enc_srgb(enc, 0);
                    jxl.set_out_profile(m_dec, enc, nullptr, 0);
                }
                break;
            case DEC_FRAME:
            {
                FrameHeader fh{};
                if (jxl.get_frame_header(m_dec, &fh) == DEC_SUCCESS)
                    duration = fh.duration();
                break;
            }
            case DEC_NEED_IMAGE_OUT:
            {
                size_t size = 0;
                if (!m_w || !m_h
                    || jxl.buf_size(m_dec, &kRgba8, &size) != DEC_SUCCESS)
                    return {};
                img = QImage(static_cast<int>(m_w), static_cast<int>(m_h),
                             QImage::Format_RGBA8888);
                if (img.isNull() || static_cast<size_t>(img.sizeInBytes()) < size
                    || jxl.set_buf(m_dec, &kRgba8, img.bits(), size)
                           != DEC_SUCCESS)
                    return {};
                break;
            }
            case DEC_FULL_IMAGE:
                if (delayMs)
                {
                    // ticks -> ms; browsers treat a tiny delay as ~10 ms
                    const double ms = 1000.0 * duration * m_tps_den / m_tps_num;
                    *delayMs        = std::max(10, static_cast<int>(ms + 0.5));
                }
                if (wrapped)
                    *wrapped = restarted;
                return img;
            case DEC_SUCCESS:
                // Past the last frame: play again from the first, once. A
                // file that yields nothing even then is broken.
                if (restarted || !start())
                    return {};
                restarted = true;
                break;
            default:
                return {}; // error, or truncated input
        }
    }
}
