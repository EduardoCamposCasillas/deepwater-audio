#include "presets/EbbPresets.h"

#include <vector>

#include <BinaryData.h>

#include "Parameters.h"
#include "dsp/CurveShapes.h"

namespace ebb::presets
{
namespace
{
const juce::Identifier pointTag { "Point" };
const juce::Identifier shapeId { "shape" };
const juce::Identifier xId { "x" }, yId { "y" }, tensionId { "tension" };
} // namespace

juce::ValueTree curveToTree (const deepwater::dsp::BreakpointCurve& curve)
{
    juce::ValueTree tree (curveTag);
    for (int i = 0; i < curve.size(); ++i)
        tree.appendChild (juce::ValueTree (pointTag, { { xId, curve[i].x }, { yId, curve[i].y }, { tensionId, curve[i].tension } }), nullptr);
    return tree;
}

deepwater::dsp::BreakpointCurve curveFromTree (const juce::ValueTree& tree, juce::StringArray* problems)
{
    const auto fail = [problems] (const juce::String& text) {
        if (problems != nullptr)
            problems->add (text);
        return ebb::dsp::defaultCurve();
    };

    if (! tree.isValid())
        return ebb::dsp::defaultCurve();
    if (! tree.hasType (curveTag))
        return fail ("curva: se esperaba <Curve>");

    // Una forma de fábrica por su nombre (los mismos del menú FORMA).
    if (tree.hasProperty (shapeId))
    {
        const auto name = tree[shapeId].toString().trim();
        for (const auto& shape : ebb::dsp::curveShapes)
            if (name.equalsIgnoreCase (juce::String::fromUTF8 (shape.name)))
                return shape.curve;
        return fail ("curva: forma desconocida \"" + name + "\"");
    }

    // Puntos sueltos (los presets del usuario y el estado del proyecto). Lo que esté fuera de rango lo sanea la curva.
    std::vector<deepwater::dsp::CurvePoint> points;
    for (const auto& point : tree)
        if (point.hasType (pointTag))
            points.push_back ({ static_cast<float> (point[xId]), static_cast<float> (point[yId]),
                                static_cast<float> (point.getProperty (tensionId, 0.0f)) });
    if (points.size() < 2)
        return fail ("curva: necesita al menos dos puntos");
    if (points.size() > static_cast<size_t> (deepwater::dsp::BreakpointCurve::maxPoints) && problems != nullptr)
        problems->add ("curva: solo se usan los primeros " + juce::String (deepwater::dsp::BreakpointCurve::maxPoints) + " puntos");

    return { points.data(), static_cast<int> (points.size()) }; // el constructor ordena y sanea
}

juce::StringArray categoryNames()
{
    // "Init" primero y "Otros" al final (convención de libs/presets). Por estilo (dembow, trap, house: cómo se usa en
    // cada uno), Mezcla = ducking que no se oye como efecto, Creativo = el ducking ES el efecto.
    return { "Init", "Dembow", "Trap", "House", "Mezcla", "Creativo", "Otros" };
}

deepwater::presets::PresetFormat format (std::function<deepwater::dsp::BreakpointCurve()> getCurve,
                                         std::function<void (const deepwater::dsp::BreakpointCurve&)> setCurve)
{
    deepwater::presets::PresetFormat result;
    result.productName = "Ebb";
    result.rootTag = "EbbPreset";
    result.fileExtension = fileExtension;
    result.categories = categoryNames();
    result.userFolder = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Ebb").getChildFile ("Presets");
    result.excludedParameters.add (ebb::params::bypass);

    result.extraStateTag = curveTag;
    result.captureExtraState = [getCurve] { return curveToTree (getCurve()); };
    result.applyExtraState = [setCurve] (const juce::ValueTree& tree) {
        juce::StringArray problems;
        setCurve (curveFromTree (tree, &problems));
        return problems;
    };

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

} // namespace ebb::presets
