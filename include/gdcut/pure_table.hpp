#pragma once

#include "gdcut/decision.hpp"
#include "gdcut/patterns.hpp"
#include <vector>

namespace gdcut {
    class PureTable { // Takes item data and pattern data in its constructor and computes both the tiling and pure DP tables
        public:
            /** @brief Constructor for PureTable
             *  @param sheet_width Width of the sheet
             *  @param sheet_height Height of the sheet
             *  @param item_widths Vector of item widths
             *  @param item_heights Vector of item heights
             *  @param item_profits Vector of item profits
             *  @param patterns_x Vector of horizontal patterns
             *  @param patterns_y Vector of vertical patterns
             */
            PureTable(int sheet_width, int sheet_height,
                const std::vector<int>& item_widths, 
                const std::vector<int>& item_heights,
                const std::vector<int>& item_profits,
                const PurePatterns& patterns_x,
                const PurePatterns& patterns_y);
            
            /** @brief Get the width of the sheet */
            int sheet_width() const;

            /** @brief Get the height of the sheet */
            int sheet_height() const;
            
            /** @brief Get the values for a given width and height
             *  @param w Width
             *  @param h Height
             *  @return Value for the given width and height
             */
            inline int32_t value(int w, int h) const { 
                return values_[w * (sheet_height_ + 1) + h]; 
            }

            /** @brief Get the decisions for a given width and height
            *  @param w Width
            *  @param h Height
            *  @return Decision for the given width and height
            */
            inline Decision decision(int w, int h) const {
                return decisions_[w * (sheet_height_ + 1) + h];
            }

            /** @brief Get the cut positions for a given width and height
            *  @param w Width
            *  @param h Height
            *  @return cut position for the given width and height
            */
            inline int32_t cut_position(int w, int h) const {
                return cut_positions_[w * (sheet_height_ + 1) + h];
            }

            /** @brief Get the best item types for a given width and height
            *  @param w Width
            *  @param h Height
            *  @return Best item for the given width and height
            */
            inline int32_t best_item(int w, int h) const {
                return best_items_[w * (sheet_height_ + 1) + h];
            }

            /** @brief Get a pointer to the values data
             *  @return Pointer to the values data
             */
            const int32_t* values_data() const;

            /** @brief Get the stride of the values data
             *  @return Stride of the values data
             */
            int values_stride() const;

        private:
            int sheet_width_;
            int sheet_height_;

            std::vector<int32_t>  values_;
            std::vector<Decision> decisions_;
            std::vector<int32_t>  cut_positions_;
            std::vector<int32_t>  best_items_;
    };
}