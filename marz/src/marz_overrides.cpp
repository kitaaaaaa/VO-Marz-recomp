// Dennou Senki Virtual-On Marz (SLPM-653.03) specific runtime overrides.
//
// Each override replaces one guest function by address. Keep them minimal and document why the
// original code cannot run as-is under the recompiled runtime.

#include "game_overrides.h"
#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>

namespace ps2_stubs
{
    void sceMpegAddBs(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    extern bool g_mpegNeverWaitForPicture;
}

namespace
{
    constexpr uint32_t kMarzEntry = 0x00100008u;

    // PS2X_TRACE_FUNCS=0x10b7e0,0x4c5b28: logs each call to these guest functions with its arguments and
    // result. Debugging aid only; the original function still runs.
    namespace trace
    {
        constexpr size_t kSlots = 32u;
        std::array<uint32_t, kSlots> g_addresses{};
        std::array<PS2Runtime::RecompiledFunction, kSlots> g_originals{};

        // PS2X_TRACE_FUNCS_FILE=path: stays quiet until that file exists, then logs PS2X_TRACE_FUNCS_LINES calls.
        bool logging()
        {
            static const char *file = std::getenv("PS2X_TRACE_FUNCS_FILE");
            if (!file)
                return true;
            static const char *linesText = std::getenv("PS2X_TRACE_FUNCS_LINES");
            static int left = linesText ? std::atoi(linesText) : 100000;
            static bool triggered = false;
            static uint32_t calls = 0u;
            if (!triggered && (++calls & 0xFFu) == 0u)
            {
                if (std::FILE *f = std::fopen(file, "rb"))
                {
                    std::fclose(f);
                    triggered = true;
                }
            }
            return triggered && left-- > 0;
        }

        template <size_t Slot>
        void wrapper(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
        {
            if (!logging())
            {
                g_originals[Slot](rdram, ctx, runtime);
                return;
            }
            std::fprintf(stderr, "[call] 0x%x a0=0x%x a1=0x%x a2=0x%x a3=0x%x ra=0x%x\n", g_addresses[Slot],
                         GPR_U32(ctx, 4), GPR_U32(ctx, 5), GPR_U32(ctx, 6), GPR_U32(ctx, 7), GPR_U32(ctx, 31));
            // Arguments that point at printable text (file names and the like) are shown as text too.
            for (int reg = 4; reg <= 7; ++reg)
            {
                const uint32_t address = GPR_U32(ctx, reg);
                if (address < 0x00100000u || (address & 0x01FFFFFFu) + 64u > PS2_RAM_SIZE)
                    continue;
                const char *text = reinterpret_cast<const char *>(rdram + (address & 0x01FFFFFFu));
                size_t n = 0u;
                while (n < 64u && text[n] >= 0x20 && text[n] < 0x7F)
                    ++n;
                if (n >= 4u && text[n] == '\0')
                    std::fprintf(stderr, "[str ] a%d=\"%s\"\n", reg - 4, text);
            }
            const uint32_t a1 = GPR_U32(ctx, 5), a2 = GPR_U32(ctx, 6);
            g_originals[Slot](rdram, ctx, runtime);
            // Many library calls return through pointers, so also show what a1 and a2 point at.
            const auto deref = [rdram](uint32_t address)
            {
                uint32_t value = 0xDEADBEEFu;
                if ((address & 0x01FFFFFFu) + 4u <= PS2_RAM_SIZE && address >= 0x00100000u)
                    std::memcpy(&value, rdram + (address & 0x01FFFFFFu), sizeof(value));
                return value;
            };
            std::fprintf(stderr, "[ret ] 0x%x v0=0x%x *a1=0x%x *a2=0x%x\n", g_addresses[Slot], GPR_U32(ctx, 2), deref(a1), deref(a2));
        }

        template <size_t... Slots>
        PS2Runtime::RecompiledFunction wrapperFor(size_t slot, std::index_sequence<Slots...>)
        {
            static constexpr std::array<PS2Runtime::RecompiledFunction, kSlots> wrappers{&wrapper<Slots>...};
            return wrappers[slot];
        }

        void install(PS2Runtime &runtime)
        {
            const char *list = std::getenv("PS2X_TRACE_FUNCS");
            if (!list)
                return;
            std::string text(list);
            size_t slot = 0u, pos = 0u;
            while (pos < text.size() && slot < kSlots)
            {
                size_t used = 0u;
                const uint32_t address = static_cast<uint32_t>(std::stoul(text.substr(pos), &used, 16));
                pos += used + 1u;
                if (!runtime.hasFunction(address))
                    continue;
                g_addresses[slot] = address;
                g_originals[slot] = runtime.lookupFunction(address);
                runtime.replaceFunction(address, wrapperFor(slot, std::make_index_sequence<kSlots>{}));
                ++slot;
            }
        }
    }

