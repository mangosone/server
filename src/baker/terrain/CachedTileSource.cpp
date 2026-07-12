#include "terrain/CachedTileSource.hpp"
#include "terrain/TileSerializer.hpp"

#include <filesystem>

namespace world::terrain
{

    CachedTileSource::CachedTileSource(std::unique_ptr<ITileSource> inner, std::string cacheDir)
        : m_inner(std::move(inner)), m_dir(std::move(cacheDir))
    {
        if (!m_dir.empty())
        {
            std::error_code ec;
            std::filesystem::create_directories(m_dir, ec); // best effort
            if (ec)
                m_dir.clear(); // can't make the dir -> run uncached rather than fail
        }
    }

    std::string CachedTileSource::pathFor(uint32_t mapId, int tx, int ty) const
    {
        return m_dir + "/t_" + std::to_string(mapId) + "_" + std::to_string(tx) + "_" +
               std::to_string(ty) + ".tile";
    }
    std::string CachedTileSource::globalWmoPath(uint32_t mapId) const
    {
        return m_dir + "/w_" + std::to_string(mapId) + ".tile";
    }

    std::shared_ptr<TerrainTile> CachedTileSource::load(uint32_t mapId, int tx, int ty)
    {
        if (m_dir.empty())
            return m_inner->load(mapId, tx, ty);

        // 1
        std::string path = pathFor(mapId, tx, ty);
        if (auto cached = readTile(path))
            return cached;

        // 2.
        std::string wmoPath = globalWmoPath(mapId);
        if (auto wcached = readTile(wmoPath))
            return wcached;

        // 3.
        auto tile = m_inner->load(mapId, tx, ty);
        if (!tile)
            return nullptr;

        // 4.
        if (tile->isGlobalWmo)
        {
            writeTile(*tile, wmoPath);
        }
        else
        {
            writeTile(*tile, path);
        }

        return tile;
    }

} // namespace world::terrain
