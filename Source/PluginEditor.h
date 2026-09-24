#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "MappingTableComponent.h"

class ExpressionMapperAudioProcessor;

/**
    The plugin editor: title bar, the "delay for unlisted notes" control and
    the mapping table. Listens to the processor so a host-driven state change
    (preset load) refreshes everything.

    The window is resizable. High-DPI scaling is handled in the CLAP wrapper
    (guiWin32Attach) so the editor always works in physical pixels.
*/
class ExpressionMapperAudioProcessorEditor
    : public juce::AudioProcessorEditor
    , public juce::ChangeListener
    , private juce::Timer
{
public:
    explicit ExpressionMapperAudioProcessorEditor (ExpressionMapperAudioProcessor&);
    ~ExpressionMapperAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void parentHierarchyChanged() override;

    void changeListenerCallback (juce::ChangeBroadcaster*) override;

private:
    void timerCallback() override;

    ExpressionMapperAudioProcessor& processor;

    juce::Label titleLabel;
    juce::Label delayLabel;
    juce::Slider delaySlider;
    juce::Label hintLabel;
    MappingTableComponent tableComponent;

    int resizeCounter = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ExpressionMapperAudioProcessorEditor)
};
