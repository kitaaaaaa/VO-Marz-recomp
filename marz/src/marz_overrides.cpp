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

    // Movies can freeze the game, so for now they are skipped: on the first frame ADV_Movie advances the
    // game state (gp-0x7788) as it does when a movie ends, and ADV_ExitMovie then runs as usual.
    // PS2X_PLAY_MOVIES=1 plays them.
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
        if (std::getenv("PS2X_PLAY_MOVIES") == nullptr)
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
        trace::install(runtime);
    }
}

PS2_REGISTER_GAME_OVERRIDE("Dennou Senki Virtual-On Marz (Japan)", "SLPM_653.03", kMarzEntry, 0u, applyMarzOverrides);
