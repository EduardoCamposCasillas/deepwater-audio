#include "presets/RiptidePresets.h"

#include <BinaryData.h>

#include "Parameters.h"

namespace riptide::presets
{

juce::StringArray categoryNames()
{
    // "Init" primero y "Otros" al final (convención de libs/presets). Instrumentos = guitarras, teclados, synths, pads.
    // Batería = una pista (bombo, caja, loop); Bus = un grupo de pistas (la batería entera, los coros).
    return { "Init",
             juce::String::fromUTF8 ("Bater\xc3\xad" "a"),
             "Bajo",
             "Voz",
             "Instrumentos",
             "Bus",
             "Master",
             "Creativo",
             "Otros" };
}

deepwater::presets::PresetFormat format()
{
    deepwater::presets::PresetFormat result;
    result.productName = "Riptide";
    result.rootTag = "RiptidePreset";
    result.fileExtension = fileExtension;
    result.categories = categoryNames();
    result.userFolder = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Riptide").getChildFile ("Presets");
    result.excludedParameters.add (riptide::params::bypass);

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

} // namespace riptide::presets
