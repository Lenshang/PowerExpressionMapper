#include "WebViewBridge.h"
#include "PluginProcessor.h"

#include <PowerExpressionMapperWebUI.h>

#include <map>

namespace
{
    //==========================================================================
    // Only our own resource-provider page may load; anything else (a link in a
    // hijacked page, a stray navigation) is refused.
    class SinglePageBrowser final : public juce::WebBrowserComponent
    {
    public:
        using WebBrowserComponent::WebBrowserComponent;

        bool pageAboutToLoad (const juce::String& newURL) override
        {
            return newURL == getResourceProviderRoot() || newURL == "about:blank";
        }
    };

    //==========================================================================
    // URL path -> binary-data resource name. juceaide mangles resource names by
    // replacing spaces and dots with underscores ("index.html" -> "index_html");
    // this table must mirror the SOURCES list of juce_add_binary_data in
    // CMakeLists.txt.
    const std::map<juce::String, const char*> uiFiles
    {
        { "index.html", "index_html" },
        { "style.css",  "style_css" },
        { "app.js",     "app_js" },
    };

    juce::String mimeTypeForPath (const juce::String& path)
    {
        if (path.endsWith (".html")) return "text/html";
        if (path.endsWith (".css"))  return "text/css";
        if (path.endsWith (".js"))   return "text/javascript";
        if (path.endsWith (".svg"))  return "image/svg+xml";
        if (path.endsWith (".png"))  return "image/png";
        return "application/octet-stream";
    }
}

//==============================================================================
WebViewUIBridge::WebViewUIBridge (ExpressionMapperAudioProcessor& processorToUse)
    : processor (processorToUse)
{
    juce::WebBrowserComponent::Options options
        = juce::WebBrowserComponent::Options {}
              .withBackend (juce::WebBrowserComponent::Options::Backend::webview2)
              .withKeepPageLoadedWhenBrowserIsHidden()
              .withNativeIntegrationEnabled()
              .withEventListener ("uiCommand",
                                  [this] (const auto& payload) { handleUiCommand (payload); })
              .withResourceProvider ([this] (const auto& url) { return getResource (url); });

   #if JUCE_WINDOWS
    // Keep the WebView2 profile out of the host's own data folder.
    options = options.withWinWebView2Options (
        juce::WebBrowserComponent::Options::WinWebView2 {}
            .withUserDataFolder (juce::File::getSpecialLocation (juce::File::tempDirectory)
                                     .getChildFile ("PowerExpressionMapper_WebView2")));
   #endif

    web = std::make_unique<SinglePageBrowser> (std::move (options));
}

WebViewUIBridge::~WebViewUIBridge()
{
    *aliveFlag = false;
}

//==============================================================================
void WebViewUIBridge::pushState()
{
    auto* root = new juce::DynamicObject();
    juce::Array<juce::var> entries;

    const int numEntries = processor.getNumEntries();
    entries.ensureStorageAllocated (numEntries);

    for (int i = 0; i < numEntries; ++i)
    {
        const auto entry = processor.getEntry (i);

        auto* entryObject = new juce::DynamicObject();
        entryObject->setProperty ("name", entry.name);
        entryObject->setProperty ("note", entry.note);
        entries.add (juce::var (entryObject));
    }

    root->setProperty ("entries", entries);
    root->setProperty ("delaySamples", processor.getDelaySamples());
    root->setProperty ("sampleRate", processor.getSampleRate());

    web->emitEventIfBrowserIsVisible ("stateChanged", juce::var (root));
}

void WebViewUIBridge::pushToast (const juce::String& kind, const juce::String& text)
{
    auto* payload = new juce::DynamicObject();
    payload->setProperty ("kind", kind);
    payload->setProperty ("text", text);

    web->emitEventIfBrowserIsVisible ("toast", juce::var (payload));
}

