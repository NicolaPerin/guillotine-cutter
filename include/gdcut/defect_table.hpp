#pragma once

#include "gdcut/affected_region.hpp"
#include "gdcut/problem.hpp"
#include "gdcut/pure_table.hpp"
#include "gdcut/defect.hpp"
#include <cstdint>
#include <vector>

namespace gdcut {
    class DefectTable {
        public:
            /**
             * @brief Constructs the DefectTable from a problem and its pure table.
             *
             * Builds tile geometry, assigns delta offsets in diagonal order,
             * and fills defect-adjusted values for all affected positions.
             * Sets overflow_ if any delta exceeds uint16 range.
             *
             * @param problem    The problem instance containing sheet dimensions and defects
             * @param pure_table The precomputed pure DP table
             * @param defect_map The map of defects on the sheet
             */
            DefectTable(const Problem& problem, const PureTable& pure_table, const DefectMap& defect_map);

            /** @brief Get the value for a given width, height, and starting position on the sheet.
             *  @param w Width of the region
             *  @param h Height of the region
             *  @param sx Starting x position on the sheet
             *  @param sy Starting y position on the sheet
             *  @return Value for the given width, height, and starting position
             */
            int32_t value(int w, int h, int sx, int sy) const;

            /** @brief Check if there are any affected positions for a given width and height.
             *  @param w Width of the region
             *  @param h Height of the region
             *  @return True if there are affected positions, false otherwise
             */
            inline bool has_affected_positions(int w, int h) const {
                return has_affected_[w * (sheet_height_ + 1) + h];
            }
            
            /** @brief Check if the number of deltas exceeds the maximum representable by uint16_t.
             *  @return True if overflow occurs, false otherwise
             */
            bool            overflow()      const;
        
        private:
            int sheet_width_, sheet_height_;
            std::vector<uint8_t>              has_affected_;
            std::vector<AffectedRegionIndex>  region_index_;
            std::vector<AffectedRegion>       regions_;
            std::vector<uint16_t>             deltas_;
            const PureTable&                  pure_table_;
            bool                              overflow_;

            // Lightweight view into a single column of delta data for one (w,h,sx).
            // Resolved once per (w,h,sx) via resolve_column() — avoids repeated
            // binary search in the inner ly loop of fill_deltas().
            struct ColumnView {
                int32_t         pure_val;
                bool            is_pure;
                int             y_span;
                int             sheet_y_start;
                const uint16_t* col_data;  // nullptr if pure
            };

            inline ColumnView resolve_column(int w, int h, int sx) const {
                const int     stride   = sheet_height_ + 1;
                const int     wh_idx   = w * stride + h;
                const int32_t pure_val = pure_table_.value(w, h);

                if (!has_affected_[wh_idx])
                    return {pure_val, true, 0, 0, nullptr};

                const AffectedRegionIndex& idx = region_index_[wh_idx];
                const int rx = sx + w;  // convert to right edge

                int lo = 0, hi = idx.region_count - 1;
                while (lo <= hi) {
                    int mid = lo + (hi - lo) / 2;
                    const AffectedRegion& r = regions_[idx.regions_offset + mid];
                    if      (rx < r.sheet_x_start)                hi = mid - 1;
                    else if (rx > r.sheet_x_start + r.width - 1)  lo = mid + 1;
                    else {
                        int lx = rx - r.sheet_x_start;  // invariant: same value as before
                        return {pure_val, false, r.height, r.sheet_y_start,
                                &deltas_[r.delta_offset + lx * r.height]};
                    }
                }
                return {pure_val, true, 0, 0, nullptr};
            }

            int32_t    column_get(const ColumnView& cv, int sy) const;

            /** @brief Computes affected position intervals for a single (w,h) pair. */
            int compute_overlap_intervals(const Problem& problem,
                                        std::vector<OverlapRegion>& scratch,
                                        int w, int h) const;

            /** @brief Merges sorted overlap intervals into AffectedRegion objects. */
            int merge_intervals(std::vector<OverlapRegion>& scratch,
                                int n_scratch,
                                std::vector<AffectedRegion>& temp_regions) const;

            /** @brief Builds region geometry for all (w,h) pairs. */
            void build_region_geometry(const Problem& problem,
                                    std::vector<AffectedRegion>& temp_regions,
                                    int stride,
                                    int min_item_width,
                                    int min_item_height);

            void assign_delta_offsets(std::vector<AffectedRegion>& temp_regions,
                                    int stride,
                                    int min_item_width,
                                    int min_item_height);

            int32_t lookup(int w, int h, int sx, int sy) const;

            void fill_deltas(const DefectMap& defect_map, 
                                    int stride,
                                    int min_item_width, 
                                    int min_item_height);
    };
}