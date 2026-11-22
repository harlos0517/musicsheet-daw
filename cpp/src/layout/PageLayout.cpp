#include "layout/PageLayout.h"

#include <algorithm>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string_view>

namespace msd
{
namespace
{

// The page in sp.
struct PageGeometry
{
    double width = 0;
    double height = 0;
    double left = 0; // margins
    double top = 0;
    double contentWidth = 0;
    double contentHeight = 0;
};

PageGeometry pageGeometry(const LayoutProfile& profile)
{
    const double mm = profile.staffSpaceMm;
    if (!(mm > 0))
        throw std::invalid_argument("layout profile \"" + profile.id + "\" has no staff size");
    const LayoutProfile::Page& page = profile.page;
    PageGeometry g;
    g.width = page.widthMm / mm;
    g.height = page.heightMm / mm;
    g.left = page.margins.left / mm;
    g.top = page.margins.top / mm;
    g.contentWidth = (page.widthMm - page.margins.left - page.margins.right) / mm;
    g.contentHeight = (page.heightMm - page.margins.top - page.margins.bottom) / mm;
    return g;
}

struct StaffEntry
{
    NodeId staffId;
    const Track* track = nullptr;
    int lines = 5;
};

std::vector<StaffEntry> staffEntries(const MusicData& music, const Layout& layout)
{
    std::vector<const Track*> tracks;
    if (layout.trackIds)
    {
        for (const NodeId& id : *layout.trackIds)
            for (const Track& track : music.tracks)
                if (track.id == id)
                    tracks.push_back(&track);
    }
    else
    {
        for (const Track& track : music.tracks)
            tracks.push_back(&track);
    }

    std::vector<StaffEntry> entries;
    for (const Track* track : tracks)
        for (const Staff& staff : track->staves)
            entries.push_back({ staff.id, track, staff.lines.value_or(5) });
    return entries;
}

double staffHeight(const StaffEntry& entry) { return std::max(0, entry.lines - 1); }

std::map<std::string_view, std::size_t> measureIndices(const MusicData& music)
{
    std::map<std::string_view, std::size_t> indices;
    for (std::size_t i = 0; i < music.measures.size(); ++i)
        indices.emplace(music.measures[i].id, i);
    return indices;
}

// The user's layout elements that the reflow keeps, by measure index.
struct Elements
{
    std::map<std::size_t, BreakType> breakAfter;
    std::set<std::size_t> noBreakAfter;
    struct Gap
    {
        std::size_t from; // measure index
        double extraSp;
    };
    std::map<NodeId, std::vector<Gap>> staffGaps; // by staff ID, in measure order

