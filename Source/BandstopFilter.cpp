/*
------------------------------------------------------------------

This file is part of the Open Ephys GUI
Copyright (C) 2022 Open Ephys

------------------------------------------------------------------

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "BandstopFilter.h"

#include "BandstopFilterEditor.h"

#include <stdio.h>


void BandstopFilterSettings::createFilters(int numChannels, float sampleRate_, double lowCut, double highCut)
{

    sampleRate = sampleRate_;

    filters.clear();

    for (int n = 0; n < numChannels; ++n)
    {
        filters.add(new Dsp::SmoothedFilterDesign
            < Dsp::Butterworth::Design::BandStop  // design type
            <4>,                                   // order
            1,                                     // number of channels (must be const)
            Dsp::DirectFormII>(1));               // realization

    }

    updateFilters(lowCut, highCut);
}

void BandstopFilterSettings::updateFilters(double lowCut, double highCut)
{
    for (int n = 0; n < filters.size(); n++)
    {
        setFilterParameters(lowCut, highCut, n);
    }
}

void BandstopFilterSettings::setFilterParameters(double lowCut, double highCut, int channel)
{
    Dsp::Params params;
    params[0] = sampleRate;                 // sample rate
    params[1] = 4;                          // order
    params[2] = (highCut + lowCut) / 2;     // center frequency
    params[3] = highCut - lowCut;           // bandwidth

    filters[channel]->setParams(params);
}


BandstopFilter::BandstopFilter()
    : GenericProcessor("Notch Filter")
{

    addFloatParameter(Parameter::STREAM_SCOPE, "high_cut", "Filter high cut", 61, 0.1, 15000, false);
    addFloatParameter(Parameter::STREAM_SCOPE, "low_cut", "Filter low cut", 59, 0.1, 15000, false);
    addMaskChannelsParameter(Parameter::STREAM_SCOPE, "Channels", "Channels to filter for this stream");

}

AudioProcessorEditor* BandstopFilter::createEditor()
{
    editor = std::make_unique<BandstopFilterEditor>(this);

    return editor.get();
}

// ----------------------------------------------------
// From the filter library documentation:
// ----------------------------------------------------
//
// each family of filters is given its own namespace
// RBJ: filters from the RBJ cookbook
// Butterworth
// ChebyshevI: ripple in the passband
// ChebyshevII: ripple in the stop band
// Elliptic: ripple in both the passband and stopband
// Bessel: theoretically with linear phase
// Legendre: "Optimum-L" filters with steepest transition and monotonic passband
// Custom: Simple filters that allow poles and zeros to be specified directly

// within each namespace exists a set of "raw filters"
// Butterworth::LowPass
//				HighPass
// 				BandPass
//				BandStop
//				LowShelf
// 				HighShelf
//				BandShelf
//
//	class templates (such as SimpleFilter) which require FilterClasses
//    expect an identifier of a raw filter
//  raw filters do not support introspection, or the Params style of changing
//    filter settings; they only offer a setup() function for updating the IIR
//    coefficients to a given set of parameters
//

// each filter family namespace also has the nested namespace "Design"
// here we have all of the raw filter names repeated, except these classes
//  also provide the Design interface, which adds introspection, polymorphism,
//  the Params style of changing filter settings, and in general all fo the features
//  necessary to interoperate with the Filter virtual base class and its derived classes

// available methods:
//
// filter->getKind()
// filter->getName()
// filter->getNumParams()
// filter->getParamInfo()
// filter->getDefaultParams()
// filter->getParams()
// filter->getParam()

// filter->setParam()
// filter->findParamId()
// filter->setParamById()
// filter->setParams()
// filter->copyParamsFrom()

// filter->getPoleZeros()
// filter->response()
// filter->getNumChannels()
// filter->reset()
// filter->process()

void BandstopFilter::updateSettings()
{
    settings.update(getDataStreams());

    for (auto stream : getDataStreams())
    {
        settings[stream->getStreamId()]->createFilters(
            stream->getChannelCount(),
            stream->getSampleRate(),
            (*stream)["low_cut"],
            (*stream)["high_cut"]
        );
    }
}


void BandstopFilter::parameterValueChanged(Parameter* param)
{

    uint16 currentStream = param->getStreamId();

    if (param->getName().equalsIgnoreCase("low_cut"))
    {

        if ((*getDataStream(currentStream))["low_cut"] >= (*getDataStream(currentStream))["high_cut"])
        {
            getDataStream(currentStream)->getParameter("low_cut")->restorePreviousValue();
            return;
        }


        settings[currentStream]->updateFilters(
            (*getDataStream(currentStream))["low_cut"],
            (*getDataStream(currentStream))["high_cut"]
        );
    }
    else if (param->getName().equalsIgnoreCase("high_cut"))
    {

        if ((*getDataStream(currentStream))["high_cut"] <= (*getDataStream(currentStream))["low_cut"])
        {
            getDataStream(currentStream)->getParameter("high_cut")->restorePreviousValue();
            return;
        }

        settings[currentStream]->updateFilters(
            (*getDataStream(currentStream))["low_cut"],
            (*getDataStream(currentStream))["high_cut"]
        );
    }
}


void BandstopFilter::process(AudioBuffer<float>& buffer)
{

    for (auto stream : getDataStreams())
    {

        if ((*stream)["enable_stream"])
        {
            BandstopFilterSettings* streamSettings = settings[stream->getStreamId()];

            const uint16 streamId = stream->getStreamId();
            const uint32 numSamples = getNumSamplesInBlock(streamId);

            for (auto localChannelIndex : *((*stream)["Channels"].getArray()))
            {
                int globalChannelIndex = getGlobalChannelIndex(stream->getStreamId(), (int)localChannelIndex);

                float* ptr = buffer.getWritePointer(globalChannelIndex);

                streamSettings->filters[localChannelIndex]->process(numSamples, &ptr);

            }
        }
    }
}