#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>

namespace Config::Lua {

    class CDependencyGraph {
      public:
        void addPath(const std::string& path, const std::string& moduleName);
        void recordDependency(const std::string& requiredPath, const std::string& dependentPath);

        bool                     hasPath(const std::string& path) const;
        std::string              moduleNameForPath(const std::string& path) const;
        std::string              moduleNameToPath(const std::string& name) const;
        std::unordered_set<std::string> getDependents(const std::string& path) const;

        void clear();

      private:
        std::unordered_map<std::string, std::string>                     m_moduleNameByPath;
        std::unordered_map<std::string, std::string>                     m_moduleNameToPath;
        std::unordered_map<std::string, std::unordered_set<std::string>> m_dependents;
    };

}
