#include "Animation/AnimatorController.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace molten {

int AnimatorController::findState(const std::string& name) const {
    for (size_t i = 0; i < states.size(); i++) {
        if (states[i].name == name) return int(i);
    }
    return -1;
}

int AnimatorController::Layer::findState(const std::string& name) const {
    for (size_t i = 0; i < states.size(); i++) {
        if (states[i].name == name) return int(i);
    }
    return -1;
}

const AnimatorController::Parameter* AnimatorController::findParameter(const std::string& name) const {
    for (const Parameter& parameter : parameters) {
        if (parameter.name == name) return &parameter;
    }
    return nullptr;
}

namespace {

std::vector<std::string> wordsOf(const std::string& line) {
    std::vector<std::string> words;
    std::istringstream in(line);
    std::string word;
    while (in >> word) {
        if (word[0] == '"') {
            // A quoted name may hold spaces.
            std::string rest = word.substr(1);
            while (!rest.empty() && rest.back() != '"' && in >> word) rest += " " + word;
            if (!rest.empty() && rest.back() == '"') rest.pop_back();
            word = rest;
        }
        words.push_back(word);
    }
    return words;
}

std::string named(const std::string& name) {
    return name.find(' ') == std::string::npos && !name.empty() ? name : "\"" + name + "\"";
}

std::string number(float value) {
    char text[32];
    std::snprintf(text, sizeof(text), "%g", double(value));
    return text;
}

}  // namespace

