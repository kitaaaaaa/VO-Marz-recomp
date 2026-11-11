// Dennou Senki Virtual-On Marz (SLPM-653.03) specific runtime overrides.
//
// Each override replaces one guest function by address. Keep them minimal and document why the
// original code cannot run as-is under the recompiled runtime.

#include "game_overrides.h"
#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"
#include "runtime/ee_scheduler.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace ps2_stubs
{
    void sceMpegAddBs(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    extern bool g_mpegNeverWaitForPicture;
    extern bool g_mpegNoPicturePacing;
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
            std::fprintf(stderr, "[call] 0x%x a0=0x%x a1=0x%x a2=0x%x a3=0x%x ra=0x%x vsync=%llu\n", g_addresses[Slot],
                         GPR_U32(ctx, 4), GPR_U32(ctx, 5), GPR_U32(ctx, 6), GPR_U32(ctx, 7), GPR_U32(ctx, 31),
                         static_cast<unsigned long long>(runtime->eeScheduler().currentVSyncTick()));
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

    // PS2X_MARZ_QUICK_HANDLING=1: the human players' robots (__MM+0x50 plmode < 4; CPU robots are 4) handle
    // closer to Virtual-On Oratorio Tangram (MSBS 5.66). Off: nothing below is installed.
    // - No input delay. MakeButton keeps the pad in a 4-slot FIFO (the robot acts 3 frames late) unless a flag
    //   is set; config/marz_overrides.toml points that test at kSkipFifo, which crt0 clears, so it is set here
    //   from MakeButton's entry.
    // - Dash cancel (watari): state 8 (F_LMD_DashCancel) plays the whole dash-end motion. A P_LMD handler,
    //   a NULL slot in the F_LMD table, now ends it after the robot's own length scaled by 10 / Temjin's
    //   (RAM_FRAME_TEM 32/32/36/36), by moving the motion frame to its last frame: F_LMD_DashCancel then takes
    //   its own end path, and MakeButton's held-press buffer starts the next dash, as OT does.
    // - Recovery after a dash attack: freeze (__MM+0x310), loaded at the end of the attack from fr_dnf[sdr]
    //   (by weapon), becomes Oratorio Tangram's value for the same direction and weapon, of the OT robot
    //   matched to each Marz type (kRecovery). The value is replaced on the next frame, in the P_LMD
    //   handlers that count it down, before they do.
    // - Turning in a ground dash, where the matched OT robot turns faster (kTurn): the homing toward the target
    //   while a dash shot fires (MoveVRCAMrot ry_mode 3: r.y += clamp(ea.y, +-rev_i)), the lever turn while
    //   firing (Rev from revi0/1/2; Marz applies it only in the shot's first phase, OT in all of them), and the
    //   lever turn of a plain dash (revq0/1/2).
    // Hooked functions run on through a copy of the original registered at an unused address (a tail-jump:
    // overrides must not call guest code, see sjis::puts).
    namespace quick
    {
        constexpr uint32_t kSkipFifo = 0x006C8BD4u;     // gp-0x789C, .sbss alignment padding
        constexpr uint32_t kFlmdTable = 0x0065ED20u;    // {F_LMD, P_LMD} per state
        constexpr uint32_t kRobots = 0x00A25E20u, kRobotSize = 0x1380u, kRobotCount = 16u;
        constexpr uint32_t kMakeButton = 0x00221180u, kFDash = 0x00256E90u, kFDashCancel = 0x00259330u;
        constexpr uint32_t kPPause = 0x0024C110u, kPJump = 0x0025B590u, kPRolling = 0x00256E50u;
        // Unused words between the end of .sdata and _fbss: copies of the originals, and the new handler. The
        // recompiler took the data table there (tbl_intra_dc_mult_1, 0x6B8F20-0x6C8B80) for code, so these slots
        // hold that function; nothing jumps into the table.
        constexpr uint32_t kSpareFiller = 0x006B8F20u;
        constexpr uint32_t kOrigMakeButton = 0x006C8B10u, kOrigFDash = 0x006C8B14u, kOrigFDashCancel = 0x006C8B18u;
        constexpr uint32_t kOrigPPause = 0x006C8B1Cu, kOrigPJump = 0x006C8B20u, kOrigPRolling = 0x006C8B24u;
        constexpr uint32_t kPDashCancel = 0x006C8B28u;
        constexpr uint32_t kMoveVrCamRot = 0x0023E270u, kOrigMoveVrCamRot = 0x006C8B2Cu;
        constexpr uint32_t kPDash = 0x00258F00u, kPDashTurn = 0x006C8B30u; // state 2's P_LMD slot gets kPDashTurn
        constexpr uint32_t kTemjinBrake[4] = {32u, 32u, 36u, 36u}; // dash-end length by dr (+0x384)

        struct Turn
        {
            uint8_t type;
            int32_t homing;  // rev_i, rp+0x178
            int32_t shot[3]; // revi0/1/2, rp+0x158/15C/160
            int32_t dash[3]; // revq0/1/2, rp+0x14C/150/154
        };
        constexpr Turn kTurn[] = {
#include "marz_quick_turn.inc"
        };
        const Turn *turnFor(uint8_t type)
        {
            for (const Turn &t : kTurn)
                if (t.type == type)
                    return &t;
            return nullptr;
        }

        // freeze by sdr (0 forward, 1 forward diagonal, 2 side, 3 back diagonal, 4 back) and weapon (left, right,
        // both). From Oratan-recomp's parameter blocks (+3516/3518/3520 + d*8) of the matched OT robot; Jaguarandi
        // and Guarayakha: their own values times OT Temjin / Marz Temjin 747A per slot.
        struct Recovery
        {
            uint8_t type;
            uint8_t frames[5][3];
        };
        constexpr Recovery kRecovery[] = {
#include "marz_quick_recovery.inc"
        };
        // Ajim (21) and Temjin A8 (42) keep their own freeze where it is shorter than the table's.
        bool keepsShorter(uint8_t type) { return type == 21u || type == 42u; }

        struct Seen
        {
            bool pending = false; // left F_LMD_Dash / F_LMD_DashCancel; freeze not looked at yet
            bool cancel = false;
            uint16_t sph = 0u;
            uint32_t sdr = 0u;
        };
        std::array<Seen, kRobotCount> g_seen{};

        uint8_t read8(const uint8_t *rdram, uint32_t address) { return rdram[address & 0x01FFFFFFu]; }
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
        void write16(uint8_t *rdram, uint32_t address, uint16_t value) { std::memcpy(rdram + (address & 0x01FFFFFFu), &value, sizeof(value)); }
        void write32(uint8_t *rdram, uint32_t address, uint32_t value) { std::memcpy(rdram + (address & 0x01FFFFFFu), &value, sizeof(value)); }

        // plmode < 4: a pad drives the robot. The training demo also gives its robot plmode 0, but MakeButton feeds
        // it MakeDemobutton's script (game mode gp-0x7784 == 7, step gp-0x7788 == 0x15), timed for vanilla handling.
        bool human(const uint8_t *rdram, uint32_t robot)
        {
            if (read32(rdram, 0x006C8CECu) == 7u && read32(rdram, 0x006C8CE8u) == 0x15u)
                return false;
            return read8(rdram, robot + 0x50u) < 4u;
        }
        Seen *seenFor(uint32_t robot)
        {
            const uint32_t offset = robot - kRobots;
            if (robot < kRobots || offset % kRobotSize != 0u || offset / kRobotSize >= kRobotCount)
                return nullptr;
            return &g_seen[offset / kRobotSize];
        }

        void makeButton(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
        {
            (void)runtime;
            write32(rdram, kSkipFifo, 1u);
            ctx->pc = kOrigMakeButton;
        }

        void noteDash(uint8_t *rdram, R5900Context *ctx, bool cancel)
        {
            const uint32_t robot = GPR_U32(ctx, 4);
            if (Seen *seen = seenFor(robot))
            {
                seen->pending = true;
                seen->cancel = cancel;
                seen->sph = read16(rdram, robot + 0x328u);
                seen->sdr = read32(rdram, robot + 0x388u);
            }
        }
        void fDash(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
        {
            (void)runtime;
            noteDash(rdram, ctx, false);
            ctx->pc = kOrigFDash;
        }
        void fDashCancel(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
        {
            (void)runtime;
            noteDash(rdram, ctx, true);
            ctx->pc = kOrigFDashCancel;
        }

        // The weapon slot freeze came from: F_LMD_Dash loads L for sph 4, R for 2, B for any other shot (8/10/12
        // are dash melee, fr_dif); F_LMD_DashCancel loads L/R/B for 4/2/6 only. -1: not a dash shot.
        int weaponSlot(const Seen &seen)
        {
            if (seen.sph == 4u)
                return 0;
            if (seen.sph == 2u)
                return 1;
            if (seen.cancel)
                return seen.sph == 6u ? 2 : -1;
            return (seen.sph == 8u || seen.sph == 10u || seen.sph == 12u) ? -1 : 2;
        }

        void retime(uint8_t *rdram, uint32_t robot)
        {
            Seen *seen = seenFor(robot);
            if (!seen || !seen->pending)
                return;
            seen->pending = false;
            const uint16_t freeze = read16(rdram, robot + 0x310u);
            if (!human(rdram, robot) || freeze == 0u || seen->sdr > 4u)
                return;
            const int weapon = weaponSlot(*seen);
            if (weapon < 0)
                return;
            const uint8_t type = read8(rdram, robot + 0x4Fu);
            for (const Recovery &r : kRecovery)
                if (r.type == type)
                {
                    const uint16_t frames = r.frames[seen->sdr][weapon];
                    write16(rdram, robot + 0x310u, keepsShorter(type) ? std::min(freeze, frames) : frames);
                    return;
                }
        }
        void pPause(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
        {
            (void)runtime;
            retime(rdram, GPR_U32(ctx, 4));
            ctx->pc = kOrigPPause;
        }
        void pJump(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
        {
            (void)runtime;
            retime(rdram, GPR_U32(ctx, 4));
            ctx->pc = kOrigPJump;
        }
        void pRolling(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
        {
            (void)runtime;
            retime(rdram, GPR_U32(ctx, 4));
            ctx->pc = kOrigPRolling;
        }

        // P_LMD for state 8. Operation calls it before MoveRobot, which flags the motion's end (fr >= mx_fr-1)
        // and then runs F_LMD_DashCancel. Fields: +0x370 motion frame, +0x3C0 its length (mx_fr), +0x384 dr.
        void pDashCancel(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
        {
            (void)runtime;
            const uint32_t robot = GPR_U32(ctx, 4);
            if (human(rdram, robot))
            {
                const uint32_t dr = std::min<uint32_t>(read32(rdram, robot + 0x384u), 3u);
                const int32_t frame = static_cast<int32_t>(read32(rdram, robot + 0x370u));
                const int32_t length = static_cast<int32_t>(read32(rdram, robot + 0x3C0u));
                const int32_t brake = std::max<int32_t>(1, (length * 10 + static_cast<int32_t>(kTemjinBrake[dr]) / 2) /
                                                                static_cast<int32_t>(kTemjinBrake[dr]));
                if (length >= 1 && brake < length && frame >= brake - 1)
                    write32(rdram, robot + 0x370u, static_cast<uint32_t>(length - 1));
            }
            ctx->pc = GPR_U32(ctx, 31);
        }

        int32_t clampTurn(int32_t angle, int32_t limit) { return std::max(-limit, std::min(angle, limit)); }
        int16_t param16(const uint8_t *rdram, uint32_t params, uint32_t offset) { return static_cast<int16_t>(read16(rdram, params + offset)); }

        // MoveVRCAMrot(robot), called by MoveRobot once a frame. In ry_mode 3 (+0x330, set when a dash attack starts)
        // it adds clamp(ea.y, +-rev_i) to r.y (+0x88), ea.y = (s16)+0x278 being the angle to the target. For a dash
        // shot (state 2, sph 2-7) whose OT homing limit is larger, the difference is added first.
        void moveVrCamRot(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
        {
            (void)runtime;
            const uint32_t robot = GPR_U32(ctx, 4);
            const uint16_t sph = read16(rdram, robot + 0x328u);
            if (human(rdram, robot) && read32(rdram, robot + 0x330u) == 3u && read16(rdram, robot + 0x326u) == 2u && sph >= 2u && sph <= 7u)
                if (const Turn *turn = turnFor(read8(rdram, robot + 0x4Fu)))
                {
                    const int32_t own = static_cast<int32_t>(read32(rdram, read32(rdram, robot + 0x28Cu) + 0x178u));
                    if (turn->homing > own)
                    {
                        const int32_t ea = static_cast<int16_t>(read16(rdram, robot + 0x278u));
                        const int32_t ry = static_cast<int32_t>(read32(rdram, robot + 0x88u));
                        write32(rdram, robot + 0x88u, static_cast<uint32_t>(ry + clampTurn(ea, turn->homing) - clampTurn(ea, own)));
                    }
                }
            ctx->pc = kOrigMoveVrCamRot;
        }

        // Rev (+0x134) is MakeButton's lever turn for this frame: (int)(band * f) for |f| <= 1, the band picked by the
        // angle to the target ea.y (+0x278): revi* while a weapon fires (+0xE4 & 0x3F), revq* in a plain dash
        // (revq2 when locked on, +0x110). This gives the same f with the OT band instead.
        int32_t rescale(int32_t rev, int32_t own, int32_t target)
        {
            return own > 0 && target > own ? static_cast<int32_t>(static_cast<int64_t>(rev) * target / own) : rev;
        }

        // P_LMD for state 2 (the dash), ahead of P_LMD_Dash: it adds Rev to r.y in a plain dash's loop phase and in a
        // dash shot's first phase. For human robots the dash shot also turns in its later phases (as in OT), and both
        // use the OT band where it is larger. Runs on into P_LMD_Dash.
        void pDashTurn(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
        {
            (void)runtime;
            const uint32_t robot = GPR_U32(ctx, 4);
            const Turn *turn = human(rdram, robot) ? turnFor(read8(rdram, robot + 0x4Fu)) : nullptr;
            if (turn)
            {
                const uint16_t sph = read16(rdram, robot + 0x328u);
                const uint32_t phase = read32(rdram, robot + 0x37Cu);
                const int32_t rev = static_cast<int32_t>(read32(rdram, robot + 0x134u));
                const int32_t ea = static_cast<int32_t>(read32(rdram, robot + 0x278u));
                const int32_t aim = std::abs(ea);
                const uint32_t params = read32(rdram, robot + 0x28Cu);
                int32_t add = 0;
                if (sph >= 2u && sph <= 7u)
                {
                    int32_t turned = rev;
                    if (read32(rdram, robot + 0xE4u) & 0x3Fu)
                    {
                        const int band = aim < 0x1000 ? 2 : aim < 0x1800 ? 1 : 0;
                        turned = rescale(rev, param16(rdram, params, 0x158u + 4u * band), turn->shot[band]);
                    }
                    add = phase == 0u ? turned - rev : turned; // P_LMD_Dash itself adds Rev in phase 0 only
                }
                else if (sph < 2u && phase == 1u)
                {
                    const int band = read16(rdram, robot + 0x110u) != 0u ? 2 : aim < 0x1800 ? 1 : 0;
                    add = rescale(rev, param16(rdram, params, 0x14Cu + 4u * band), turn->dash[band]) - rev;
                }
                if (add != 0)
                    write32(rdram, robot + 0x88u, read32(rdram, robot + 0x88u) + static_cast<uint32_t>(add));
            }
            ctx->pc = kPDash;
        }

        bool spare(PS2Runtime &runtime, uint32_t address)
        {
            return !runtime.hasFunction(address) || runtime.lookupFunction(address) == runtime.lookupFunction(kSpareFiller);
        }

        struct Hook
        {
            uint32_t address, copy;
            PS2Runtime::RecompiledFunction fn;
        };
        constexpr Hook kHooks[] = {
            {kMakeButton, kOrigMakeButton, &makeButton}, {kFDash, kOrigFDash, &fDash},
            {kFDashCancel, kOrigFDashCancel, &fDashCancel}, {kPPause, kOrigPPause, &pPause},
            {kPJump, kOrigPJump, &pJump}, {kPRolling, kOrigPRolling, &pRolling},
            {kMoveVrCamRot, kOrigMoveVrCamRot, &moveVrCamRot},
        };

        bool hook(PS2Runtime &runtime, uint32_t address, uint32_t copy, PS2Runtime::RecompiledFunction fn)
        {
            runtime.replaceFunction(copy, runtime.lookupFunction(address));
            return runtime.replaceFunction(address, fn);
        }

        void install(PS2Runtime &runtime)
        {
            const char *on = std::getenv("PS2X_MARZ_QUICK_HANDLING");
            if (!on || std::strcmp(on, "1") != 0)
                return;
            uint8_t *rdram = runtime.memory().getRDRAM();
            // Check everything first, so a mismatch installs nothing.
            bool fits = read32(rdram, kFlmdTable + 8u * 8u + 4u) == 0u && read32(rdram, kFlmdTable + 2u * 8u + 4u) == kPDash &&
                        spare(runtime, kPDashCancel) && spare(runtime, kPDashTurn);
            for (const Hook &h : kHooks)
                fits = fits && runtime.hasFunction(h.address) && spare(runtime, h.copy);
            if (!fits)
            {
                std::fprintf(stderr, "[marz] PS2X_MARZ_QUICK_HANDLING: unexpected game code, not installed\n");
                return;
            }
            bool ok = runtime.replaceFunction(kPDashCancel, &pDashCancel) && runtime.replaceFunction(kPDashTurn, &pDashTurn);
            for (const Hook &h : kHooks)
                ok = ok && hook(runtime, h.address, h.copy, h.fn);
            if (ok)
            {
                write32(rdram, kFlmdTable + 8u * 8u + 4u, kPDashCancel);
                write32(rdram, kFlmdTable + 2u * 8u + 4u, kPDashTurn);
            }
            std::fprintf(stderr, "[marz] PS2X_MARZ_QUICK_HANDLING: %s\n", ok ? "on" : "failed to install");
        }
    }

    // PS2X_MARZ_WIDESCREEN=1: 16:9. InitScreen (0x14FEB0) calls njSetAspect(1.0, 1.0), its only caller; the
    // X aspect becomes 0.75, as in PCSX2's widescreen patch for this disc (SLPM-65303_E7DE50CA.pnach, which
    // sets the lui at 0x14FF74 to 0x3F40), and the picture is shown at 16:9 (as PS2X_ASPECT=16:9).
    //
    // The HUD and menus keep their 4:3 shape, squeezed to 0.75 about the screen centre (x' = 320 + (x - 320) * 0.75):
    // - Cell sprites (every 2D HUD/menu sprite, through njDrawCellSprite2D 0x1B4D10; njDrawCellStream2D calls it):
    //   X_gs = 2048 + Sx * (x - W/2), Sx = 0xA892B0, read there; Sx is 0.75x for the call. Its other readers are the
    //   full-screen quads (njDrawQuadDirectColorTexture 0x21C910) and njCharacter. Of the quads, only movie frames
    //   (Movie_DrawTexture) get the squeeze; the blur and noise quads redraw the wide 3D frame.
    // - HUD models (PutHudModel: gauges, lock-on sights, enemy marks, radar) are 3D and already get the aspect. The
    //   radar (PutMap 0x4608D0) moves the projection centre to its own position with njSetScreenCenter 0x1CD630, so that
    //   X is remapped; so are the scissor windows AddSetWindow 0x376050 sets for the HUD (AddHudChain) and the 2D text
    //   screens below, and the briefing map window (231,67)-(583,227) PutCellSpriteShirei passes to Clip2Dadj 0x3762F0
    //   (unsqueezed, the parts of the map sprites it cuts off showed as dark strips beside the map frame).
    // - SJIS text (libpfont, samplePuts 0x3D5E10): the screen matrix 0xD46290 FontInit builds (identity, X translation
    //   1728 = 2048 - 320 at +0x30) gets X scale 0.75 and translation 1808 before each string.
    namespace widescreen
    {
        constexpr uint32_t kNjSetAspect = 0x001CD340u, kOrigNjSetAspect = 0x006C8B34u; // copies in the quick:: spare range
        constexpr uint32_t kCellSprite = 0x001B4D10u, kOrigCellSprite = 0x006C8B4Cu, kUndoCellSprite = 0x006C8B50u;
        constexpr uint32_t kSamplePuts = 0x003D5E10u, kOrigSamplePuts = 0x006C8B54u;
        constexpr uint32_t kSetScreenCenter = 0x001CD630u, kOrigSetScreenCenter = 0x006C8B58u;
        constexpr uint32_t kAddSetWindow = 0x00376050u, kOrigAddSetWindow = 0x006C8B5Cu;
        constexpr uint32_t kQuad = 0x0021C910u, kOrigQuad = 0x006C8B60u; // njDrawQuadDirectColorTexture
        constexpr uint32_t kClip2Dadj = 0x003762F0u, kOrigClip2Dadj = 0x006C8B64u;
        constexpr uint32_t kSx = 0x00A892B0u, kFontScreen = 0x00D46290u;
        constexpr float kSqueeze = 0.75f;
        struct Range
        {
            uint32_t start, end;
        };
        constexpr Range kPutMap = {0x004608D0u, 0x00461DD8u};
        constexpr Range kMovieDrawTexture = {0x00512FB0u, 0x005130D0u};
        constexpr Range kPutRedGamen = {0x004746F0u, 0x00474FA8u}; // with PutRedGamen_EP6B_Continue
        constexpr Range kPutCellSpriteShirei = {0x004505F0u, 0x00452810u};
        // AddSetWindow callers whose windows hold 2D: AddHudChain, PutMissionIntroMessage, TR_PutBitmap, MS_Database.
        constexpr Range kWindowCallers[] = {
            {0x00456A60u, 0x00457354u}, {0x004957B0u, 0x004972A0u}, {0x004A9770u, 0x004A9CE8u}, {0x00526360u, 0x005273E4u}};

        bool within(uint32_t address, const Range &r) { return address >= r.start && address < r.end; }
        float squeeze(float x) { return 320.0f + (x - 320.0f) * kSqueeze; }
        float readFloat(const uint8_t *rdram, uint32_t address)
        {
            const uint32_t bits = quick::read32(rdram, address);
            float value = 0.0f;
            std::memcpy(&value, &bits, sizeof(value));
            return value;
        }
        void writeFloat(uint8_t *rdram, uint32_t address, float value)
        {
            uint32_t bits = 0u;
            std::memcpy(&bits, &value, sizeof(bits));
            quick::write32(rdram, address, bits);
        }

        void njSetAspect(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
        {
            (void)rdram;
            (void)runtime;
            ctx->f[12] *= 0.75f;
            ctx->pc = kOrigNjSetAspect;
        }

        struct SavedSx
        {
            uint32_t ra, sx;
        };
        std::vector<SavedSx> g_sx;
        // Runs the original with Sx squeezed; kUndoCellSprite puts Sx back when it returns.
        void squeezeSx(uint8_t *rdram, R5900Context *ctx, uint32_t original)
        {
            const uint32_t sx = quick::read32(rdram, kSx);
            g_sx.push_back({GPR_U32(ctx, 31), sx});
            writeFloat(rdram, kSx, readFloat(rdram, kSx) * kSqueeze);
            SET_GPR_U32(ctx, 31, kUndoCellSprite);
            ctx->pc = original;
        }
        // PutRedGamen's full-screen red hit flash (640x448) keeps full width.
        void cellSprite(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
        {
            (void)runtime;
            if (within(GPR_U32(ctx, 31), kPutRedGamen))
                ctx->pc = kOrigCellSprite;
            else
                squeezeSx(rdram, ctx, kOrigCellSprite);
        }
        // Movie frames (Movie_DrawTexture's (0,0)-(640,448) quad) are shown 4:3. The quad's other callers, ExecBlur and
        // MS_ExecNoise, redraw the wide 3D frame and keep full width.
        void quad(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
        {
            (void)runtime;
            if (within(GPR_U32(ctx, 31), kMovieDrawTexture))
                squeezeSx(rdram, ctx, kOrigQuad);
            else
                ctx->pc = kOrigQuad;
        }
        void undoCellSprite(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
        {
            (void)runtime;
            const SavedSx saved = g_sx.back();
            g_sx.pop_back();
            quick::write32(rdram, kSx, saved.sx);
            ctx->pc = saved.ra;
        }

        void samplePuts(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
        {
            (void)runtime;
            writeFloat(rdram, kFontScreen, kSqueeze);
            writeFloat(rdram, kFontScreen + 0x30u, 2048.0f - 320.0f * kSqueeze);
            ctx->pc = kOrigSamplePuts;
        }

        void setScreenCenter(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
        {
            (void)rdram;
            (void)runtime;
            if (within(GPR_U32(ctx, 31), kPutMap))
                ctx->f[12] = squeeze(ctx->f[12]);
            ctx->pc = kOrigSetScreenCenter;
        }

        void addSetWindow(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
        {
            (void)rdram;
            (void)runtime;
            const uint32_t ra = GPR_U32(ctx, 31);
            // AddHudChain also sets the full-screen and split-screen viewports ((0..319 or 320..639) x (0..447) and
            // (0,0)-(639,447)); the translucent store buffers drawn after the direct chains keep that scissor, so these
            // stay full width.
            const int32_t x0 = static_cast<int32_t>(GPR_U32(ctx, 4)), x1 = static_cast<int32_t>(GPR_U32(ctx, 6));
            const bool viewport = (x0 == 0 || x0 == 320) && (x1 == 319 || x1 == 639);
            for (const Range &r : kWindowCallers)
                if (!viewport && within(ra, r))
                {
                    for (int reg : {4, 6})
                    {
                        const float x = squeeze(static_cast<float>(static_cast<int32_t>(GPR_U32(ctx, reg))));
                        SET_GPR_U32(ctx, reg, static_cast<uint32_t>(static_cast<int32_t>(std::lround(x))));
                    }
                    break;
                }
            ctx->pc = kOrigAddSetWindow;
        }

        // Clip2Dadj(float rect[4]): PutCellSpriteShirei's rects are stack copies of 0x603A60 (0,0,639,447: full screen,
        // kept) and 0x603A70 (the map window), refilled on every call, so the window is squeezed in place.
        void clip2Dadj(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
        {
            (void)runtime;
            const uint32_t rect = GPR_U32(ctx, 4);
            if (within(GPR_U32(ctx, 31), kPutCellSpriteShirei) && !(readFloat(rdram, rect) == 0.0f && readFloat(rdram, rect + 8u) == 639.0f))
                for (uint32_t x : {rect, rect + 8u})
                    writeFloat(rdram, x, squeeze(readFloat(rdram, x)));
            ctx->pc = kOrigClip2Dadj;
        }

        void install(PS2Runtime &runtime)
        {
            const char *on = std::getenv("PS2X_MARZ_WIDESCREEN");
            if (!on || std::strcmp(on, "1") != 0)
                return;
            struct Hook
            {
                uint32_t address, copy;
                PS2Runtime::RecompiledFunction fn;
            };
            const Hook hooks[] = {{kNjSetAspect, kOrigNjSetAspect, &njSetAspect}, {kCellSprite, kOrigCellSprite, &cellSprite},
                                  {kSamplePuts, kOrigSamplePuts, &samplePuts}, {kSetScreenCenter, kOrigSetScreenCenter, &setScreenCenter},
                                  {kAddSetWindow, kOrigAddSetWindow, &addSetWindow}, {kQuad, kOrigQuad, &quad},
                                  {kClip2Dadj, kOrigClip2Dadj, &clip2Dadj}};
            bool fits = quick::spare(runtime, kUndoCellSprite);
            for (const Hook &h : hooks)
                fits = fits && runtime.hasFunction(h.address) && quick::spare(runtime, h.copy);
            bool ok = fits && runtime.replaceFunction(kUndoCellSprite, &undoCellSprite);
            for (const Hook &h : hooks)
                ok = ok && quick::hook(runtime, h.address, h.copy, h.fn);
            if (ok)
                runtime.setDisplayAspect(16.0f / 9.0f);
            std::fprintf(stderr, "[marz] PS2X_MARZ_WIDESCREEN: %s\n", ok ? "on" : fits ? "failed to install" : "unexpected game code, not installed");
        }
    }

    // PS2X_MARZ_VOOT_BUTTONS=1: Type A (pad preset 1: stick mode 1, turbo Square, target change Circle, L1/R1 turn)
    // laid out as the user's VOOT 5.66: Square and Circle are the two turbo buttons (table +0x0A -> bit 0x10, +0x0C
    // -> bit 0x01), L1 and R1 both change target (+0x12 -> bit 0x04, read by ChangeTarget 0x2281D0), and GetStick
    // (0x224B38, 0x224F28) no longer turns on L1/R1, only on the right stick. Each hook changes the table or pad word,
    // runs the original with ra pointing at an undo stub, and the stub puts it back, so the saved settings and other
    // presets are never changed.
    namespace voot
    {
        constexpr uint32_t kGetStick = 0x002249B0u, kGetButton = 0x00227500u, kGetButtonOn = 0x00227970u;
        constexpr uint32_t kOrigGetStick = 0x006C8B38u, kOrigGetButton = 0x006C8B3Cu, kOrigGetButtonOn = 0x006C8B40u;
        constexpr uint32_t kUndoStick = 0x006C8B44u, kUndoButton = 0x006C8B48u; // more of the quick:: spare range
        constexpr uint32_t kTableP1 = 0x00A248C0u, kTableP2 = 0x00A248D8u;
        constexpr uint32_t kPadP1 = 0x006C8DB0u, kPadP2 = 0x006C8DACu; // gp-0x76C0 / gp-0x76C4: pad entry, +8 = held
        constexpr uint16_t kTypeA = 1u, kSquare = 0x0080u, kCircle = 0x0020u, kL1R1 = 0x000Cu;
        constexpr uint32_t kMasks[3] = {0x0Au, 0x0Cu, 0x12u};
        constexpr uint16_t kVoot[3] = {kSquare, kCircle, kL1R1};

        struct SavedTable
        {
            uint32_t ra, table;
            uint16_t masks[3];
        };
        struct SavedPad
        {
            uint32_t ra, pad, removed;
        };
        std::vector<SavedTable> g_tables;
        std::vector<SavedPad> g_pads;

        // The table and pad GetButton/GetButtonOn/GetStick use for this robot, when it is a human on Type A. They
        // pick P2 for any non-zero plmode; gp-0x7834 == 4 with gp-0x7878 set is the split-pad mode (GetButton2,
        // GetStick2), which has its own fixed layout.
        bool typeA(const uint8_t *rdram, uint32_t robot, uint32_t &table, uint32_t &pad)
        {
            if (quick::read32(rdram, 0x006C8C3Cu) == 4u && quick::read32(rdram, 0x006C8BF8u) != 0u)
                return false;
            if (!quick::human(rdram, robot))
                return false;
            const bool p2 = quick::read8(rdram, robot + 0x50u) != 0u;
            table = p2 ? kTableP2 : kTableP1;
            pad = quick::read32(rdram, p2 ? kPadP2 : kPadP1);
            return quick::read16(rdram, table) == kTypeA && quick::read16(rdram, table + 2u) == 0u;
        }

        void button(uint8_t *rdram, R5900Context *ctx, uint32_t original)
        {
            uint32_t table = 0u, pad = 0u;
            if (typeA(rdram, GPR_U32(ctx, 4), table, pad))
            {
                SavedTable saved{GPR_U32(ctx, 31), table, {}};
                for (int i = 0; i < 3; ++i)
                {
                    saved.masks[i] = quick::read16(rdram, table + kMasks[i]);
                    quick::write16(rdram, table + kMasks[i], kVoot[i]);
                }
                g_tables.push_back(saved);
                SET_GPR_U32(ctx, 31, kUndoButton);
            }
            ctx->pc = original;
        }
        void getButton(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
        {
            (void)runtime;
            button(rdram, ctx, kOrigGetButton);
        }
        void getButtonOn(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
        {
            (void)runtime;
            button(rdram, ctx, kOrigGetButtonOn);
        }
        void undoButton(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
        {
            (void)runtime;
            const SavedTable saved = g_tables.back();
            g_tables.pop_back();
            for (int i = 0; i < 3; ++i)
                quick::write16(rdram, saved.table + kMasks[i], saved.masks[i]);
            ctx->pc = saved.ra;
        }

        void getStick(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
        {
            (void)runtime;
            uint32_t table = 0u, pad = 0u;
            if (typeA(rdram, GPR_U32(ctx, 4), table, pad) && pad != 0u)
            {
                const uint32_t held = quick::read32(rdram, pad + 8u);
                g_pads.push_back({GPR_U32(ctx, 31), pad, held & kL1R1});
                quick::write32(rdram, pad + 8u, held & ~static_cast<uint32_t>(kL1R1));
                SET_GPR_U32(ctx, 31, kUndoStick);
            }
            ctx->pc = kOrigGetStick;
        }
        void undoStick(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
        {
            (void)runtime;
            const SavedPad saved = g_pads.back();
            g_pads.pop_back();
            quick::write32(rdram, saved.pad + 8u, quick::read32(rdram, saved.pad + 8u) | saved.removed);
            ctx->pc = saved.ra;
        }

        void install(PS2Runtime &runtime)
        {
            const char *on = std::getenv("PS2X_MARZ_VOOT_BUTTONS");
            if (!on || std::strcmp(on, "1") != 0)
                return;
            bool fits = runtime.hasFunction(kGetStick) && runtime.hasFunction(kGetButton) && runtime.hasFunction(kGetButtonOn);
            for (uint32_t slot : {kOrigGetStick, kOrigGetButton, kOrigGetButtonOn, kUndoStick, kUndoButton})
                fits = fits && quick::spare(runtime, slot);
            bool ok = fits && runtime.replaceFunction(kUndoStick, &undoStick) && runtime.replaceFunction(kUndoButton, &undoButton) &&
                      quick::hook(runtime, kGetStick, kOrigGetStick, &getStick) &&
                      quick::hook(runtime, kGetButton, kOrigGetButton, &getButton) &&
                      quick::hook(runtime, kGetButtonOn, kOrigGetButtonOn, &getButtonOn);
            std::fprintf(stderr, "[marz] PS2X_MARZ_VOOT_BUTTONS: %s\n", ok ? "on" : fits ? "failed to install" : "unexpected game code, not installed");
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
        // Nor wait for the picture's vsync: while CRI's idle thread sits in that wait, a movie stop's border handshake
        // (adxm_goto_svr_border) stacks WakeupThread calls on it, and it later never sleeps (freeze at the title).
        // PS2X_MPEG_PICTURE_PACE=1 keeps the wait.
        ps2_stubs::g_mpegNoPicturePacing = std::getenv("PS2X_MPEG_PICTURE_PACE") == nullptr;
        // sceCdLayerSearchFile(file, name, layer) has no runtime handler, so the original code runs and
        // polls a CD RPC server that nothing answers. CRI's dvCi opens the CVM through it. The arguments
        // match sceCdSearchFile apart from the layer, which a single-layer image doesn't need.
        ps2_game_overrides::bindAddressHandler(runtime, 0x00116D48u, "sceCdSearchFile");
        // Text with single-byte ASCII (the English translation); PS2X_SJIS_ORIGINAL=1 keeps the game's SjisPuts.
        if (std::getenv("PS2X_SJIS_ORIGINAL") == nullptr && runtime.hasFunction(0x003D61A0u) && runtime.hasFunction(0x003D5E10u))
        {
            runtime.replaceFunction(0x003D61A0u, &sjis::puts);
        }
        quick::install(runtime);
        widescreen::install(runtime);
        voot::install(runtime);
        trace::install(runtime);
    }
}

PS2_REGISTER_GAME_OVERRIDE("Dennou Senki Virtual-On Marz (Japan)", "SLPM_653.03", kMarzEntry, 0u, applyMarzOverrides);
