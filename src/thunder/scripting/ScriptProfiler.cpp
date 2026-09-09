#include "thunder/scripting/ScriptProgram.hpp"

#include <string>

namespace thunder {

std::string ScriptProfiler::dump_flamegraph_json() const {
    std::string out = "{\"name\":\"root\",\"value\":0,\"children\":[";
    bool first = true;
    for (const auto& r : records_) {
        if (!first) out += ",";
        first = false;
        out += "{\"name\":\"0x" + std::to_string(r.script_hash) + "\",";
        out += "\"invocations\":" + std::to_string(r.invocations) + ",";
        out += "\"total_ns\":" + std::to_string(r.total_nanoseconds) + ",";
        out += "\"max_ns\":" + std::to_string(r.max_nanoseconds) + ",";
        out += "\"value\":" + std::to_string(r.total_nanoseconds) + "}";
    }
    out += "]}";
    return out;
}

} // namespace thunder
