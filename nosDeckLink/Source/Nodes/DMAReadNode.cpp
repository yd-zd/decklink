// Copyright MediaZ Teknoloji A.S. All Rights Reserved.
#include <Nodos/PluginHelpers.hpp>

#include "DeckLink_generated.h"

#include <nosDeckLinkSubsystem/nosDeckLinkSubsystem.h>
#include <nosUtil/Stopwatch.hpp>

#include "nosVulkanSubsystem/Helpers.hpp"
#include "nosVulkanSubsystem/nosVulkanSubsystem.h"

namespace nos::decklink
{
    
struct DMAReadNode : NodeContext
{
	DMAReadNode(nosFbNodePtr node) : NodeContext(node)
	{
	}

	nosResult ExecuteNode(nosNodeExecuteParams* params) override
	{
		NodeExecuteParams execParams = params;
		nosResourceShareInfo bufferToWrite = vkss:: ConvertToResourceInfo(*InterpretPinValue<sys::vulkan::Buffer>(*execParams[NOS_NAME_STATIC("BufferToWrite")].Data));
		ChannelId* channelId = InterpretPinValue<ChannelId>(*execParams[NOS_NAME_STATIC("ChannelId")].Data);

		if (!bufferToWrite.Memory.Handle)
		{
			nosEngine.LogE("DMA read target buffer is not valid.");
			return NOS_RESULT_FAILED;
		}

		auto deviceIndex = channelId->device_index();
		auto channel = static_cast<nosDeckLinkChannel>(channelId->channel_index());
		if (deviceIndex < 0)
			return NOS_RESULT_PENDING;

		uint8_t* buffer = nosVulkan->Map(&bufferToWrite);
		auto inputBufferSize = bufferToWrite.Memory.Size;

		nosDeckLink->DMATransfer(deviceIndex, channel, buffer, inputBufferSize);

		bufferToWrite.Info.Buffer.FieldType = NOS_TEXTURE_FIELD_TYPE_PROGRESSIVE;
		nosEngine.SetPinValue(execParams[NOS_NAME_STATIC("Output")].Id, Buffer::From(vkss::ConvertBufferInfo(bufferToWrite)));

		return NOS_RESULT_SUCCESS;
	}
};

nosResult RegisterDMAReadNode(nosNodeFunctions* functions)
{
	NOS_BIND_NODE_CLASS(NOS_NAME("DMARead"), DMAReadNode, functions)
    return NOS_RESULT_SUCCESS;
}
}
