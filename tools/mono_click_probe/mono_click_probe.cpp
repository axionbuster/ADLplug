#include "opnmidi.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

namespace {
struct Event {
    double time = 0.0;
    std::string type;
    int channel = 0;
    int a = 0;
    int b = 0;
};

struct MonoNote {
    uint8_t pitch = 0;
    uint8_t velocity = 0;
};

struct PendingMonoNoteOn {
    bool active = false;
    uint8_t pitch = 0;
    uint8_t velocity = 0;
    bool port = false;
    uint8_t port_time = 0;
    unsigned remaining_samples = 0;
};

struct Spike {
    double time = 0.0;
    double ratio = 0.0;
    double peak_delta = 0.0;
};

struct Player {
    explicit Player(unsigned sample_rate)
        : device(opn2_init(sample_rate))
    {}

    ~Player()
    {
        if (device)
            opn2_close(device);
    }

    OPN2_MIDIPlayer *device = nullptr;
};

void send_midi3(Player &player, uint8_t status, uint8_t d1, uint8_t d2)
{
    switch (status >> 4) {
    case 0x8:
        opn2_rt_noteOff(player.device, status & 0x0f, d1);
        break;
    case 0x9:
        if (d2 == 0)
            opn2_rt_noteOff(player.device, status & 0x0f, d1);
        else
            opn2_rt_noteOn(player.device, status & 0x0f, d1, d2);
        break;
    case 0xB:
        opn2_rt_controllerChange(player.device, status & 0x0f, d1, d2);
        break;
    default:
        break;
    }
}

bool set_emulator(Player &player, const std::string &needle)
{
    for (int i = 0; i < 32; ++i) {
        if (opn2_switchEmulator(player.device, i) < 0)
            continue;
        const char *name = opn2_chipEmulatorName(player.device);
        if (!name)
            continue;
        std::string lower(name);
        std::transform(lower.begin(), lower.end(), lower.begin(),
                       [](unsigned char c) { return (char)std::tolower(c); });
        if (lower.find(needle) != std::string::npos)
            return true;
    }
    return false;
}

std::vector<Event> load_events(const char *path)
{
    std::ifstream in(path);
    if (!in)
        throw std::runtime_error("failed to open events file");

    std::vector<Event> events;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty())
            continue;
        std::istringstream ss(line);
        Event ev;
        ss >> ev.time >> ev.type >> ev.channel >> ev.a >> ev.b;
        events.push_back(ev);
    }
    return events;
}

void write_wav(const char *path, const std::vector<float> &left, const std::vector<float> &right, unsigned sample_rate)
{
    std::ofstream out(path, std::ios::binary);
    if (!out)
        throw std::runtime_error("failed to open wav output");

    const uint32_t frames = (uint32_t)left.size();
    const uint16_t channels = 2;
    const uint16_t bits_per_sample = 16;
    const uint32_t byte_rate = sample_rate * channels * bits_per_sample / 8;
    const uint16_t block_align = channels * bits_per_sample / 8;
    const uint32_t data_size = frames * block_align;
    const uint32_t riff_size = 36 + data_size;

    auto write_u16 = [&out](uint16_t v) { out.write(reinterpret_cast<const char *>(&v), sizeof(v)); };
    auto write_u32 = [&out](uint32_t v) { out.write(reinterpret_cast<const char *>(&v), sizeof(v)); };

    out.write("RIFF", 4);
    write_u32(riff_size);
    out.write("WAVE", 4);
    out.write("fmt ", 4);
    write_u32(16);
    write_u16(1);
    write_u16(channels);
    write_u32(sample_rate);
    write_u32(byte_rate);
    write_u16(block_align);
    write_u16(bits_per_sample);
    out.write("data", 4);
    write_u32(data_size);

    for (uint32_t i = 0; i < frames; ++i) {
        auto to_i16 = [](float x) {
            x = std::max(-1.0f, std::min(1.0f, x));
            return (int16_t)std::lrintf(x * 32767.0f);
        };
        int16_t ls = to_i16(left[i]);
        int16_t rs = to_i16(right[i]);
        out.write(reinterpret_cast<const char *>(&ls), sizeof(ls));
        out.write(reinterpret_cast<const char *>(&rs), sizeof(rs));
    }
}

} // namespace