    // Sofdec's video path is Sony's sample player: the demuxer fills a ring (ViBuf) and DMA sends whole
    // 2 KB packets to the IPU for libmpeg. The runtime has no IPU but decodes libmpeg's video with FFmpeg,
    // and this build has no software decoder (MPV_DecodeFrmSj just returns -1), so the packets go to
    // sceMpegAddBs where the DMA would have started, and the ring advances as if the IPU had taken them.
    namespace sofdec
    {
        constexpr uint32_t kViBufToMpeg = 0x48u; // The sceMpeg struct sits this far before the ViBuf.

        uint32_t read32(const uint8_t *rdram, uint32_t address)
        {
            uint32_t value = 0u;
            std::memcpy(&value, rdram + (address & 0x01FFFFFFu), sizeof(value));
            return value;
        }

        void write32(uint8_t *rdram, uint32_t address, uint32_t value)
        {
            std::memcpy(rdram + (address & 0x01FFFFFFu), &value, sizeof(value));
        }

        void addBs(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime, uint32_t mpeg, uint32_t data, uint32_t bytes)
        {
            SET_GPR_U32(ctx, 4, mpeg);
            SET_GPR_U32(ctx, 5, data);
            SET_GPR_U32(ctx, 6, bytes);
            ps2_stubs::sceMpegAddBs(rdram, ctx, runtime);
        }

