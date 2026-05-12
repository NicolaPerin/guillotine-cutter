#pragma once

#include <vector>

namespace gdcut {
    class PurePatterns {
        public:
            /** 
             * @brief Constructs the PurePatterns object. 
             * 
             * @param item_sizes  List of item sizes (widths and heights) to consider for cutting patterns.
             * @param max_dim     Maximum dimension (width or height) of the sheet to consider
            */
            PurePatterns(const std::vector<int>& item_sizes, int max_dim);

            /**
             * @brief Returns the maximum dimension of the sheet considered for pattern generation.
             */
            int max_dim() const;

            /** 
             * @brief Checks if a given dimension is reachable by cutting patterns using the provided item sizes.
             * 
             * A dimension d is considered reachable if it can be formed by summing up the item sizes in some combination, without exceeding d. 
             * This method allows for quick checks during the DP to determine if a particular cut can lead to a valid pattern.
             * 
             * @param dim The dimension to check for reachability.
             */
            bool is_reachable(int dim)  const;
            
            /** 
             * @brief Returns the maximum reachable dimension that is less than or equal to the given dimension.
             * 
             * This method is useful for quickly determining the largest possible cut size that can be made from a sheet of a given dimension, 
             * which can help optimize the cutting patterns and reduce unnecessary checks during the DP.
             * 
             * @param dim The dimension for which to find the maximum reachable dimension.
             */
            int  max_reachable(int dim) const;
            
            /** 
             * @brief Returns a vector of normal cut points for each dimension up to max_dim.
             * 
             * @param dim The dimension for which to retrieve the normal cut points.
             * For each dimension d, this vector contains the cut points that can be used to split a sheet of dimension d into two smaller dimensions that are both reachable by cutting patterns.
             */
            const std::vector<int>& normal_cuts(int dim) const;
            
            /** 
             * @brief Returns a vector of raster cut points for each dimension up to max_dim.
             * 
             * @param dim The dimension for which to retrieve the raster cut points.
             * For each dimension d, this vector contains the cut points that can be used to split a sheet of dimension d into two smaller dimensions, where at least one of them is reachable by cutting patterns. This allows for more aggressive cutting strategies that may not require both sides to be fully reachable.
             */
            const std::vector<int>& raster_points(int dim) const;

            /** 
             * @brief Returns a vector of all normal cut positions.
             */
            const std::vector<int>& all_normal_positions() const;

        private:
            int max_dim_;
            std::vector<bool> is_reachable_;
            std::vector<int> max_reachable_;
            std::vector<std::vector<int>> normal_cuts_;
            std::vector<std::vector<int>> raster_points_;
            std::vector<int> all_normal_positions_;
    };
}