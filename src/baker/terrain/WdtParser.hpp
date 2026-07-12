#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <optional>

#include "terrain/AdtParser.hpp" // for AdtPlacement

namespace world::terrain
{

    struct WdtData
    {
        bool hasGlobalWmo = false;
        bool hasMainChunk = false;
        std::string globalWmoName;

        // Global WMO placement (if any)
        std::optional<AdtPlacement> globalWmoPlacement;

        uint32_t mphdFlags = 0;

        // 64x64 grid
        std::array<std::array<bool, 64>, 64> hasAdt{};
        bool hasAnyAdt() const
        {
            for (int y = 0; y < 64; ++y)
            {
                for (int x = 0; x < 64; ++x)
                {
                    if (hasAdt[y][x])
                        return true;
                }
            }
            return false;
        }
    };

    bool parseWdt(const uint8_t *data, size_t size, WdtData &out);

    inline bool parseWdt(const std::vector<uint8_t> &bytes, WdtData &out)
    {
        return parseWdt(bytes.data(), bytes.size(), out);
    }

} // namespace world::terrain
