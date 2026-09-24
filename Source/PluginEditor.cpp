#include "PluginEditor.h"
#include "PluginProcessor.h"

ExpressionMapperAudioProcessorEditor::ExpressionMapperAudioProcessorEditor (ExpressionMapperAudioProcessor& p)
    : juce::AudioProcessorEditor (&p)
    , processor (p)
    , tableComponent (processor)
{
    titleLabel.setText ("PowerExpressionMapper", juce::dontSendNotification);
    titleLabel.setFont (juce::Font (juce::FontOptions (18.0f).withStyle ("Bold")));
    titleLabel.setJustificationType (juce::Justification::centredLeft);
    titleLabel.setColour (juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible (titleLabel);

    delayLabel.setText ("Delay for unlisted notes:", juce::dontSendNotification);
    delayLabel.setJustificationType (juce::Justification::centredLeft);
    delayLabel.setColour (juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible (delayLabel);

    delaySlider.setSliderStyle (juce::Slider::LinearHorizontal);
    delaySlider.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 70, 22);
    delaySlider.setRange (0.0, 8192.0, 1.0);
    delaySlider.setTextValueSuffix (" samples");
    delaySlider.setTooltip ("Notes that are NOT in the list are delayed by this many samples.\n"
                            "Notes in the list always trigger directly, without delay.");
    delaySlider.setValue (processor.getDelaySamples(), juce::dontSendNotification);
    delaySlider.onValueChange = [this]
    {
        processor.setDelaySamples ((int) delaySlider.getValue());
    };
    addAndMakeVisible (delaySlider);

    hintLabel.setText ("Notes in the list pass through instantly; all other notes are delayed. "
                       "Entry names are sent to the host for the piano roll / note lanes.",
                       juce::dontSendNotification);
    hintLabel.setFont (juce::Font (juce::FontOptions (12.0f)));
    hintLabel.setJustificationType (juce::Justification::centredLeft);
    hintLabel.setColour (juce::Label::textColourId, juce::Colours::grey);
    addAndMakeVisible (hintLabel);

    addAndMakeVisible (tableComponent);
    tableComponent.refresh();

    processor.addChangeListener (this);

    setResizable (true, true);
    setResizeLimits (420, 300, 1600, 1000);

    setSize (640, 480);
}

ExpressionMapperAudioProcessorEditor::~ExpressionMapperAudioProcessorEditor()
{
    stopTimer();
    processor.removeChangeListener (this);
}

void ExpressionMapperAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff2b2b2b));
}

void ExpressionMapperAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced (10);

    titleLabel.setBounds (area.removeFromTop (28));
    area.removeFromTop (2);

    // Delay control row: label + slider side by side.
    auto delayRow = area.removeFromTop (26);
    delayLabel.setBounds (delayRow.removeFromLeft (160));
    delaySlider.setBounds (delayRow);

    area.removeFromTop (4);
    hintLabel.setBounds (area.removeFromTop (18));
    area.removeFromTop (4);

    tableComponent.setBounds (area);
}

void ExpressionMapperAudioProcessorEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    tableComponent.refresh();
    // A host preset load may also have changed the delay.
    delaySlider.setValue (processor.getDelaySamples(), juce::dontSendNotification);
    repaint();
}

void ExpressionMapperAudioProcessorEditor::parentHierarchyChanged()
{
    // When the CLAP wrapper attaches our window (addToDesktop), the peer is
    // created with the system DPI scale (e.g. 1.5 at 150%).  The wrapper then
    // overrides the scale to 1.0, but the host window size was already set
    // using the old scale.  We trigger a delayed resize to force the host to
    // re-query our size, which is now correct at scale 1.0.
    startTimerHz (20);
}

void ExpressionMapperAudioProcessorEditor::timerCallback()
{
    // Re-assert our size a few times so the host window catches up with the
    // corrected scale factor.
    setSize (getWidth(), getHeight());
    ++resizeCounter;

    if (resizeCounter >= 5)
        stopTimer();
}
