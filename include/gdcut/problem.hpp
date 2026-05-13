#pragma once

#include "gdcut/defect.hpp"
#include <string>
#include <vector>

namespace gdcut {

/**
 * @brief Holds all input data for a guillotine cutting problem instance.
 *
 * Pure data container — no solver infrastructure. Can be loaded from JSON
 * via from_json(). Passed by const reference to Solver and other components.
 */
class Problem {
public:
    /**
     * @brief Constructs a Problem from raw data.
     *
     * @param sheet_width   Width of the sheet
     * @param sheet_height  Height of the sheet
     * @param item_widths   Item widths
     * @param item_heights  Item heights
     * @param item_profits  Item profits (use area for unweighted)
     * @param defects       List of defects on the sheet
     */
    Problem(int sheet_width, int sheet_height,
            std::vector<int> item_widths,
            std::vector<int> item_heights,
            std::vector<int> item_profits,
            std::vector<Defect> defects);

    /**
     * @brief Loads a Problem from a JSON file.
     *
     * @param path Path to the JSON file
     * @return Problem instance
     * @throws std::runtime_error if the file cannot be opened or parsed
     */
    static Problem from_json(const std::string& path);

    /** @brief Width of the sheet */
    inline int sheet_width()  const { return sheet_width_; }

    /** @brief Height of the sheet */
    inline int sheet_height() const { return sheet_height_; }

    /** @brief Number of item types */
    inline int n_items()      const { return item_widths_.size(); }

    /** @brief True if the sheet has at least one defect */
    bool has_defects() const;

    /** @brief Item widths */
    const std::vector<int>& item_widths()  const;

    /** @brief Item heights */
    const std::vector<int>& item_heights() const;

    /** @brief Item profits */
    const std::vector<int>& item_profits() const;

    /** @brief List of defects on the sheet */
    const std::vector<Defect>& defects() const;

private:
    int              sheet_width_;
    int              sheet_height_;
    std::vector<int> item_widths_;
    std::vector<int> item_heights_;
    std::vector<int> item_profits_;
    std::vector<Defect> defects_;
};

} // namespace gdcut