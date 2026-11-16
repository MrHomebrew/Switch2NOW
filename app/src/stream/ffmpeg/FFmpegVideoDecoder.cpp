#include "FFmpegVideoDecoder.hpp"
#include "AVFrameHolder.hpp"
#include "TransferFrameReuse.hpp"
#include "DecodedFrameGate.hpp"
#include "borealis.hpp"
#include "../DecodeQueuePolicy.hpp"
#include "../../stream_settings.hpp"
#include "../../video_quality_policy.hpp"
#include <cerrno>
#include <chrono>
#include <cstring>
#include <sstream>
extern "C" {
#include <libavutil/pixdesc.h>
#include <libavutil/hwcontext.h>
}
#ifdef __SWITCH2__
#include <switch/kernel/svc.h>
#include <switch/result.h>
#endif
#ifdef PLATFORM_APPLE
extern "C" {
#include <libavcodec/videotoolbox.h>
}
#endif

// Disables the deblocking filter at the cost of image quality
#define DISABLE_LOOP_FILTER 0x1
// Uses the low latency decode flag (disables multithreading)
#define LOW_LATENCY_DECODE 0x2

//#if defined(PLATFORM_TVOS)
//#define DECODER_BUFFER_SIZE 92 * 1024 * 4
//#else
//#define DECODER_BUFFER_SIZE 92 * 1024 * 2
//#endif
#if defined(PLATFORM_ANDROID)
#include <jni.h>
#include <libavcodec/jni.h>
#include <libavutil/hwcontext_mediacodec.h>

//static JavaVM *mJavaVM = NULL;
//JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *vm, void *reserved)
//{
////    av_jni_set_java_vm(vm, NULL);
//    return JNI_VERSION_1_4;
//}
#endif

#ifdef __SWITCH2__
namespace {

constexpr uint8_t kAnnexBStartCode[4] = {0x00, 0x00, 0x00, 0x01};

std::vector<uint8_t> nal_to_rbsp(const uint8_t* nal, size_t len) {
    std::vector<uint8_t> rbsp;
    rbsp.reserve(len);
    int zeros = 0;
    for (size_t i = 0; i < len; ++i) {
        const uint8_t b = nal[i];
        if (zeros >= 2 && b == 0x03) {
            zeros = 0;
            continue;
        }
        rbsp.push_back(b);
        zeros = (b == 0) ? (zeros + 1) : 0;
    }
    return rbsp;
}

class BitReader {
  public:
    BitReader(const uint8_t* data, size_t size) : data_(data), size_(size) {}

    bool read_bit(uint32_t& out) {
        if (byte_ >= size_) return false;
        out = (data_[byte_] >> (7 - bit_)) & 1u;
        if (++bit_ == 8) {
            bit_ = 0;
            ++byte_;
        }
        return true;
    }

    bool read_bits(int n, uint32_t& out) {
        out = 0;
        for (int i = 0; i < n; ++i) {
            uint32_t b = 0;
            if (!read_bit(b)) return false;
            out = (out << 1) | b;
        }
        return true;
    }

    bool skip_bits(int n) {
        uint32_t tmp = 0;
        for (int i = 0; i < n; ++i)
            if (!read_bit(tmp)) return false;
        return true;
    }

    bool read_ue(uint32_t& out) {
        int zeros = 0;
        uint32_t b = 0;
        while (read_bit(b) && b == 0) {
            if (++zeros > 30) return false;
        }
        if (b == 0) return false;
        uint32_t suffix = 0;
        if (zeros > 0 && !read_bits(zeros, suffix)) return false;
        out = ((1u << zeros) - 1u) + suffix;
        return true;
    }

    bool read_se() {
        uint32_t dummy = 0;
        return read_ue(dummy);
    }

