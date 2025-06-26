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
	static constexpr uint64_t TIMEOUT_MS = 1000;

	using NodeContext::NodeContext;

	nosResult OnCreate(nosFbNodePtr node) override
	{
		AddPinValueWatcher(NOS_NAME("EnableSync"), [this](nos::Buffer const& newVal, std::optional<nos::Buffer> oldValue) {
			if (!oldValue || *oldValue != newVal)
				nosEngine.SendPathRestart(NodeId);
		});
		return NOS_RESULT_SUCCESS;
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

	bool IsSyncEnabled()
	{
		auto buf = GetWatchedPinValue(NOS_NAME("EnableSync"));
		if (!buf.has_value() || buf->Size != sizeof(bool))
			return false;
		return *reinterpret_cast<const bool*>(buf->Data) == true;
	}

	nosResult SyncPathStarts(nosWaitResult* out)
	{
		if (!IsChannelOpen())
			return NOS_RESULT_FAILED;
		nosDeckLinkChannelState ch{};

		if (NOS_RESULT_SUCCESS != nosDeckLink->WaitFrame(GetDeviceIndex(), GetChannel(), TIMEOUT_MS))
			return NOS_RESULT_FAILED;
		if (NOS_RESULT_SUCCESS != nosDeckLink->GetChannelState(GetDeviceIndex(), GetChannel(), &ch))
			return NOS_RESULT_FAILED;
		if (out)
		{
			std::chrono::system_clock::duration frameTimestamp =
				std::chrono::duration_cast<std::chrono::system_clock::duration>(
					std::chrono::nanoseconds(ch.LastFrameInfo.TimestampNs));
			nosEngine.LogI("(Device %d, %s) Frame timestamp at %s",
						   GetDeviceIndex(),
						   nosDeckLink->GetChannelName(GetChannel()),
						   std::format("{:%H:%M:%S}", frameTimestamp)
							   .c_str());
			out->TimeSinceLastEventNs = ch.TimeInFrameNs;
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
		nosDeckLink->WaitFrame(GetDeviceIndex(), GetChannel(), TIMEOUT_MS);
		return NOS_RESULT_SUCCESS;
	}

	void OnPathStartInitiated() override
	{
		// This possibly takes a long time(more than a frame)
		if (IsInput())
			nosDeckLink->WaitFrame(GetDeviceIndex(), GetChannel(), TIMEOUT_MS);
		nosVec2u deltaSecs{};
		if (NOS_RESULT_SUCCESS != nosDeckLink->GetCurrentDeltaSecondsOfChannel(GetDeviceIndex(), GetChannel(), &deltaSecs))
		{
			nosDeckLinkDeviceInfo info{};
			nosDeckLink->GetDeviceInfoByIndex(GetDeviceIndex(), &info);
			nosEngine.LogE("Failed to get current delta seconds of channel %s on device %s", nosDeckLink->GetChannelName(GetChannel()), info.Desc.UniqueDisplayName);
			return;
		}
		nosRegisterEventParams params{
			.EventGroupId = IsSyncEnabled() ? NOS_SYNC_DEFAULT_EVENT_GROUP_ID : NOS_SYNC_NO_SYNC_EVENT_GROUP_ID,
			.DeltaSeconds = deltaSecs,
			.UserData = this,
			.ResetFn = nullptr,
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
			if (IsChannelOpen())
				nosDeckLink->ResetDropDetection(GetDeviceIndex(), GetChannel());
		}
	}

	void OnPathStop() override
	{
		if (WaitId != 0)
		{
			nosSync->UnregisterEvent(WaitId);
			WaitId = 0;
		}
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
