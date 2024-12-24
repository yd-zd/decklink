// Copyright MediaZ Teknoloji A.S. All Rights Reserved.
#include <Nodos/PluginHelpers.hpp>

#include <nosUtil/Stopwatch.hpp>

#include <nosDeckLinkSubsystem/nosDeckLinkSubsystem.h>

#include "Generated/DeckLink_generated.h"

// External
#include <nosVulkanSubsystem/nosVulkanSubsystem.h>

namespace nos::decklink
{
	
struct WaitFrameNode : NodeContext
{
	WaitFrameNode(const nosFbNode* node) : NodeContext(node)
	{
	}

	void OnPinValueChanged(nos::Name pinName, nosUUID pinId, nosBuffer value) override
	{ 
		if (pinName == NOS_NAME("ChannelId"))
		{
			auto& newChannelId = *InterpretPinValue<ChannelId>(value);
			if (CurChannelId == newChannelId)
				return;
			CurChannelId = newChannelId;
		}
		else if (pinName == NOS_NAME("WaitField"))
		{
			WaitField = *InterpretPinValue<nosTextureFieldType>(value);
		}
	}

	nosTextureFieldType WaitField = NOS_TEXTURE_FIELD_TYPE_UNKNOWN;

	nosResult ExecuteNode(nosNodeExecuteParams* params) override
	{
		auto deviceIndex = CurChannelId.device_index();
		auto channel = static_cast<nosDeckLinkChannel>(CurChannelId.channel_index());
		nosDeckLink->WaitFrame(deviceIndex, channel, 100, static_cast<nosMediaIOInterlacedFieldType>(WaitField));
		return NOS_RESULT_SUCCESS;
	}

	void OnPathStartInitiated() override
	{
		auto deviceIndex = CurChannelId.device_index();
		auto channel = static_cast<nosDeckLinkChannel>(CurChannelId.channel_index());
		// This possibly takes a long time(more than a frame)
		if(CurChannelId.is_input())
			nosDeckLink->WaitFrame(deviceIndex, channel, 100, NOS_MEDIAIO_INTERLACED_FIELD_TYPE_INVALID);
	}

	// This is to ensure that the first frame aligns with decklink frame
	void OnPathStart() override
	{
		auto deviceIndex = CurChannelId.device_index();
		auto channel = static_cast<nosDeckLinkChannel>(CurChannelId.channel_index());
		nosDeckLink->WaitFrame(deviceIndex, channel, 100, static_cast<nosMediaIOInterlacedFieldType>(WaitField));
	}

	ChannelId CurChannelId;
	
};

nosResult RegisterWaitFrameNode(nosNodeFunctions* functions)
{
	NOS_BIND_NODE_CLASS(NOS_NAME_STATIC("WaitFrame"), WaitFrameNode, functions)
	return NOS_RESULT_SUCCESS;
}
}