  private:
    const uint8_t* data_;
    size_t size_;
    size_t byte_ = 0;
    int bit_ = 0;
};

void skip_scaling_list(BitReader& br, int size) {
    int last = 8, next = 8;
    for (int j = 0; j < size; ++j) {
        if (next != 0) {
            uint32_t v = 0;
            if (!br.read_ue(v)) return;
            const int32_t delta = (v & 1u) ? static_cast<int32_t>((v + 1u) >> 1)
                                           : -static_cast<int32_t>(v >> 1);
            next = (last + delta + 256) % 256;
        }
        last = (next == 0) ? last : next;
    }
}

void parse_sps_vui(const uint8_t* nal, size_t len, int& colorspace, bool& full_range) {
    if (len < 4) return;
    std::vector<uint8_t> rbsp = nal_to_rbsp(nal + 1, len - 1);
    BitReader br(rbsp.data(), rbsp.size());

    uint32_t profile_idc = 0, dummy = 0;
    if (!br.read_bits(8, profile_idc)) return;
    if (!br.skip_bits(16)) return;
    if (!br.read_ue(dummy)) return;

    if (profile_idc == 100 || profile_idc == 110 || profile_idc == 122 ||
        profile_idc == 244 || profile_idc == 44 || profile_idc == 83 ||
        profile_idc == 86 || profile_idc == 118 || profile_idc == 128 ||
        profile_idc == 138 || profile_idc == 139 || profile_idc == 134) {
        uint32_t chroma_fmt = 0;
        if (!br.read_ue(chroma_fmt)) return;
        if (chroma_fmt == 3 && !br.skip_bits(1)) return;
        if (!br.read_ue(dummy) || !br.read_ue(dummy) || !br.skip_bits(1)) return;
        uint32_t seq_scaling = 0;
        if (!br.read_bit(seq_scaling)) return;
        if (seq_scaling) {
            const int n = (chroma_fmt != 3) ? 8 : 12;
            for (int i = 0; i < n; ++i) {
                uint32_t present = 0;
                if (!br.read_bit(present)) return;
                if (present) skip_scaling_list(br, i < 6 ? 16 : 64);
            }
        }
    }

    if (!br.read_ue(dummy)) return;
    uint32_t poc_type = 0;
    if (!br.read_ue(poc_type)) return;
    if (poc_type == 0) {
        if (!br.read_ue(dummy)) return;
    } else if (poc_type == 1) {
        if (!br.skip_bits(1) || !br.read_se() || !br.read_se()) return;
        uint32_t num_ref = 0;
        if (!br.read_ue(num_ref)) return;
        for (uint32_t i = 0; i < num_ref; ++i)
            if (!br.read_se()) return;
    }

    if (!br.read_ue(dummy) || !br.skip_bits(1)) return;
    if (!br.read_ue(dummy) || !br.read_ue(dummy)) return;
    uint32_t frame_mbs_only = 0;
    if (!br.read_bit(frame_mbs_only)) return;
    if (!frame_mbs_only && !br.skip_bits(1)) return;
    if (!br.skip_bits(1)) return;
    uint32_t cropping = 0;
    if (!br.read_bit(cropping)) return;
    if (cropping) {
        for (int i = 0; i < 4; ++i)
            if (!br.read_ue(dummy)) return;
    }

    uint32_t vui_present = 0;
    if (!br.read_bit(vui_present) || !vui_present) return;

    uint32_t ar_present = 0;
    if (!br.read_bit(ar_present)) return;
    if (ar_present) {
        uint32_t ar_idc = 0;
        if (!br.read_bits(8, ar_idc)) return;
        if (ar_idc == 255 && !br.skip_bits(32)) return;
    }
    uint32_t overscan = 0;
    if (!br.read_bit(overscan)) return;
    if (overscan && !br.skip_bits(1)) return;

    uint32_t video_signal = 0;
    if (!br.read_bit(video_signal) || !video_signal) return;
    if (!br.skip_bits(3)) return;
    uint32_t full = 0;
    if (!br.read_bit(full)) return;
    full_range = (full != 0);

    uint32_t color_desc = 0;
    if (!br.read_bit(color_desc) || !color_desc) return;
    uint32_t primaries = 0, transfer = 0, matrix = 0;
    if (!br.read_bits(8, primaries) || !br.read_bits(8, transfer) ||
        !br.read_bits(8, matrix)) {
        return;
    }
    colorspace = static_cast<int>(matrix);
}

bool extract_sps_pps(const uint8_t* data, size_t size,
                     std::vector<uint8_t>& out_config,
                     int& colorspace, bool& full_range) {
    std::vector<std::pair<const uint8_t*, size_t>> sps;
    std::vector<std::pair<const uint8_t*, size_t>> pps;

    size_t i = 0;
    while (i + 3 <= size) {
        size_t sc_len = 0;
        if (i + 4 <= size && data[i] == 0 && data[i + 1] == 0 &&
            data[i + 2] == 0 && data[i + 3] == 1) {
            sc_len = 4;
        } else if (data[i] == 0 && data[i + 1] == 0 && data[i + 2] == 1) {
            sc_len = 3;
        } else {
            ++i;
            continue;
        }

        const size_t nal_start = i + sc_len;
        size_t nal_end = size;
        for (size_t j = nal_start; j + 3 <= size; ++j) {
            if (data[j] == 0 && data[j + 1] == 0 &&
                (data[j + 2] == 1 ||
                 (j + 4 <= size && data[j + 2] == 0 && data[j + 3] == 1))) {
                nal_end = j;
                break;
            }
        }
        if (nal_end > nal_start) {
            const uint8_t* nal = data + nal_start;
            const size_t len = nal_end - nal_start;
            const uint8_t nal_type = nal[0] & 0x1Fu;
            if (nal_type == 7) {
                sps.emplace_back(nal, len);
                parse_sps_vui(nal, len, colorspace, full_range);
            } else if (nal_type == 8) {
                pps.emplace_back(nal, len);
            }
        }
        i = nal_end;
    }

    if (sps.empty() || pps.empty()) return false;
    out_config.clear();
    for (const auto& n : sps) {
        out_config.insert(out_config.end(), kAnnexBStartCode, kAnnexBStartCode + 4);
        out_config.insert(out_config.end(), n.first, n.first + n.second);
    }
    for (const auto& n : pps) {
        out_config.insert(out_config.end(), kAnnexBStartCode, kAnnexBStartCode + 4);
        out_config.insert(out_config.end(), n.first, n.first + n.second);
    }
    return true;
}

} // namespace
#endif // __SWITCH2__