//==============================================================================
void WebViewUIBridge::handleUiCommand (const juce::var& command)
{
    const auto type = command.getProperty ("type", {}).toString();

    if (type == "requestState")
    {
        pushState();
    }
    else if (type == "setDelay")
    {
        processor.setDelaySamples ((int) command.getProperty ("samples", 0));
    }
    else if (type == "setEntryName")
    {
        const int index = (int) command.getProperty ("index", -1);

        if (juce::isPositiveAndBelow (index, processor.getNumEntries()))
        {
            auto entry = processor.getEntry (index);
            entry.name = command.getProperty ("name", {}).toString();
            processor.setEntry (index, entry);
        }
    }
    else if (type == "setEntryNote")
    {
        const int index = (int) command.getProperty ("index", -1);

        if (juce::isPositiveAndBelow (index, processor.getNumEntries()))
        {
            const int note = juce::jlimit (0, 127, (int) command.getProperty ("note", -1));
            auto entry = processor.getEntry (index);
            entry.note = note;
            processor.setEntry (index, entry);
        }
    }
    else if (type == "addEntry")
    {
        const int index = (int) command.getProperty ("index", -1);

        if (juce::isPositiveAndBelow (index, processor.getNumEntries()))
            processor.insertEntry (index);
        else
            processor.addEntry();
    }
    else if (type == "removeEntry")
    {
        const int index = (int) command.getProperty ("index", -1);

        if (juce::isPositiveAndBelow (index, processor.getNumEntries()))
            processor.removeEntry (index);
    }
    else if (type == "clearEntries")
    {
        processor.clearEntries();
    }
    else if (type == "importFile")
    {
        openImportDialog();
    }
    else if (type == "exportFile")
    {
        openExportDialog();
    }
}

//==============================================================================
void WebViewUIBridge::openImportDialog()
{
    chooser = std::make_shared<juce::FileChooser> ("Import Mapping", juce::File(),
                                                   "*.exprmap;*.bwdrm;*.csv");

    chooser->launchAsync (juce::FileBrowserComponent::openMode
                              | juce::FileBrowserComponent::canSelectFiles,
                          [fc = chooser, alive = std::weak_ptr<bool> (aliveFlag), this]
                          (const juce::FileChooser& fileChooser)
                          {
                              if (alive.expired())
                                  return;

                              const auto file = fileChooser.getResult();

                              if (! file.existsAsFile())
                                  return;

                              if (processor.importFromFile (file))
                              {
                                  pushToast ("info", "Imported "
                                                         + juce::String (processor.getNumEntries())
                                                         + " entries from "
                                                         + file.getFileName() + ".");
                              }
                              else
                              {
                                  pushToast ("error",
                                             "Could not import the selected file.\n"
                                             "Expected CSV lines like: Name,Note "
                                             "(e.g. \"Kick,36\" or \"Kick,D1\").");
                              }
                          });
}

void WebViewUIBridge::openExportDialog()
{
    const auto defaultFile = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                                 .getChildFile ("expression-map.exprmap");

    chooser = std::make_shared<juce::FileChooser> ("Export Mapping", defaultFile, "*.exprmap");

    chooser->launchAsync (juce::FileBrowserComponent::saveMode
                              | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
                          [fc = chooser, alive = std::weak_ptr<bool> (aliveFlag), this]
                          (const juce::FileChooser& fileChooser)
                          {
                              if (alive.expired())
                                  return;

                              const auto file = fileChooser.getResult().withFileExtension ("exprmap");

                              if (file.getFileName().isEmpty())
                                  return;

                              if (processor.exportToFile (file))
                                  pushToast ("info", "Exported to " + file.getFullPathName() + ".");
                              else
                                  pushToast ("error", "Could not write to the selected file.");
                          });
}

//==============================================================================
std::optional<juce::WebBrowserComponent::Resource>
    WebViewUIBridge::getResource (const juce::String& url) const
{
    auto path = url.fromFirstOccurrenceOf ("/", false, false);
    path = path.upToFirstOccurrenceOf ("?", false, false);
    path = path.upToFirstOccurrenceOf ("#", false, false);

    if (path.isEmpty())
        path = "index.html";

    if (const auto* resourceName = [&]() -> const char*
        {
            if (const auto it = uiFiles.find (path); it != uiFiles.end())
                return it->second;
            return nullptr;
        }())
    {
        int dataSize = 0;

        if (const auto* data = PowerExpressionMapperWebUI::getNamedResource (resourceName, dataSize))
        {
            std::vector<std::byte> bytes;
            bytes.reserve ((size_t) dataSize);

            for (int i = 0; i < dataSize; ++i)
                bytes.push_back (std::byte { (unsigned char) data[i] });

            return juce::WebBrowserComponent::Resource { std::move (bytes), mimeTypeForPath (path) };
        }
    }

    return {};
}
