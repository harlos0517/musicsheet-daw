#include "playback/MidiFile.h"

#include <algorithm>
#include <cmath>

#include "playback/JsMath.h"

namespace msd
{

namespace
{

using Bytes = std::vector<std::uint8_t>;

// What Uint8Array.from makes of a number: truncated, then modulo 256.
std::uint8_t byte(double value) { return static_cast<std::uint8_t>(static_cast<long long>(value) & 0xff); }

void append(Bytes& to, const Bytes& bytes) { to.insert(to.end(), bytes.begin(), bytes.end()); }

Bytes text(const std::string& value) // the string is UTF-8 already
{
    return Bytes(value.begin(), value.end());
}

Bytes uint(std::uint32_t value, int bytes)
{
    Bytes result;
    for (int i = 0; i < bytes; ++i)
        result.push_back(static_cast<std::uint8_t>((value >> (8 * (bytes - 1 - i))) & 0xff));
    return result;
}

// MIDI variable-length quantity.
Bytes vlq(std::uint32_t value)
{
    Bytes result { static_cast<std::uint8_t>(value & 0x7f) };
    for (std::uint32_t rest = value >> 7; rest > 0; rest >>= 7)
        result.insert(result.begin(), static_cast<std::uint8_t>((rest & 0x7f) | 0x80));
    return result;
}

Bytes meta(std::uint8_t type, const Bytes& data)
{
    Bytes result { 0xff, type };
    append(result, vlq(static_cast<std::uint32_t>(data.size())));
    append(result, data);
    return result;
}

Bytes chunk(const std::string& id, const Bytes& data)
{
    Bytes result = text(id);
    append(result, uint(static_cast<std::uint32_t>(data.size()), 4));
    append(result, data);
    return result;
}

struct TimedBytes
{
    double tick = 0;
    Bytes bytes;
};

Bytes trackChunk(const std::vector<TimedBytes>& events)
{
    Bytes data;
    double last = 0;
    for (const TimedBytes& event : events)
    {
        const double tick = std::max(last, jsRound(event.tick));
        append(data, vlq(static_cast<std::uint32_t>(tick - last)));
        append(data, event.bytes);
        last = tick;
    }
    data.push_back(0);
    append(data, meta(0x2f, {}));
    return chunk("MTrk", data);
}

struct Signature
{
    double beats = 4;
    double unit = 4;
};

Signature soundingTimeSignature(const MeasureInfo& info, int tpq)
{
    const Signature written { static_cast<double>(info.timeSig.beats), static_cast<double>(info.timeSig.unit) };
    if (info.lengthTicks == written.beats * tpq * 4 / written.unit)
        return written;
    for (int unit = 1; unit <= 64; unit *= 2)
    {
        const double beats = info.lengthTicks * unit / (tpq * 4);
        if (beats == std::floor(beats) && beats >= 1 && beats <= 255)
            return { beats, static_cast<double>(unit) };
    }
    return written;
}

std::uint8_t status(MidiMessageType type)
{
    switch (type)
    {
    case MidiMessageType::NoteOff: return 0x80;
    case MidiMessageType::NoteOn: return 0x90;
    case MidiMessageType::Cc: return 0xb0;
    }
    return 0;
}

} // namespace

std::vector<std::uint8_t> writeMidiFile(const std::string& title, int tpq, const TempoMap& tempo,
                                        const std::vector<PlayedMeasure>& played,
                                        const std::vector<MeasureInfo>& timeline,
                                        const std::vector<RenderedTrack>& tracks)
{
    std::vector<TimedBytes> conductor { { 0, meta(0x03, text(title)) } };
    bool first = true;
    Signature signature;
    for (const PlayedMeasure& measure : played)
    {
        const Signature sounding = soundingTimeSignature(timeline[measure.measureIndex], tpq);
        if (!first && sounding.beats == signature.beats && sounding.unit == signature.unit)
            continue;
        first = false;
        signature = sounding;
        conductor.push_back(
            { measure.startTick, meta(0x58, { byte(sounding.beats), byte(std::log2(sounding.unit)), 24, 8 }) });
    }
    for (const TempoPoint& point : tempo.points())
    {
        const double microsPerQuarter = jsRound(60000000 / point.qpm);
        conductor.push_back({ point.tick, meta(0x51, uint(static_cast<std::uint32_t>(microsPerQuarter), 3)) });
    }
    std::stable_sort(conductor.begin(), conductor.end(),
                     [](const TimedBytes& a, const TimedBytes& b) { return a.tick < b.tick; });

    std::vector<Bytes> chunks { trackChunk(conductor) };
    for (const RenderedTrack& track : tracks)
    {
        struct Event
        {
            double tick;
            int order;
            Bytes bytes;
        };
        std::vector<Event> events;
        events.reserve(track.messages.size());
        for (const MidiMessage& message : track.messages)
            events.push_back({ jsRound(tempo.msToTick(message.timeMs)),
                               messageOrder(message),
                               { static_cast<std::uint8_t>(status(message.type) | byte(message.channel - 1)),
                                 byte(message.data1), byte(message.data2) } });
        std::stable_sort(events.begin(), events.end(), [](const Event& a, const Event& b) {
            return a.tick != b.tick ? a.tick < b.tick : a.order < b.order;
        });

        std::vector<TimedBytes> timed { { 0, meta(0x03, text(track.name)) } };
        timed.reserve(events.size() + 1);
        for (Event& event : events)
            timed.push_back({ event.tick, std::move(event.bytes) });
        chunks.push_back(trackChunk(timed));
    }

    Bytes data = uint(1, 2);
    append(data, uint(static_cast<std::uint32_t>(chunks.size()), 2));
    append(data, uint(static_cast<std::uint32_t>(tpq), 2));
    Bytes file = chunk("MThd", data);
    for (const Bytes& c : chunks)
        append(file, c);
    return file;
}

} // namespace msd
