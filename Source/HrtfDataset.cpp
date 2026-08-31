#include "HrtfDataset.h"
#include <mysofa.h>

HrtfDataset::HrtfDataset() = default;

HrtfDataset::~HrtfDataset()
{
    close();
}

void HrtfDataset::close()
{
    if (easy != nullptr)
    {
        mysofa_close (easy);
        easy = nullptr;
    }
    filterLength = 0;
}

bool HrtfDataset::load (const juce::File& sofaFile, double sampleRate, juce::String& outError)
{
    close(); // release any previously loaded dataset first, see the header's own comment

    int err = MYSOFA_OK;
    int filterLen = 0;
    auto* opened = mysofa_open (sofaFile.getFullPathName().toRawUTF8(), (float) sampleRate, &filterLen, &err);

    if (opened == nullptr || err != MYSOFA_OK)
    {
        outError = "Could not open SOFA file \"" + sofaFile.getFullPathName()
                    + "\" (libmysofa error code " + juce::String (err) + ")";
        if (opened != nullptr)
            mysofa_close (opened);
        return false;
    }

    easy = opened;
    filterLength = filterLen;
    return true;
}

bool HrtfDataset::getFilter (Vec3 direction, std::vector<float>& outLeft, std::vector<float>& outRight,
                              float& outDelayLeftSamples, float& outDelayRightSamples) const
{
    if (easy == nullptr)
        return false;

    outLeft.assign ((size_t) filterLength, 0.0f);
    outRight.assign ((size_t) filterLength, 0.0f);

    mysofa_getfilter_float (easy, direction.x, direction.y, direction.z,
                             outLeft.data(), outRight.data(),
                             &outDelayLeftSamples, &outDelayRightSamples);
    return true;
}