FFmpegVideoDecoder::FFmpegVideoDecoder() {
//    AVBufferRef* deviceRef = av_hwdevice_ctx_alloc(AV_HWDEVICE_TYPE_MEDIACODEC);
//    AVHWDeviceContext* ctx = (AVHWDeviceContext*)deviceRef->data;
//    AVMediaCodecDeviceContext* hwctx = (AVMediaCodecDeviceContext*)ctx->hwctx;
////    hwctx->surface = ;
//    av_hwdevice_ctx_init(deviceRef);
}

FFmpegVideoDecoder::~FFmpegVideoDecoder() = default;

void ffmpegLog(void* ptr, int level, const char* fmt, va_list vargs) {
    std::string message;
    va_list ap_copy;
    va_copy(ap_copy, vargs);
    size_t len = vsnprintf(nullptr, 0, fmt, ap_copy);
    message.resize(len + 1);  // need space for NUL
    vsnprintf(&message[0], len + 1,fmt, vargs);
    message.resize(len);  // remove the NUL
    brls::Logger::debug("FFmpeg [LOG]: {}", message.c_str());
}

void FFmpegVideoDecoder::append_setup_diagnostic(const std::string& value) {
    if (!m_setup_diagnostics.empty())
        m_setup_diagnostics += " | ";
    m_setup_diagnostics += value;
}

int FFmpegVideoDecoder::fail_setup(const char* stage, int error_code,
                                   const std::string& detail) {
    char error[AV_ERROR_MAX_STRING_SIZE] = {};
    const int normalized_error = error_code < 0 ? error_code : AVERROR_UNKNOWN;
    av_strerror(normalized_error, error, sizeof(error));

    std::string message = std::string("stage=") + (stage ? stage : "unknown") +
                          " code=" + std::to_string(error_code) +
                          " error=" + error;
    if (!detail.empty())
        message += " detail=" + detail;
    append_setup_diagnostic(message);
    brls::Logger::error("FFmpeg setup: {}", message);
    return error_code < 0 ? error_code : -1;
}

#ifdef OPENNOW_ENABLE_NVDEC
AVPixelFormat FFmpegVideoDecoder::select_hardware_format(
    AVCodecContext* context, const AVPixelFormat* formats) {
    auto* self = context
        ? static_cast<FFmpegVideoDecoder*>(context->opaque)
        : nullptr;
    if (!formats) {
        if (self)
            self->append_setup_diagnostic("getFormat=no-formats");
        return AV_PIX_FMT_NONE;
    }

    std::ostringstream offered;
    for (const AVPixelFormat* format = formats; *format != AV_PIX_FMT_NONE; ++format) {
        if (offered.tellp() > 0)
            offered << ",";
        const char* name = av_get_pix_fmt_name(*format);
        offered << (name ? name : "unknown") << "(" << static_cast<int>(*format) << ")";
        if (*format == AV_PIX_FMT_NVTEGRA) {
            if (self)
                self->append_setup_diagnostic("getFormat=nvtegra offered=" + offered.str());
            return *format;
        }
    }

    if (self)
        self->append_setup_diagnostic("getFormat=missing-nvtegra offered=" + offered.str());
    return AV_PIX_FMT_NONE;
}
#else
AVPixelFormat FFmpegVideoDecoder::select_hardware_format(
    AVCodecContext* context, const AVPixelFormat* formats) {
    (void)context;
    return formats ? formats[0] : AV_PIX_FMT_NONE;
}
#endif

