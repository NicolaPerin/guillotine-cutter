#include "gdcut/patterns.hpp"
#include <vector>
#include <set>

namespace gdcut {
    PurePatterns::PurePatterns(const std::vector<int>& item_sizes, int max_dim) : max_dim_(max_dim) {

        // Step 1: Compute reachability of dimensions up to max_dim using a bottom-up DP approach.
        is_reachable_.resize(max_dim + 1, false);
        is_reachable_[0] = true; // base case: dimension 0 is always reachable (no cuts)
        for (auto item_size : item_sizes)
            for (int position = item_size; position <= max_dim_; ++position)
                if (is_reachable_[position - item_size])
                    is_reachable_[position] = true;

        // Collect all normal cut positions for quick access
        for (int z = 0; z <= max_dim_; ++z)
            if (is_reachable_[z])
                all_normal_positions_.push_back(z);

        // Step 2: Precompute the maximum reachable dimension for each dimension up to max_dim.
        max_reachable_.resize(max_dim + 1);
        max_reachable_[0] = 0; // base case: max reachable dimension for 0 is 0
        for (int position = 1; position <= max_dim_; ++position)
            if (is_reachable_[position])
                max_reachable_[position] = position; // if position is reachable, it is the max reachable for itself
            else
                max_reachable_[position] = max_reachable_[position - 1]; // otherwise, inherit the max reachable from the previous position

        // Step 3: Precompute normal cut points for each dimension up to max_dim.
        normal_cuts_.resize(max_dim + 1);
        for (int dimension = 1; dimension <= max_dim_; ++ dimension)
            for (int cut_point = 1; cut_point < dimension; ++cut_point)
                if (is_reachable_[cut_point])
                    normal_cuts_[dimension].push_back(cut_point);

        // Step 4: Precompute raster cut points for each dimension up to max_dim.
        //
        // Raster points are a subset of normal patterns where dominated positions
        // are removed. A position z is dominated if there is a better position z'
        // that achieves the same or higher fill on both sides of the cut.
        //
        // For each dimension w, the raster points are:
        //   R(w) = { max_reachable[w-z] | z in normal_cuts[w] union {0} }
        //
        // For each cut position z, we look at the remaining space (w-z) and find
        // the best we can fill from that side. Only these optimal positions are kept.
        raster_points_.resize(max_dim + 1);
        for (int dimension = 1; dimension <= max_dim; ++dimension) {
            std::set<int> result_set;

            // z=0: take the largest reachable value for the full dimension
            result_set.insert(max_reachable_[dimension]);

            // for each normal cut point, find the best fill in the remaining space
            for (auto cut_point : normal_cuts_[dimension])
                result_set.insert(max_reachable_[dimension - cut_point]);

            // skip zero — means no item fits on that side
            for (auto value : result_set)
                if (value > 0 && value < dimension) // only valid cut points strictly between 0 and dimension
                    raster_points_[dimension].push_back(value);
        }
    }

    int                     PurePatterns::max_dim()                 const { return max_dim_; }
    bool                    PurePatterns::is_reachable(int dim)     const { return is_reachable_[dim]; }
    int                     PurePatterns::max_reachable(int dim)    const { return max_reachable_[dim]; }
    const std::vector<int>& PurePatterns::normal_cuts(int dim)      const { return normal_cuts_[dim]; }
    const std::vector<int>& PurePatterns::raster_points(int dim)    const { return raster_points_[dim]; }
    const std::vector<int>& PurePatterns::all_normal_positions()    const { return all_normal_positions_; }
}