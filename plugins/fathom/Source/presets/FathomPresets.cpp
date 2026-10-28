#include "presets/FathomPresets.h"

#include <vector>

#include <BinaryData.h>

#include "Parameters.h"

namespace fathom::presets
{

juce::StringArray categoryNames()
{
    // "Init" primero y "Otros" al final (convención de libs/presets). Por lo que se ecualiza (como está ordenada una
    // mezcla de reggaetón): voces, el 808 y el kick del dembow, la percusión, los buses y el master; Creativo = el EQ ES
    // el efecto (teléfono, radio, filtro para automatizar).
    return { "Init", "Voces", "Dembow", juce::String::fromUTF8 ("Percusi\xc3\xb3n"), "Buses", "Master", "Creativo", "Otros" };
}

deepwater::presets::PresetFormat format (std::function<void()> beforeLoad, std::function<void()> afterLoad)
{
    deepwater::presets::PresetFormat result;
    result.productName = "Fathom";
    result.rootTag = "FathomPreset";
    result.fileExtension = fileExtension;
    result.categories = categoryNames();
    result.userFolder = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Fathom").getChildFile ("Presets");
    result.excludedParameters.add (fathom::params::bypass);
    result.beforeLoad = std::move (beforeLoad);
    result.afterLoad = std::move (afterLoad);

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

} // namespace fathom::presets
