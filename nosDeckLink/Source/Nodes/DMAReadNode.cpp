// Copyright MediaZ Teknoloji A.S. All Rights Reserved.
#include <Nodos/Plugin.hpp>

#include "Generated/DeckLink_generated.h"

#include <nosDeckLinkSubsystem/nosDeckLinkSubsystem.h>
#include <Nodos/Utils/Stopwatch.hpp>

#include "nosSysVulkan/Helpers.hpp"
#include "nosSysVulkan/nosVulkanSubsystem.h"

namespace nos::decklink
{
    
struct DMAReadNode : NodeContext
{
	using NodeContext::NodeContext;

	nosResult ExecuteNode(nos::NodeExecuteParams const& params) override
	{
		auto bufferToWrite = params.GetPinObject(NOS_NAME("BufferToWrite"));
		const auto& channelId = *params.GetPinData<ChannelId>(NOS_NAME("ChannelId"));

		if (!bufferToWrite)
		{
			nosEngine.LogE("DMA read target buffer is not valid.");
			return NOS_RESULT_FAILED;
		}

		auto deviceIndex = channelId.device_index();
		auto channel = static_cast<nosDeckLinkChannel>(channelId.channel_index());

		uint8_t* buffer = nosVulkan->Map(bufferToWrite);
		auto bufInfo = sys::vulkan::GetResourceInfo(bufferToWrite);
		if (!buffer)
		{
			nosEngine.LogE("Failed to map DMA read target buffer.");
			return NOS_RESULT_FAILED;
		}
		if (!bufInfo)
		{
			nosEngine.LogE("Failed to get DMA read target buffer info.");
			return NOS_RESULT_FAILED;
		}
		auto inputBufferSize = bufInfo->Buffer.Size;

		nosDeckLink->DMATransfer(deviceIndex, channel, buffer, inputBufferSize);

		nosVulkan->SetResourceFieldType(bufferToWrite, NOS_TEXTURE_FIELD_TYPE_PROGRESSIVE);
		SetPinObject(NOS_NAME("Output"), bufferToWrite);

		return NOS_RESULT_SUCCESS;
	}
};

nosResult RegisterDMAReadNode(nosNodeFunctions* functions)
{
	NOS_BIND_NODE_CLASS(NOS_NAME("DMARead"), DMAReadNode, functions)
    return NOS_RESULT_SUCCESS;
}
}
