// Copyright MediaZ Teknoloji A.S. All Rights Reserved.
#include <Nodos/Plugin.hpp>
#include <algorithm>
#include <vector>
#include <cmath>

#include <nosSysDecklink/nosDeckLinkSubsystem.h>
#include <nosSysVulkan/nosVulkanSubsystem.h>
#include <nosSysVulkan/Helpers.hpp>

#include "nosDecklink/DeckLink_generated.h"
#include "nosAudio/Audio_generated.h"
#include "nosAudio/AudioConversions.hpp"

namespace nos::decklink
{
struct AudioWriteNode : NodeContext
{
	using NodeContext::NodeContext;

	std::string LastStatusMessage;

	void SetNodeStatusMessageIfChanged(const std::string& message, fb::NodeStatusMessageType type)
	{
		if (LastStatusMessage != message)
		{
			SetNodeStatusMessage(message, type);
			LastStatusMessage = message;
		}
	}

	void OnPathStart() override { BackBuffer.clear(); }

	nosResult ExecuteNode(nos::NodeExecuteParams const& params) override
	{
		const auto& channelId = *params.GetPinData<ChannelId>(NOS_NAME("ChannelId"));
		auto deviceIndex = channelId.device_index();
		auto channel = static_cast<nosDeckLinkChannel>(channelId.channel_index());

		// Read AudioPacket composite: fields 'desc' and 'buffer'
		CompositeObjectRef fullAudio = params.GetPinObject(NOS_NAME("Audio"));
		if (!fullAudio)
			return NOS_RESULT_SUCCESS; // nothing to write

		auto descObj = fullAudio.GetField(NOS_NAME("desc"));
		auto bufObj = fullAudio.GetField(NOS_NAME("buffer"));
		if (!descObj || !bufObj)
			return NOS_RESULT_FAILED;

		auto descBuf = GetObjectDataView(*descObj);
		if (!descBuf)
			return NOS_RESULT_FAILED;
		auto& packetDesc = *static_cast<const nos::audio::AudioPacketDescriptor*>(descBuf->Data);

		uint32_t sampleRate = packetDesc.sample_rate();
		uint32_t numSamples = packetDesc.num_samples();
		uint32_t channelCount = packetDesc.channel_count();
		// Buffer contains int24 stored in MSB of int32
		const int32_t* inSamples = reinterpret_cast<const int32_t*>(nosVulkan->Map(*bufObj));
		if (!inSamples)
			return NOS_RESULT_FAILED;

		// Ask DeckLink format for sample type
		uint32_t dlRate = 0, sampleTypeBits = 0, dlChannels = 0;
		nosDeckLink->GetAudioFormat(deviceIndex, channel, &dlRate, &sampleTypeBits, &dlChannels);
		if (dlRate && dlRate != sampleRate)
		{
			SetNodeStatusMessageIfChanged("DeckLink device expects " + std::to_string(dlRate) +
											  " Hz\nInput sample rate is " + std::to_string(sampleRate) + " Hz",
										  fb::NodeStatusMessageType::FAILURE);
			return NOS_RESULT_FAILED;
		}
		if (dlChannels && dlChannels != channelCount)
		{
			// channel mismatch; write only min(dlChannels, channelCount)
			channelCount = std::min(dlChannels, channelCount);
		}

		if (!BackBuffer.empty())
		{
			uint32_t written = 0;
			nosResult res = nosDeckLink->DMAAudioTransfer(
				deviceIndex, channel, BackBuffer.data(), uint32_t(BackBuffer.size() / channelCount), &written);
			if (res != NOS_RESULT_SUCCESS)
			{
				nosEngine.LogW("AudioWrite: Failed to write backbuffered audio samples to DeckLink device");
				return res;
			}
			for (uint32_t i = written * channelCount; i < BackBuffer.size(); ++i)
			{
				BackBuffer[i - written * channelCount] = BackBuffer[i];
			}
			BackBuffer.resize(BackBuffer.size() - written * channelCount);
		}
		// Convert shifted-int24 to PCM according to sampleTypeBits
		// Input: 24-bit samples stored in MSB of int32 (bits [31:8]), LSB is zero
		const uint32_t frames = numSamples;
		std::vector<int32_t> pcm(frames * channelCount);
		for (uint32_t i = 0; i < frames * channelCount; ++i)
		{
			// Extract 24-bit from MSB and extend to 32-bit
			auto sampleFloat = audio::ShiftedInt24ToFloat(inSamples[i]);
			// Scale to int32
			pcm[i] =
				static_cast<int32_t>(std::round(sampleFloat * static_cast<float>((1ULL << sampleTypeBits) / 2 - 1)));
		}
		uint32_t written = 0;
		nosResult res = nosDeckLink->DMAAudioTransfer(deviceIndex, channel, pcm.data(), frames, &written);
		if (written != frames)
		{
			nosEngine.LogW(
				"AudioWrite: Not all audio frames were written to DeckLink device (written %u / %u)", written, frames);
			for (uint32_t i = written * channelCount; i < frames * channelCount; ++i)
			{
				BackBuffer.push_back(pcm[i]);
			}
		}
		return res;
	}

	std::vector<int32_t> BackBuffer;
};

nosResult RegisterAudioWriteNode(nosNodeFunctions* functions)
{
	NOS_BIND_NODE_CLASS(NOS_NAME("AudioWrite"), AudioWriteNode, functions)
	return NOS_RESULT_SUCCESS;
}
} // namespace nos::decklink