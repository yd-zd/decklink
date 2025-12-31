// Copyright MediaZ Teknoloji A.S. All Rights Reserved.
#include <Nodos/Plugin.hpp>
#include <algorithm>
#include <vector>
#include <cmath>

#include <nosSysDecklink/nosDeckLinkSubsystem.h>
#include <nosSysVulkan/nosVulkanSubsystem.h>
#include <nosSysVulkan/Helpers.hpp>

#include "Generated/DeckLink_generated.h"
#include "nosAudio/Audio_generated.h"

namespace nos::decklink
{
struct AudioWriteNode : NodeContext
{
    using NodeContext::NodeContext;

    nosResult ExecuteNode(nos::NodeExecuteParams const& params) override
    {
        const auto& channelId = *params.GetPinData<ChannelId>(NOS_NAME("ChannelId"));
        auto deviceIndex = channelId.device_index();
        auto channel = static_cast<nosDeckLinkChannel>(channelId.channel_index());

        // Read AudioPacket composite: fields 'desc' and 'buffer'
        auto fullAudio = params.GetPinObject(NOS_NAME("Audio"));
        if (!fullAudio)
            return NOS_RESULT_SUCCESS; // nothing to write

        ObjectRef descObj{}, bufObj{};
        nosEngine.ObjectAPI->GetField(fullAudio, NOS_NAME("desc"), &descObj.GetStorage());
        nosEngine.ObjectAPI->GetField(fullAudio, NOS_NAME("buffer"), &bufObj.GetStorage());
        if (!descObj || !bufObj)
            return NOS_RESULT_FAILED;

        nosImmutableBuffer descBuf{};
        if (NOS_RESULT_SUCCESS != nosEngine.ObjectAPI->GetObjectDataView(descObj, &descBuf))
            return NOS_RESULT_FAILED;
        auto& packetDesc = *static_cast<const nos::audio::AudioPacketDescriptor*>(descBuf.Data);

        uint32_t sampleRate = packetDesc.sample_rate();
        uint32_t numSamples = packetDesc.num_samples();
        uint32_t channelCount = packetDesc.channel_count();
        // Buffer contains int24 stored in MSB of int32
		const int32_t* inSamples = reinterpret_cast<const int32_t*>(nosVulkan->Map(bufObj));
        if (!inSamples)
            return NOS_RESULT_FAILED;

        // Ask DeckLink format for sample type
        uint32_t dlRate = 0, sampleTypeBits = 0, dlChannels = 0;
        nosDeckLink->GetAudioFormat(deviceIndex, channel, &dlRate, &sampleTypeBits, &dlChannels);
        if (dlRate && dlRate != sampleRate)
        {
            // simple rate mismatch; proceed but note potential drift
        }
        if (dlChannels && dlChannels != channelCount)
        {
            // channel mismatch; write only min(dlChannels, channelCount)
            channelCount = std::min(dlChannels, channelCount);
        }

        // Convert shifted-int24 to PCM according to sampleTypeBits
        const uint32_t frames = numSamples;
        std::vector<uint8_t> pcm;
        if (sampleTypeBits == 16)
        {
            pcm.resize(size_t(frames) * channelCount * 2);
            int16_t* out16 = reinterpret_cast<int16_t*>(pcm.data());
            for (uint32_t i = 0; i < frames * channelCount; ++i)
            {
                // Recover 24-bit sample then normalize to float
                int32_t s24 = (inSamples[i] >> 8);
                float v = std::max(-1.0f, std::min(1.0f, float(s24) / 8388607.0f));
                out16[i] = static_cast<int16_t>(std::lrintf(v * 32767.0f));
            }
        }
        else
        {
            pcm.resize(size_t(frames) * channelCount * 4);
            int32_t* out32 = reinterpret_cast<int32_t*>(pcm.data());
            for (uint32_t i = 0; i < frames * channelCount; ++i)
            {
                int32_t s24 = (inSamples[i] >> 8);
                float v = std::max(-1.0f, std::min(1.0f, float(s24) / 8388607.0f));
                out32[i] = static_cast<int32_t>(std::lrintf(v * 2147483647.0f));
            }
        }

        uint32_t written = 0;
        nosResult r = nosDeckLink->WriteAudioSamplesSync(deviceIndex, channel, pcm.data(), pcm.size(), &written);
        (void)r;
        return NOS_RESULT_SUCCESS;
    }
};

nosResult RegisterAudioWriteNode(nosNodeFunctions* functions)
{
    NOS_BIND_NODE_CLASS(NOS_NAME("AudioWrite"), AudioWriteNode, functions)
    return NOS_RESULT_SUCCESS;
}
} // namespace nos::decklink