std::optional<AnimatorController> AnimatorController::parse(const std::string& text, std::string* error) {
    AnimatorController controller;
    std::istringstream lines(text);
    std::string line;
    int lineNumber = 0;
    auto fail = [&](const std::string& why) -> std::optional<AnimatorController> {
        if (error) *error = "line " + std::to_string(lineNumber) + ": " + why;
        return std::nullopt;
    };
    State* state = nullptr;
    // Where states, transitions and "start" go: the controller's own, or the layer begun last.
    std::vector<State>* states = &controller.states;
    std::vector<Transition>* transitions = &controller.transitions;
    std::string* start = &controller.start;
    bool sawHeader = false;
    while (std::getline(lines, line)) {
        lineNumber++;
        size_t comment = line.find('#');
        if (comment != std::string::npos) line = line.substr(0, comment);
        std::vector<std::string> w = wordsOf(line);
        if (w.empty()) continue;
        const std::string& key = w[0];
        if (key == "molten-animator") {
            sawHeader = true;
        } else if (key == "parameter" && w.size() >= 3) {
            Parameter parameter;
            parameter.kind = w[1] == "bool" ? Parameter::Kind::Bool : w[1] == "trigger" ? Parameter::Kind::Trigger : Parameter::Kind::Float;
            if (w[1] != "bool" && w[1] != "trigger" && w[1] != "float") return fail("a parameter is float, bool or trigger");
            parameter.name = w[2];
            if (w.size() > 3) parameter.value = w[3] == "true" ? 1.0f : w[3] == "false" ? 0.0f : std::strtof(w[3].c_str(), nullptr);
            controller.parameters.push_back(parameter);
        } else if (key == "layer" && w.size() >= 2) {
            controller.layers.push_back(Layer{});
            Layer& layer = controller.layers.back();
            layer.name = w[1];
            for (size_t i = 2; i < w.size(); i++) {
                if (w[i] == "additive") layer.additive = true;
                else if (w[i] == "weight" && i + 1 < w.size()) {
                    const std::string& value = w[++i];
                    char* end = nullptr;
                    float number = std::strtof(value.c_str(), &end);
                    if (end && *end == 0 && end != value.c_str()) layer.weight = std::clamp(number, 0.0f, 1.0f);
                    else layer.weightBy = value;
                } else if (w[i] == "mask") {
                    while (i + 1 < w.size() && w[i + 1] != "weight" && w[i + 1] != "additive") layer.mask.push_back(w[++i]);
                } else {
                    return fail("\"" + w[i] + "\" is not understood in a layer");
                }
            }
            states = &layer.states;
            transitions = &layer.transitions;
            start = &layer.start;
            state = nullptr;
        } else if (key == "state" && w.size() >= 2) {
            states->push_back(State{});
            state = &states->back();
            state->name = w[1];
            for (size_t i = 2; i + 1 < w.size(); i += 2) {
                if (w[i] == "blend") {
                    state->blendBy = w[i + 1];
                    // A second name, not a keyword: the other parameter of a 2D blend tree.
                    if (i + 2 < w.size() && w[i + 2] != "loops" && w[i + 2] != "speed" && w[i + 2] != "blend") state->blendByY = w[i + 2], i++;
                } else if (w[i] == "loops") state->loops = w[i + 1] != "0" && w[i + 1] != "false";
                else if (w[i] == "speed") state->speed = std::strtof(w[i + 1].c_str(), nullptr);
            }
        } else if ((key == "motion" || key == "event" || key == "loops" || key == "speed" || key == "blend") && !state) {
            return fail("\"" + key + "\" before any state");
        } else if (key == "motion" && w.size() >= 2) {
            state->motions.push_back(Motion{w[1], w.size() > 2 ? std::strtof(w[2].c_str(), nullptr) : 0.0f,
                                            w.size() > 3 ? std::strtof(w[3].c_str(), nullptr) : 0.0f});
        } else if (key == "event" && w.size() >= 2) {
            state->events.push_back(Event{w[1], w.size() > 2 ? std::clamp(std::strtof(w[2].c_str(), nullptr), 0.0f, 1.0f) : 0.0f});
        } else if (key == "loops" && w.size() >= 2) {
            state->loops = w[1] != "0" && w[1] != "false";
        } else if (key == "speed" && w.size() >= 2) {
            state->speed = std::strtof(w[1].c_str(), nullptr);
        } else if (key == "blend" && w.size() >= 2) {
            state->blendBy = w[1];
            if (w.size() > 2) state->blendByY = w[2];
        } else if (key == "start" && w.size() >= 2) {
            *start = w[1];
        } else if (key == "transition" && w.size() >= 3) {
            Transition transition;
            transition.from = w[1];
            transition.to = w[2];
            size_t i = 3;
            if (transition.to == "->" && w.size() >= 4) transition.to = w[3], i = 4;      // "transition A -> B" reads well too
            while (i < w.size()) {
                if (w[i] == "when" || w[i] == "and") {
                    if (i + 1 >= w.size()) return fail("\"" + w[i] + "\" with nothing after it");
                    Condition condition;
                    condition.parameter = w[i + 1];
                    i += 2;
                    if (i + 1 < w.size() && (w[i] == ">" || w[i] == "<")) {
                        condition.test = w[i] == ">" ? Condition::Test::Greater : Condition::Test::Less;
                        condition.value = std::strtof(w[i + 1].c_str(), nullptr);
                        i += 2;
                    } else if (i < w.size() && (w[i] == "true" || w[i] == "false")) {
                        condition.test = w[i] == "true" ? Condition::Test::IsTrue : Condition::Test::IsFalse;
                        i++;
                    } else {
                        condition.test = Condition::Test::IsTrue;         // a bool, or a trigger: decided below
                    }
                    transition.conditions.push_back(condition);
                } else if (w[i] == "exit" && i + 1 < w.size()) {
                    transition.exitAt = std::clamp(std::strtof(w[i + 1].c_str(), nullptr), 0.0f, 1.0f);
                    i += 2;
                } else if (w[i] == "blend" && i + 1 < w.size()) {
                    transition.blend = std::max(std::strtof(w[i + 1].c_str(), nullptr), 0.0f);
                    i += 2;
                } else {
                    return fail("\"" + w[i] + "\" is not understood in a transition");
                }
            }
            transitions->push_back(transition);
        } else {
            return fail("\"" + key + "\" is not understood");
        }
    }
    if (!sawHeader) return fail("not an animator file: the first line must be \"molten-animator 1\"");
    // Triggers are tested as triggers, whatever the line said; and every state named is there.
    auto check = [&](std::vector<Transition>& transitions, const std::vector<State>& states, std::string& start) -> std::optional<std::string> {
        auto findState = [&](const std::string& name) {
            for (size_t i = 0; i < states.size(); i++) if (states[i].name == name) return int(i);
            return -1;
        };
        for (Transition& transition : transitions) {
            for (Condition& condition : transition.conditions) {
                const Parameter* parameter = controller.findParameter(condition.parameter);
                if (!parameter) {
                    lineNumber = 0;
                    return "the transition " + transition.from + " to " + transition.to + " tests \"" + condition.parameter + "\", which is no parameter";
                }
                if (parameter->kind == Parameter::Kind::Trigger) condition.test = Condition::Test::Triggered;
            }
            if (transition.from != "Any" && findState(transition.from) < 0) return "no state called \"" + transition.from + "\"";
            if (findState(transition.to) < 0) return "no state called \"" + transition.to + "\"";
        }
        if (start.empty() && !states.empty()) start = states.front().name;
        return std::nullopt;
    };
    if (std::optional<std::string> problem = check(controller.transitions, controller.states, controller.start)) return fail(*problem);
    for (Layer& layer : controller.layers) {
        if (std::optional<std::string> problem = check(layer.transitions, layer.states, layer.start)) return fail("layer " + layer.name + ": " + *problem);
        if (!layer.weightBy.empty() && !controller.findParameter(layer.weightBy)) return fail("layer " + layer.name + ": \"" + layer.weightBy + "\" is no parameter");
    }
    return controller;
}

