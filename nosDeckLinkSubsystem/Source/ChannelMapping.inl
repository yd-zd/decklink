// Copyright MediaZ Teknoloji A.S. All Rights Reserved.
#pragma once

#include <unordered_map>
#include <string>

#include "DeckLinkAPI.h"
#include "nosDeckLinkSubsystem/nosDeckLinkSubsystem.h"

namespace nos::decklink
{

inline const std::unordered_map<std::string, uint32_t>& GetSDIPortCounts()
{
	static std::unordered_map<std::string, uint32_t> map{};
	if (!map.empty())
		return map;
	map["DeckLink 8K Pro"] = 4;
	map["DeckLink Quad 2"] = 8;
	map["DeckLink Duo 2"] = 4;
	map["DeckLink 4K Pro"] = 4;
	return map;
}

// Model Name -> Profile -> SubDeviceIndex -> Mode -> Connectors
typedef std::unordered_map<std::string, std::unordered_map<std::optional<BMDProfileID>, std::unordered_map<int64_t, std::unordered_map<nosDeckLinkChannel, std::unordered_set<nosMediaIODirection>>>>> ConnectorMap;
inline const ConnectorMap& GetChannelMap()
{
	static ConnectorMap map{};
	if (!map.empty())
		return map;

	// TODO: Currently, only half-duplex profiles are included, add other profiles too.
	auto& deckLink8KPro = map["DeckLink 8K Pro"] = {};
	deckLink8KPro[bmdProfileFourSubDevicesHalfDuplex][0][NOS_DECKLINK_CHANNEL_SINGLE_LINK_1] = {NOS_MEDIAIO_DIRECTION_INPUT, NOS_MEDIAIO_DIRECTION_OUTPUT};
	deckLink8KPro[bmdProfileFourSubDevicesHalfDuplex][1][NOS_DECKLINK_CHANNEL_SINGLE_LINK_3] = {NOS_MEDIAIO_DIRECTION_INPUT, NOS_MEDIAIO_DIRECTION_OUTPUT};
	deckLink8KPro[bmdProfileFourSubDevicesHalfDuplex][2][NOS_DECKLINK_CHANNEL_SINGLE_LINK_2] = {NOS_MEDIAIO_DIRECTION_INPUT, NOS_MEDIAIO_DIRECTION_OUTPUT};
	deckLink8KPro[bmdProfileFourSubDevicesHalfDuplex][3][NOS_DECKLINK_CHANNEL_SINGLE_LINK_4] = {NOS_MEDIAIO_DIRECTION_INPUT, NOS_MEDIAIO_DIRECTION_OUTPUT};

	auto& deckLinkQuad2 = map["DeckLink Quad 2"] = {};
	deckLinkQuad2[bmdProfileFourSubDevicesHalfDuplex][0][NOS_DECKLINK_CHANNEL_SINGLE_LINK_1] = {NOS_MEDIAIO_DIRECTION_INPUT, NOS_MEDIAIO_DIRECTION_OUTPUT};
	deckLinkQuad2[bmdProfileFourSubDevicesHalfDuplex][1][NOS_DECKLINK_CHANNEL_SINGLE_LINK_3] = {NOS_MEDIAIO_DIRECTION_INPUT, NOS_MEDIAIO_DIRECTION_OUTPUT};
	deckLinkQuad2[bmdProfileFourSubDevicesHalfDuplex][2][NOS_DECKLINK_CHANNEL_SINGLE_LINK_5] = {NOS_MEDIAIO_DIRECTION_INPUT, NOS_MEDIAIO_DIRECTION_OUTPUT};
	deckLinkQuad2[bmdProfileFourSubDevicesHalfDuplex][3][NOS_DECKLINK_CHANNEL_SINGLE_LINK_7] = {NOS_MEDIAIO_DIRECTION_INPUT, NOS_MEDIAIO_DIRECTION_OUTPUT};
	deckLinkQuad2[bmdProfileFourSubDevicesHalfDuplex][4][NOS_DECKLINK_CHANNEL_SINGLE_LINK_2] = {NOS_MEDIAIO_DIRECTION_INPUT, NOS_MEDIAIO_DIRECTION_OUTPUT};
	deckLinkQuad2[bmdProfileFourSubDevicesHalfDuplex][5][NOS_DECKLINK_CHANNEL_SINGLE_LINK_4] = {NOS_MEDIAIO_DIRECTION_INPUT, NOS_MEDIAIO_DIRECTION_OUTPUT};
	deckLinkQuad2[bmdProfileFourSubDevicesHalfDuplex][6][NOS_DECKLINK_CHANNEL_SINGLE_LINK_6] = {NOS_MEDIAIO_DIRECTION_INPUT, NOS_MEDIAIO_DIRECTION_OUTPUT};
	deckLinkQuad2[bmdProfileFourSubDevicesHalfDuplex][7][NOS_DECKLINK_CHANNEL_SINGLE_LINK_8] = {NOS_MEDIAIO_DIRECTION_INPUT, NOS_MEDIAIO_DIRECTION_OUTPUT};

	auto& deckLinkDuo2 = map["DeckLink Duo 2"] = {};
	deckLinkDuo2[bmdProfileTwoSubDevicesHalfDuplex][0][NOS_DECKLINK_CHANNEL_SINGLE_LINK_1] = {NOS_MEDIAIO_DIRECTION_INPUT, NOS_MEDIAIO_DIRECTION_OUTPUT};
	deckLinkDuo2[bmdProfileTwoSubDevicesHalfDuplex][1][NOS_DECKLINK_CHANNEL_SINGLE_LINK_3] = {NOS_MEDIAIO_DIRECTION_INPUT, NOS_MEDIAIO_DIRECTION_OUTPUT};
	deckLinkDuo2[bmdProfileTwoSubDevicesHalfDuplex][2][NOS_DECKLINK_CHANNEL_SINGLE_LINK_2] = {NOS_MEDIAIO_DIRECTION_INPUT, NOS_MEDIAIO_DIRECTION_OUTPUT};
	deckLinkDuo2[bmdProfileTwoSubDevicesHalfDuplex][3][NOS_DECKLINK_CHANNEL_SINGLE_LINK_4] = {NOS_MEDIAIO_DIRECTION_INPUT, NOS_MEDIAIO_DIRECTION_OUTPUT};

	auto& deckLink4KPro = map["DeckLink 4K Pro"] = {};
	deckLink4KPro[std::nullopt][0][NOS_DECKLINK_CHANNEL_SINGLE_LINK_IN_A] = {NOS_MEDIAIO_DIRECTION_INPUT};
	deckLink4KPro[std::nullopt][0][NOS_DECKLINK_CHANNEL_SINGLE_LINK_IN_B] = {NOS_MEDIAIO_DIRECTION_INPUT};
	deckLink4KPro[std::nullopt][0][NOS_DECKLINK_CHANNEL_SINGLE_LINK_OUT_A] = {NOS_MEDIAIO_DIRECTION_OUTPUT};
	deckLink4KPro[std::nullopt][0][NOS_DECKLINK_CHANNEL_SINGLE_LINK_OUT_B] = {NOS_MEDIAIO_DIRECTION_OUTPUT};
	return map;
}
	
}
