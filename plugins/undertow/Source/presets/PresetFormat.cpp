#include "presets/PresetManager.h"

#include <BinaryData.h>

namespace undertow::presets
{

deepwater::presets::PresetFormat format()
{
    deepwater::presets::PresetFormat result;
    result.productName = "Undertow";
    result.rootTag = "UndertowPreset";
    result.fileExtension = fileExtension;
    for (const auto* name : categoryNames)
        result.categories.add (name);
    result.userFolder = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Undertow").getChildFile ("Presets");

    // Los .uwpreset de Presets/Factory, compilados dentro del plugin (juce_add_binary_data).
    result.factoryFiles = [] {
        std::vector<deepwater::presets::FactoryFile> files;
        for (int i = 0; i < BinaryData::namedResourceListSize; ++i)
        {
            int size = 0;
            const char* data = BinaryData::getNamedResource (BinaryData::namedResourceList[i], size);
            files.push_back ({ BinaryData::getNamedResourceOriginalFilename (BinaryData::namedResourceList[i]),
                               juce::String::fromUTF8 (data, size) });
        }
        return files;
    };
    return result;
}

} // namespace undertow::presets
