#include "gdcut/cut_candidates.hpp"
#include <vector>
#include <algorithm>

namespace {

int max_reachable_extended(const gdcut::PurePatterns& patterns,
                            const gdcut::DefectMap& defect_map,
                            int primary_origin,
                            int secondary_origin,
                            int primary_span,
                            int secondary_span,
                            bool use_x) {
    int best = patterns.max_reachable(primary_span);

    for (const auto& defect : defect_map.defects()) {
        int def_primary_low    = use_x ? defect.x       : defect.y;
        int def_primary_high   = use_x ? defect.x_end() : defect.y_end();
        int def_secondary_low  = use_x ? defect.y       : defect.x;
        int def_secondary_high = use_x ? defect.y_end() : defect.x_end();

        bool overlaps_primary   = def_primary_low   < primary_origin   + primary_span
                                && def_primary_high  > primary_origin;
        bool overlaps_secondary = def_secondary_low  < secondary_origin + secondary_span
                                && def_secondary_high > secondary_origin;

        if (!overlaps_primary || !overlaps_secondary) continue;

        int defect_boundary = def_primary_high - primary_origin;
        if (defect_boundary <= 0 || defect_boundary > primary_span) continue;

        int candidate = defect_boundary + patterns.max_reachable(primary_span - defect_boundary);
        if (candidate > best) best = candidate;
    }

    return best;
}

} // anonymous namespace

namespace gdcut {

PureCuts::PureCuts(const PurePatterns& patterns_x, const PurePatterns& patterns_y)
    : patterns_x_(patterns_x), patterns_y_(patterns_y) {}

const std::vector<int>& PureCuts::x_cuts(int x, int y, int w, int h) const {
    (void)x; (void)y; (void)h;
    return patterns_x_.raster_points(w);
}

const std::vector<int>& PureCuts::y_cuts(int x, int y, int w, int h) const {
    (void)x; (void)y; (void)w;
    return patterns_y_.raster_points(h);
}

ExtendedCuts::ExtendedCuts(const PurePatterns& patterns_x,
                            const PurePatterns& patterns_y,
                            const DefectMap& defect_map)
    : patterns_x_(patterns_x)
    , patterns_y_(patterns_y)
    , defect_map_(defect_map)
{}

void ExtendedCuts::compute_cuts(int primary_origin, int secondary_origin,
                                 int primary_span,   int secondary_span,
                                 const PurePatterns& patterns,
                                 bool use_x,
                                 std::vector<int>& out) const {
    out.clear();

    scratch_offsets_.clear();
    scratch_offsets_.push_back(0);
    for (const auto& defect : defect_map_.defects()) {
        bool overlaps_primary   = (use_x ? defect.x   : defect.y)       < primary_origin   + primary_span
                                && (use_x ? defect.x_end() : defect.y_end()) > primary_origin;
        bool overlaps_secondary = (use_x ? defect.y   : defect.x)       < secondary_origin + secondary_span
                                && (use_x ? defect.y_end() : defect.x_end()) > secondary_origin;
        if (!overlaps_primary || !overlaps_secondary) continue;
        int edge_to_defect = primary_origin + primary_span - (use_x ? defect.x : defect.y);
        if (edge_to_defect > 0 && edge_to_defect < primary_span)
            scratch_offsets_.push_back(edge_to_defect);
    }

    scratch_positions_.clear();
    for (int defect_offset : scratch_offsets_) {
        for (int np : patterns.all_normal_positions()) {
            int pos = defect_offset + np;
            if (pos >= primary_span) break;
            scratch_positions_.push_back(pos);
        }
    }
    std::sort(scratch_positions_.begin(), scratch_positions_.end());
    scratch_positions_.erase(
        std::unique(scratch_positions_.begin(), scratch_positions_.end()),
        scratch_positions_.end());

    for (int reversed_pos : scratch_positions_) {
        int left_space = primary_span - reversed_pos;
        int best_cut   = max_reachable_extended(
            patterns, defect_map_,
            primary_origin, secondary_origin,
            left_space, secondary_span, use_x);
        if (best_cut > 0 && best_cut < primary_span)
            out.push_back(best_cut);
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
}

const std::vector<int>& ExtendedCuts::x_cuts(int x, int y, int w, int h) const {
    compute_cuts(x, y, w, h, patterns_x_, true,  x_cuts_cache_);
    return x_cuts_cache_;
}

const std::vector<int>& ExtendedCuts::y_cuts(int x, int y, int w, int h) const {
    compute_cuts(y, x, h, w, patterns_y_, false, y_cuts_cache_);
    return y_cuts_cache_;
}

} // namespace gdcut