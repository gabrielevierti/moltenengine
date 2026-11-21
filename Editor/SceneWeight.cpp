#include "SceneWeight.hpp"

#include <algorithm>
#include <cstdio>
#include <map>
#include <set>

namespace molten {

WeightReport weighScene(const std::vector<WeightPart>& parts, WeightOrder order) {
    WeightReport report;
    std::map<const void*, int> users;
    std::map<std::pair<const void*, const void*>, int> together;
    for (const WeightPart& part : parts) {
        if (part.mesh) users[part.mesh]++;
        together[{part.mesh, part.material}]++;
    }
    report.draws = int(together.size());
    std::set<const void*> meshes, textures;
    for (const WeightPart& part : parts) {
        EntityWeight row;
        row.entity = part.entity;
        row.name = part.name;
        row.triangles = part.triangles * part.copies;
        row.meshBytes = part.meshBytes;
        row.sharedWith = part.mesh ? users[part.mesh] - 1 : 0;
        row.drawnWith = together[{part.mesh, part.material}] - 1;
        for (const auto& [texture, bytes] : part.textures) {
            row.textureBytes += bytes;
            if (texture && textures.insert(texture).second) report.textureBytes += bytes;
        }
        if (part.mesh && meshes.insert(part.mesh).second) report.meshBytes += part.meshBytes;
        report.triangles += row.triangles;
        report.rows.push_back(row);
    }
    report.meshes = int(meshes.size());
    report.textures = int(textures.size());
    std::stable_sort(report.rows.begin(), report.rows.end(), [order](const EntityWeight& a, const EntityWeight& b) {
        switch (order) {
            case WeightOrder::Triangles: return a.triangles > b.triangles;
            case WeightOrder::Memory: return a.meshBytes + a.textureBytes > b.meshBytes + b.textureBytes;
            case WeightOrder::Name: return a.name < b.name;
        }
        return false;
    });
    return report;
}

size_t pictureBytes(int width, int height, int bytesPerPixel, bool hasMipmaps) {
    size_t bytes = size_t(std::max(width, 0)) * size_t(std::max(height, 0)) * size_t(std::max(bytesPerPixel, 0));
    return hasMipmaps ? bytes + bytes / 3 : bytes;
}

std::string bytesText(size_t bytes) {
    char text[32];
    if (bytes >= 1024 * 1024) std::snprintf(text, sizeof(text), "%.1f MB", double(bytes) / (1024.0 * 1024.0));
    else if (bytes >= 1024) std::snprintf(text, sizeof(text), "%.0f KB", double(bytes) / 1024.0);
    else std::snprintf(text, sizeof(text), "%zu B", bytes);
    return text;
}

}  // namespace molten