int FFmpegVideoDecoder::setup(int video_format, int width, int height,
                              int redraw_rate, void* context, int dr_flags) {
    m_setup_diagnostics.clear();
    m_first_output_format_logged = false;
    m_transfer_layouts.fill({0,0});
    m_transfer_storage_reused = m_transfer_storage_fresh = 0;
    m_stream_fps = redraw_rate;
    m_requires_hardware_frames =
        (dr_flags & VIDEO_DECODER_REQUIRE_HARDWARE) != 0;
    m_uses_hardware_frames =
        ((dr_flags & VIDEO_DECODER_PREFER_HARDWARE) != 0 ||
         m_requires_hardware_frames) &&
        (dr_flags & VIDEO_DECODER_FORCE_SOFTWARE) == 0;
    m_transfer_hardware_frames = m_uses_hardware_frames &&
        (dr_flags & VIDEO_DECODER_COPY_HARDWARE_FRAMES) != 0;
    append_setup_diagnostic(
        "request=" + std::string(m_requires_hardware_frames ? "required" :
            m_uses_hardware_frames ? "preferred" : "software") +
        " resolution=" + std::to_string(width) + "x" + std::to_string(height) +
        " fps=" + std::to_string(redraw_rate) +
        " output=" + std::string(m_transfer_hardware_frames
            ? "software-copy" : m_uses_hardware_frames ? "native" : "software"));

    std::string format;
    switch (video_format) {
        case VIDEO_FORMAT_H264:
            format = "H264";
            break;
        case VIDEO_FORMAT_H265:
            format = "HEVC";
            break;
        case VIDEO_FORMAT_H265_MAIN10:
            format = "HEVC HDR";
            break;
        case VIDEO_FORMAT_AV1_MAIN8:
            format = "AV1";
            break;
        case VIDEO_FORMAT_AV1_MAIN10:
            format = "AV1 HDR";
            break;
        default:
            format = "UNKNOWN";
            break;
    }
    brls::Logger::debug("FFMpeg's AVCodec version: {}.{}.{}", AV_VERSION_MAJOR(avcodec_version()), AV_VERSION_MINOR(avcodec_version()), AV_VERSION_MICRO(avcodec_version()));
    brls::Logger::info(
        "FFmpeg: Setup with format: {}, width: {}, height: {}, fps: {}", format, width, height, redraw_rate);

#ifdef __SWITCH2__
    if ((video_format & VIDEO_FORMAT_MASK_H264) && m_uses_hardware_frames) {
        m_switch2_hw_enabled = true;
        m_switch2_hw_ready = false;
        m_switch2_codec_config.clear();
        m_switch2_frame_index = 0;
        m_switch2_width = std::max(width, 1920);
        m_switch2_height = std::max(height, 1080);
        m_switch2_colorspace = AVCOL_SPC_BT709;
        m_switch2_color_full = false;
        m_transfer_hardware_frames = true;

        AVFrameHolder::instance().prepare(
            opennow::video::ReadyFrameQueueCapacity(),
            opennow::video::StartupReadyFrameDepth(),
            std::chrono::microseconds(
                opennow::video::FrameWaitBudgetMicroseconds(redraw_rate)));

        m_frames_size = 5 + 1;
        m_frames = new AVFrame*[m_frames_size];
        for (int i = 0; i < m_frames_size; i++) {
            m_frames[i] = av_frame_alloc();
            if (!m_frames[i]) {
                return fail_setup("alloc_output_frame", AVERROR(ENOMEM),
                                  "index=" + std::to_string(i));
            }
        }
        append_setup_diagnostic("decoder=switch2-viddec setup=ok");
        brls::Logger::info("Switch2: VideoDecoder setup ok ({})", m_setup_diagnostics);
        return 0;
    }
    m_switch2_hw_enabled = false;
    m_uses_hardware_frames = false;
#endif

    av_log_set_level(AV_LOG_WARNING);
    // av_log_set_callback(&ffmpegLog); // Uncomment to see FFMpeg logs
#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(58, 10, 100)
    avcodec_register_all();
#endif

    m_packet = av_packet_alloc();

    int perf_lvl = LOW_LATENCY_DECODE;
#if defined(PLATFORM_SWITCH)
    // H.264 deblocking is one of the largest software decode costs on Tegra X1.
    // The cloud stream is already filtered by the encoder, so trading a small
    // amount of edge quality for stable frame time is preferable to frame loss.
    if (!m_uses_hardware_frames)
        perf_lvl |= DISABLE_LOOP_FILTER;
#endif

#ifdef PLATFORM_ANDROID
    if (video_format & VIDEO_FORMAT_MASK_H264) {
        m_decoder = avcodec_find_decoder_by_name("h264_mediacodec");
    } else if (video_format & VIDEO_FORMAT_MASK_H265) {
        m_decoder = avcodec_find_decoder_by_name("hevc_mediacodec");
    } else {
        // Unsupported decoder type
    }
#else
    if (video_format & VIDEO_FORMAT_MASK_H264) {
#if defined(OPENNOW_ENABLE_NVDEC)
        // NVTEGRA is an FFmpeg hardware acceleration path attached to the
        // regular H.264 decoder, not a separately registered h264_nvtegra
        // decoder. Looking up that name always failed and triggered fallback.
        m_decoder = avcodec_find_decoder(AV_CODEC_ID_H264);
#else
        if (m_requires_hardware_frames)
            return fail_setup("compile_support", AVERROR(ENOSYS),
                              "OPENNOW_ENABLE_NVDEC is not enabled");
        m_uses_hardware_frames = false;
        m_decoder = avcodec_find_decoder(AV_CODEC_ID_H264);
#endif
    } else if (video_format & VIDEO_FORMAT_MASK_H265) {
        m_decoder = avcodec_find_decoder(AV_CODEC_ID_HEVC);
    } else {
        // Unsupported decoder type
    }
#endif

    if (m_decoder == nullptr) {
        return fail_setup("find_decoder", AVERROR_DECODER_NOT_FOUND,
                          "codec=" + format);
    }
    append_setup_diagnostic("decoder=" + std::string(m_decoder->name));

    m_decoder_context = avcodec_alloc_context3(m_decoder);
    if (m_decoder_context == nullptr) {
        return fail_setup("alloc_context", AVERROR(ENOMEM));
    }

    if (perf_lvl & DISABLE_LOOP_FILTER) {
        const auto settings = opennow::LoadStreamSettings();
        const auto tuning = opennow::video::ResolveQualityTuning(settings.image_quality_mode);
        // Keep deblocking on reference pictures in enhanced modes. This removes
        // persistent block edges while still skipping work on disposable frames.
        m_decoder_context->skip_loop_filter = tuning.preserve_reference_deblocking
            ? AVDISCARD_NONREF : AVDISCARD_ALL;
    }

    if (perf_lvl & LOW_LATENCY_DECODE)
        // Use low delay single threaded encoding
        m_decoder_context->flags |= AV_CODEC_FLAG_LOW_DELAY;

    // Reject malformed access units instead of letting error concealment turn
    // missing reference data into persistent green/repeated macroblocks. The
    // session recovery path will hold the last good frame and request an IDR.
    m_decoder_context->err_recognition =
        AV_EF_BITSTREAM | AV_EF_BUFFER | AV_EF_EXPLODE;

    // NVDEC submits asynchronously and does not benefit from CPU threads.
    // Slice threading keeps software decoding parallel without holding
    // dependent pictures in FFmpeg's frame-thread pipeline.
    const int decoder_threads = m_uses_hardware_frames ? 1 : 3;
    m_decoder_context->thread_type =
        m_uses_hardware_frames ? 0 : FF_THREAD_SLICE;
    m_decoder_context->thread_count = decoder_threads;
    append_setup_diagnostic(
        std::string("threading=") +
        (m_uses_hardware_frames ? "hardware-async-1" : "slice-3"));

    m_decoder_context->width = width;
    m_decoder_context->height = height;

#if defined(OPENNOW_ENABLE_NVDEC) && defined(PLATFORM_SWITCH) && defined(BOREALIS_USE_DEKO3D)
        AVHWDeviceType hwType = m_uses_hardware_frames
            ? AV_HWDEVICE_TYPE_NVTEGRA : AV_HWDEVICE_TYPE_NONE;
#elif defined(PLATFORM_ANDROID)
        AVHWDeviceType hwType = AV_HWDEVICE_TYPE_MEDIACODEC;
#elif defined(PLATFORM_APPLE)
        AVHWDeviceType hwType = AV_HWDEVICE_TYPE_VIDEOTOOLBOX;
#else
        AVHWDeviceType hwType = AV_HWDEVICE_TYPE_NONE;
#endif

    int err = 0;
    if (hwType != AV_HWDEVICE_TYPE_NONE) {
        std::ostringstream configurations;
        for (int index = 0;; ++index) {
            const AVCodecHWConfig* config = avcodec_get_hw_config(m_decoder, index);
            if (!config)
                break;
            if (configurations.tellp() > 0)
                configurations << ",";
            const char* pixel_name = av_get_pix_fmt_name(config->pix_fmt);
            configurations << (pixel_name ? pixel_name : "unknown")
                           << "/device=" << static_cast<int>(config->device_type)
                           << "/methods=" << config->methods;
        }
        append_setup_diagnostic(
            "hwConfigs=" + (configurations.str().empty()
                ? std::string("not-advertised") : configurations.str()));

        if ((err = av_hwdevice_ctx_create(&hw_device_ctx, hwType, nullptr, nullptr, 0)) < 0) {
            return fail_setup("create_hwdevice", err,
                              "type=nvtegra(" + std::to_string(hwType) + ")");
        }
        m_decoder_context->hw_device_ctx = av_buffer_ref(hw_device_ctx);
        if (!m_decoder_context->hw_device_ctx)
            return fail_setup("reference_hwdevice", AVERROR(ENOMEM));
        m_decoder_context->opaque = this;
        m_decoder_context->get_format = &FFmpegVideoDecoder::select_hardware_format;
        append_setup_diagnostic("hwDevice=nvtegra-created");
    } else {
        append_setup_diagnostic("hwDevice=none");
        brls::Logger::info("FFmpeg: using software frames for Deko3D upload");
    }

#ifdef OPENNOW_ENABLE_NVDEC
    if (m_uses_hardware_frames)
        m_decoder_context->pix_fmt = AV_PIX_FMT_NVTEGRA;
#endif

    err = avcodec_open2(m_decoder_context, m_decoder, nullptr);
    if (err < 0) {
        return fail_setup("open_codec", err,
                          "decoder=" + std::string(m_decoder->name));
    }
    append_setup_diagnostic(
        "codecOpen=ok activeThreads=" +
        std::to_string(m_decoder_context->thread_count) +
        " activeThreadType=" + std::to_string(m_decoder_context->active_thread_type));

    AVFrameHolder::instance().prepare(
        opennow::video::ReadyFrameQueueCapacity(),
        opennow::video::StartupReadyFrameDepth(),
        std::chrono::microseconds(
            opennow::video::FrameWaitBudgetMicroseconds(redraw_rate)));

    // One extra frame for decoder output while the frame holder owns deep copies.
    m_frames_size = 5 + 1;
    m_frames = new AVFrame*[m_frames_size];

    tmp_frame = av_frame_alloc();
    if (!tmp_frame) {
        return fail_setup("alloc_temp_frame", AVERROR(ENOMEM));
    }

    for (int i = 0; i < m_frames_size; i++) {
        auto& frame = m_frames[i];
        frame = av_frame_alloc();
        if (frame == nullptr) {
            return fail_setup("alloc_output_frame", AVERROR(ENOMEM),
                              "index=" + std::to_string(i));
        }
    }

    append_setup_diagnostic("setup=ok");
    brls::Logger::info("FFmpeg: Setup done: {}", m_setup_diagnostics);
    return 0;
}

