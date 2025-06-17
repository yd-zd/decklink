// Copyright MediaZ Teknoloji A.S. All Rights Reserved.
#include <Nodos/PluginHelpers.hpp>

#include <nosVulkanSubsystem/nosVulkanSubsystem.h>
#include <nosVulkanSubsystem/Helpers.hpp>
#include <nosDeckLinkSubsystem/nosDeckLinkSubsystem.h>

#include <nosUtil/Stopwatch.hpp>

#include "Generated/DeckLink_generated.h"

namespace nos::decklink
{
	
struct DMAWriteNode : NodeContext
{
	DMAWriteNode(nosFbNodePtr node) : NodeContext(node)
	{
	}

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
			auto& newChannelId = *InterpretPinValue<ChannelId>(value);
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

	nosResult ExecuteNode(nosNodeExecuteParams* params) override
	{
		nosResourceShareInfo inputBuffer{};
		auto fieldType = nos::sys::vulkan::FieldType::UNKNOWN;
		for (size_t i = 0; i < params->PinCount; ++i)
		{
			auto& pin = params->Pins[i];
			if (pin.Name == NOS_NAME_STATIC("Input"))
				inputBuffer = vkss::ConvertToResourceInfo(*InterpretPinValue<sys::vulkan::Buffer>(*pin.Data));
			if (pin.Name == NOS_NAME("FieldType"))
				fieldType = *InterpretPinValue<sys::vulkan::FieldType>(*pin.Data);
		}

		auto deviceIndex = CurChannelId.device_index();
		auto channel = static_cast<nosDeckLinkChannel>(CurChannelId.channel_index());

		if (!inputBuffer.Memory.Handle)
			return NOS_RESULT_FAILED;

		auto buffer = nosVulkan->Map(&inputBuffer);
		nosDeckLinkChannelState chBefore{};
		nosDeckLink->GetChannelState(deviceIndex, channel, &chBefore);
		nosDeckLink->DMATransfer(deviceIndex, channel, buffer, inputBuffer.Info.Buffer.Size);
		nosDeckLinkChannelState chAfter{};
		nosDeckLink->GetChannelState(deviceIndex, channel, &chAfter);
		
		if (chAfter.LastFrameInfo.FrameNumber != chBefore.LastFrameInfo.FrameNumber+1)
		{
			nosEngine.LogI("Dropped: %llu frames on device %d, channel %s (Before: %llu, After: %llu)",
						   chAfter.LastFrameInfo.FrameNumber - (chBefore.LastFrameInfo.FrameNumber + 1),
						   deviceIndex,
						   nosDeckLink->GetChannelName(channel),
						   chBefore.LastFrameInfo.FrameNumber,
						   chAfter.LastFrameInfo.FrameNumber);
			nosEngine.SendPathRestart(NodeId);
		}

		nosScheduleNodeParams schedule {
			.NodeId = NodeId,
			.AddScheduleCount = 1
		};
		nosEngine.ScheduleNode(&schedule);
		
		return NOS_RESULT_SUCCESS;
	}

	void OnPathStart() override
	{
		nosScheduleNodeParams schedule{.NodeId = NodeId, .AddScheduleCount = 1};
		nosEngine.ScheduleNode(&schedule);
		FrameCount = 0;
	}

	uint64_t FrameCount = 0;
	ChannelId CurChannelId;
	nosVec2u DeltaSeconds{0, 0};
};

nosResult RegisterDMAWriteNode(nosNodeFunctions* functions)
{
	NOS_BIND_NODE_CLASS(NOS_NAME_STATIC("DMAWrite"), DMAWriteNode, functions)
	return NOS_RESULT_SUCCESS;
}
}