std::string AnimatorController::toText() const {
    std::string out = "molten-animator 1\n";
    for (const Parameter& parameter : parameters) {
        out += "parameter " + std::string(parameter.kind == Parameter::Kind::Bool ? "bool" : parameter.kind == Parameter::Kind::Trigger ? "trigger" : "float") +
               " " + named(parameter.name);
        if (parameter.kind != Parameter::Kind::Trigger) out += " " + number(parameter.value);
        out += "\n";
    }
    auto writeStates = [&](const std::vector<State>& states, const std::string& indent) {
    for (const State& state : states) {
        out += "\n" + indent + "state " + named(state.name);
        if (!state.blendBy.empty()) out += " blend " + named(state.blendBy) + (state.blendByY.empty() ? "" : " " + named(state.blendByY));
        if (!state.loops) out += " loops 0";
        if (state.speed != 1) out += " speed " + number(state.speed);
        out += "\n";
        for (const Motion& motion : state.motions) out += indent + "  motion " + named(motion.clip) + (state.blendBy.empty() ? "" : " " + number(motion.at)) +
                                                    (state.blendByY.empty() ? "" : " " + number(motion.y)) + "\n";
        for (const Event& event : state.events) out += indent + "  event " + named(event.name) + " " + number(event.at) + "\n";
    }
    };
    auto writeTransitions = [&](const std::vector<Transition>& transitions, const std::string& start, const std::string& indent) {
    if (!start.empty()) out += "\n" + indent + "start " + named(start) + "\n";
    for (const Transition& transition : transitions) {
        out += indent + "transition " + named(transition.from) + " " + named(transition.to);
        for (size_t i = 0; i < transition.conditions.size(); i++) {
            const Condition& condition = transition.conditions[i];
            out += (i == 0 ? " when " : " and ") + named(condition.parameter);
            switch (condition.test) {
                case Condition::Test::IsTrue: out += " true"; break;
                case Condition::Test::IsFalse: out += " false"; break;
                case Condition::Test::Greater: out += " > " + number(condition.value); break;
                case Condition::Test::Less: out += " < " + number(condition.value); break;
                case Condition::Test::Triggered: break;
            }
        }
        if (transition.exitAt >= 0) out += " exit " + number(transition.exitAt);
        if (transition.blend != 0.2f) out += " blend " + number(transition.blend);
        out += "\n";
    }
    };
    writeStates(states, "");
    writeTransitions(transitions, start, "");
    for (const Layer& layer : layers) {
        out += "\nlayer " + named(layer.name);
        if (!layer.mask.empty()) {
            out += " mask";
            for (const std::string& bone : layer.mask) out += " " + named(bone);
        }
        if (!layer.weightBy.empty()) out += " weight " + named(layer.weightBy);
        else if (layer.weight != 1) out += " weight " + number(layer.weight);
        if (layer.additive) out += " additive";
        out += "\n";
        writeStates(layer.states, "  ");
        writeTransitions(layer.transitions, layer.start, "  ");
    }
    return out;
}

std::vector<float> AnimatorController::blendWeights(const std::vector<Motion>& motions, float value) {
    std::vector<float> weights(motions.size(), 0.0f);
    if (motions.empty()) return weights;
    std::vector<size_t> order(motions.size());
    for (size_t i = 0; i < order.size(); i++) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return motions[a].at < motions[b].at; });
    if (value <= motions[order.front()].at) {
        weights[order.front()] = 1;
        return weights;
    }
    if (value >= motions[order.back()].at) {
        weights[order.back()] = 1;
        return weights;
    }
    for (size_t k = 0; k + 1 < order.size(); k++) {
        float low = motions[order[k]].at, high = motions[order[k + 1]].at;
        if (value >= low && value <= high) {
            float t = high > low ? (value - low) / (high - low) : 0.0f;
            weights[order[k]] = 1 - t;
            weights[order[k + 1]] = t;
            break;
        }
    }
    return weights;
}

std::vector<float> AnimatorController::blendWeights2D(const std::vector<Motion>& motions, float x, float y) {
    std::vector<float> weights(motions.size(), 0.0f);
    if (motions.empty()) return weights;
    float sum = 0;
    for (size_t i = 0; i < motions.size(); i++) {
        float weight = 1;
        float px = x - motions[i].at, py = y - motions[i].y;
        for (size_t j = 0; j < motions.size() && weight > 0; j++) {
            if (j == i) continue;
            float ex = motions[j].at - motions[i].at, ey = motions[j].y - motions[i].y;
            float length = ex * ex + ey * ey;
            if (length <= 1e-8f) continue;                         // two at the same point
            weight = std::min(weight, std::clamp(1 - (px * ex + py * ey) / length, 0.0f, 1.0f));
        }
        weights[i] = weight;
        sum += weight;
    }
    if (sum > 0) {
        for (float& weight : weights) weight /= sum;
        return weights;
    }
    // Outside every band (cannot happen for a point among them): the nearest alone.
    size_t nearest = 0;
    float best = 1e30f;
    for (size_t i = 0; i < motions.size(); i++) {
        float d = (x - motions[i].at) * (x - motions[i].at) + (y - motions[i].y) * (y - motions[i].y);
        if (d < best) best = d, nearest = i;
    }
    weights[nearest] = 1;
    return weights;
}

}  // namespace molten
