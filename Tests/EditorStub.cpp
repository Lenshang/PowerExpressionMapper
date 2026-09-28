#include "../Source/PluginProcessor.h"

// The headless test runner is built with JUCE_WEB_BROWSER=0 and does not link
// the WebView editor (that would require the WebView2 SDK on Windows). The
// processor's vtable still needs a createEditor() definition to link.
juce::AudioProcessorEditor* ExpressionMapperAudioProcessor::createEditor()
{
    return nullptr;
}
