#include "gdcut/cut_candidates.hpp"
#include <vector>
#include <set>

namespace {

    // Computes the maximum reachable cut position in a sub-sheet, extended to
    // account for defect boundaries. For a pure sub-sheet this reduces to
    // patterns.max_reachable(primary_span). For a contaminated sub-sheet,
    // defect right (or top) edges create additional reachable positions: after
    // placing items up to a defect boundary, the remaining space may allow
    // a further reach.
    //
    // primary_origin   — left (x) or bottom (y) edge of the sub-sheet
    // secondary_origin — bottom (y) or left (x) edge in perpendicular direction
    // primary_span     — width (or height) of the sub-sheet
    // secondary_span   — height (or width) of the sub-sheet
    // use_x            — true for x-direction, false for y-direction
    int max_reachable_extended(const gdcut::PurePatterns& patterns,
                            const gdcut::DefectMap& defect_map,
                            int primary_origin,
                            int secondary_origin,
                            int primary_span,
                            int secondary_span,
                            bool use_x) {
        int best = patterns.max_reachable(primary_span);

        for (const auto& defect : defect_map.defects()) {
            int def_primary_low  = use_x ? defect.x : defect.y;
            int def_primary_high = use_x ? defect.x_end() : defect.y_end();
            int def_secondary_low = use_x ? defect.y : defect.x;
            int def_secondary_high = use_x ? defect.y_end() : defect.x_end();

            bool overlaps_primary   = (def_primary_low      < primary_origin   + primary_span)
                                    && (def_primary_high    > primary_origin);
            bool overlaps_secondary = (def_secondary_low    < secondary_origin + secondary_span)
                                    && (def_secondary_high  > secondary_origin);

            if (!overlaps_primary || !overlaps_secondary) continue;

            int defect_boundary = def_primary_high - primary_origin;

            if (defect_boundary <= 0 || defect_boundary > primary_span) continue;

            int remaining         = primary_span - defect_boundary;
            int fill_after_defect = patterns.max_reachable(remaining);
            int candidate         = defect_boundary + fill_after_defect;

            if (candidate > best)
                best = candidate;
        }

        return best;
    }
} // anonymous namespace

namespace gdcut {
    PureCuts::PureCuts(const PurePatterns& patterns_x, const PurePatterns& patterns_y)
        : patterns_x_(patterns_x), patterns_y_(patterns_y) {}

    const std::vector<int>& PureCuts::x_cuts(int x, int y, int w, int h) const { 
        (void)x; (void)y; (void)h; // parameters are unused for pure cuts, silence unused parameter warnings
        return patterns_x_.raster_points(w); 
    }
    const std::vector<int>& PureCuts::y_cuts(int x, int y, int w, int h) const { 
        (void)x; (void)y; (void)w; // parameters are unused for pure cuts, silence unused parameter warnings
        return patterns_y_.raster_points(h); 
    }

    ExtendedCuts::ExtendedCuts(const PurePatterns& patterns_x, const PurePatterns& patterns_y, const DefectMap& defect_map)
        : patterns_x_(patterns_x), patterns_y_(patterns_y), defect_map_(defect_map) {}

