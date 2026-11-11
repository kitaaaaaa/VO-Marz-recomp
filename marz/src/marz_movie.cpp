// Native movie player for Virtual-On Marz: plays mp4 conversions of the game's Sofdec movies (tools/convert_movies.sh)
// with FFmpeg instead of running CRI's Sofdec under the runtime's MPEG emulation.
//
// The game plays its 5 movies (OP_MOVIE0001A/B, ENDINGA/B/C) only through ADV_InitMovie 0x512760, ADV_Movie 0x512B80
// and ADV_ExitMovie 0x512E00. These are replaced here, keeping their effects on the game state (state gp-0x7788,
// skip rules, gp-0x6570/0x6574) and their sound calls (AllSoundOff, PlaySe, njTextureFilterMode) as tail jumps.
// The main loop's movie mode (gp-0x6564 = 1) is not used: the game keeps drawing its (black) frame and waiting for
// vblanks, and the movie is drawn over the picture on the host thread, in a 4:3 box, timed by its own audio stream.
// Sofdec, its CRI threads and the runtime's sceMpeg emulation do not run. PS2X_MARZ_SOFDEC=1 keeps the old path, as do
// missing mp4 files and --skip-intro. PS2X_MARZ_MOVIE_LOG=1 prints the player's timing once per second.

#include "game_overrides.h"
#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"

#include "raylib.h"

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/imgutils.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// Drawn over the game's picture on every present (ps2_runtime.cpp, host thread), given the picture rectangle.
extern std::function<void(float, float, float, float)> g_presentOverlay;

namespace marz_movie
{
    namespace
    {
        constexpr int kSampleRate = 48000;
        constexpr int kChannels = 2;
        constexpr size_t kMaxQueuedFrames = 24u;
        constexpr size_t kAudioRingSamples = static_cast<size_t>(kSampleRate) * kChannels * 2u; // 2 s

        struct Frame
        {
            double pts = 0.0;
            std::vector<uint8_t> rgba;
        };

        class Player
        {
        public:
            bool start(const std::filesystem::path &path, float gain);
            void stop();
            bool finished() const { return m_finished.load(std::memory_order_acquire); }
            bool active() const { return m_active.load(std::memory_order_acquire); }
            void drawOverlay(float x, float y, float w, float h); // host thread
            void pullAudio(int16_t *out, unsigned frames);         // audio thread

        private:
            void decodeLoop();
            double clockSeconds();

            std::filesystem::path m_path;
            std::thread m_thread;
            std::atomic<bool> m_stop{false}, m_active{false}, m_finished{false}, m_decodeDone{false};
            std::atomic<bool> m_hasAudio{false};
            float m_gain = 1.0f;
            int m_width = 0, m_height = 0;

            std::mutex m_mutex; // guards m_frames and the audio ring
            std::deque<Frame> m_frames;
            std::vector<int16_t> m_ring = std::vector<int16_t>(kAudioRingSamples);
            size_t m_ringRead = 0u, m_ringSize = 0u;
            std::atomic<uint64_t> m_audioFramesPlayed{0};

            // host thread only
            bool m_streamLoaded = false, m_texLoaded = false;
            AudioStream m_stream{};
            Texture2D m_tex{};
            double m_shownPts = -1.0;
            std::chrono::steady_clock::time_point m_clockStart{}; // when the audio position last moved
            bool m_clockStarted = false;
            double m_lastAudio = 0.0;
            uint64_t m_shown = 0u, m_dropped = 0u;
            std::chrono::steady_clock::time_point m_lastLog{};
        };

        Player g_player;
        std::atomic<Player *> g_audioPlayer{nullptr};

        bool logging()
        {
            static const bool on = std::getenv("PS2X_MARZ_MOVIE_LOG") != nullptr;
            return on;
        }

        void audioCallback(void *buffer, unsigned int frames)
        {
            Player *player = g_audioPlayer.load(std::memory_order_acquire);
            if (player)
                player->pullAudio(static_cast<int16_t *>(buffer), frames);
            else
                std::memset(buffer, 0, static_cast<size_t>(frames) * kChannels * sizeof(int16_t));
        }

        bool Player::start(const std::filesystem::path &path, float gain)
        {
            stop();
            m_path = path;
            m_gain = gain;
            m_stop = false;
            m_finished = false;
            m_decodeDone = false;
            m_hasAudio = false;
            m_audioFramesPlayed = 0u;
            {
                std::lock_guard lock(m_mutex);
                m_frames.clear();
                m_ringRead = m_ringSize = 0u;
            }
            m_active = true;
            m_thread = std::thread([this] { decodeLoop(); });
            return true;
        }

