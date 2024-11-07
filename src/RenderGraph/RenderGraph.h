#pragma once

#include <iostream>
#include <vector>
#include <stack>
#include <unordered_map>

#include <RenderResources/RenderResourceManager.h>
#include "RenderPass.h"

namespace Muyo
{

struct RenderResourceHandle
{
    const IRenderResource* pRenderResource = nullptr;
    uint32_t nVersion = 0;
};

struct RenderGraphNode
{
    std::vector<RenderResourceHandle> m_vInputResources;
    std::vector<RenderResourceHandle> m_vOuptputResources;
    IRenderPass* pRenderPass = nullptr;
};

class RenderGraph
{
public:
  void AddNode(const std::vector<const IRenderResource*>& vInputResources, const std::vector<const IRenderResource*>& vOutputResources, IRenderPass* pRenderPass)
  {
      // Bump output resource version
      m_vRGNs.push_back(RenderGraphNode());
      RenderGraphNode& node = m_vRGNs.back();
      for (auto* outputResource : vOutputResources)
      {
          if (m_mResourceVersion.count(outputResource) == 0)
          {
              m_mResourceVersion[outputResource] = 0;
          }
          else
          {
              m_mResourceVersion[outputResource]++;
          }

          node.m_vOuptputResources.push_back({ outputResource, m_mResourceVersion[outputResource] });
      }

      for (auto* inputResource : vInputResources)
      {
          node.m_vInputResources.push_back({ inputResource, m_mResourceVersion[inputResource] });
      }
      node.pRenderPass = pRenderPass;
  }

  void ConstructAdjList()
  {
      for (size_t i = 0; i < m_vRGNs.size(); i ++)
          for (size_t j = i; i < m_vRGNs.size(); j ++)
      {
          for (const RenderResourceHandle& inputHandle : m_vRGNs[j].m_vInputResources)
          {
              if (std::find(m_vRGNs[i].m_vOuptputResources.begin(), m_vRGNs[i].m_vOuptputResources.end(), inputHandle) != m_vRGNs[i].m_vOuptputResources.end())
              {
                  // Addresses are valid as long as the graph doesn't move.
                  m_mAdjList[&m_vRGNs[i]].push_back(&m_vRGNs[j]);
                  break;
              }
          }
      }
  }

private:
  std::vector<RenderGraphNode> m_vRGNs;
  std::unordered_map<const RenderGraphNode*, std::vector<const RenderGraphNode*>> m_mAdjList;
  std::unordered_map<const IRenderResource*, uint32_t> m_mResourceVersion;
};

template <typename T>
class Graph {
private:
    std::unordered_map<T, std::vector<T>> adjacencyList;

    void dfsUtil(const T& vertex, std::unordered_map<T, bool>& visited, std::stack<T>& stack) {
        visited[vertex] = true;

        for (const T& neighbor : adjacencyList[vertex]) {
            if (!visited[neighbor]) {
                dfsUtil(neighbor, visited, stack);
            }
        }

        stack.push(vertex);
    }

public:
    void addEdge(const T& from, const T& to) {
        adjacencyList[from].push_back(to);
    }

    std::vector<T> topologicalSort() {
        std::unordered_map<T, bool> visited;
        std::stack<T> stack;

        for (const auto& pair : adjacencyList) {
            if (!visited[pair.first]) {
                dfsUtil(pair.first, visited, stack);
            }
        }

        std::vector<T> result;
        while (!stack.empty()) {
            result.push_back(stack.top());
            stack.pop();
        }

        return result;
    }
};


}    // namespace Muyo
