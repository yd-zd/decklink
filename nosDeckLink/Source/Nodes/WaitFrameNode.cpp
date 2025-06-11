// Copyright MediaZ Teknoloji A.S. All Rights Reserved.
#include <Nodos/PluginHelpers.hpp>

#include <nosUtil/Stopwatch.hpp>

#include <nosDeckLinkSubsystem/nosDeckLinkSubsystem.h>
#include <nosSync/nosSync.h>

#include "Generated/DeckLink_generated.h"

// External
#include <nosVulkanSubsystem/nosVulkanSubsystem.h>

namespace nos::decklink
{

struct WaitFrameNode : NodeContext
{
	WaitFrameNode(nosFbNodePtr node) : NodeContext(node)
	{
	}

	static nosResult SyncPathStarts(void* ctx, uint64_t* outVblTimestampNs, uint64_t* outVblCount)
	{
		return static_cast<WaitFrameNode*>(ctx)->SyncPathStarts(outVblTimestampNs, outVblCount);
	}

	nosResult SyncPathStarts(uint64_t* outVblTimestampNs, uint64_t* outVblCount)
	{
		auto deviceIndex = CurChannelId.device_index();
		auto channel = static_cast<nosDeckLinkChannel>(CurChannelId.channel_index());
		if (NOS_RESULT_SUCCESS != nosDeckLink->WaitFrame(deviceIndex, channel, 100))
			return NOS_RESULT_FAILED;
		nosDeckLinkFrameTimingInfo timingInfo{};
		if (NOS_RESULT_SUCCESS != nosDeckLink->GetLastWaitedFrameTimingInfo(deviceIndex, channel, &timingInfo))
			return NOS_RESULT_FAILED;
		if (outVblTimestampNs)
			*outVblTimestampNs = timingInfo.TimestampNs;
		if (outVblCount)
			*outVblCount = timingInfo.FramesArrived;
		return NOS_RESULT_SUCCESS;
	}

	void OnPinValueChanged(nos::Name pinName, uuid const& pinId, nosBuffer value) override
	{ 
		if (pinName == NOS_NAME("ChannelId"))
		{
			auto& newChannelId = *InterpretPinValue<ChannelId>(value);
			if (CurChannelId == newChannelId)
				return;
			CurChannelId = newChannelId;
		}
	}

	nosResult ExecuteNode(nosNodeExecuteParams* params) override
	{
		auto deviceIndex = CurChannelId.device_index();
		auto channel = static_cast<nosDeckLinkChannel>(CurChannelId.channel_index());
		nosDeckLink->WaitFrame(deviceIndex, channel, 100);
		return NOS_RESULT_SUCCESS;
	}

	void OnPathStartInitiated() override
	{
		auto deviceIndex = CurChannelId.device_index();
		auto channel = static_cast<nosDeckLinkChannel>(CurChannelId.channel_index());
		// This possibly takes a long time(more than a frame)
		if (CurChannelId.is_input())
			nosDeckLink->WaitFrame(deviceIndex, channel, 100);
		nosVec2u deltaSecs{};
		if (NOS_RESULT_SUCCESS != nosDeckLink->GetCurrentDeltaSecondsOfChannel(deviceIndex, channel, &deltaSecs))
		{
			nosDeckLinkDeviceInfo info{};
			nosDeckLink->GetDeviceInfoByIndex(deviceIndex, &info);
			nosEngine.LogE("Failed to get current delta seconds of channel %s on device %s", nosDeckLink->GetChannelName(channel), info.Desc.UniqueDisplayName);
			return;
		}
		nosRegisterEventParams params{
			.EventGroupId = 1,
			.DeltaSeconds = deltaSecs,
			.UserData = this,
			.WaitFn = WaitFrameNode::SyncPathStarts,
			.OutEventId = &WaitId,
		};
		nosSync->RegisterEvent(&params);
	}

	// This is to ensure that the first frame aligns with decklink frame
	void OnPathStart() override
	{
		auto deviceIndex = CurChannelId.device_index();
		auto channel = static_cast<nosDeckLinkChannel>(CurChannelId.channel_index());
		uint64_t timestampNs = 0, vblCount = 0;
		if (WaitId)
			nosSync->WaitForConsensus(WaitId, &timestampNs, &vblCount);
	}

	void OnPathStop() override
	{
		if (WaitId != 0)
		{
			nosSync->UnregisterEvent(WaitId);
			WaitId = 0;
		}
	}

	ChannelId CurChannelId;
	uint64_t WaitId = 0; // Event ID for the wait event
};

nosResult RegisterWaitFrameNode(nosNodeFunctions* functions)
{
	NOS_BIND_NODE_CLASS(NOS_NAME_STATIC("WaitFrame"), WaitFrameNode, functions)
	return NOS_RESULT_SUCCESS;
}
}
