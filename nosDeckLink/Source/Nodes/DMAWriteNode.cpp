// Copyright MediaZ Teknoloji A.S. All Rights Reserved.
#include <Nodos/Plugin.hpp>

#include <nosVulkanSubsystem/nosVulkanSubsystem.h>
#include <nosVulkanSubsystem/Helpers.hpp>
#include <nosDeckLinkSubsystem/nosDeckLinkSubsystem.h>

#include <Nodos/Utils/Stopwatch.hpp>

#include "Generated/DeckLink_generated.h"

namespace nos::decklink
{
	
struct DMAWriteNode : NodeContext
{
	using NodeContext::NodeContext;

	void GetScheduleInfo(nosScheduleInfo* out) override
	{
		*out = nosScheduleInfo{
			.Importance = 1,
			.DeltaSeconds = DeltaSeconds,
			.Type = NOS_SCHEDULE_TYPE_ON_DEMAND,
		};
	}

	void OnPinValueChanged(nos::Name pinName, uuid const& pinId, nosBuffer value) override
	{ 
		if (pinName == NOS_NAME_STATIC("ChannelId"))
		{
			auto& newChannelId = *static_cast<ChannelId*>(value.Data);
			if (CurChannelId == newChannelId)
				return;
			CurChannelId = newChannelId;
			nosVec2u deltaSeconds{0, 0};
			if (CurChannelId.device_index() != -1)
			{
				nosDeckLink->GetCurrentDeltaSecondsOfChannel(CurChannelId.device_index(), static_cast<nosDeckLinkChannel>(CurChannelId.channel_index()), &deltaSeconds);
				if (memcmp(&deltaSeconds, &DeltaSeconds, sizeof(deltaSeconds)) != 0)
				{
					DeltaSeconds = deltaSeconds;
					nosEngine.RecompilePath(NodeId);
				}
			}
		}
	}

	nosResult ExecuteNode(nos::NodeExecuteParams const& params) override
	{
		auto inputBuffer = params.GetPinObject(NOS_NAME("Input"));

		auto deviceIndex = CurChannelId.device_index();
		auto channel = static_cast<nosDeckLinkChannel>(CurChannelId.channel_index());

		if (!inputBuffer)
			return NOS_RESULT_FAILED;

		auto buffer = nosVulkan->Map(inputBuffer);
		if (!buffer)
		{
			nosEngine.LogE("Failed to map DMA write input buffer.");
			return NOS_RESULT_FAILED;
		}
		auto bufInfo = sys::vulkan::GetResourceInfo(inputBuffer);
		if (!bufInfo)
		{
			nosEngine.LogE("Failed to get DMA write input buffer info.");
			return NOS_RESULT_FAILED;
		}
		nosDeckLink->DMATransfer(deviceIndex, channel, buffer, bufInfo->Buffer.Size);

		SendScheduleRequest(1);
		return NOS_RESULT_SUCCESS;
	}

	void OnPathStart() override
	{
		nosScheduleNodeParams schedule{.NodeId = NodeId, .AddScheduleCount = 1};
		nosEngine.ScheduleNode(&schedule);
	}

	ChannelId CurChannelId;
	nosVec2u DeltaSeconds{0, 0};
};

nosResult RegisterDMAWriteNode(nosNodeFunctions* functions)
{
	NOS_BIND_NODE_CLASS(NOS_NAME_STATIC("DMAWrite"), DMAWriteNode, functions)
	return NOS_RESULT_SUCCESS;
}
}
