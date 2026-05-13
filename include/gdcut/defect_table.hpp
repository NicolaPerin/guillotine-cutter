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
            bool    has_affected_positions(int w, int h) const;

            /** @brief Get a pointer to the deltas data
             *  @return Pointer to the deltas data
             */
            const uint16_t* deltas_data()   const;

            /** @brief Get the total number of deltas across all affected regions.
             *  @return Total number of deltas
             */
            int64_t         total_deltas()  const;
            
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

            ColumnView resolve_column(int w, int h, int sx) const;
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