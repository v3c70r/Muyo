#pragma once
#include "ContextBase.h"

#include <array>

enum CONTEXT
{
    CONTEXT_OBJECT,
    CONTEXT_UI,
    CONTEXT_IMMEDIATE,
    CONTEXT_COUNT
};

class ContextManager
{
public:
    ContextBase& getContext(CONTEXT context) {
        return m_contexts[context];
    }

private:
    std::array<Context, CONTEXT_COUNT> m_contexts;
};
