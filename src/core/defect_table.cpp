#include "gdcut/defect_table.hpp"
#include <algorithm>

namespace gdcut {

DefectTable::DefectTable(const Problem& problem,
                          const PureTable& pure_table,
                          const DefectMap& defect_map)
    : sheet_width_(problem.sheet_width())
    , sheet_height_(problem.sheet_height())
    , pure_table_(pure_table)
    , overflow_(false)
{
    const int stride     = sheet_height_ + 1;
    const int table_size = (sheet_width_ + 1) * stride;

    // Always allocate index arrays so lookup() is safe even for pure sheets.
    // has_affected_ is zero-initialized — all positions are implicitly pure.
    has_affected_.resize(table_size, 0);
    region_index_.resize(table_size);

    // Nothing to fill if there are no defects or no items.
    if (!problem.has_defects()) return;
    if (problem.n_items()      == 0) return;

    // Rectangles smaller than the smallest item can never contain any item
    // and are skipped in all three construction steps.
    const int min_item_width  = *std::min_element(problem.item_widths().begin(),
                                                   problem.item_widths().end());
    const int min_item_height = *std::min_element(problem.item_heights().begin(),
                                                   problem.item_heights().end());

    // temp_regions is a staging area for AffectedRegion objects built in Step 1.
    // At this point each region has:
    //   - sheet_x_start, sheet_y_start: position on the sheet
    //   - width, height:                dimensions of the affected area
    //   - delta_offset = 0:             placeholder, not yet known
    //
    // Step 2 (assign_delta_offsets) will:
    //   - compute the correct starting index in the flat deltas_ array
    //     for each region and store it in delta_offset
    //   - move temp_regions into regions_, the permanent storage
    //     used by lookup() and fill_deltas()
    std::vector<AffectedRegion> temp_regions;

    // Step 1: For each (w,h), find all sheet positions where placing a w×h
    // rectangle overlaps at least one defect, and store them as AffectedRegion
    // objects in temp_regions. 
    // Populates has_affected_ and region_index_.
    build_region_geometry(problem, temp_regions, stride, min_item_width, min_item_height);

    // Step 2: Assign delta_offset to each AffectedRegion in diagonal order (d = w+h).
    // Diagonal order guarantees that when fill_deltas() processes (w,h), all
    // smaller sub-problems (z,h) and (w,z) for z < w or z < h have already been
    // assigned offsets and filled. This mirrors the bottom-up dynamic programming
    // ordering: a cell (w,h) can only be computed after all cells it depends on
    // are already computed. Specifically:
    //   - vertical cut at z:   needs (z,h) and (w-z,h), both have w+h < current d
    //   - horizontal cut at z: needs (w,z) and (w,h-z), both have w+h < current d
    // Since d = w+h is strictly smaller for all sub-problems, processing in
    // increasing d order ensures correctness.
    assign_delta_offsets(temp_regions, stride, min_item_width, min_item_height);

    // Step 3: For each position in each AffectedRegion, classify it as pure
    // (delta=0) or impure, and for impure positions compute the optimal value
    // by trying all guillotine cuts. Store delta = pure_value - best_value.
    fill_deltas(defect_map, stride, min_item_width, min_item_height);
}

// For a rectangle of size (w, h), computes for each defect the range of
// sheet positions (sx, sy) where placing the rectangle would overlap it.
//
// This is needed because the DefectTable must know which placements are
// affected by defects. Precomputing this once during construction means
// that lookup() can determine in O(log n) — where n is the number of
// merged regions for a given (w,h), typically very small — whether a
// position is pure or impure, rather than checking all m defects at
// query time. Since lookup() is called O(W^2 * H^2) times during the
// DP fill, this precomputation is critical for performance.
//
// For a defect at (dx, dy) with size (dw, dh), the rectangle overlaps it if:
//   dx < sx + w  and  sx < dx + dw   →  sx in [dx - w + 1, dx + dw - 1]
//   dy < sy + h  and  sy < dy + dh   →  sy in [dy - h + 1, dy + dh - 1]
//
// Both ranges are clamped to valid sheet positions [0, sheet_dim - rect_dim].
//
// One OverlapRegion per defect is written into scratch, which is pre-allocated
// once with size n_defects and reused across all (w,h) iterations to avoid
// repeated heap allocation. Only indices [0, return_value) contain valid
// intervals — the rest is stale data from previous iterations and must be
// ignored by the caller.
int DefectTable::compute_overlap_intervals(const Problem& problem,
                                            std::vector<OverlapRegion>& scratch,
                                            int w, int h) const {
    int n_scratch = 0;

    for (const auto& defect : problem.defects()) {
        int x_lo = defect.x - w + 1;
        int x_hi = defect.x_end() - 1;
        int y_lo = defect.y - h + 1;
        int y_hi = defect.y_end() - 1;

        // clamp to valid sheet positions
        x_lo = std::max(x_lo, 0);
        x_hi = std::min(x_hi, sheet_width_  - w);
        y_lo = std::max(y_lo, 0);
        y_hi = std::min(y_hi, sheet_height_ - h);

        if (x_lo <= x_hi && y_lo <= y_hi)
            scratch[n_scratch++] = {x_lo, x_hi, y_lo, y_hi};
    }

    return n_scratch;
}

// Merges n_scratch overlap intervals (sorted by x_start) into AffectedRegion
// objects and appends them to temp_regions.
//
// Multiple defects can produce overlapping or adjacent intervals in x for the
// same (w,h) pair. Storing them as-is would mean a given sx could appear in
// more than one region, making binary search in lookup() ambiguous — it would
// not know which region to use. Merging guarantees each sx belongs to at most
// one region, which is required for the binary search to be correct.
//
// Merging takes the union in x and the bounding box in y. The bounding box
// in y is a conservative approximation — it may include some positions where
// placing the rectangle does not actually overlap any defect. Those positions
// are correctly identified as pure during fill_deltas() by checking whether
// the rectangle contains any defective cells, and get delta=0.
//
// Returns the number of regions added to temp_regions.
int DefectTable::merge_intervals(std::vector<OverlapRegion>& scratch,
                                  int n_scratch,
                                  std::vector<AffectedRegion>& temp_regions) const {

    // sort by x_start so adjacent intervals are contiguous
    std::sort(scratch.begin(), scratch.begin() + n_scratch,
              [](const OverlapRegion& a, const OverlapRegion& b) {
                  return a.x_start < b.x_start;
              });

    int regions_start = static_cast<int>(temp_regions.size());
    OverlapRegion current_interval = scratch[0];

    for (int i = 1; i < n_scratch; ++i) {
        if (scratch[i].x_start <= current_interval.x_end + 1) {
            // intervals overlap or are adjacent in x — extend the current region
            current_interval.x_end   = std::max(current_interval.x_end,   scratch[i].x_end);
            current_interval.y_start = std::min(current_interval.y_start,  scratch[i].y_start);
            current_interval.y_end   = std::max(current_interval.y_end,    scratch[i].y_end);
        } else {
            // gap in x — flush current_intervalrent region and start a new one
            int rw = current_interval.x_end - current_interval.x_start + 1;
            int rh = current_interval.y_end - current_interval.y_start + 1;
            temp_regions.push_back({(int16_t)current_interval.x_start, (int16_t)current_interval.y_start,
                                    (int16_t)rw, (int16_t)rh,
                                    0});  // delta_offset assigned in Step 2
            current_interval = scratch[i];
        }
    }

    // flush the last accumulated region
    int rw = current_interval.x_end - current_interval.x_start + 1;
    int rh = current_interval.y_end - current_interval.y_start + 1;
    temp_regions.push_back({(int16_t)current_interval.x_start, 
                            (int16_t)current_interval.y_start,
                            (int16_t)rw, (int16_t)rh, 0});

    return static_cast<int>(temp_regions.size()) - regions_start;
}

// Iterates over all (w, h) pairs in range [min_item_width...W] x [min_item_height...H].
// For each pair, computes overlap intervals and merges them into AffectedRegion
// objects. Updates has_affected_ and region_index_ for pairs with regions.
void DefectTable::build_region_geometry(const Problem& problem,
                                         std::vector<AffectedRegion>& temp_regions,
                                         int stride,
                                         int min_item_width,
                                         int min_item_height) {

    // scratch is reused across all (w,h) iterations to avoid repeated allocation
    std::vector<OverlapRegion> scratch(problem.defects().size());

    for (int w = min_item_width; w <= sheet_width_; ++w) {
        for (int h = min_item_height; h <= sheet_height_; ++h) {
            int wh_idx    = w * stride + h;
            int n_scratch = compute_overlap_intervals(problem, scratch, w, h);

            if (n_scratch == 0) continue;  // no defects affect this (w,h)

            int regions_start = static_cast<int>(temp_regions.size());
            int regions_added = merge_intervals(scratch, n_scratch, temp_regions);

            has_affected_[wh_idx]                = 1;
            region_index_[wh_idx].region_count   = regions_added;
            region_index_[wh_idx].regions_offset = regions_start;
        }
    }
}

// Assigns delta_offset to each AffectedRegion in diagonal order (d = w + h).
// After all offsets are assigned, moves temp_regions into regions_ (permanent
// storage) and allocates the deltas_ array, zero-initialised so that pure
// positions within affected regions need no explicit write in fill_deltas().
void DefectTable::assign_delta_offsets(std::vector<AffectedRegion>& temp_regions,
                                        int stride,
                                        int min_item_width,
                                        int min_item_height) {
    int64_t running_offset = 0;
    const int max_d = sheet_width_ + sheet_height_;

    for (int d = 2; d <= max_d; ++d) {
        for (int w = min_item_width; w <= sheet_width_; ++w) {
            int h = d - w;
            if (h < min_item_height || h > sheet_height_) continue;

            int wh_idx = w * stride + h;
            if (!has_affected_[wh_idx]) continue;

            AffectedRegionIndex& idx = region_index_[wh_idx];
            for (int r = 0; r < idx.region_count; ++r) {

                AffectedRegion& region = temp_regions[idx.regions_offset + r];
                region.delta_offset = running_offset;

                // layout is column-major: index = lx * height + ly
                running_offset += (int64_t)region.width * region.height;
            }
        }
    }

    // temp_regions now has all geometry and offsets — move into permanent storage
    regions_ = std::move(temp_regions);

    // allocate delta array; zero-init means pure positions need no explicit write
    deltas_.resize(running_offset, 0);
}

// Looks up the defect-adjusted value for placing a w×h rectangle at (sx, sy).
//
// If (w,h) has no affected positions, the position is pure and pure_value is
// returned directly in O(1). Otherwise, binary search finds the region
// containing sx (regions are x-sorted and x-disjoint). If sy falls outside
// the region's y range, the position is also pure. Otherwise the stored delta
// is subtracted from the pure value.
//
// Column-major layout: delta index = lx * height + ly, where
//   lx = sx - region.sheet_x_start
//   ly = sy - region.sheet_y_start
int32_t DefectTable::lookup(int w, int h, int sx, int sy) const {
    const int    stride   = sheet_height_ + 1;
    const int    wh_idx   = w * stride + h;
    const int32_t pure_val = pure_table_.value(w, h);

    if (!has_affected_[wh_idx]) return pure_val;

    const AffectedRegionIndex& idx = region_index_[wh_idx];

    // binary search over x-sorted x-disjoint regions
    int lo = 0, hi = idx.region_count - 1;
    while (lo <= hi) {
        int mid = lo + (hi - lo) / 2;
        const AffectedRegion& r = regions_[idx.regions_offset + mid];
        if      (sx < r.sheet_x_start)                 hi = mid - 1;
        else if (sx > r.sheet_x_start + r.width - 1)   lo = mid + 1;
        else {
            int lx = sx - r.sheet_x_start;
            int ly = sy - r.sheet_y_start;
            // sy outside region's y range — position is pure
            if (ly < 0 || ly >= r.height) return pure_val;
            return pure_val - deltas_[r.delta_offset + lx * r.height + ly];
        }
    }
    // sx not in any region — position is pure
    return pure_val;
}

int32_t DefectTable::value(int w, int h, int sx, int sy) const { return lookup(w, h, sx, sy); }

DefectTable::ColumnView DefectTable::resolve_column(int w, int h, int sx) const {
    const int    stride   = sheet_height_ + 1;
    const int    wh_idx   = w * stride + h;
    const int32_t pure_val = pure_table_.value(w, h);

    if (!has_affected_[wh_idx])
        return {pure_val, true, 0, 0, nullptr};

    const AffectedRegionIndex& idx = region_index_[wh_idx];

    int lo = 0, hi = idx.region_count - 1;
    while (lo <= hi) {
        int mid = lo + (hi - lo) / 2;
        const AffectedRegion& r = regions_[idx.regions_offset + mid];
        if      (sx < r.sheet_x_start)                hi = mid - 1;
        else if (sx > r.sheet_x_start + r.width - 1)  lo = mid + 1;
        else {
            int lx = sx - r.sheet_x_start;
            return {pure_val, false, r.height, r.sheet_y_start,
                    &deltas_[r.delta_offset + lx * r.height]};
        }
    }
    return {pure_val, true, 0, 0, nullptr};
}

int32_t DefectTable::column_get(const ColumnView& cv, int sy) const {
    if (cv.is_pure) return cv.pure_val;
    int ly = sy - cv.sheet_y_start;
    if (ly < 0 || ly >= cv.y_span) return cv.pure_val;
    return cv.pure_val - cv.col_data[ly];
}

// For each position in each AffectedRegion, computes the optimal value by
// trying all guillotine cuts and stores delta = pure_value - best_value.
//
// Pure positions (no defective cells in rectangle) get delta=0 and are
// skipped. Impure positions try all vertical cuts z in [1,w) and horizontal
// cuts z in [1,h), taking the best combined value from lookup() on both halves.
// lookup() recursively uses already-filled deltas for smaller sub-problems,
// which is why diagonal offset assignment is required.
//
// Deltas exceeding uint16 range are clamped to UINT16_MAX and overflow_ is set.
void DefectTable::fill_deltas(const DefectMap& defect_map,
                               int stride,
                               int min_item_width,
                               int min_item_height) {
    // col_best[ly] — best value found so far for position ly in current column
    // is_impure[ly] — true if position (sx, sheet_y_start+ly) overlaps a defect
    // Allocated once and reused across all columns to avoid repeated allocation.
    std::vector<int32_t> col_best;
    std::vector<uint8_t> is_impure;

    for (int w = min_item_width; w <= sheet_width_; ++w) {
        for (int h = min_item_height; h <= sheet_height_; ++h) {
            int wh_idx = w * stride + h;
            if (!has_affected_[wh_idx]) continue;

            const int32_t              pure_val = pure_table_.value(w, h);
            const AffectedRegionIndex& idx      = region_index_[wh_idx];

            for (int r = 0; r < idx.region_count; ++r) {
                const AffectedRegion& region = regions_[idx.regions_offset + r];
                const int y_span = region.height;

                if ((int)col_best.size()  < y_span) col_best.resize(y_span);
                if ((int)is_impure.size() < y_span) is_impure.resize(y_span);

                for (int lx = 0; lx < region.width; ++lx) {
                    const int sx = region.sheet_x_start + lx;

                    // Pass 1: classify each sy as pure or impure
                    int impure_count = 0;
                    for (int ly = 0; ly < y_span; ++ly) {
                        const int sy = region.sheet_y_start + ly;
                        if (defect_map.count_defective_cells_in_rectangle(
                                sx, sy, sx + w, sy + h) == 0) {
                            deltas_[region.delta_offset + lx * y_span + ly] = 0;
                            is_impure[ly] = 0;
                        } else {
                            is_impure[ly] = 1;
                            col_best[ly]  = 0;
                            ++impure_count;
                        }
                    }

                    if (impure_count == 0) continue;

                    // Pass 2: vertical cuts — resolve column ONCE per z
                    for (int z = 1; z < w; ++z) {
                        ColumnView left  = resolve_column(z,     h, sx);
                        ColumnView right = resolve_column(w - z, h, sx + z);
                        for (int ly = 0; ly < y_span; ++ly) {
                            if (!is_impure[ly]) continue;
                            const int sy = region.sheet_y_start + ly;
                            const int32_t v = column_get(left,  sy)
                                            + column_get(right, sy);
                            if (v > col_best[ly]) col_best[ly] = v;
                        }
                    }

                    // Pass 3: horizontal cuts — resolve column ONCE per z
                    for (int z = 1; z < h; ++z) {
                        ColumnView top    = resolve_column(w, z,     sx);
                        ColumnView bottom = resolve_column(w, h - z, sx);
                        for (int ly = 0; ly < y_span; ++ly) {
                            if (!is_impure[ly]) continue;
                            const int sy = region.sheet_y_start + ly;
                            const int32_t v = column_get(top,    sy)
                                            + column_get(bottom, sy + z);
                            if (v > col_best[ly]) col_best[ly] = v;
                        }
                    }

                    // Pass 4: write deltas
                    for (int ly = 0; ly < y_span; ++ly) {
                        if (!is_impure[ly]) continue;
                        int32_t delta = pure_val - col_best[ly];
                        if (delta > UINT16_MAX) {
                            overflow_ = true;
                            delta = UINT16_MAX;
                        }
                        deltas_[region.delta_offset + lx * y_span + ly] =
                            static_cast<uint16_t>(delta);
                    }
                }
            }
        }
    }
}



bool            DefectTable::has_affected_positions(int w, int h) const {
    return has_affected_[w * (sheet_height_ + 1) + h];
}
const uint16_t* DefectTable::deltas_data()  const { return deltas_.data(); }
int64_t         DefectTable::total_deltas() const { return static_cast<int64_t>(deltas_.size()); }
bool            DefectTable::overflow()     const { return overflow_; }

} // namespace gdcut