void FFmpegVideoDecoder::cleanup() {
    brls::Logger::info("FFmpeg: Cleanup...");

#ifdef __SWITCH2__
    close_switch2_decoder();
    m_switch2_hw_enabled = false;
#endif

    av_packet_free(&m_packet);

    if (hw_device_ctx) {
        av_buffer_unref(&hw_device_ctx);
    }

    if (m_decoder_context) {
        avcodec_free_context(&m_decoder_context);
    }

    if (m_frames) {
        for (int i = 0; i < m_frames_size; i++) {
            av_frame_free(&m_frames[i]);
        }

        delete[] m_frames;
        m_frames = nullptr;
        m_frames_size = 0;
    }

    if (tmp_frame) {
        av_frame_free(&tmp_frame);
    }

    AVFrameHolder::instance().cleanup();

    brls::Logger::info("FFmpeg: Cleanup done!");
}

int FFmpegVideoDecoder::submit_decode_unit(uint8_t* indata, int inlen, int64_t pts) {
    m_submission={};
    m_receive_error=0;
    if (!indata || inlen <= 0)
        return 0;

    if (static_cast<std::size_t>(inlen) >
        opennow::video::MaximumDecodeUnitBytes()) {
        brls::Logger::error("FFmpeg: Big buffer to decode...");
        m_submission.codec_error=-1;
        return -1;
    }

    m_frames_in++;

#ifdef __SWITCH2__
    if (m_switch2_hw_enabled) {
        return submit_switch2_decode_unit(indata, inlen, pts);
    }
#endif

    int decoded_frames = 0;
    auto drain_frames = [&]() {
        while ((m_frame = get_frame(true)) != nullptr) {
            if ((m_frame->flags & AV_FRAME_FLAG_CORRUPT) != 0 ||
                m_frame->decode_error_flags != 0) {
                const auto corrupt_count=m_corrupt_frames_dropped.fetch_add(1)+1;
                ++m_submission.corrupt_frames;
                if(m_frame_gate) m_frame_gate->Publish(m_frame);
                if (corrupt_count <= 5 || corrupt_count % 60 == 0) {
                    brls::Logger::warning(
                        "FFmpeg: dropping corrupt frame flags={} decodeErrors={} total={}",
                        m_frame->flags, m_frame->decode_error_flags,
                        corrupt_count);
                }
                av_frame_unref(m_frame);
                continue;
            }
            m_frames_out++;
            if(m_frame_gate) {
                if(m_frame_gate->Publish(m_frame)) ++decoded_frames;
            } else {
                ++decoded_frames;
                AVFrameHolder::instance().push(m_frame);
            }
        }
    };

    int decode_result = decode(reinterpret_cast<char*>(indata), inlen, pts);
    if (decode_result == AVERROR(EAGAIN)) {
        drain_frames();
        decode_result = decode(reinterpret_cast<char*>(indata), inlen, pts);
    }

    if (decode_result < 0) {
        m_submission.codec_error=decode_result;
        return decode_result;
    }
    m_submission.packet_accepted=true;

    drain_frames();

    if(m_receive_error<0) {
        m_submission.codec_error=m_receive_error;
        return m_receive_error;
    }
    if (decoded_frames == 0 && m_submission.corrupt_frames)
        return AVERROR_INVALIDDATA;

    return decoded_frames;
}

