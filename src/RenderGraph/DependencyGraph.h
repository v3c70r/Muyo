#pragma once
#include <queue>
#include <set>
#include <unordered_map>
#include <vector>

namespace Muyo
{
/// A directed acyclic graph used to order render graph nodes.
///
/// Edges are `from -> to` ("`to` depends on `from`"). `AddNode` registers isolated nodes so they
/// are still scheduled; `AddEdge` validates before mutating, so a rejected edge leaves the graph
/// usable. The graph is generic over the node identifier type (which must be orderable).
///
/// @tparam T Node identifier (e.g. `std::string` for node names).
template <typename T>
class DependencyGraph
{
public:
    /// Register a node with no edges (in-degree 0) so it is included in the ordering.
    void AddNode(const T& node)
    {
        m_adjacencyList.try_emplace(node);
        m_inDegree.try_emplace(node, 0);
    }

    /// Add a dependency edge `from -> to` (i.e. `to` must run after `from`).
    ///
    /// The edge is only inserted if it is valid: self-edges are rejected, duplicates are a no-op,
    /// and an edge that would introduce a cycle is rejected without modifying the graph.
    ///
    /// @return `true` if the edge exists afterwards (inserted or already present), `false` if it
    ///         was rejected (self-edge or would create a cycle).
    bool AddEdge(const T& from, const T& to)
    {
        if (from == to)
        {
            return false;
        }

        auto& neighbors = m_adjacencyList[from];
        if (neighbors.find(to) != neighbors.end())
        {
            return true;  // duplicate edge: idempotent
        }
        m_inDegree.try_emplace(from, 0);

        // Tentatively insert, then roll back if it would create a cycle.
        neighbors.insert(to);
        m_inDegree[to]++;

        if (HasCycle())
        {
            neighbors.erase(to);
            m_inDegree[to]--;
            return false;
        }
        return true;
    }

    /// Order all nodes so that every node appears after its dependencies (Kahn's algorithm).
    ///
    /// Deterministic (ties are broken by node order) and cycle-safe: if the graph contains a cycle
    /// the returned order simply omits the nodes involved in it.
    /// @return The nodes in a valid execution order.
    std::vector<T> TopologicalSort() const
    {
        std::unordered_map<T, int> inDegree = m_inDegree;
        std::set<T> ready;
        for (const auto& [node, degree] : inDegree)
        {
            if (degree == 0) ready.insert(node);
        }

        std::vector<T> result;
        result.reserve(inDegree.size());
        while (!ready.empty())
        {
            const T node = *ready.begin();
            ready.erase(ready.begin());
            result.push_back(node);

            const auto it = m_adjacencyList.find(node);
            if (it == m_adjacencyList.end()) continue;
            for (const T& neighbor : it->second)
            {
                auto degreeIt = inDegree.find(neighbor);
                if (degreeIt == inDegree.end()) continue;
                if (--degreeIt->second == 0)
                {
                    ready.insert(neighbor);
                }
            }
        }

        return result;
    }

    /// Group nodes into levels that can run in parallel (Kahn's algorithm).
    /// @return One vector of nodes per dependency level, in order.
    std::vector<std::vector<T>> GetParallelExecutionLevels() const
    {
        std::queue<T> q;
        std::vector<std::vector<T>> levels;
        std::unordered_map<T, int> tempInDegree = m_inDegree;

        for (const auto& pair : tempInDegree)
        {
            if (pair.second == 0) q.push(pair.first);
        }

        while (!q.empty())
        {
            std::vector<T> level;
            size_t size = q.size();

            for (size_t i = 0; i < size; ++i)
            {
                T node = q.front();
                q.pop();
                level.push_back(node);

                const auto it = m_adjacencyList.find(node);
                if (it == m_adjacencyList.end()) continue;
                for (const T& neighbor : it->second)
                {
                    if (--tempInDegree[neighbor] == 0)
                    {
                        q.push(neighbor);
                    }
                }
            }
            levels.push_back(level);
        }

        return levels;
    }

    /// @return `true` if a direct edge `from -> to` exists.
    bool IsAdjacentTo(const T& from, const T& to) const
    {
        const auto it = m_adjacencyList.find(from);
        return it != m_adjacencyList.end() && it->second.find(to) != it->second.end();
    }

    /// @return `true` if the graph currently contains a cycle.
    bool HasCycle() const
    {
        // A cyclic graph has nodes that never reach in-degree 0, so Kahn's ordering is short.
        size_t nVisitCount = 0;
        std::unordered_map<T, int> inDegree = m_inDegree;
        std::queue<T> q;
        for (const auto& [node, degree] : inDegree)
        {
            if (degree == 0) q.push(node);
        }
        while (!q.empty())
        {
            const T node = q.front();
            q.pop();
            ++nVisitCount;
            const auto it = m_adjacencyList.find(node);
            if (it == m_adjacencyList.end()) continue;
            for (const T& neighbor : it->second)
            {
                if (--inDegree[neighbor] == 0) q.push(neighbor);
            }
        }
        return nVisitCount != inDegree.size();
    }

    /// @return Number of registered nodes.
    size_t NodeCount() const { return m_inDegree.size(); }

private:
    std::unordered_map<T, std::set<T>> m_adjacencyList;
    std::unordered_map<T, int> m_inDegree;  // Track in-degrees for cycle detection and ordering
};
}  // namespace Muyo