    // The extra room below a staff on a system starting at `measure`.
    double gapBelow(const NodeId& staffId, std::size_t measure) const
    {
        const auto it = staffGaps.find(staffId);
        if (it == staffGaps.end())
            return 0;
        double extra = 0;
        for (const Gap& gap : it->second)
            if (gap.from <= measure)
                extra = gap.extraSp;
        return extra;
    }
};

// Lets std::visit take one lambda per alternative.
template <class... Ts>
struct Overloaded : Ts...
{
    using Ts::operator()...;
};

Elements collectElements(const Layout& layout, const std::map<std::string_view, std::size_t>& indices)
{
    Elements elements;
    auto indexOf = [&](const NodeId& id) -> std::optional<std::size_t> {
        const auto it = indices.find(id);
        return it == indices.end() ? std::nullopt : std::optional(it->second);
    };
    for (const LayoutElement& element : layout.elements)
        std::visit(Overloaded {
                       [&](const BreakElement& e) {
                           if (const auto i = indexOf(e.nodeId))
                           {
                               // A page break wins over a system break on the same measure.
                               BreakType& type = elements.breakAfter.try_emplace(*i, e.type).first->second;
                               if (e.type == BreakType::Page)
                                   type = BreakType::Page;
                           }
                       },
                       [&](const NoBreakElement& e) {
                           if (const auto i = indexOf(e.nodeId))
                               elements.noBreakAfter.insert(*i);
                       },
                       [&](const StaffGapElement& e) {
                           if (const auto i = indexOf(e.nodeId))
                               elements.staffGaps[e.staffId].push_back({ *i, e.extraSp });
                       },
                       [](const auto&) {},
                   },
                   element);
    for (auto& [staffId, gaps] : elements.staffGaps)
        std::stable_sort(gaps.begin(), gaps.end(), [](const auto& a, const auto& b) { return a.from < b.from; });
    return elements;
}

// Each staff's top line in a system starting at measure `start`; returns the system's
// height, from the first staff's top line to the last one's bottom line.
double placeStaves(const std::vector<StaffEntry>& entries, const LayoutProfile::Distances& distances,
                   const Elements& elements, std::size_t start, std::map<NodeId, double>& staffYs)
{
    double y = 0;
    for (std::size_t s = 0; s < entries.size(); ++s)
    {
        if (s > 0)
        {
            const StaffEntry& previous = entries[s - 1];
            y += staffHeight(previous) + (previous.track == entries[s].track ? distances.staffSp : distances.trackSp)
                 + elements.gapBelow(previous.staffId, start);
        }
        staffYs[entries[s].staffId] = y;
    }
    return entries.empty() ? 0.0 : y + staffHeight(entries.back());
}

std::vector<SystemStaff> unplacedStaves(const std::vector<StaffEntry>& entries)
{
    std::vector<SystemStaff> staves;
    for (const StaffEntry& entry : entries)
        staves.push_back({ entry.staffId, 0 });
    return staves;
}

} // namespace

std::vector<NodeId> layoutStaffIds(const MusicData& music, const Layout& layout)
{
    std::vector<NodeId> ids;
    for (const StaffEntry& entry : staffEntries(music, layout))
        ids.push_back(entry.staffId);
    return ids;
}

double systemHeightSp(const MusicData& music, const Layout& layout, const LayoutProfile& profile)
{
    std::map<NodeId, double> staffYs;
    return placeStaves(staffEntries(music, layout), profile.distances, {}, 0, staffYs);
}

bool systemFitsPage(const MusicData& music, const Layout& layout, const LayoutProfile& profile)
{
    return systemHeightSp(music, layout, profile) <= pageGeometry(profile).contentHeight;
}

Flow reflow(const ScoreInput& in)
{
    const PageGeometry page = pageGeometry(in.profile);
    const std::vector<StaffEntry> entries = staffEntries(in.music, in.layout);
    const std::size_t count = in.music.measures.size();
    if (entries.empty() || count == 0)
        return {};

    const auto indices = measureIndices(in.music);
    const Elements elements = collectElements(in.layout, indices);
    const std::vector<MeasureWidth> widths = measureWidths({ .music = in.music,
                                                             .profile = in.profile,
                                                             .font = in.font,
                                                             .staves = unplacedStaves(entries),
                                                             .firstMeasure = 0,
                                                             .measureCount = count,
                                                             .header = in.header,
                                                             .notes = in.notes,
                                                             .beams = in.beams });

    // Systems: [start, end) measure ranges.
    std::vector<std::pair<std::size_t, std::size_t>> systems;
    for (std::size_t i = 0; i < count;)
    {
        const double available = page.contentWidth - (systems.empty() ? in.profile.distances.firstIndentSp : 0.0);
        const std::size_t start = i;
        double total = widths[i].first;
        ++i;
        while (i < count)
        {
            if (elements.breakAfter.contains(i - 1))
                break;
            const bool keep = elements.noBreakAfter.contains(i - 1);
            if (!keep && total + widths[i].other > available)
                break;
            total += widths[i].other;
            ++i;
        }
        systems.push_back({ start, i });
    }

    // Staves within each system, and systems on pages.
    const LayoutProfile::Distances& distances = in.profile.distances;
    Flow flow;
    FlowPage current;
    double y = 0;
    for (std::size_t k = 0; k < systems.size(); ++k)
    {
        const std::size_t start = systems[k].first;
        FlowSystem system;
        system.startMeasureId = in.music.measures[start].id;
        const double height = placeStaves(entries, distances, elements, start, system.staffYSp);

        const auto breakBefore =
            k > 0 ? elements.breakAfter.find(systems[k - 1].second - 1) : elements.breakAfter.end();
        const bool pageBreak = breakBefore != elements.breakAfter.end() && breakBefore->second == BreakType::Page;
        if (!current.systems.empty() && (pageBreak || y + height > page.contentHeight))
        {
            flow.pages.push_back(std::move(current));
            current = {};
            y = 0;
        }
        system.ySp = y;
        current.systems.push_back(std::move(system));
        y += height + distances.systemSp;
    }
    flow.pages.push_back(std::move(current));
    return flow;
}

namespace
{

// The systems of a flow that still have their start measure, and how to lay out each.
class FlowSystems
{
public:
    FlowSystems(const ScoreInput& in, const Flow& flow)
        : in(in)
        , page(pageGeometry(in.profile))
        , entries(staffEntries(in.music, in.layout))
        , count(in.music.measures.size())
    {
        if (entries.empty() || count == 0 || flow.pages.empty())
            return;
        const auto indices = measureIndices(in.music);
        // In order. The first always starts at the first measure, so measures added
        // before it have a place.
        for (std::size_t p = 0; p < flow.pages.size(); ++p)
            for (const FlowSystem& system : flow.pages[p].systems)
            {
                const auto it = indices.find(system.startMeasureId);
                if (it == indices.end())
                    continue;
                if (!starts.empty() && it->second <= starts.back().measure)
                    continue; // out of order: its measures stay with the system before
                starts.push_back({ p, &system, it->second });
            }
        if (!starts.empty())
            starts.front().measure = 0;
    }