void FFmpegVideoDecoder::reset_stream() {
    if (m_decoder_context)
        avcodec_flush_buffers(m_decoder_context);
}

int FFmpegVideoDecoder::capabilities() const {
    return 0;
}

int FFmpegVideoDecoder::decode(char* indata, int inlen, int64_t pts) {
    m_packet->data = (uint8_t*)indata;
    m_packet->size = inlen;
    m_packet->pts = pts;
    m_packet->dts = pts;

#if !defined(PLATFORM_SWITCH)
    int policy;
    sched_param params{};
    pthread_getschedparam(pthread_self(), &policy, &params);
    params.sched_priority = sched_get_priority_max(policy);
    pthread_setschedparam(pthread_self(), policy, &params);
#endif

//    m_decoder_context->skip_frame = AVDISCARD_ALL;

    int err = avcodec_send_packet(m_decoder_context, m_packet);
    if (err == AVERROR(EAGAIN)) {
        brls::Logger::debug("FFmpeg: decoder wants frame drain before accepting more data");
        return err;
    }

    if (err != 0) {
        char error[512];
        av_strerror(err, error, sizeof(error));
        brls::Logger::error("FFmpeg: Decode failed - {}", error);
        return err;
    }

    return 0;
}

AVFrame* FFmpegVideoDecoder::get_frame(bool native_frame) {
    if (!m_decoder_context || !m_frames || m_frames_size <= 0)
        return nullptr;

    int err;
    AVFrame* resultFrame = m_frames[m_next_frame];
    AVFrame* decodeFrame = resultFrame;
    const bool reusable_transfer = hw_device_ctx && m_transfer_hardware_frames;
    if (!reusable_transfer) av_frame_unref(resultFrame);

#if !defined(PLATFORM_ANDROID) && !defined(USE_METAL_RENDERER)
    if (hw_device_ctx
#if defined(BOREALIS_USE_DEKO3D)
        && m_transfer_hardware_frames
#endif
    ) {
        // For HW->SW transfer path we decode into a temporary hardware frame.
        av_frame_unref(tmp_frame);
        decodeFrame = tmp_frame;
    }
#endif

    if ((err = avcodec_receive_frame(m_decoder_context, decodeFrame)) < 0) {
        if (err == AVERROR(EAGAIN)) {
            return nullptr;
        }

        if (err == AVERROR_EOF) {
            return nullptr;
        }

        char a[AV_ERROR_MAX_STRING_SIZE] = { 0 };
        m_receive_error=err;
        brls::Logger::error("FFmpeg: Error receiving frame with error {}",  av_make_error_string(a, AV_ERROR_MAX_STRING_SIZE, err));
        return nullptr;
    }

    if (!m_first_output_format_logged) {
        m_first_output_format_logged = true;
        const char* pixel_name =
            av_get_pix_fmt_name(static_cast<AVPixelFormat>(decodeFrame->format));
        append_setup_diagnostic(
            "firstOutput=" + std::string(pixel_name ? pixel_name : "unknown") +
            "(" + std::to_string(decodeFrame->format) + ")" +
            " hwFrames=" + std::to_string(hw_device_ctx ? 1 : 0));
        append_setup_diagnostic("colorSpace=" + std::to_string(decodeFrame->colorspace) +
            " colorRange=" + std::to_string(decodeFrame->color_range) +
            " chromaLocation=" + std::to_string(decodeFrame->chroma_location));
        brls::Logger::info("FFmpeg: first decoded output: {}",
                           m_setup_diagnostics);
    }

    if (hw_device_ctx) {
#if defined(BOREALIS_USE_DEKO3D)
        if (!m_transfer_hardware_frames) {
            // Zero-copy is kept as an explicit NVDEC diagnostic backend.
            resultFrame = decodeFrame;
        } else {
            const auto* hw_frames = decodeFrame->hw_frames_ctx
                ? reinterpret_cast<const AVHWFramesContext*>(decodeFrame->hw_frames_ctx->data) : nullptr;
            const auto layout = hw_frames ? std::pair{hw_frames->width, hw_frames->height} : std::pair{0,0};
            const bool reused_storage = opennow::video::RetainTransferStorage(resultFrame,
                hw_frames ? hw_frames->sw_format : AV_PIX_FMT_NONE,
                decodeFrame->width, decodeFrame->height,
                hw_frames && m_transfer_layouts[m_next_frame] == layout);
            if (!reused_storage) av_frame_unref(resultFrame);
            const auto transfer_started = std::chrono::steady_clock::now();
            if ((err = av_hwframe_transfer_data(resultFrame, decodeFrame, 0)) < 0) {
                char a[AV_ERROR_MAX_STRING_SIZE] = { 0 };
                brls::Logger::error(
                    "FFmpeg: Error transferring NVTEGRA frame with error {}",
                    av_make_error_string(a, AV_ERROR_MAX_STRING_SIZE, err));
                return nullptr;
            }
            const uint64_t transfer_us = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - transfer_started).count();
            m_transfer_us_total.fetch_add(transfer_us, std::memory_order_relaxed);
            m_transfer_count.fetch_add(1, std::memory_order_relaxed);
            m_transfer_layouts[m_next_frame] = layout;
            (reused_storage ? m_transfer_storage_reused : m_transfer_storage_fresh).fetch_add(1, std::memory_order_relaxed);
            if (transfer_us > m_transfer_us_max.load(std::memory_order_relaxed))
                m_transfer_us_max.store(transfer_us, std::memory_order_relaxed);
            av_frame_copy_props(resultFrame, decodeFrame);
        }
#elif defined(PLATFORM_ANDROID) || defined(USE_METAL_RENDERER)
        // Keep hardware-backed frame references per queue slot.
        resultFrame = decodeFrame;
#else

#if defined(PLATFORM_SWITCH) && !defined(BOREALIS_USE_DEKO3D)
        for (int i = 0; i < 2; ++i) {
            if (((uintptr_t)resultFrame->data[i] & 0xff) || (resultFrame->linesize[i] & 0xff)) {
                brls::Logger::error("Frame address/pitch not aligned to 256, falling back to cpu transfer");
                break;
            }
        }
#endif

        // Copy hardware frame into software frame
        av_frame_unref(resultFrame);
        if ((err = av_hwframe_transfer_data(resultFrame, decodeFrame, 0)) < 0) {
            char a[AV_ERROR_MAX_STRING_SIZE] = { 0 };
            brls::Logger::error("FFmpeg: Error transferring the data to system memory with error {}",  av_make_error_string(a, AV_ERROR_MAX_STRING_SIZE, err));
            return nullptr;
        }

        av_frame_copy_props(resultFrame, decodeFrame);
#endif
    } else {
        resultFrame = decodeFrame;
    }

    m_current_frame = m_next_frame;
    m_next_frame = (m_current_frame + 1) % m_frames_size;
    if (native_frame)
        return resultFrame;

    return nullptr;
}