int main(int argc, char **argv)
{
    if (argc != 6 && argc != 7) {
        std::cerr << "usage: mono_click_probe <bank.wopn> <events.tsv> <out.wav> <seconds> <mono|poly> [emulator-substring]\n";
        return 2;
    }

    const char *bank_path = argv[1];
    const char *events_path = argv[2];
    const char *wav_path = argv[3];
    const double duration_seconds = std::atof(argv[4]);
    const std::string mode = argv[5];
    const std::string emulator = argc == 7 ? argv[6] : "mame";

    constexpr unsigned sample_rate = 44100;
    constexpr unsigned channel = 1;
    constexpr unsigned mono_delay_samples = 44;
    constexpr uint8_t program = 56;
    const bool mono_mode = mode != "poly";
    constexpr bool portamento = false;
    constexpr uint8_t portamento_time = 20;

    Player player(sample_rate);
    if (!player.device) {
        std::cerr << "failed to init OPNMIDI\n";
        return 1;
    }
    if (!set_emulator(player, emulator))
        std::cerr << "warning: failed to select emulator containing '" << emulator << "', using current default\n";
    opn2_setNumChips(player.device, 2);
    opn2_setSoftPanEnabled(player.device, 1);
    opn2_setLoopEnabled(player.device, 0);
    if (opn2_openBankFile(player.device, bank_path) < 0) {
        std::cerr << "bank open failed: " << opn2_errorInfo(player.device) << "\n";
        return 1;
    }
    std::cout << "emulator\t" << opn2_chipEmulatorName(player.device) << "\n";

    opn2_rt_patchChange(player.device, channel, program);

    std::vector<Event> events = load_events(events_path);
    const unsigned total_frames = (unsigned)std::ceil(duration_seconds * sample_rate);
    std::vector<float> left(total_frames);
    std::vector<float> right(total_frames);
    std::vector<unsigned> event_frames;
    std::vector<MonoNote> mono_note_stack[16];
    int mono_sounding[16];
    std::fill(std::begin(mono_sounding), std::end(mono_sounding), -1);
    PendingMonoNoteOn pending[16];

    OPNMIDI_AudioFormat format {};
    format.type = OPNMIDI_SampleType_F32;
    format.containerSize = sizeof(float);
    format.sampleOffset = sizeof(float);

    auto schedule_pending = [&](unsigned ch, uint8_t pitch, uint8_t velocity) {
        pending[ch].active = true;
        pending[ch].pitch = pitch;
        pending[ch].velocity = velocity;
        pending[ch].port = portamento;
        pending[ch].port_time = portamento_time;
        pending[ch].remaining_samples = mono_delay_samples;
    };

    auto cancel_pending = [&](unsigned ch) { pending[ch].active = false; };

    auto trigger_pending = [&](unsigned ch) {
        if (!pending[ch].active)
            return;
        send_midi3(player, uint8_t(0xB0 | ch), 65, pending[ch].port ? 127 : 0);
        if (pending[ch].port)
            send_midi3(player, uint8_t(0xB0 | ch), 5, pending[ch].port_time);
        opn2_rt_noteOn(player.device, ch, pending[ch].pitch, pending[ch].velocity);
        mono_sounding[ch] = pending[ch].pitch;
        pending[ch].active = false;
    };

    auto handle_event = [&](const Event &ev) {
        event_frames.push_back((unsigned)std::lround(ev.time * sample_rate));

        if (ev.type == "control_change") {
            opn2_rt_controllerChange(player.device, ev.channel, ev.a, ev.b);
            return;
        }
        if (ev.type == "pitchwheel") {
            const int value = ev.a + 8192;
            opn2_rt_pitchBendML(player.device, ev.channel, (value >> 7) & 0x7F, value & 0x7F);
            return;
        }

        const bool is_note_on = ev.type == "note_on" && ev.b > 0;
        const bool is_note_off = ev.type == "note_off" || (ev.type == "note_on" && ev.b == 0);
        if (!mono_mode || (!is_note_on && !is_note_off)) {
            if (is_note_on)
                opn2_rt_noteOn(player.device, ev.channel, ev.a, ev.b);
            else if (is_note_off)
                opn2_rt_noteOff(player.device, ev.channel, ev.a);
            return;
        }

        auto &stack = mono_note_stack[ev.channel];
        int &sound = mono_sounding[ev.channel];
        const uint8_t pitch = (uint8_t)ev.a;
        const uint8_t vel = (uint8_t)ev.b;

        if (is_note_on) {
            stack.erase(std::remove_if(stack.begin(), stack.end(),
                        [pitch](const MonoNote &n) { return n.pitch == pitch; }),
                        stack.end());
            MonoNote note;
            note.pitch = pitch;
            note.velocity = vel;
            stack.push_back(note);

            if (pending[ev.channel].active) {
                schedule_pending(ev.channel, pitch, vel);
            } else if (sound >= 0 && sound != (int)pitch) {
                opn2_rt_noteOffFast(player.device, ev.channel, (uint8_t)sound);
                sound = -1;
                schedule_pending(ev.channel, pitch, vel);
            } else if (sound < 0) {
                send_midi3(player, uint8_t(0xB0 | ev.channel), 65, portamento ? 127 : 0);
                if (portamento)
                    send_midi3(player, uint8_t(0xB0 | ev.channel), 5, portamento_time);
                opn2_rt_noteOn(player.device, ev.channel, pitch, vel);
                sound = pitch;
            }
            return;
        }

        stack.erase(std::remove_if(stack.begin(), stack.end(),
                    [pitch](const MonoNote &n) { return n.pitch == pitch; }),
                    stack.end());

        if (pending[ev.channel].active && pending[ev.channel].pitch == pitch) {
            if (!stack.empty())
                schedule_pending(ev.channel, stack.back().pitch, stack.back().velocity);
            else
                cancel_pending(ev.channel);
        }

        if (sound == (int)pitch) {
            if (!stack.empty()) {
                opn2_rt_noteOffFast(player.device, ev.channel, pitch);
                sound = -1;
                schedule_pending(ev.channel, stack.back().pitch, stack.back().velocity);
            } else {
                opn2_rt_noteOff(player.device, ev.channel, pitch);
                sound = -1;
                cancel_pending(ev.channel);
            }
        }
    };

    unsigned frame = 0;
    size_t next_event = 0;
    while (frame < total_frames) {
        const unsigned next_event_frame = next_event < events.size()
            ? std::min(total_frames, (unsigned)std::lround(events[next_event].time * sample_rate))
            : total_frames;

        unsigned advance = next_event_frame > frame ? (next_event_frame - frame) : 0;
        for (unsigned ch = 0; ch < 16; ++ch)
            if (pending[ch].active)
                advance = std::min(advance, pending[ch].remaining_samples);

        if (advance == 0 && next_event >= events.size()) {
            for (unsigned ch = 0; ch < 16; ++ch)
                if (pending[ch].active && pending[ch].remaining_samples == 0)
                    trigger_pending(ch);
            const unsigned remain = total_frames - frame;
            opn2_generateFormat(player.device, (int)(remain * 2),
                                (OPN2_UInt8 *)&left[frame], (OPN2_UInt8 *)&right[frame], &format);
            break;
        }

        if (advance > 0) {
            opn2_generateFormat(player.device, (int)(advance * 2),
                                (OPN2_UInt8 *)&left[frame], (OPN2_UInt8 *)&right[frame], &format);
            frame += advance;
            for (unsigned ch = 0; ch < 16; ++ch)
                if (pending[ch].active)
                    pending[ch].remaining_samples -= advance;
        }

        while (next_event < events.size() &&
               (unsigned)std::lround(events[next_event].time * sample_rate) <= frame) {
            handle_event(events[next_event]);
            ++next_event;
        }
        for (unsigned ch = 0; ch < 16; ++ch)
            if (pending[ch].active && pending[ch].remaining_samples == 0)
                trigger_pending(ch);
    }

    write_wav(wav_path, left, right, sample_rate);

    std::vector<double> deltas;
    deltas.reserve(total_frames);
    for (unsigned i = 1; i < total_frames; ++i) {
        const double dl = std::fabs((double)left[i] - (double)left[i - 1]);
        const double dr = std::fabs((double)right[i] - (double)right[i - 1]);
        deltas.push_back(std::max(dl, dr));
    }
    std::vector<double> sorted = deltas;
    std::sort(sorted.begin(), sorted.end());
    const double baseline = sorted.empty() ? 0.0 : sorted[sorted.size() / 2];

    std::vector<Spike> spikes;
    for (unsigned ef : event_frames) {
        if (ef == 0 || ef >= total_frames)
            continue;
        const unsigned start = ef > 32 ? ef - 32 : 1;
        const unsigned end = std::min(total_frames - 1, ef + 32);
        double peak = 0.0;
        for (unsigned i = start; i <= end; ++i) {
            const double dl = std::fabs((double)left[i] - (double)left[i - 1]);
            const double dr = std::fabs((double)right[i] - (double)right[i - 1]);
            peak = std::max(peak, std::max(dl, dr));
        }
        Spike spike;
        spike.time = ef / (double)sample_rate;
        spike.ratio = baseline > 0.0 ? peak / baseline : 0.0;
        spike.peak_delta = peak;
        spikes.push_back(spike);
    }
    std::sort(spikes.begin(), spikes.end(), [](const Spike &a, const Spike &b) { return a.ratio > b.ratio; });

    std::cout << "rendered_frames\t" << total_frames << "\n";
    std::cout << "median_delta\t" << baseline << "\n";
    for (size_t i = 0; i < std::min<size_t>(spikes.size(), 8); ++i)
        std::cout << "spike\t" << spikes[i].time << "\t" << spikes[i].peak_delta << "\t" << spikes[i].ratio << "\n";
    return 0;
}
