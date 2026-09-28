#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

class ExpressionMapperAudioProcessor;

/**
    Owns the plugin's WebView user interface and bridges it to the processor.

    The web page (Source/WebUI) is embedded into the binary via CMake binary
    data and served through the WebBrowserComponent's resource provider, so
    the plugin works fully offline.

    Protocol (JUCE native integration, all on the message thread):
      - UI -> plugin:  "uiCommand" events, see handleUiCommand();
      - plugin -> UI:  "stateChanged" (full snapshot, see pushState()) and
                       "toast" (transient user feedback) events.

    Every structural edit made through the UI goes through the processor's
    edit operations, which broadcast a change message; the editor forwards
    that to pushState() so the page always mirrors the processor.
*/
class WebViewUIBridge
{
public:
    explicit WebViewUIBridge (ExpressionMapperAudioProcessor& processorToUse);
    ~WebViewUIBridge();

    juce::WebBrowserComponent& getWebComponent() { return *web; }

    /** Sends a full state snapshot (entries + delay + sample rate) to the page. */
    void pushState();

    /** Sends transient feedback (kind: "info" or "error") to the page. */
    void pushToast (const juce::String& kind, const juce::String& text);

private:
    void handleUiCommand (const juce::var& command);
    void openImportDialog();
    void openExportDialog();

    std::optional<juce::WebBrowserComponent::Resource> getResource (const juce::String& url) const;

    ExpressionMapperAudioProcessor& processor;
    std::unique_ptr<juce::WebBrowserComponent> web;
    std::shared_ptr<juce::FileChooser> chooser;

    /** Lets async dialog callbacks detect that the bridge died while a
        file dialog was still open. */
    std::shared_ptr<bool> aliveFlag { std::make_shared<bool> (true) };

    JUCE_DECLARE_NON_COPYABLE (WebViewUIBridge)
};