VideoDecodeStats* FFmpegVideoDecoder::video_decode_stats() {
    return (VideoDecodeStats*)&m_video_decode_stats_cache;
}

#ifdef __SWITCH2__

void FFmpegVideoDecoder::close_switch2_decoder() {
    if (m_switch2_hw_ready) {
        videoDecoderClose(&m_switch2_decoder);
        m_switch2_hw_ready = false;
    }
    m_switch2_codec_config.clear();
}

bool FFmpegVideoDecoder::ensure_switch2_decoder(const uint8_t* data, size_t size) {
    std::vector<uint8_t> new_config;
    int cs = m_switch2_colorspace;
    bool full = m_switch2_color_full;
    if (extract_sps_pps(data, size, new_config, cs, full)) {
        m_switch2_colorspace = cs;
        m_switch2_color_full = full;
        if (!m_switch2_hw_ready || new_config != m_switch2_codec_config) {
            close_switch2_decoder();
            VideoDecoderConfig cfg {};
            cfg.codec = ViddecCodec_H264;
            cfg.width = static_cast<u32>(m_switch2_width);
            cfg.height = static_cast<u32>(m_switch2_height);
            cfg.image_count = VIDEO_DECODER_DEFAULT_IMAGES;
            cfg.frame_rate = static_cast<u32>(m_stream_fps > 0 ? m_stream_fps : 60);
            cfg.tiling = ViddecTilingMode_PitchLinear;
            cfg.codec_config = new_config.data();
            cfg.codec_config_size = new_config.size();
            const Result rc = videoDecoderCreate(&m_switch2_decoder, &cfg);
            if (R_FAILED(rc)) {
                brls::Logger::error("Switch2 viddec: videoDecoderCreate failed 0x{:x}", rc);
                m_submission.codec_error = -1;
                return false;
            }
            m_switch2_hw_ready = true;
            m_switch2_codec_config = std::move(new_config);
        }
    }
    return m_switch2_hw_ready;
}

