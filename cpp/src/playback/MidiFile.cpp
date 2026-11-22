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

// MIDI variable-length quantity: seven bits a byte, the highest first, every byte but
// the last with its top bit set.
void appendVlq(Bytes& to, std::uint32_t value)
{
    std::uint8_t reversed[5];
    int count = 0;
    reversed[count++] = static_cast<std::uint8_t>(value & 0x7f);
    for (std::uint32_t rest = value >> 7; rest > 0; rest >>= 7)
        reversed[count++] = static_cast<std::uint8_t>((rest & 0x7f) | 0x80);
    while (count > 0)
        to.push_back(reversed[--count]);
}

Bytes meta(std::uint8_t type, const Bytes& data)
{
    Bytes result { 0xff, type };
    appendVlq(result, static_cast<std::uint32_t>(data.size()));
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

// A track as it is written, event by event: the time since the last one (never less
// than nothing), then the event's bytes. Written into one buffer, so that a track of a
// million messages is not a million small vectors.
class TrackData
{
public:
    explicit TrackData(std::size_t events) { data_.reserve(events * 4 + 32); }

    void add(double tick, const std::uint8_t* bytes, std::size_t size)
    {
        const double at = std::max(last_, jsRound(tick));
        appendVlq(data_, static_cast<std::uint32_t>(at - last_));
        data_.insert(data_.end(), bytes, bytes + size);
        last_ = at;
    }
    void add(double tick, const Bytes& bytes) { add(tick, bytes.data(), bytes.size()); }

    Bytes chunk()
    {
        data_.push_back(0);
        append(data_, meta(0x2f, {}));
        return msd::chunk("MTrk", data_);
    }

private:
    Bytes data_;
    double last_ = 0;
};

Bytes trackChunk(const std::vector<TimedBytes>& events)
{
    TrackData data(events.size());
    for (const TimedBytes& event : events)
        data.add(event.tick, event.bytes);
    return data.chunk();
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
            std::uint8_t bytes[3];
        };
        std::vector<Event> events;
        events.reserve(track.messages.size());
        TempoMap::Cursor cursor(tempo); // the messages are in time order
        for (const MidiMessage& message : track.messages)
            events.push_back({ jsRound(cursor.msToTick(message.timeMs)),
                               messageOrder(message),
                               { static_cast<std::uint8_t>(status(message.type) | byte(message.channel - 1)),
                                 byte(message.data1), byte(message.data2) } });
        std::stable_sort(events.begin(), events.end(), [](const Event& a, const Event& b) {
            return a.tick != b.tick ? a.tick < b.tick : a.order < b.order;
        });

        TrackData data(events.size() + 1);
        data.add(0, meta(0x03, text(track.name)));
        for (const Event& event : events)
            data.add(event.tick, event.bytes, 3);
        chunks.push_back(data.chunk());
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
