// Commands.cpp
#include "Commands.hpp"

#include <algorithm>

namespace molten::editor {

std::string Command::menu() const { return path.substr(0, path.find('/')); }

std::string Command::name() const {
    if (nameNow) return nameNow();
    size_t slash = path.rfind('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::string Command::where() const {
    size_t last = path.rfind('/');
    if (last == std::string::npos) return {};
    std::string out;
    for (char c : path.substr(0, last)) {
        if (c == '/') out += " > ";
        else out += c;
    }
    return out;
}

Command& Commands::add(std::string id, std::string path, std::function<void()> run) {
    Command command;
    command.id = std::move(id);
    command.path = std::move(path);
    command.run = std::move(run);
    commands_.push_back(std::move(command));
    return commands_.back();
}

void Commands::line(const std::string& menuPath) { add({}, menuPath + "/-"); }

Command* Commands::find(const std::string& id) {
    if (id.empty()) return nullptr;
    for (Command& command : commands_) {
        if (command.id == id) return &command;
    }
    return nullptr;
}

const Command* Commands::find(const std::string& id) const { return const_cast<Commands*>(this)->find(id); }

bool Commands::run(const std::string& id) const {
    const Command* command = find(id);
    if (!command || !command->run || !command->isEnabled()) return false;
    command->run();
    return true;
}

std::vector<Commands::Row> Commands::rowsOf(const std::string& menuPath) const {
    std::vector<Row> rows;
    const std::string prefix = menuPath + "/";
    for (const Command& command : commands_) {
        if (command.path.compare(0, prefix.size(), prefix) != 0) continue;
        std::string rest = command.path.substr(prefix.size());
        size_t slash = rest.find('/');
        if (slash == std::string::npos) {
            rows.push_back(Row{&command, {}});
            continue;
        }
        // Something of a submenu: the submenu is listed once, where it is first heard of.
        std::string submenu = rest.substr(0, slash);
        bool isListed = std::any_of(rows.begin(), rows.end(), [&](const Row& row) { return row.submenu == submenu; });
        if (!isListed) rows.push_back(Row{nullptr, submenu});
    }
    // A line is between things: none first, none last, never two together.
    std::vector<Row> tidy;
    for (const Row& row : rows) {
        bool isLine = row.command && row.command->isLine();
        if (isLine && (tidy.empty() || (tidy.back().command && tidy.back().command->isLine()))) continue;
        tidy.push_back(row);
    }
    while (!tidy.empty() && tidy.back().command && tidy.back().command->isLine()) tidy.pop_back();
    return tidy;
}

std::vector<std::string> Commands::menus() const {
    std::vector<std::string> menus;
    for (const Command& command : commands_) {
        std::string menu = command.menu();
        if (std::find(menus.begin(), menus.end(), menu) == menus.end()) menus.push_back(menu);
    }
    return menus;
}

}  // namespace molten::editor