int FFmpegVideoDecoder::submit_switch2_decode_unit(const uint8_t* indata, int inlen, int64_t pts) {
    if (!ensure_switch2_decoder(indata, static_cast<size_t>(inlen))) {
        return m_submission.codec_error < 0 ? m_submission.codec_error : 0;
    }

    Result rc = videoDecoderSubmit(&m_switch2_decoder, indata, static_cast<size_t>(inlen),
                                   m_switch2_frame_index++ * 16666667LL, false,
                                   1000000000ULL);
    if (R_FAILED(rc)) {
        brls::Logger::error("Switch2 viddec: submit failed 0x{:x}", rc);
        m_submission.codec_error = -1;
        return -1;
    }
    m_submission.packet_accepted = true;

    int decoded_frames = 0;
    VideoFrame vf {};
    rc = videoDecoderGetFrame(&m_switch2_decoder, &vf, 0);
    if (rc == KERNELRESULT(TimedOut)) {
        rc = videoDecoderGetFrame(&m_switch2_decoder, &vf, 5000000ULL);
    }

    while (R_SUCCEEDED(rc)) {
        if (vf.width > 0 && vf.height > 0 && vf.planes[0] && vf.planes[1] &&
            m_frames && m_frames_size > 0) {
            AVFrame* out_frame = m_frames[m_next_frame];
            const int w = static_cast<int>(vf.width);
            const int h = static_cast<int>(vf.height);
            const int ch = (h + 1) / 2;

            bool buffer_ok = (out_frame->format == AV_PIX_FMT_NV12 &&
                              out_frame->width == w && out_frame->height == h &&
                              av_frame_make_writable(out_frame) >= 0);
            if (!buffer_ok) {
                av_frame_unref(out_frame);
                out_frame->format = AV_PIX_FMT_NV12;
                out_frame->width = w;
                out_frame->height = h;
                buffer_ok = (av_frame_get_buffer(out_frame, 32) >= 0);
            }

            if (buffer_ok) {
                for (int r = 0; r < h; ++r) {
                    std::memcpy(out_frame->data[0] + static_cast<size_t>(r) * out_frame->linesize[0],
                                vf.planes[0] + static_cast<size_t>(r) * vf.pitches[0],
                                static_cast<size_t>(w));
                }
                for (int r = 0; r < ch; ++r) {
                    std::memcpy(out_frame->data[1] + static_cast<size_t>(r) * out_frame->linesize[1],
                                vf.planes[1] + static_cast<size_t>(r) * vf.pitches[1],
                                static_cast<size_t>(w));
                }
                out_frame->pts = pts;
                out_frame->pkt_dts = pts;
                out_frame->flags = 0;
                out_frame->decode_error_flags = 0;
                out_frame->colorspace = static_cast<AVColorSpace>(m_switch2_colorspace);
                out_frame->color_range = m_switch2_color_full
                    ? AVCOL_RANGE_JPEG : AVCOL_RANGE_MPEG;

                m_current_frame = m_next_frame;
                m_next_frame = (m_current_frame + 1) % m_frames_size;
                m_frames_out++;
                if (m_frame_gate) {
                    if (m_frame_gate->Publish(out_frame)) ++decoded_frames;
                } else {
                    ++decoded_frames;
                    AVFrameHolder::instance().push(out_frame);
                }
            }
        }
        videoDecoderReleaseFrame(&m_switch2_decoder, &vf);
        rc = videoDecoderGetFrame(&m_switch2_decoder, &vf, 0);
    }

    if (R_FAILED(rc) && rc != KERNELRESULT(TimedOut)) {
        brls::Logger::error("Switch2 viddec: getFrame failed 0x{:x}", rc);
        m_submission.codec_error = -1;
        return -1;
    }

    return decoded_frames;
}

#endif // __SWITCH2__
