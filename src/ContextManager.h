#pragma once
#include "ContextBase.h"
#include "RenderContext.h"
#include "ImmediateContext.h"

#include <memory>
#include <array>

enum CONTEXT
{
    CONTEXT_SCENE,
    CONTEXT_UI,
    CONTEXT_IMMEDIATE,
    CONTEXT_COUNT
};

class ContextManager
{
public:
    void Initalize()
    {
        m_contexts[CONTEXT_SCENE] = std::make_unique<RenderContext>();
        m_contexts[CONTEXT_UI] = std::make_unique<RenderContext>();
        // TODO: Implement shared immediate context
        //m_contexts[CONTEXT_IMMEDIATE] = std::make_unique<ImmediateContext>();
    }
    ContextBase* getContext(CONTEXT context) {
        return m_contexts[context].get();
    }
private:
    // Todo: Smart pointer?
    std::array<std::unique_ptr<ContextBase>, CONTEXT_COUNT> m_contexts;
};
