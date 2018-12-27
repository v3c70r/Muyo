#pragma once
class ContextBase
{
public:
    virtual void startRecording() = 0;
    virtual void endRecording() = 0;
    virtual bool isRecording() const = 0;
};