        // viBufAddDMA(ViBuf *f) @ 0x504968. Fields: +0 data, +8 packets, +0xC first packet, +0x10 packets at
        // the IPU (always 0 here), +0x14 bytes put and not yet sent, +0x18 ring size in bytes.
        void addDma(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
        {
            const uint32_t viBuf = GPR_U32(ctx, 4);
            const uint32_t data = read32(rdram, viBuf + 0x00u);
            const uint32_t packets = read32(rdram, viBuf + 0x08u);
            const uint32_t first = read32(rdram, viBuf + 0x0Cu);
            const uint32_t pending = read32(rdram, viBuf + 0x14u);
            const uint32_t send = pending >> 11u;
            if (send != 0u && packets != 0u)
            {
                const uint32_t untilWrap = std::min(send, packets - first);
                addBs(rdram, ctx, runtime, viBuf - kViBufToMpeg, data + (first << 11u), untilWrap << 11u);
                if (send > untilWrap)
                    addBs(rdram, ctx, runtime, viBuf - kViBufToMpeg, data, (send - untilWrap) << 11u);
                write32(rdram, viBuf + 0x0Cu, (first + send) % packets);
                write32(rdram, viBuf + 0x14u, pending - (send << 11u));
            }
            SET_GPR_U32(ctx, 2, 1u);
            ctx->pc = GPR_U32(ctx, 31);
        }

        // viBufRestartDMA @ 0x504C68 and viBufStopDMA @ 0x504B68 only manage the IPU's DMA channels.
        void noDma(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
        {
            (void)rdram;
            (void)runtime;
            SET_GPR_U32(ctx, 2, 0u);
            ctx->pc = GPR_U32(ctx, 31);
        }
    }

    // SjisPuts(str, a1, a2, a3, f12, f13) @ 0x3D61A0 turns Shift-JIS text into UTF-8 for scePFont and hands it to
    // samplePuts @ 0x3D5E10. It reads every character as two bytes and looks it up in the table at 0x5F7530
    // (sorted ranges: u16 first code, u16, u32 pointer to the UCS-2 codes from there); a code outside the table
    // ends the call without drawing anything. So single-byte ASCII, which English text needs, drew nothing.
    // This version does the same lookup for two-byte characters and draws 0x20-0x7E with the full-width Latin
    // glyphs (see below).
    namespace sjis
    {
        constexpr uint32_t kTable = 0x005F7530u;
        constexpr uint32_t kLastEntry = 0x58u;
        constexpr uint32_t kSamplePuts = 0x003D5E10u;
        constexpr size_t kTextMax = 0x400u * 3u;

        uint16_t read16(const uint8_t *rdram, uint32_t address)
        {
            uint16_t value = 0u;
            std::memcpy(&value, rdram + (address & 0x01FFFFFFu), sizeof(value));
            return value;
        }

        uint32_t read32(const uint8_t *rdram, uint32_t address)
        {
            uint32_t value = 0u;
            std::memcpy(&value, rdram + (address & 0x01FFFFFFu), sizeof(value));
            return value;
        }

        // The game's binary search over the range table; 0 when the code is not in it.
        uint16_t toUcs2(const uint8_t *rdram, uint32_t code)
        {
            if (code < read16(rdram, kTable) || code >= read16(rdram, kTable + kLastEntry * 8u))
                return 0u;
            int32_t lo = 0, hi = static_cast<int32_t>(kLastEntry);
            for (int guard = 0; guard < 32 && lo <= hi; ++guard)
            {
                const int32_t mid = (lo + hi) / 2;
                const uint32_t first = read16(rdram, kTable + static_cast<uint32_t>(mid) * 8u);
                const uint32_t next = read16(rdram, kTable + static_cast<uint32_t>(mid + 1) * 8u);
                if (code < first)
                    hi = mid - 1;
                else if (code >= next)
                    lo = mid + 1;
                else
                    return read16(rdram, read32(rdram, kTable + static_cast<uint32_t>(mid) * 8u + 4u) + (code - first) * 2u);
            }
            return 0u;
        }

        void puts(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
        {
            const uint32_t ra = GPR_U32(ctx, 31);
            uint32_t src = GPR_U32(ctx, 4);
            std::string out;
            for (uint32_t n = 0u; n < 0x400u; ++n)
            {
                const uint8_t b0 = rdram[src & 0x01FFFFFFu];
                if (b0 == 0u)
                    break;
                if (b0 == '\n' || b0 == '\t')
                {
                    out.push_back(static_cast<char>(b0));
                    ++src;
                    continue;
                }
                uint16_t ucs = 0u;
                if (b0 >= 0x20u && b0 < 0x7Fu)
                {
                    // The fonts have no ASCII glyphs: use the full-width Latin ones (narrowed by
                    // tools/font_narrow.py), and U+FFE3, blanked by the same tool, as the space.
                    // " ' - ~ have no full-width glyph; they take the font's ” ’ ‐ 〜. A backslash is dropped.
                    switch (b0)
                    {
                    case ' ': ucs = 0xFFE3u; break;
                    case '"': ucs = 0x201Du; break;
                    case '\'': ucs = 0x2019u; break;
                    case '-': ucs = 0x2010u; break;
                    case '~': ucs = 0x301Cu; break;
                    case '\\': ++src; continue;
                    default: ucs = static_cast<uint16_t>(0xFF01u + (b0 - 0x21u)); break;
                    }
                    src -= 1u;
                }
                else
                    ucs = toUcs2(rdram, (static_cast<uint32_t>(b0) << 8) | rdram[(src + 1u) & 0x01FFFFFFu]);
                if (ucs == 0u)
                {
                    ctx->pc = ra; // as the original: a character it cannot map ends the call
                    return;
                }
                out.push_back(static_cast<char>(0xE0u | (ucs >> 12)));
                out.push_back(static_cast<char>(0x80u | ((ucs >> 6) & 0x3Fu)));
                out.push_back(static_cast<char>(0x80u | (ucs & 0x3Fu)));
                src += 2u;
            }
            // The UTF-8 text and a copy of the 3-float vector a3 points at (the original passes its own copy too) go
            // in a guest buffer of our own. Then jump to samplePuts with the caller's return address and let the
            // dispatcher run it. Calling it from here instead breaks: every guest call inside it is a scheduler
            // checkpoint (dispatchGuestBranch), and when one yields, samplePuts returns half done for the scheduler
            // to resume later; forcing pc back to the caller then skips the rest, including the epilogues that
            // restore saved registers. PutMissionIntroMessage lost its record pointer ($16) that way, read a BGM
            // number of 0 for every narration page and restarted channel 1 on PBSver20.adx, so MS_IntroDemo
            // waited forever for that stream to complete.
            static uint32_t buffer = 0u;
            if (!buffer)
                buffer = runtime->guestMalloc(0x10u + kTextMax + 1u, 16u);
            const uint32_t vector = buffer, text = buffer + 0x10u;
            const size_t n = std::min<size_t>(out.size(), kTextMax);
            std::memcpy(rdram + (vector & 0x01FFFFFFu), rdram + (GPR_U32(ctx, 7) & 0x01FFFFFFu), 12u);
            std::memcpy(rdram + (text & 0x01FFFFFFu), out.data(), n);
            rdram[(text + n) & 0x01FFFFFFu] = 0u;
            SET_GPR_U32(ctx, 4, text);
            SET_GPR_U32(ctx, 7, vector);
            ctx->pc = kSamplePuts;
        }
    }

    // --skip-intro on the runner's command line (for testing) skips the movies: on the first frame ADV_Movie
    // advances the game state (gp-0x7788) as it does when a movie ends, and ADV_ExitMovie then runs as usual.
    void skipMovie(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        (void)runtime;
        uint32_t state = 0u;
        std::memcpy(&state, rdram + 0x006C8CE8u, sizeof(state));
        ++state;
        std::memcpy(rdram + 0x006C8CE8u, &state, sizeof(state));
        ctx->pc = GPR_U32(ctx, 31);
    }

    void applyMarzOverrides(PS2Runtime &runtime)
    {
        if (ps2_game_overrides::hasCommandLineOption("--skip-intro"))
            runtime.replaceFunction(0x00512B80u, &skipMovie);
        runtime.replaceFunction(0x00504968u, &sofdec::addDma);
        runtime.replaceFunction(0x00504C68u, &sofdec::noDma);
        runtime.replaceFunction(0x00504B68u, &sofdec::noDma);
        // The thread asking for a picture is the one that feeds the decoder, so it must not block on one.
        ps2_stubs::g_mpegNeverWaitForPicture = true;
        // sceCdLayerSearchFile(file, name, layer) has no runtime handler, so the original code runs and
        // polls a CD RPC server that nothing answers. CRI's dvCi opens the CVM through it. The arguments
        // match sceCdSearchFile apart from the layer, which a single-layer image doesn't need.
        ps2_game_overrides::bindAddressHandler(runtime, 0x00116D48u, "sceCdSearchFile");
        // Text with single-byte ASCII (the English translation); PS2X_SJIS_ORIGINAL=1 keeps the game's SjisPuts.
        if (std::getenv("PS2X_SJIS_ORIGINAL") == nullptr && runtime.hasFunction(0x003D61A0u) && runtime.hasFunction(0x003D5E10u))
        {
            runtime.replaceFunction(0x003D61A0u, &sjis::puts);
        }
        trace::install(runtime);
    }
}

PS2_REGISTER_GAME_OVERRIDE("Dennou Senki Virtual-On Marz (Japan)", "SLPM_653.03", kMarzEntry, 0u, applyMarzOverrides);
