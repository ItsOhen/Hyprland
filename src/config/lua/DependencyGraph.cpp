#include "DependencyGraph.hpp"

#include <algorithm>

using namespace Config::Lua;

void CDependencyGraph::addPath(const std::string& path, const std::string& moduleName) {
    if (!m_moduleNameByPath.contains(path))
        m_moduleNameByPath[path] = moduleName;
    m_moduleNameToPath[moduleName] = path;
}

void CDependencyGraph::recordDependency(const std::string& requiredPath, const std::string& dependentPath) {
    m_dependents[requiredPath].insert(dependentPath);
}

bool CDependencyGraph::hasPath(const std::string& path) const {
    return m_moduleNameByPath.contains(path);
}

std::string CDependencyGraph::moduleNameForPath(const std::string& path) const {
    auto it = m_moduleNameByPath.find(path);
    return it != m_moduleNameByPath.end() ? it->second : "";
}

std::string CDependencyGraph::moduleNameToPath(const std::string& name) const {
    auto it = m_moduleNameToPath.find(name);
    return it != m_moduleNameToPath.end() ? it->second : "";
}

std::unordered_set<std::string> CDependencyGraph::getDependents(const std::string& path) const {
    auto it = m_dependents.find(path);
    return it != m_dependents.end() ? it->second : std::unordered_set<std::string>{};
}

void CDependencyGraph::clear() {
    m_moduleNameByPath.clear();
    m_moduleNameToPath.clear();
    m_dependents.clear();
}
