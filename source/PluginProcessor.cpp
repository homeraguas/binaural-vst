#pragma once
#include "PluginProcessor.h"
#include "PluginEditor.h"

HrtfBiAuralAudioProcessor::HrtfBiAuralAudioProcessor()
{
	tailL.resize(200, 0.f);
	tailR.resize(200, 0.f);

	setLatencySamples(nTaps / 2);
	crossover.set(44100, crossover.f0);

	// zero-out 
	for (int i = 0; i < currentHrir[0].size(); ++i)
	{
		currentHrir[0][i] = 0.f;
		currentHrir[1][i] = 0.f;
	}
}

HrtfBiAuralAudioProcessor::~HrtfBiAuralAudioProcessor()
{
}
const String HrtfBiAuralAudioProcessor::getName() const
{
	return JucePlugin_Name;
}

int HrtfBiAuralAudioProcessor::getNumParameters()
{
	return 0;
}

float HrtfBiAuralAudioProcessor::getParameter(int index)
{
	return 0.0f;
}

void HrtfBiAuralAudioProcessor::setParameter(int index, float newValue)
{
}

const String HrtfBiAuralAudioProcessor::getParameterName(int index)
{
	return String();
}

const String HrtfBiAuralAudioProcessor::getParameterText(int index)
{
	return String();
}

const String HrtfBiAuralAudioProcessor::getInputChannelName(int channelIndex) const
{
	return String(channelIndex + 1);
}

const String HrtfBiAuralAudioProcessor::getOutputChannelName(int channelIndex) const
{
	return String(channelIndex + 1);
}

bool HrtfBiAuralAudioProcessor::isInputChannelStereoPair(int index) const
{
	return true;
}

bool HrtfBiAuralAudioProcessor::isOutputChannelStereoPair(int index) const
{
	return true;
}

bool HrtfBiAuralAudioProcessor::acceptsMidi() const
{
#if JucePlugin_WantsMidiInput
	return true;
#else
	return false;
#endif
}

bool HrtfBiAuralAudioProcessor::producesMidi() const
{
#if JucePlugin_ProducesMidiOutput
	return true;
#else
	return false;
#endif
}

bool HrtfBiAuralAudioProcessor::silenceInProducesSilenceOut() const
{
	return false;
}

double HrtfBiAuralAudioProcessor::getTailLengthSeconds() const
{
	return 0.0;
}

int HrtfBiAuralAudioProcessor::getNumPrograms()
{
	return 1;   // NB: some hosts don't cope very well if you tell them there are 0 programs,
	// so this should be at least 1, even if you're not really implementing programs.
}

int HrtfBiAuralAudioProcessor::getCurrentProgram()
{
	return 0;
}

void HrtfBiAuralAudioProcessor::setCurrentProgram(int index)
{
}

const String HrtfBiAuralAudioProcessor::getProgramName(int index)
{
	return String();
}

void HrtfBiAuralAudioProcessor::changeProgramName(int index, const String& newName)
{
}

void HrtfBiAuralAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
	crossover.loPass.reset();
	crossover.hiPass.reset();
}

void HrtfBiAuralAudioProcessor::releaseResources()
{
	// When playback stops, you can use this as an opportunity to free up any
	// spare memory, etc.
}

void HrtfBiAuralAudioProcessor::processBlock(AudioSampleBuffer& buffer, MidiBuffer& midiMessages)
{
	const float* in = buffer.getReadPointer(0);
	float* outL = buffer.getWritePointer(0);
	float* outR = buffer.getWritePointer(1);
	int bufferLength = buffer.getNumSamples();

	// create two copies of mono input
	std::vector<float> loPassIn(in, in + bufferLength);
	std::vector<float> hiPassIn(in, in + bufferLength);
	// send to crossover
	crossover.loPass.processSamples(&loPassIn[0], bufferLength);
	crossover.hiPass.processSamples(&hiPassIn[0], bufferLength);

	// gradually interpolate hrir
	auto& targetHrir = hrtfContainer.hrir;
	for (int i = 0; i < currentHrir[0].size(); ++i)
	{
		currentHrir[0][i] += (targetHrir[0][i] - currentHrir[0][i]) * tRate;
		currentHrir[1][i] += (targetHrir[1][i] - currentHrir[1][i]) * tRate;
	}

	// convolve high frequencies with hrir
	auto hiPassOutL = convolve(&hiPassIn[0], bufferLength, currentHrir[0].data(), nTaps);
	auto hiPassOutR = convolve(&hiPassIn[0], bufferLength, currentHrir[1].data(), nTaps);

	// add nTaps of output from previous block
	for (int n = 0; n < nTaps; ++n)
	{
		hiPassOutL[n] += tailL[n];
		hiPassOutR[n] += tailR[n];
	}
	// buffer last nTaps of output
	for (int n = 0; n < nTaps; ++n)
	{
		tailL[n] = hiPassOutL[hiPassOutL.size() - nTaps + n];
		tailR[n] = hiPassOutR[hiPassOutR.size() - nTaps + n];
	}

	for (int n = 0; n < bufferLength; ++n)
	{
		outL[n] = loPassIn[n] + hiPassOutL[n];
		outR[n] = loPassIn[n] + hiPassOutR[n];
	}
}

//==============================================================================
bool HrtfBiAuralAudioProcessor::hasEditor() const
{
	return true; // (change this to false if you choose to not supply an editor)
}

AudioProcessorEditor* HrtfBiAuralAudioProcessor::createEditor()
{
	return new HrtfBiAuralAudioProcessorEditor(*this);
}

//==============================================================================
void HrtfBiAuralAudioProcessor::getStateInformation(MemoryBlock& destData)
{
	// You should use this method to store your parameters in the memory block.
	// You could do that either as raw data, or use the XML or ValueTree classes
	// as intermediaries to make it easy to save and load complex data.
}

void HrtfBiAuralAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
	// You should use this method to restore your parameters from this memory block,
	// whose contents will have been created by the getStateInformation() call.
}

//==============================================================================
// This creates new instances of the plugin..
AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
	return new HrtfBiAuralAudioProcessor();
}

std::vector<float> HrtfBiAuralAudioProcessor::convolve(const float* x, int xlen, const float* h, int hlen)
{
	std::vector<float> y(xlen + hlen - 1, 0);
	int N;
	for (int n = 0; n < y.size(); ++n)
	{
		N = std::min(n, hlen);
		for (int i = 0; i < N; ++i)
		{
			if (n - i < xlen)
				y[n] += x[n - i] * h[i];
		}
	}
	return y;
}

void HrtfBiAuralAudioProcessor::updateHrir(float azimuth, float elevation)
{
	hrtfContainer.interpolate(azimuth, elevation);

}
