#pragma once
#include <set>
#include <queue>
#include <stack>
#include <unordered_map>
#include <vector>

namespace Muyo
{
/// A directed acyclic graph used to order render graph nodes.
///
/// Edges are `from -> to` ("`to` depends on `from`"). Adding an edge that would introduce a cycle
/// is rejected by `AddEdge`. The graph is generic over the node identifier type.
///
/// @tparam T Node identifier (e.g. `std::string` for node names).
template <typename T>
class DependencyGraph
{
private:
    std::unordered_map<T, std::set<T>> m_adjacencyList;
    std::unordered_map<T, int> m_inDegree;  // Track in-degrees for parallel execution

public:
    /// Add a dependency edge `from -> to` (i.e. `to` must run after `from`).
    /// @return `false` if the edge would create a cycle (the edge is still inserted).
    bool AddEdge(const T& from, const T& to)
    {
        m_adjacencyList[from].insert(to);
        m_inDegree[to]++;
        if (m_inDegree.find(from) == m_inDegree.end()) m_inDegree[from] = 0;

        // Check if adding this edge creates a cycle
        return !HasCycle();
    }

    /// Order all nodes so that every node appears after its dependencies.
    /// @return The nodes in a valid execution order.
    std::vector<T> TopologicalSort() const
    {
        std::unordered_map<T, bool> visited;
        std::stack<T> stack;

        for (const auto& pair : m_adjacencyList)
        {
            if (!visited[pair.first])
            {
                DfsUtil(pair.first, visited, stack);
            }
        }

        std::vector<T> result;
        while (!stack.empty())
        {
            result.push_back(stack.top());
            stack.pop();
        }

        return result;
    }

    /// Group nodes into levels that can run in parallel (Kahn's algorithm).
    /// @return One vector of nodes per dependency level, in order.
    std::vector<std::vector<T>> GetParallelExecutionLevels()
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

                for (const T& neighbor : m_adjacencyList[node])
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
        return std::ranges::find(m_adjacencyList.at(from).begin(), m_adjacencyList.at(from).end(), to) != m_adjacencyList.at(from).end();
    }

    /// @return `true` if the graph currently contains a cycle.
    bool HasCycle()
    {
        std::unordered_map<T, bool> visited;
        std::unordered_map<T, bool> recursionStack;

        for (const auto& pair : m_adjacencyList)
        {
            if (HasCycleUtil(pair.first, visited, recursionStack)) return true;
        }

        return false;
    }

private:
    void DfsUtil(const T& vertex, std::unordered_map<T, bool>& visited, std::stack<T>& stack) const
    {
        visited[vertex] = true;

        for (const T& neighbor : m_adjacencyList.at(vertex))
        {
            if (!visited[neighbor])
            {
                DfsUtil(neighbor, visited, stack);
            }
        }

        stack.push(vertex);
    }

    bool HasCycleUtil(const T& node, std::unordered_map<T, bool>& visited, std::unordered_map<T, bool>& recursionStack)
    {
        if (recursionStack[node]) return true;  // Back edge found, cycle detected
        if (visited[node]) return false;

        visited[node] = true;
        recursionStack[node] = true;

        for (const T& neighbor : m_adjacencyList[node])
        {
            if (HasCycleUtil(neighbor, visited, recursionStack)) return true;
        }

        recursionStack[node] = false;  // Backtrack
        return false;
    }
};
}  // namespace Muyo
