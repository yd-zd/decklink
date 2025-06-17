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

uint64_t NowNs()
{
	return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

struct WaitFrameNode : NodeContext
{
	WaitFrameNode(nosFbNodePtr node) : NodeContext(node)
	{
	}

	static nosResult SyncPathStarts(void* ctx, nosWaitResult* outRes)
	{
		return static_cast<WaitFrameNode*>(ctx)->SyncPathStarts(outRes);
	}

	int32_t GetDeviceIndex() const
	{
		return CurChannelId.device_index();
	}

	nosDeckLinkChannel GetChannel() const
	{
		return static_cast<nosDeckLinkChannel>(CurChannelId.channel_index());
	}

	bool IsInput() const { return CurChannelId.is_input(); }

	bool IsChannelOpen()
	{
		nosDeckLinkChannelState state{};
		if (NOS_RESULT_SUCCESS != nosDeckLink->GetChannelState(GetDeviceIndex(), GetChannel(), &state))
			return false;
		if (!state.IsOpen || !state.IsStreaming)
			return false;
		return true;
	}

	nosResult SyncPathStarts(nosWaitResult* out)
	{
		if (!IsChannelOpen())
			return NOS_RESULT_FAILED;
		nosDeckLinkChannelState ch{};
		if (NOS_RESULT_SUCCESS != nosDeckLink->WaitFrame(GetDeviceIndex(), GetChannel(), 100))
			return NOS_RESULT_FAILED;
		auto steadyClockNowNs = NowNs();
		if (NOS_RESULT_SUCCESS != nosDeckLink->GetChannelState(GetDeviceIndex(), GetChannel(), &ch))
			return NOS_RESULT_FAILED;
		auto lastVblTimestampNs = ch.LastFrameInfo.TimestampNs;
		if (out)
		{
			out->TimeSinceLastEventNs = steadyClockNowNs - lastVblTimestampNs;
			double timeSecs = (double)out->TimeSinceLastEventNs / 1'000'000;
			nosEngine.LogD("Time Since Last Event: %.6f ms", timeSecs);
			out->EventCount = ch.LastFrameInfo.FrameNumber;
		}
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
		nosDeckLink->WaitFrame(GetDeviceIndex(), GetChannel(), 100);
		return NOS_RESULT_SUCCESS;
	}

	void OnPathStartInitiated() override
	{
		// This possibly takes a long time(more than a frame)
		if (IsInput())
			nosDeckLink->WaitFrame(GetDeviceIndex(), GetChannel(), 100);
		nosVec2u deltaSecs{};
		if (NOS_RESULT_SUCCESS != nosDeckLink->GetCurrentDeltaSecondsOfChannel(GetDeviceIndex(), GetChannel(), &deltaSecs))
		{
			nosDeckLinkDeviceInfo info{};
			nosDeckLink->GetDeviceInfoByIndex(GetDeviceIndex(), &info);
			nosEngine.LogE("Failed to get current delta seconds of channel %s on device %s", nosDeckLink->GetChannelName(GetChannel()), info.Desc.UniqueDisplayName);
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
		if (WaitId)
		{
			uint64_t timestampNs = 0, vblCount = 0;
			nosSync->WaitForConsensus(WaitId, &timestampNs, &vblCount);
		}
		if (!IsInput())
			nosDeckLink->SetAutoSchedulingEnabled(GetDeviceIndex(), GetChannel(), NOS_FALSE);
	}

	void OnPathStop() override
	{
		if (WaitId != 0)
		{
			nosSync->UnregisterEvent(WaitId);
			WaitId = 0;
		}
		if (!IsInput())
			nosDeckLink->SetAutoSchedulingEnabled(GetDeviceIndex(), GetChannel(), NOS_TRUE);
	}

	ChannelId CurChannelId{};
	uint64_t WaitId = 0; // Event ID for the wait event
};

nosResult RegisterWaitFrameNode(nosNodeFunctions* functions)
{
	NOS_BIND_NODE_CLASS(NOS_NAME_STATIC("WaitFrame"), WaitFrameNode, functions)
	return NOS_RESULT_SUCCESS;
}
}
