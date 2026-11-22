#include "Scene/SceneAhead.hpp"

#include "Assets/Gltf.hpp"
#include "Assets/ImportSettings.hpp"
#include "Core/TextFile.hpp"

#include <stb_image.h>

#include <algorithm>
#include <cstring>
#include <map>
#include <sstream>
#include <thread>

namespace molten {

namespace {

/// The word a line starts with and the text in quotes after it: `mesh "Models/car.obj" ...`.
bool keyAndName(const std::string& line, std::string& key, std::string& name) {
    size_t start = line.find_first_not_of(" \t");
    if (start == std::string::npos) return false;
    size_t end = line.find_first_of(" \t", start);
    if (end == std::string::npos) return false;
    size_t open = line.find_first_not_of(" \t", end);
    if (open == std::string::npos || line[open] != '"') return false;
    size_t close = line.find('"', open + 1);
    if (close == std::string::npos) return false;
    key = line.substr(start, end - start);
    name = line.substr(open + 1, close - open - 1);
    return true;
}

bool endsWith(const std::string& text, const char* tail) {
    size_t length = std::strlen(tail);
    return text.size() >= length && text.compare(text.size() - length, length, tail) == 0;
}

void addOnce(std::vector<std::string>& list, const std::string& name) {
    if (!name.empty() && std::find(list.begin(), list.end(), name) == list.end()) list.push_back(name);
}

/// The pictures a material's lines name: "baseColorTexture", "normalTexture" and the like.
void picturesOf(const std::string& text, std::vector<std::string>& pictures) {
    std::istringstream lines(text);
    std::string line, key, name;
    while (std::getline(lines, line)) {
        if (keyAndName(line, key, name) && endsWith(key, "Texture")) addOnce(pictures, name);
    }
}

}  // namespace

SceneNeeds sceneNeeds(const std::string& sceneText, const std::function<bool(const std::string& name, std::string& text)>& readText) {
    SceneNeeds needs;
    std::istringstream lines(sceneText);
    std::string line, key, name;
    while (std::getline(lines, line)) {
        if (!keyAndName(line, key, name)) continue;
        if (key == "mesh") {
            bool isBuiltIn = name.empty() || name == "cube" || name == "sphere" || name == "plane" || name == "cylinder";
            if (!isBuiltIn) addOnce(needs.models, name);
        } else if (key == "materialAsset") {
            addOnce(needs.materials, name);
        } else if (endsWith(key, "Texture")) {
            addOnce(needs.pictures, name);          // a material written in the scene itself
        }
    }
    // The pictures of the materials that are files of their own.
    for (const std::string& material : needs.materials) {
        std::string text;
        if (readText && readText(material, text)) picturesOf(text, needs.pictures);
    }
    return needs;
}

SceneAhead readSceneAhead(const std::string& name, const std::string& path, const std::function<std::string(const std::string&)>& pathOf,
                          SceneAheadProgress* progress, int threads) {
    SceneAhead ahead;
    ahead.name = name;
    std::optional<std::string> text = text::readFile(path);
    if (!text) {
        if (progress) progress->done.store(progress->total.load());
        return ahead;
    }
    ahead.text = std::move(*text);
    ahead.isRead = true;

    SceneNeeds needs = sceneNeeds(ahead.text, [&](const std::string& material, std::string& out) {
        std::optional<std::string> read = text::readFile(pathOf(material));
        if (read) out = std::move(*read);
        return read.has_value();
    });

    // One job per file: a model file with many parts is read once, for all of them.
    struct Job {
        std::string file;                       // a model file, or a picture
        std::vector<std::string> parts;         // a model: the names asked for ("car.glb#3", "car.glb")
        std::vector<SceneAhead::Model> models;  // what it made
        SceneAhead::Picture picture;
        bool isPicture = false;
    };
    std::vector<Job> jobs;
    std::map<std::string, size_t> modelJobs;
    for (const std::string& model : needs.models) {
        std::string file = isGltfFile(model) ? gltfFileOf(model) : model;
        auto found = modelJobs.find(file);
        if (found == modelJobs.end()) {
            found = modelJobs.emplace(file, jobs.size()).first;
            jobs.push_back(Job{});
            jobs.back().file = file;
        }
        jobs[found->second].parts.push_back(model);
    }
    for (const std::string& picture : needs.pictures) {
        Job job;
        job.file = picture;
        job.isPicture = true;
        jobs.push_back(std::move(job));
    }
    if (progress) {
        progress->total.store(int(jobs.size()) + 1);
        progress->done.store(1);
    }

    auto run = [&pathOf](Job& job) {
        std::string file = pathOf(job.file);
        if (job.isPicture) {
            int width = 0, height = 0, channels = 0;
            if (unsigned char* pixels = stbi_load(file.c_str(), &width, &height, &channels, 4)) {
                job.picture.name = job.file;
                job.picture.width = width;
                job.picture.height = height;
                job.picture.pixels.assign(pixels, pixels + size_t(width) * size_t(height) * 4);
                stbi_image_free(pixels);
            }
        } else if (isGltfFile(job.file)) {
            std::optional<GltfModel> model = GltfModel::load(file);
            if (!model) return;
            ImportSettings settings = ImportSettings::load(file);
            std::vector<GltfModel::Part> parts = model->parts();
            for (const std::string& wanted : job.parts) {
                int part = -1;
                gltfFileOf(wanted, &part);
                MeshData data;
                if (part < 0) data = model->assembled(model->restPose());
                else if (size_t(part) < parts.size()) data = model->part(parts[size_t(part)]);
                if (data.positions.empty()) continue;
                settings.apply(data);
                job.models.push_back({wanted, std::move(data)});
            }
        } else if (std::optional<std::string> obj = text::readFile(file)) {
            if (std::optional<MeshData> parsed = MeshData::parseOBJ(*obj)) {
                ImportSettings::load(file).apply(*parsed);
                if (!parsed->positions.empty()) job.models.push_back({job.file, std::move(*parsed)});
            }
        }
    };

    // Each worker takes the next job nobody has taken; each job writes only into itself.
    std::atomic<size_t> next{0};
    auto work = [&] {
        for (size_t index = next.fetch_add(1); index < jobs.size(); index = next.fetch_add(1)) {
            run(jobs[index]);
            if (progress) progress->done.fetch_add(1);
        }
    };
    int count = threads > 0 ? threads : int(std::thread::hardware_concurrency()) - 1;
    count = std::clamp(count, 1, 8);
    count = std::min(count, int(std::max<size_t>(jobs.size(), 1)));
    std::vector<std::thread> workers;
    for (int i = 1; i < count; i++) workers.emplace_back(work);
    work();
    for (std::thread& worker : workers) worker.join();

    for (Job& job : jobs) {
        if (job.isPicture) {
            if (!job.picture.pixels.empty()) ahead.pictures.push_back(std::move(job.picture));
        } else {
            for (SceneAhead::Model& model : job.models) ahead.models.push_back(std::move(model));
        }
    }
    return ahead;
}

}  // namespace molten
