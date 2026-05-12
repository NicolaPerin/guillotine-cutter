#include "gdcut/pure_table.hpp"
#include <vector>

namespace gdcut {

PureTable::PureTable(int sheet_width, int sheet_height,
                     const std::vector<int>& item_widths,
                     const std::vector<int>& item_heights,
                     const std::vector<int>& item_profits,
                     const PurePatterns& patterns_x,
                     const PurePatterns& patterns_y)
    : sheet_width_(sheet_width), sheet_height_(sheet_height)
{
    const int stride  = sheet_height_ + 1;
    const int n_items = static_cast<int>(item_widths.size());

    values_.resize       ((sheet_width_ + 1) * stride, 0);
    decisions_.resize    ((sheet_width_ + 1) * stride, Decision::Empty);
    cut_positions_.resize((sheet_width_ + 1) * stride, 0);
    best_items_.resize   ((sheet_width_ + 1) * stride, -1);

    // Step 1: Tiling table
    // For each rectangle size (w, h), find the item type that maximises value
    // when tiled in a grid of tiles_x × tiles_y copies:
    //   tiling_value = (w / item_width) * (h / item_height) * item_profit
    // Results go directly into values_ and best_items_ as the baseline for
    // Step 2, which may improve on them with guillotine cuts.
    for (int rect_width = 1; rect_width <= sheet_width_; ++rect_width) {
        for (int rect_height = 1; rect_height <= sheet_height_; ++rect_height) {
            int32_t best_value    = 0;
            int32_t best_item_idx = -1;  // -1 = no item fits

            for (int item_idx = 0; item_idx < n_items; ++item_idx) {
                int tiles_x = rect_width  / item_widths[item_idx];
                int tiles_y = rect_height / item_heights[item_idx];
                if (tiles_x > 0 && tiles_y > 0) {
                    int32_t tiling_value = tiles_x * tiles_y * item_profits[item_idx];
                    if (tiling_value > best_value) {
                        best_value    = tiling_value;
                        best_item_idx = item_idx;
                    }
                }
            }

            int idx = rect_width * stride + rect_height;
            values_[idx]     = best_value;
            best_items_[idx] = best_item_idx;
        }
    }

    // Step 2: Pure DP table
    // For each rectangle size (w, h), starting from the tiling baseline,
    // try all normal-pattern cut positions in x and y. A guillotine cut at
    // position z splits (w, h) into two sub-rectangles whose values are
    // already computed (bottom-up order guarantees this). Update if better.
    //
    // Only cuts up to w/2 (resp. h/2) are tried — cutting at z gives the
    // same pair as cutting at w-z, so symmetry halves the search space.
    //
    // The decision table records what was optimal for backtracking:
    //   Empty    — no item fits and no cut helps
    //   Fill     — tiling with best_items_[idx] is optimal
    //   CutX/CutY — guillotine cut at cut_positions_[idx] is optimal
    for (int rect_width = 1; rect_width <= sheet_width_; ++rect_width) {
        // x cuts depend only on rect_width — hoist outside the height loop
        const std::vector<int>& x_cuts = patterns_x.normal_cuts(rect_width);

        for (int rect_height = 1; rect_height <= sheet_height_; ++rect_height) {
            int idx = rect_width * stride + rect_height;

            int32_t  best_value        = values_[idx];
            Decision best_decision     = (best_items_[idx] >= 0) ? Decision::Fill : Decision::Empty;
            int32_t  best_cut_position = (best_items_[idx] >= 0) ? best_items_[idx] : 0;

            // try vertical cuts (splits width)
            for (int cut_position : x_cuts) {
                if (cut_position > rect_width / 2) break;  // sorted — safe to break
                int32_t v = values_[cut_position * stride + rect_height]
                          + values_[(rect_width - cut_position) * stride + rect_height];
                if (v > best_value) {
                    best_value        = v;
                    best_decision     = Decision::CutX;
                    best_cut_position = cut_position;
                }
            }

            // try horizontal cuts (splits height)
            // y cuts depend on rect_height — declared inside the loop
            const std::vector<int>& y_cuts = patterns_y.normal_cuts(rect_height);
            for (int cut_position : y_cuts) {
                if (cut_position > rect_height / 2) break;  // sorted — safe to break
                int32_t v = values_[rect_width * stride + cut_position]
                          + values_[rect_width * stride + (rect_height - cut_position)];
                if (v > best_value) {
                    best_value        = v;
                    best_decision     = Decision::CutY;
                    best_cut_position = cut_position;
                }
            }

            // write back — subsequent iterations depend on updated values_
            values_[idx]        = best_value;
            decisions_[idx]     = best_decision;
            cut_positions_[idx] = best_cut_position;
        }
    }
}

int            PureTable::sheet_width()  const { return sheet_width_; }
int            PureTable::sheet_height() const { return sheet_height_; }
int32_t        PureTable::value(int w, int h)        const { return values_   [w * (sheet_height_ + 1) + h]; }
Decision       PureTable::decision(int w, int h)     const { return decisions_ [w * (sheet_height_ + 1) + h]; }
int32_t        PureTable::cut_position(int w, int h) const { return cut_positions_[w * (sheet_height_ + 1) + h]; }
int32_t        PureTable::best_item(int w, int h)    const { return best_items_[w * (sheet_height_ + 1) + h]; }
const int32_t* PureTable::values_data()              const { return values_.data(); }
int            PureTable::values_stride()            const { return sheet_height_ + 1; }

} // namespace gdcut