    const std::vector<int>& ExtendedCuts::x_cuts(int x, int y, int w, int h) const {
        x_cuts_cache_.clear();

        // rp_t and reversed_normal_positions are allocated per call.
        // x_cuts_cache_ reuses its memory across calls via clear() which keeps
        // capacity. If profiling shows this is a bottleneck, replace std::set
        // with a sorted std::vector + std::sort/std::unique.

        // Step 1: Collect defect right-edge offsets in reversed coordinates.
        //
        // The extended raster point formula works in a reversed coordinate system
        // where the origin is the right edge of the sub-sheet. For each defect
        // overlapping this sub-sheet, we compute how far its left edge is from
        // the right side of the sub-sheet. These offsets are the starting points
        // for the reversed normal pattern computation in Step 2.
        // 0 is always included — it represents the case with no defect offset
        // (items pushed as far right as possible from the right edge).
        std::vector<int> reversed_defect_offsets = {0};
        for (const auto& defect : defect_map_.defects()) {
            bool overlaps_x = (defect.x     < x + w) && (defect.x_end() > x);
            bool overlaps_y = (defect.y     < y + h) && (defect.y_end() > y);
            if (!overlaps_x || !overlaps_y) continue;

            // distance from the right edge of the sub-sheet to the left edge
            // of the defect, in reversed coordinates
            int right_edge_to_defect_left = x + w - defect.x;
            if (right_edge_to_defect_left > 0 && right_edge_to_defect_left < w)
                reversed_defect_offsets.push_back(right_edge_to_defect_left);
        }

        // Step 2: Build reversed normal positions.
        //
        // For each reversed defect offset, add all reachable item-size sums.
        // This gives all positions reachable in the reversed coordinate system —
        // i.e., all positions from which items can be packed rightward to the
        // sheet edge or a defect boundary.
        std::set<int> reversed_normal_positions;
        for (int defect_offset : reversed_defect_offsets) {
            for (int reachable_sum : patterns_x_.all_normal_positions()) {
                int reversed_position = defect_offset + reachable_sum;
                if (reversed_position >= w) break;  // sorted — safe to break
                reversed_normal_positions.insert(reversed_position);
            }
        }

        // Step 3: Convert reversed positions to extended raster points.
        //
        // For each reversed position z, the remaining space to the left is (w-z).
        // The best cut position we can place in that left space — accounting for
        // defects — is max_reachable_extended(w-z). This is the extended raster
        // point: the optimal left-side cut position that pairs with z on the right.
        std::set<int> extended_raster_points;
        for (int reversed_position : reversed_normal_positions) {
            int left_space    = w - reversed_position;
            int best_left_cut = max_reachable_extended(
                patterns_x_, defect_map_, x, y, left_space, h, true);
            if (best_left_cut > 0 && best_left_cut < w)
                extended_raster_points.insert(best_left_cut);
        }

        x_cuts_cache_.assign(extended_raster_points.begin(),
                            extended_raster_points.end());
        return x_cuts_cache_;
    }

    const std::vector<int>& ExtendedCuts::y_cuts(int x, int y, int w, int h) const {
        y_cuts_cache_.clear();

        // The logic is the same as in x_cuts but applied to the y-dimension.
        std::vector<int> reversed_defect_offsets = {0};
        for (const auto& defect : defect_map_.defects()) {
            bool overlaps_x = (defect.x     < x + w) && (defect.x_end() > x);
            bool overlaps_y = (defect.y     < y + h) && (defect.y_end() > y);
            if (!overlaps_x || !overlaps_y) continue;

            int top_edge_to_defect_bottom = y + h - defect.y;
            if (top_edge_to_defect_bottom > 0 && top_edge_to_defect_bottom < h)
                reversed_defect_offsets.push_back(top_edge_to_defect_bottom);
        }

        std::set<int> reversed_normal_positions;
        for (int defect_offset : reversed_defect_offsets) {
            for (int reachable_sum : patterns_y_.all_normal_positions()) {
                int reversed_position = defect_offset + reachable_sum;
                if (reversed_position >= h) break;  // sorted — safe to break
                reversed_normal_positions.insert(reversed_position);
            }
        }

        std::set<int> extended_raster_points;
        for (int reversed_position : reversed_normal_positions) {
            int bottom_space    = h - reversed_position;
            int best_bottom_cut = max_reachable_extended(
                patterns_y_, defect_map_, x, y, bottom_space, w, false);
            if (best_bottom_cut > 0 && best_bottom_cut < h)
                extended_raster_points.insert(best_bottom_cut);
        }

        y_cuts_cache_.assign(extended_raster_points.begin(),
                            extended_raster_points.end());
        return y_cuts_cache_;
    }
}