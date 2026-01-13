// Copyright MediaZ Teknoloji A.S. All Rights Reserved.
#include <Nodos/Plugin.hpp>
#include <algorithm>
#include <vector>

#include <nosSysDecklink/nosDeckLinkSubsystem.h>
#include <nosSysVulkan/nosVulkanSubsystem.h>
#include <nosSysVulkan/Helpers.hpp>

#include "nosAudio/Audio_generated.h"
#include "nosAudio/AudioConversions.hpp"

#include "nosDecklink/DeckLink_generated.h"

namespace nos::decklink
{
struct AudioReadNode : NodeContext
{
    using NodeContext::NodeContext;

    // Reusable audio sample buffer (int24-in-int32)
    TypedObjectRef<sys::vulkan::Buffer> OutputAudioBuffer;

    nosResult ExecuteNode(nos::NodeExecuteParams const& params) override
    {
  //       const auto& channelId = *params.GetPinData<ChannelId>(NOS_NAME("ChannelId"));
  //       auto deviceIndex = channelId.device_index();
  //       auto channel = static_cast<nosDeckLinkChannel>(channelId.channel_index());
  //
  //       // Query audio format
  //       uint32_t sampleRate = 0, sampleTypeBits = 0, channelCount = 0;
  //       if (nosDeckLink->GetAudioFormat(deviceIndex, channel, &sampleRate, &sampleTypeBits, &channelCount) != NOS_RESULT_SUCCESS)
  //           return NOS_RESULT_FAILED;
  //       const uint32_t bytesPerSample = (sampleTypeBits == 16) ? 2u : 4u;
  //
  //       // Pull latest audio PCM block
  //       std::vector<uint8_t> pcm;
  //       pcm.resize(480 * channelCount * bytesPerSample); // default 10ms chunk fallback
  //       size_t copied = 0;
  //       nosDeckLink->GetLatestAudioInput(deviceIndex, channel, pcm.data(), pcm.size(), &copied);
  //       if (copied == 0)
  //       {
  //           // No audio; skip emitting packet this tick
  //           return NOS_RESULT_SUCCESS;
  //       }
  //       pcm.resize(copied);
  //
  //       // Compute samples
  //       uint32_t numSamples = static_cast<uint32_t>(copied / (bytesPerSample * channelCount));
  //       size_t audioBytes = size_t(numSamples) * channelCount * sizeof(int32_t); // int24 stored MSB in int32
  //
  //       // Ensure buffer capacity
  //       size_t allocated = OutputAudioBuffer ? sys::vulkan::GetResourceInfo(OutputAudioBuffer)->Size : 0;
  //       if (!OutputAudioBuffer || allocated < audioBytes)
  //       {
  //           OutputAudioBuffer = {};
  //           nosBufferInfo bufDesc{};
  //           bufDesc.Size = static_cast<uint32_t>(audioBytes);
  //           bufDesc.Usage = nosBufferUsage(NOS_BUFFER_USAGE_STORAGE_BUFFER | NOS_BUFFER_USAGE_TRANSFER_DST | NOS_BUFFER_USAGE_TRANSFER_SRC);
  //           bufDesc.MemoryFlags = nosMemoryFlags(NOS_MEMORY_FLAGS_HOST_VISIBLE);
  //           bufDesc.ElementType = NOS_BUFFER_ELEMENT_TYPE_INT32;
  //           OutputAudioBuffer = sys::vulkan::CreateBuffer(bufDesc, "DeckLink Audio Read Buffer");
  //           if (!OutputAudioBuffer)
  //               return NOS_RESULT_FAILED;
  //       }
  //
  //       // Map and write int24-in-int32 samples
  //       int32_t* outSamples = reinterpret_cast<int32_t*>(nosVulkan->Map(OutputAudioBuffer));
  //       if (!outSamples)
  //           return NOS_RESULT_FAILED;
  //       if (bytesPerSample == 2)
  //       {
  //           const int16_t* in16 = reinterpret_cast<const int16_t*>(pcm.data());
  //           for (uint32_t i = 0; i < numSamples * channelCount; ++i)
  //           {
  //               float v = std::max(-1.0f, std::min(1.0f, float(in16[i]) / 32768.0f));
  //               outSamples[i] = audio::FloatToShiftedInt24(v);
  //           }
  //       }
  //       else
  //       {
  //           const int32_t* in32 = reinterpret_cast<const int32_t*>(pcm.data());
  //           for (uint32_t i = 0; i < numSamples * channelCount; ++i)
  //           {
  //               float v = std::max(-1.0f, std::min(1.0f, float(in32[i]) / 2147483648.0f));
		// 		outSamples[i] = audio::FloatToShiftedInt24(v);
  //           }
  //       }
  //
  //       // Build AudioPacket composite object
		// audio::AudioPacketDescriptor audioPacketDesc(
		// 	sampleRate, numSamples, audio::BitDepth::AUDIO_BIT_DEPTH_24_BIT, 4, channelCount);
  //       auto descObj = PrimitiveObjectRef::Create(NOS_NAME("nos.audio.AudioPacketDescriptor"), nos::Buffer::From(audioPacketDesc));
  //       std::unordered_map<nos::Name, nos::ObjectRef> audioFields;
  //       audioFields[NOS_NAME("desc")] = descObj.value_or(ObjectRef());
  //       audioFields[NOS_NAME("buffer")] = OutputAudioBuffer;
  //       auto audioPacket = CompositeObjectRef::Create(NOS_NAME("nos.audio.AudioPacket"), audioFields);
  //       if (!audioPacket)
  //           return NOS_RESULT_FAILED;
  //
  //       SetPinObject(NOS_NAME("Audio"), *audioPacket);
        return NOS_RESULT_SUCCESS;
    }
};

nosResult RegisterAudioReadNode(nosNodeFunctions* functions)
{
    NOS_BIND_NODE_CLASS(NOS_NAME("AudioRead"), AudioReadNode, functions)
    return NOS_RESULT_SUCCESS;
}
} // namespace nos::decklink
