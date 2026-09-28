#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "WebViewBridge.h"

class ExpressionMapperAudioProcessor;

/**
    The plugin editor: a single WebView component filling the window.

    All widgets (mapping table, delay control, import/export) live in the
    embedded web page (Source/WebUI); this class only hosts the view, forwards
    host-driven change messages into the page (preset loads from the host) and
    keeps the CLAP-wrapper DPI workaround from the previous native UI.

    The window is resizable.
*/
class ExpressionMapperAudioProcessorEditor
    : public juce::AudioProcessorEditor
    , public juce::ChangeListener
    , private juce::Timer
{
public:
    explicit ExpressionMapperAudioProcessorEditor (ExpressionMapperAudioProcessor&);
    ~ExpressionMapperAudioProcessorEditor() override;

    void resized() override;
    void parentHierarchyChanged() override;

    void changeListenerCallback (juce::ChangeBroadcaster*) override;

private:
    void timerCallback() override;

    ExpressionMapperAudioProcessor& processor;
    WebViewUIBridge bridge;

    int resizeCounter = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ExpressionMapperAudioProcessorEditor)
};