        void Player::stop()
        {
            m_stop = true;
            if (m_thread.joinable())
                m_thread.join();
            m_active = false;
        }

        void Player::decodeLoop()
        {
            AVFormatContext *format = nullptr;
            AVCodecContext *video = nullptr, *audio = nullptr;
            SwsContext *sws = nullptr;
            SwrContext *swr = nullptr;
            AVPacket *packet = av_packet_alloc();
            AVFrame *frame = av_frame_alloc();
            int videoIndex = -1, audioIndex = -1;
            double videoTimeBase = 0.0;
            std::vector<int16_t> pcm;

            auto openCodec = [&](int index) -> AVCodecContext *
            {
                const AVCodecParameters *par = format->streams[index]->codecpar;
                const AVCodec *codec = avcodec_find_decoder(par->codec_id);
                AVCodecContext *ctx = codec ? avcodec_alloc_context3(codec) : nullptr;
                if (!ctx || avcodec_parameters_to_context(ctx, par) < 0 || avcodec_open2(ctx, codec, nullptr) < 0)
                {
                    avcodec_free_context(&ctx);
                    return nullptr;
                }
                return ctx;
            };

            // Frames go into the queue as RGBA; audio into the ring as s16 stereo at 48 kHz. Blocks while both are full.
            auto handleVideo = [&]()
            {
                if (!sws)
                {
                    m_width = frame->width;
                    m_height = frame->height;
                    sws = sws_getContext(frame->width, frame->height, static_cast<AVPixelFormat>(frame->format), frame->width,
                                         frame->height, AV_PIX_FMT_RGBA, SWS_BILINEAR, nullptr, nullptr, nullptr);
                }
                Frame out;
                const int64_t ts = frame->best_effort_timestamp != AV_NOPTS_VALUE ? frame->best_effort_timestamp : frame->pts;
                out.pts = ts != AV_NOPTS_VALUE ? static_cast<double>(ts) * videoTimeBase : 0.0;
                out.rgba.resize(static_cast<size_t>(m_width) * m_height * 4u);
                uint8_t *dst[1] = {out.rgba.data()};
                const int dstStride[1] = {m_width * 4};
                sws_scale(sws, frame->data, frame->linesize, 0, frame->height, dst, dstStride);
                while (!m_stop)
                {
                    {
                        std::lock_guard lock(m_mutex);
                        if (m_frames.size() < kMaxQueuedFrames)
                        {
                            m_frames.push_back(std::move(out));
                            return;
                        }
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(2));
                }
            };
            auto handleAudio = [&]()
            {
                if (!swr)
                {
                    AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
                    if (swr_alloc_set_opts2(&swr, &stereo, AV_SAMPLE_FMT_S16, kSampleRate, &frame->ch_layout,
                                            static_cast<AVSampleFormat>(frame->format), frame->sample_rate, 0, nullptr) < 0 ||
                        swr_init(swr) < 0)
                    {
                        swr_free(&swr);
                        return;
                    }
                }
                const int maxOut = swr_get_out_samples(swr, frame->nb_samples);
                pcm.resize(static_cast<size_t>(std::max(maxOut, 0)) * kChannels);
                uint8_t *dst[1] = {reinterpret_cast<uint8_t *>(pcm.data())};
                const int got = swr_convert(swr, dst, maxOut, const_cast<const uint8_t **>(frame->extended_data), frame->nb_samples);
                size_t done = 0u;
                const size_t total = static_cast<size_t>(std::max(got, 0)) * kChannels;
                while (done < total && !m_stop)
                {
                    {
                        std::lock_guard lock(m_mutex);
                        while (done < total && m_ringSize < m_ring.size())
                        {
                            m_ring[(m_ringRead + m_ringSize) % m_ring.size()] = pcm[done++];
                            ++m_ringSize;
                        }
                    }
                    if (done < total)
                        std::this_thread::sleep_for(std::chrono::milliseconds(2));
                }
            };
            auto receive = [&](AVCodecContext *ctx, bool isVideo)
            {
                while (!m_stop && avcodec_receive_frame(ctx, frame) == 0)
                {
                    if (isVideo)
                        handleVideo();
                    else
                        handleAudio();
                    av_frame_unref(frame);
                }
            };

            const std::string path = m_path.string();
            if (avformat_open_input(&format, path.c_str(), nullptr, nullptr) == 0 && avformat_find_stream_info(format, nullptr) >= 0)
            {
                videoIndex = av_find_best_stream(format, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
                audioIndex = av_find_best_stream(format, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
                if (videoIndex >= 0)
                {
                    video = openCodec(videoIndex);
                    videoTimeBase = av_q2d(format->streams[videoIndex]->time_base);
                }
                if (audioIndex >= 0)
                    audio = openCodec(audioIndex);
                m_hasAudio = audio != nullptr;
            }
            if (!video)
                std::fprintf(stderr, "[marz-movie] cannot play %s\n", path.c_str());

            while (video && !m_stop && av_read_frame(format, packet) >= 0)
            {
                if (packet->stream_index == videoIndex && avcodec_send_packet(video, packet) == 0)
                    receive(video, true);
                else if (audio && packet->stream_index == audioIndex && avcodec_send_packet(audio, packet) == 0)
                    receive(audio, false);
                av_packet_unref(packet);
            }
            if (video && !m_stop)
            {
                avcodec_send_packet(video, nullptr);
                receive(video, true);
            }
            if (audio && !m_stop)
            {
                avcodec_send_packet(audio, nullptr);
                receive(audio, false);
            }
            m_decodeDone = true;
            // Finished once every frame and sample has been shown or played.
            while (!m_stop)
            {
                {
                    std::lock_guard lock(m_mutex);
                    if (m_frames.empty() && m_ringSize == 0u)
                        break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            m_finished = true;

            av_frame_free(&frame);
            av_packet_free(&packet);
            sws_freeContext(sws);
            swr_free(&swr);
            avcodec_free_context(&video);
            avcodec_free_context(&audio);
            avformat_close_input(&format);
        }

        void Player::pullAudio(int16_t *out, unsigned frames)
        {
            const size_t want = static_cast<size_t>(frames) * kChannels;
            size_t got = 0u;
            {
                std::lock_guard lock(m_mutex);
                // Whole frames only, so the clock counts what was really played.
                const size_t take = std::min(want, m_ringSize - m_ringSize % kChannels);
                for (; got < take; ++got)
                    out[got] = m_ring[(m_ringRead + got) % m_ring.size()];
                m_ringRead = (m_ringRead + take) % m_ring.size();
                m_ringSize -= take;
            }
            std::fill(out + got, out + want, int16_t{0});
            m_audioFramesPlayed.fetch_add(got / kChannels, std::memory_order_acq_rel);
        }

        // The audio position while audio plays; when it stops moving for more than 0.1 s (no audio track or device, a
        // stall, or audio shorter than the video, as in the endings) the wall clock carries on from it.
        double Player::clockSeconds()
        {
            const auto now = std::chrono::steady_clock::now();
            const double audio = static_cast<double>(m_audioFramesPlayed.load(std::memory_order_acquire)) / kSampleRate;
            if (!m_clockStarted || audio != m_lastAudio)
            {
                m_clockStarted = true;
                m_lastAudio = audio;
                m_clockStart = now;
                return audio;
            }
            const double still = std::chrono::duration<double>(now - m_clockStart).count();
            return still > 0.1 ? audio + still : audio;
        }

        void Player::drawOverlay(float x, float y, float w, float h)
        {
            if (!active())
            {
                if (m_streamLoaded)
                {
                    g_audioPlayer = nullptr;
                    StopAudioStream(m_stream);
                    UnloadAudioStream(m_stream);
                    m_streamLoaded = false;
                }
                if (m_texLoaded)
                {
                    UnloadTexture(m_tex);
                    m_texLoaded = false;
                }
                m_shownPts = -1.0;
                m_clockStarted = false;
                m_lastAudio = 0.0;
                m_shown = m_dropped = 0u;
                return;
            }
            if (!m_streamLoaded)
            {
                m_stream = LoadAudioStream(kSampleRate, 16, kChannels);
                g_audioPlayer = this;
                SetAudioStreamCallback(m_stream, &audioCallback);
                SetAudioStreamVolume(m_stream, m_gain);
                PlayAudioStream(m_stream);
                m_streamLoaded = true;
                m_lastLog = std::chrono::steady_clock::now();
            }

            // The newest frame that is due; older due frames are dropped.
            const double clock = clockSeconds();
            Frame next;
            bool have = false;
            {
                std::lock_guard lock(m_mutex);
                while (!m_frames.empty() && m_frames.front().pts <= clock)
                {
                    if (have)
                        ++m_dropped;
                    next = std::move(m_frames.front());
                    m_frames.pop_front();
                    have = true;
                }
            }
            if (have)
            {
                if (!m_texLoaded)
                {
                    Image image{next.rgba.data(), m_width, m_height, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
                    m_tex = LoadTextureFromImage(image);
                    SetTextureFilter(m_tex, TEXTURE_FILTER_BILINEAR);
                    m_texLoaded = true;
                }
                else
                {
                    UpdateTexture(m_tex, next.rgba.data());
                }
                m_shownPts = next.pts;
                ++m_shown;
            }

            if (m_texLoaded)
            {
                // The movies are 4:3 (640x448 with a 14:15 pixel aspect): a 4:3 box centred in the picture.
                float boxW = h * 4.0f / 3.0f, boxH = h;
                if (boxW > w)
                {
                    boxW = w;
                    boxH = w * 3.0f / 4.0f;
                }
                DrawRectangle(static_cast<int>(x), static_cast<int>(y), static_cast<int>(w + 1.0f), static_cast<int>(h + 1.0f), BLACK);
                DrawTexturePro(m_tex, Rectangle{0.0f, 0.0f, static_cast<float>(m_width), static_cast<float>(m_height)},
                               Rectangle{x + (w - boxW) * 0.5f, y + (h - boxH) * 0.5f, boxW, boxH}, Vector2{0.0f, 0.0f}, 0.0f, WHITE);
            }

            if (logging())
            {
                const auto now = std::chrono::steady_clock::now();
                if (now - m_lastLog >= std::chrono::seconds(1))
                {
                    m_lastLog = now;
                    size_t queued = 0u, ring = 0u;
                    {
                        std::lock_guard lock(m_mutex);
                        queued = m_frames.size();
                        ring = m_ringSize / kChannels;
                    }
                    std::fprintf(stderr, "[marz-movie] clock=%.3f shownPts=%.3f lag=%.3f shown=%llu dropped=%llu queued=%zu audio=%.3fs decodeDone=%d\n",
                                 clock, m_shownPts, clock - m_shownPts, static_cast<unsigned long long>(m_shown),
                                 static_cast<unsigned long long>(m_dropped), queued, static_cast<double>(ring) / kSampleRate,
                                 m_decodeDone.load() ? 1 : 0);
                }
            }
        }

        // Guest addresses (gp = 0x6D0470).
        constexpr uint32_t kState = 0x006C8CE8u;    // gp-0x7788: game state
        constexpr uint32_t kMode = 0x006C8CECu;     // gp-0x7784: 0x13 = ending
        constexpr uint32_t kPadPtr = 0x006C8DB0u;   // gp-0x76C0: pad 1; +0x10 pressed buttons
        constexpr uint32_t kEndCount = 0x006C9EF8u; // gp-0x6578: MS_EndMovie's frame count (endings skip at >= 0x79)
        constexpr uint32_t kTimer = 0x006C9EFCu;    // gp-0x6574
        constexpr uint32_t kStarted = 0x006C9F00u;  // gp-0x6570 (byte)
        constexpr uint32_t kSelect = 0x00CA0448u;   // opening variant (bit 0) / ending (value % 3)
        constexpr uint32_t kVolume = 0x00CA50BEu;   // u16, 0..100
        constexpr uint32_t kAllSoundOff = 0x004558A0u;
        constexpr uint32_t kPlaySe = 0x00454280u;
        constexpr uint32_t kTextureFilterMode = 0x001C7210u;
        constexpr uint32_t kButtonStart = 0x800u;

        uint32_t read32(const uint8_t *rdram, uint32_t address)
        {
            uint32_t value = 0u;
            std::memcpy(&value, rdram + address, 4);
            return value;
        }

        void write32(uint8_t *rdram, uint32_t address, uint32_t value)
        {
            std::memcpy(rdram + address, &value, 4);
        }

        std::filesystem::path moviesDir()
        {
            return PS2Runtime::getIoPaths().elfDirectory / "movies";
        }

        // The file ADV_InitMovie would start (0x512A0C-0x512B40).
        const char *movieName(const uint8_t *rdram)
        {
            const uint32_t select = read32(rdram, kSelect);
            if (read32(rdram, kMode) == 0x13u)
            {
                static const char *const endings[3] = {"ENDINGC_00000", "ENDINGA_00000", "ENDINGB_00000"};
                return endings[select % 3u];
            }
            return (select & 1u) ? "OP_MOVIE0001B" : "OP_MOVIE0001A";
        }

        // CalcToDB 0x4559C0 (0.1 dB), clamped to [-999, 0] as in ADV_InitMovie, then as a gain.
        float volumeGain(const uint8_t *rdram)
        {
            uint16_t v = 0u;
            std::memcpy(&v, rdram + kVolume, 2);
            int db = 0;
            if (v == 0u)
                db = -960;
            else if (v < 100u)
                db = static_cast<int>(200.0f * std::log10(static_cast<float>(v) / 100.0f));
            db = std::clamp(db, -999, 0);
            return std::pow(10.0f, static_cast<float>(db) / 200.0f);
        }

        void initMovie(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
        {
            (void)runtime;
            const std::filesystem::path path = moviesDir() / (std::string(movieName(rdram)) + ".mp4");
            if (logging())
                std::fprintf(stderr, "[marz-movie] start %s\n", path.string().c_str());
            g_player.start(path, volumeGain(rdram));
            rdram[kStarted] = 1u;
            write32(rdram, kTimer, 0u);
            write32(rdram, kState, read32(rdram, kState) + 1u);
            ctx->pc = kAllSoundOff; // tail call, returns to ADV_InitMovie's caller
        }

        void movieFrame(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
        {
            (void)runtime;
            const int32_t timer = static_cast<int32_t>(read32(rdram, kTimer));
            write32(rdram, kTimer, static_cast<uint32_t>(timer > 0 ? timer - 1 : timer + 0x1E0));

            const bool ending = read32(rdram, kMode) == 0x13u;
            const bool start = (read32(rdram, read32(rdram, kPadPtr) + 0x10u) & kButtonStart) != 0u;
            const bool ended = g_player.finished();
            const bool skip = start && (!ending || static_cast<int32_t>(read32(rdram, kEndCount)) >= 0x79);
            if (!ended && !skip)
            {
                ctx->pc = GPR_U32(ctx, 31);
                return;
            }
            if (logging())
                std::fprintf(stderr, "[marz-movie] %s\n", ended ? "ended" : "skipped");
            g_player.stop();
            write32(rdram, kState, read32(rdram, kState) + 1u);
            if (start && !ending)
            {
                SET_GPR_U32(ctx, 4, 0x113u);
                SET_GPR_U32(ctx, 5, 0u);
                SET_GPR_U32(ctx, 6, 0u);
                ctx->pc = kPlaySe;
                return;
            }
            ctx->pc = GPR_U32(ctx, 31);
        }

        void exitMovie(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
        {
            (void)runtime;
            g_player.stop();
            write32(rdram, kState, read32(rdram, kMode) == 0x13u ? 0x88u : 0x17u);
            SET_GPR_U32(ctx, 4, 0x2000u);
            ctx->pc = kTextureFilterMode;
        }
    }

    void install(PS2Runtime &runtime)
    {
        const char *sofdec = std::getenv("PS2X_MARZ_SOFDEC");
        if ((sofdec && std::strcmp(sofdec, "1") == 0) || ps2_game_overrides::hasCommandLineOption("--skip-intro"))
            return;
        static const char *const names[] = {"OP_MOVIE0001A", "OP_MOVIE0001B", "ENDINGA_00000", "ENDINGB_00000", "ENDINGC_00000"};
        for (const char *name : names)
        {
            std::error_code ec;
            if (!std::filesystem::exists(moviesDir() / (std::string(name) + ".mp4"), ec))
            {
                std::fprintf(stderr, "[marz] native movies: %s.mp4 missing in %s (tools/convert_movies.sh), using Sofdec\n",
                             name, moviesDir().string().c_str());
                return;
            }
        }
        runtime.replaceFunction(0x00512760u, &initMovie);
        runtime.replaceFunction(0x00512B80u, &movieFrame);
        runtime.replaceFunction(0x00512E00u, &exitMovie);
        g_presentOverlay = [](float x, float y, float w, float h) { g_player.drawOverlay(x, y, w, h); };
        std::fprintf(stderr, "[marz] native movies: on (PS2X_MARZ_SOFDEC=1 for Sofdec)\n");
    }
}