    std::size_t size() const { return starts.size(); }
    std::size_t pageOf(std::size_t k) const { return starts[k].page; }
    std::size_t firstMeasure(std::size_t k) const { return starts[k].measure; }
    std::size_t endMeasure(std::size_t k) const { return k + 1 < starts.size() ? starts[k + 1].measure : count; }
    const PageGeometry& geometry() const { return page; }

    PlacedSystem layout(std::size_t k) const
    {
        const Start& start = starts[k];

        // Staves where the flow put them; one it doesn't know goes below the others.
        std::vector<SystemStaff> staves;
        double bottom = 0;
        for (const StaffEntry& entry : entries)
        {
            const auto it = start.system->staffYSp.find(entry.staffId);
            if (it != start.system->staffYSp.end())
                bottom = std::max(bottom, it->second + staffHeight(entry));
        }
        for (const StaffEntry& entry : entries)
        {
            const auto it = start.system->staffYSp.find(entry.staffId);
            if (it != start.system->staffYSp.end())
                staves.push_back({ entry.staffId, it->second });
            else
            {
                const double y = staves.empty() && bottom == 0 ? 0.0 : bottom + in.profile.distances.trackSp;
                staves.push_back({ entry.staffId, y });
                bottom = y + staffHeight(entry);
            }
        }

        const double indent = in.profile.distances.firstIndentSp;
        const bool firstSystem = k == 0;
        const double available = page.contentWidth - (firstSystem ? indent : 0.0);
        SystemInput system { .music = in.music,
                             .profile = in.profile,
                             .font = in.font,
                             .staves = std::move(staves),
                             .firstMeasure = start.measure,
                             .measureCount = endMeasure(k) - start.measure,
                             .width = available,
                             .header = in.header,
                             .notes = in.notes,
                             .beams = in.beams };
        SystemLayout layout;
        if (k + 1 == starts.size())
        {
            // The last system keeps its natural width unless it is nearly full.
            system.width.reset();
            layout = layoutSystem(system);
            if (layout.width >= in.profile.breaking.lastSystemFillMin * available)
            {
                system.width = available;
                layout = layoutSystem(system);
            }
        }
        else
        {
            layout = layoutSystem(system);
        }

        const Point origin { page.left + (firstSystem ? indent : 0.0), page.top + start.system->ySp };
        return { origin, std::move(layout) };
    }

private:
    struct Start
    {
        std::size_t page;
        const FlowSystem* system;
        std::size_t measure;
    };

    const ScoreInput& in;
    PageGeometry page;
    std::vector<StaffEntry> entries;
    std::size_t count;
    std::vector<Start> starts;
};

} // namespace

std::vector<PageLayout> layoutPages(const ScoreInput& in, const Flow& flow)
{
    const FlowSystems systems(in, flow);
    if (systems.size() == 0)
        return {};
    std::vector<PageLayout> pages(flow.pages.size());
    for (PageLayout& p : pages)
    {
        p.widthSp = systems.geometry().width;
        p.heightSp = systems.geometry().height;
    }
    for (std::size_t k = 0; k < systems.size(); ++k)
        pages[systems.pageOf(k)].systems.push_back(systems.layout(k));
    return pages;
}

bool relayoutMeasures(const ScoreInput& in, const Flow& flow, const std::vector<NodeId>& measureIds,
                      std::vector<PageLayout>& pages)
{
    const FlowSystems systems(in, flow);
    if (pages.size() != flow.pages.size())
        return false;
    std::vector<std::size_t> perPage(pages.size(), 0);
    for (std::size_t k = 0; k < systems.size(); ++k)
        ++perPage[systems.pageOf(k)];
    for (std::size_t p = 0; p < pages.size(); ++p)
        if (pages[p].systems.size() != perPage[p])
            return false;

    const auto indices = measureIndices(in.music);
    std::vector<std::size_t> changed;
    for (const NodeId& id : measureIds)
    {
        const auto it = indices.find(id);
        if (it != indices.end())
            changed.push_back(it->second);
    }

    std::vector<std::size_t> onPage(pages.size(), 0); // systems seen on each page so far
    for (std::size_t k = 0; k < systems.size(); ++k)
    {
        const std::size_t p = systems.pageOf(k);
        const std::size_t index = onPage[p]++;
        const bool holds = std::any_of(changed.begin(), changed.end(), [&](std::size_t m) {
            return m >= systems.firstMeasure(k) && m < systems.endMeasure(k);
        });
        if (holds)
            pages[p].systems[index] = systems.layout(k);
    }
    return true;
}

} // namespace msd
