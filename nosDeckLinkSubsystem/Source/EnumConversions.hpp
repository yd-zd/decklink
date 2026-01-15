// Copyright MediaZ Teknoloji A.S. All Rights Reserved.
#pragma once

#include <vector>

#include <DeckLinkAPI.h>
#include <nosMediaio/nosMediaIO.h>
#include <nosSysDecklink/nosDeckLinkSubsystem.h>

namespace nos::decklink
{
constexpr std::vector<BMDDisplayMode> GetDisplayModesForFrameGeometry(nosMediaIOFrameGeometry frameGeometry)
{
	// Map nosMediaIOFrameGeometry to BMDDisplayMode
	switch (frameGeometry)
	{
	case NOS_MEDIAIO_FRAME_GEOMETRY_NTSC:
		return {bmdModeNTSC, bmdModeNTSC2398, bmdModeNTSCp};
	case NOS_MEDIAIO_FRAME_GEOMETRY_PAL:
		return {bmdModePAL, bmdModePALp};
	case NOS_MEDIAIO_FRAME_GEOMETRY_HD720:
		return {bmdModeHD720p50, bmdModeHD720p5994, bmdModeHD720p60};
	case NOS_MEDIAIO_FRAME_GEOMETRY_HD1080:
		return {
			bmdModeHD1080p2398, bmdModeHD1080p24, bmdModeHD1080p25, bmdModeHD1080p2997,
			bmdModeHD1080p30, bmdModeHD1080p4795, bmdModeHD1080p48, bmdModeHD1080p50,
			bmdModeHD1080p5994, bmdModeHD1080p6000, bmdModeHD1080i50, bmdModeHD1080i5994,
			bmdModeHD1080i6000
		};
	case NOS_MEDIAIO_FRAME_GEOMETRY_2K:
		return {bmdMode2k2398, bmdMode2k24, bmdMode2k25};
	case NOS_MEDIAIO_FRAME_GEOMETRY_2KDCI:
		return {
			bmdMode2kDCI2398, bmdMode2kDCI24, bmdMode2kDCI25, bmdMode2kDCI2997,
			bmdMode2kDCI30, bmdMode2kDCI4795, bmdMode2kDCI48, bmdMode2kDCI50,
			bmdMode2kDCI5994, bmdMode2kDCI60, bmdMode2kDCI9590, bmdMode2kDCI96,
			bmdMode2kDCI100, bmdMode2kDCI11988, bmdMode2kDCI120
		};
	case NOS_MEDIAIO_FRAME_GEOMETRY_4K2160:
		return {
			bmdMode4K2160p2398, bmdMode4K2160p24, bmdMode4K2160p25, bmdMode4K2160p2997,
			bmdMode4K2160p30, bmdMode4K2160p4795, bmdMode4K2160p48, bmdMode4K2160p50,
			bmdMode4K2160p5994, bmdMode4K2160p60, bmdMode4K2160p9590, bmdMode4K2160p96,
			bmdMode4K2160p100, bmdMode4K2160p11988, bmdMode4K2160p120
		};
	case NOS_MEDIAIO_FRAME_GEOMETRY_4KDCI:
		return {
			bmdMode4kDCI2398, bmdMode4kDCI24, bmdMode4kDCI25, bmdMode4kDCI2997,
			bmdMode4kDCI30, bmdMode4kDCI4795, bmdMode4kDCI48, bmdMode4kDCI50,
			bmdMode4kDCI5994, bmdMode4kDCI60
		};
	case NOS_MEDIAIO_FRAME_GEOMETRY_8K4320:
		return {
			bmdMode8K4320p2398, bmdMode8K4320p24, bmdMode8K4320p25, bmdMode8K4320p2997,
			bmdMode8K4320p30, bmdMode8K4320p4795, bmdMode8K4320p48, bmdMode8K4320p50,
			bmdMode8K4320p5994, bmdMode8K4320p60
		};
	case NOS_MEDIAIO_FRAME_GEOMETRY_8KDCI:
		return {
			bmdMode8kDCI2398, bmdMode8kDCI24, bmdMode8kDCI25, bmdMode8kDCI2997,
			bmdMode8kDCI30, bmdMode8kDCI4795, bmdMode8kDCI48, bmdMode8kDCI50,
			bmdMode8kDCI5994, bmdMode8kDCI60
		};
	case NOS_MEDIAIO_FRAME_GEOMETRY_640x480:
		return {bmdMode640x480p60};
	case NOS_MEDIAIO_FRAME_GEOMETRY_800x600:
		return {bmdMode800x600p60};
	case NOS_MEDIAIO_FRAME_GEOMETRY_1440x900:
		return {bmdMode1440x900p50, bmdMode1440x900p60};
	case NOS_MEDIAIO_FRAME_GEOMETRY_1440x1080:
		return {bmdMode1440x1080p50, bmdMode1440x1080p60};
	case NOS_MEDIAIO_FRAME_GEOMETRY_1600x1200:
		return {bmdMode1600x1200p50, bmdMode1600x1200p60};
	case NOS_MEDIAIO_FRAME_GEOMETRY_1920x1200:
		return {bmdMode1920x1200p50, bmdMode1920x1200p60};
	case NOS_MEDIAIO_FRAME_GEOMETRY_1920x1440:
		return {bmdMode1920x1440p50, bmdMode1920x1440p60};
	case NOS_MEDIAIO_FRAME_GEOMETRY_2560x1440:
		return {bmdMode2560x1440p50, bmdMode2560x1440p60};
	case NOS_MEDIAIO_FRAME_GEOMETRY_2560x1600:
		return {bmdMode2560x1600p50, bmdMode2560x1600p60};
	case NOS_MEDIAIO_FRAME_GEOMETRY_INVALID:
	default:
		return {};
	}
}

constexpr BMDDisplayMode GetDeckLinkDisplayMode(nosMediaIOVideoScanType scanType, nosMediaIOFrameGeometry geometry, nosMediaIOFrameRate frameRate) {
	if (scanType == NOS_MEDIAIO_VIDEO_INTERLACED_SCAN)
	{
		switch (geometry)
		{
			case NOS_MEDIAIO_FRAME_GEOMETRY_HD1080:
			{
				switch (frameRate)
				{
					case NOS_MEDIAIO_FRAME_RATE_50: return bmdModeHD1080i50;
					case NOS_MEDIAIO_FRAME_RATE_5994: return bmdModeHD1080i5994;
					case NOS_MEDIAIO_FRAME_RATE_60: return bmdModeHD1080i6000;
					default: return bmdModeUnknown;
				}
			}
			case NOS_MEDIAIO_FRAME_GEOMETRY_NTSC:
			{
				switch (frameRate)
				{
					case NOS_MEDIAIO_FRAME_RATE_2398: return bmdModeNTSC2398;
					case NOS_MEDIAIO_FRAME_RATE_2997: return bmdModeNTSC;
					default: return bmdModeUnknown;
				}
			}
			case NOS_MEDIAIO_FRAME_GEOMETRY_PAL:
			{
				switch (frameRate)
				{
					default: return bmdModePAL;
				}
			}
			default: return bmdModeUnknown;
		}
	}
	switch (geometry) {
		case NOS_MEDIAIO_FRAME_GEOMETRY_NTSC:
			switch (frameRate) {
				case NOS_MEDIAIO_FRAME_RATE_2398: return bmdModeNTSC2398;
				default: return bmdModeNTSC;
			}
		
		case NOS_MEDIAIO_FRAME_GEOMETRY_PAL:
			switch (frameRate) {
				default: return bmdModePAL;
			}
		
		case NOS_MEDIAIO_FRAME_GEOMETRY_HD720:
			switch (frameRate) {
				case NOS_MEDIAIO_FRAME_RATE_50: return bmdModeHD720p50;
				case NOS_MEDIAIO_FRAME_RATE_5994: return bmdModeHD720p5994;
				case NOS_MEDIAIO_FRAME_RATE_60: return bmdModeHD720p60;
				default: return bmdModeUnknown;
			}
		
		case NOS_MEDIAIO_FRAME_GEOMETRY_HD1080:
			switch (frameRate) {
				case NOS_MEDIAIO_FRAME_RATE_2398: return bmdModeHD1080p2398;
				case NOS_MEDIAIO_FRAME_RATE_24: return bmdModeHD1080p24;
				case NOS_MEDIAIO_FRAME_RATE_25: return bmdModeHD1080p25;
				case NOS_MEDIAIO_FRAME_RATE_2997: return bmdModeHD1080p2997;
				case NOS_MEDIAIO_FRAME_RATE_30: return bmdModeHD1080p30;
				case NOS_MEDIAIO_FRAME_RATE_4795: return bmdModeHD1080p4795;
				case NOS_MEDIAIO_FRAME_RATE_48: return bmdModeHD1080p48;
				case NOS_MEDIAIO_FRAME_RATE_50: return bmdModeHD1080p50;
				case NOS_MEDIAIO_FRAME_RATE_5994: return bmdModeHD1080p5994;
				case NOS_MEDIAIO_FRAME_RATE_60: return bmdModeHD1080p6000;
				case NOS_MEDIAIO_FRAME_RATE_9590: return bmdModeHD1080p9590;
				case NOS_MEDIAIO_FRAME_RATE_96: return bmdModeHD1080p96;
				case NOS_MEDIAIO_FRAME_RATE_100: return bmdModeHD1080p100;
				case NOS_MEDIAIO_FRAME_RATE_11988: return bmdModeHD1080p11988;
				case NOS_MEDIAIO_FRAME_RATE_120: return bmdModeHD1080p120;
				default: return bmdModeUnknown;
			}
		
		case NOS_MEDIAIO_FRAME_GEOMETRY_2K:
			switch (frameRate) {
				case NOS_MEDIAIO_FRAME_RATE_2398: return bmdMode2k2398;
				case NOS_MEDIAIO_FRAME_RATE_24: return bmdMode2k24;
				case NOS_MEDIAIO_FRAME_RATE_25: return bmdMode2k25;
				default: return bmdModeUnknown;
			}
		
		case NOS_MEDIAIO_FRAME_GEOMETRY_2KDCI:
			switch (frameRate) {
				case NOS_MEDIAIO_FRAME_RATE_2398: return bmdMode2kDCI2398;
				case NOS_MEDIAIO_FRAME_RATE_24: return bmdMode2kDCI24;
				case NOS_MEDIAIO_FRAME_RATE_25: return bmdMode2kDCI25;
				case NOS_MEDIAIO_FRAME_RATE_2997: return bmdMode2kDCI2997;
				case NOS_MEDIAIO_FRAME_RATE_30: return bmdMode2kDCI30;
				case NOS_MEDIAIO_FRAME_RATE_4795: return bmdMode2kDCI4795;
				case NOS_MEDIAIO_FRAME_RATE_48: return bmdMode2kDCI48;
				case NOS_MEDIAIO_FRAME_RATE_50: return bmdMode2kDCI50;
				case NOS_MEDIAIO_FRAME_RATE_5994: return bmdMode2kDCI5994;
				case NOS_MEDIAIO_FRAME_RATE_60: return bmdMode2kDCI60;
				case NOS_MEDIAIO_FRAME_RATE_9590: return bmdMode2kDCI9590;
				case NOS_MEDIAIO_FRAME_RATE_96: return bmdMode2kDCI96;
				case NOS_MEDIAIO_FRAME_RATE_100: return bmdMode2kDCI100;
				case NOS_MEDIAIO_FRAME_RATE_11988: return bmdMode2kDCI11988;
				case NOS_MEDIAIO_FRAME_RATE_120: return bmdMode2kDCI120;
				default: return bmdModeUnknown;
			}
		
		case NOS_MEDIAIO_FRAME_GEOMETRY_4K2160:
			switch (frameRate) {
				case NOS_MEDIAIO_FRAME_RATE_2398: return bmdMode4K2160p2398;
				case NOS_MEDIAIO_FRAME_RATE_24: return bmdMode4K2160p24;
				case NOS_MEDIAIO_FRAME_RATE_25: return bmdMode4K2160p25;
				case NOS_MEDIAIO_FRAME_RATE_2997: return bmdMode4K2160p2997;
				case NOS_MEDIAIO_FRAME_RATE_30: return bmdMode4K2160p30;
				case NOS_MEDIAIO_FRAME_RATE_4795: return bmdMode4K2160p4795;
				case NOS_MEDIAIO_FRAME_RATE_48: return bmdMode4K2160p48;
				case NOS_MEDIAIO_FRAME_RATE_50: return bmdMode4K2160p50;
				case NOS_MEDIAIO_FRAME_RATE_5994: return bmdMode4K2160p5994;
				case NOS_MEDIAIO_FRAME_RATE_60: return bmdMode4K2160p60;
				case NOS_MEDIAIO_FRAME_RATE_9590: return bmdMode4K2160p9590;
				case NOS_MEDIAIO_FRAME_RATE_96: return bmdMode4K2160p96;
				case NOS_MEDIAIO_FRAME_RATE_100: return bmdMode4K2160p100;
				case NOS_MEDIAIO_FRAME_RATE_11988: return bmdMode4K2160p11988;
				case NOS_MEDIAIO_FRAME_RATE_120: return bmdMode4K2160p120;
				default: return bmdModeUnknown;
			}
		
		case NOS_MEDIAIO_FRAME_GEOMETRY_4KDCI:
			switch (frameRate) {
				case NOS_MEDIAIO_FRAME_RATE_2398: return bmdMode4kDCI2398;
				case NOS_MEDIAIO_FRAME_RATE_24: return bmdMode4kDCI24;
				case NOS_MEDIAIO_FRAME_RATE_25: return bmdMode4kDCI25;
				case NOS_MEDIAIO_FRAME_RATE_2997: return bmdMode4kDCI2997;
				case NOS_MEDIAIO_FRAME_RATE_30: return bmdMode4kDCI30;
				case NOS_MEDIAIO_FRAME_RATE_4795: return bmdMode4kDCI4795;
				case NOS_MEDIAIO_FRAME_RATE_48: return bmdMode4kDCI48;
				case NOS_MEDIAIO_FRAME_RATE_50: return bmdMode4kDCI50;
				case NOS_MEDIAIO_FRAME_RATE_5994: return bmdMode4kDCI5994;
				case NOS_MEDIAIO_FRAME_RATE_60: return bmdMode4kDCI60;
				case NOS_MEDIAIO_FRAME_RATE_9590: return bmdMode4kDCI9590;
				case NOS_MEDIAIO_FRAME_RATE_96: return bmdMode4kDCI96;
				case NOS_MEDIAIO_FRAME_RATE_100: return bmdMode4kDCI100;
				case NOS_MEDIAIO_FRAME_RATE_11988: return bmdMode4kDCI11988;
				case NOS_MEDIAIO_FRAME_RATE_120: return bmdMode4kDCI120;
				default: return bmdModeUnknown;
			}
		
		case NOS_MEDIAIO_FRAME_GEOMETRY_8K4320:
			switch (frameRate) {
				case NOS_MEDIAIO_FRAME_RATE_2398: return bmdMode8K4320p2398;
				case NOS_MEDIAIO_FRAME_RATE_24: return bmdMode8K4320p24;
				case NOS_MEDIAIO_FRAME_RATE_25: return bmdMode8K4320p25;
				case NOS_MEDIAIO_FRAME_RATE_2997: return bmdMode8K4320p2997;
				case NOS_MEDIAIO_FRAME_RATE_30: return bmdMode8K4320p30;
				case NOS_MEDIAIO_FRAME_RATE_4795: return bmdMode8K4320p4795;
				case NOS_MEDIAIO_FRAME_RATE_48: return bmdMode8K4320p48;
				case NOS_MEDIAIO_FRAME_RATE_50: return bmdMode8K4320p50;
				case NOS_MEDIAIO_FRAME_RATE_5994: return bmdMode8K4320p5994;
				case NOS_MEDIAIO_FRAME_RATE_60: return bmdMode8K4320p60;
				default: return bmdModeUnknown;
			}
		
		case NOS_MEDIAIO_FRAME_GEOMETRY_640x480:
			switch (frameRate) {
				case NOS_MEDIAIO_FRAME_RATE_60: return bmdMode640x480p60;
				default: return bmdModeUnknown;
			}
		
		case NOS_MEDIAIO_FRAME_GEOMETRY_800x600:
			switch (frameRate) {
				case NOS_MEDIAIO_FRAME_RATE_60: return bmdMode800x600p60;
				default: return bmdModeUnknown;
			}
		
		case NOS_MEDIAIO_FRAME_GEOMETRY_1440x900:
			switch (frameRate) {
				case NOS_MEDIAIO_FRAME_RATE_50: return bmdMode1440x900p50;
				case NOS_MEDIAIO_FRAME_RATE_60: return bmdMode1440x900p60;
				default: return bmdModeUnknown;
			}
		
		case NOS_MEDIAIO_FRAME_GEOMETRY_1440x1080:
			switch (frameRate) {
				case NOS_MEDIAIO_FRAME_RATE_50: return bmdMode1440x1080p50;
				case NOS_MEDIAIO_FRAME_RATE_60: return bmdMode1440x1080p60;
				default: return bmdModeUnknown;
			}
		
		case NOS_MEDIAIO_FRAME_GEOMETRY_1600x1200:
			switch (frameRate) {
				case NOS_MEDIAIO_FRAME_RATE_50: return bmdMode1600x1200p50;
				case NOS_MEDIAIO_FRAME_RATE_60: return bmdMode1600x1200p60;
				default: return bmdModeUnknown;
			}
		
		case NOS_MEDIAIO_FRAME_GEOMETRY_1920x1200:
			switch (frameRate) {
				case NOS_MEDIAIO_FRAME_RATE_50: return bmdMode1920x1200p50;
				case NOS_MEDIAIO_FRAME_RATE_60: return bmdMode1920x1200p60;
				default: return bmdModeUnknown;
			}
		
		case NOS_MEDIAIO_FRAME_GEOMETRY_1920x1440:
			switch (frameRate) {
				case NOS_MEDIAIO_FRAME_RATE_50: return bmdMode1920x1440p50;
				case NOS_MEDIAIO_FRAME_RATE_60: return bmdMode1920x1440p60;
				default: return bmdModeUnknown;
			}
		
		case NOS_MEDIAIO_FRAME_GEOMETRY_2560x1440:
			switch (frameRate) {
				case NOS_MEDIAIO_FRAME_RATE_50: return bmdMode2560x1440p50;
				case NOS_MEDIAIO_FRAME_RATE_60: return bmdMode2560x1440p60;
				default: return bmdModeUnknown;
			}
		
		case NOS_MEDIAIO_FRAME_GEOMETRY_2560x1600:
			switch (frameRate) {
				case NOS_MEDIAIO_FRAME_RATE_50: return bmdMode2560x1600p50;
				case NOS_MEDIAIO_FRAME_RATE_60: return bmdMode2560x1600p60;
				default: return bmdModeUnknown;
			}
		
		default:
			return bmdModeUnknown;
	}
}
	
constexpr BMDPixelFormat GetDeckLinkPixelFormat(nosMediaIOPixelFormat fmt)
{
	switch (fmt)
	{
	case NOS_MEDIAIO_PIXEL_FORMAT_YCBCR_8BIT:
		return bmdFormat8BitYUV;
	case NOS_MEDIAIO_PIXEL_FORMAT_YCBCR_10BIT:
		return bmdFormat10BitYUV;
	}
	return bmdFormatUnspecified;
}

constexpr nosMediaIOPixelFormat GetPixelFormatFromDeckLink(BMDPixelFormat fmt)
{
	switch (fmt)
	{
	case bmdFormat8BitYUV:
		return NOS_MEDIAIO_PIXEL_FORMAT_YCBCR_8BIT;
	case bmdFormat10BitYUV:
		return NOS_MEDIAIO_PIXEL_FORMAT_YCBCR_10BIT;
	}
	return NOS_MEDIAIO_PIXEL_FORMAT_INVALID;
}
	
constexpr nosMediaIOFrameRate GetFrameRateFromDisplayMode(BMDDisplayMode displayMode)
{
	switch (displayMode)
	{
	case bmdModeNTSC2398:
	case bmdModeHD1080p2398:
	case bmdMode2k2398:
	case bmdMode2kDCI2398:
	case bmdMode4K2160p2398:
	case bmdMode4kDCI2398:
	case bmdMode8K4320p2398:
	case bmdMode8kDCI2398:
		return NOS_MEDIAIO_FRAME_RATE_2398;

	case bmdModeHD1080p24:
	case bmdMode2k24:
	case bmdMode2kDCI24:
	case bmdMode4K2160p24:
	case bmdMode4kDCI24:
	case bmdMode8K4320p24:
	case bmdMode8kDCI24:
		return NOS_MEDIAIO_FRAME_RATE_24;

	case bmdModePAL:
	case bmdModePALp:
	case bmdModeHD1080p25:
	case bmdMode2k25:
	case bmdMode2kDCI25:
	case bmdMode4K2160p25:
	case bmdMode4kDCI25:
	case bmdMode8K4320p25:
	case bmdMode8kDCI25:
		return NOS_MEDIAIO_FRAME_RATE_25;

	case bmdModeNTSC:
	case bmdModeNTSCp:
	case bmdModeHD1080p2997:
	case bmdMode2kDCI2997:
	case bmdMode4K2160p2997:
	case bmdMode4kDCI2997:
	case bmdMode8K4320p2997:
	case bmdMode8kDCI2997:
		return NOS_MEDIAIO_FRAME_RATE_2997;

	case bmdModeHD1080p30:
	case bmdMode2kDCI30:
	case bmdMode4K2160p30:
	case bmdMode4kDCI30:
	case bmdMode8K4320p30:
	case bmdMode8kDCI30:
		return NOS_MEDIAIO_FRAME_RATE_30;

	case bmdModeHD1080p4795:
	case bmdMode2kDCI4795:
	case bmdMode4K2160p4795:
	case bmdMode4kDCI4795:
	case bmdMode8K4320p4795:
	case bmdMode8kDCI4795:
		return NOS_MEDIAIO_FRAME_RATE_4795;

	case bmdModeHD1080p48:
	case bmdMode2kDCI48:
	case bmdMode4K2160p48:
	case bmdMode4kDCI48:
	case bmdMode8K4320p48:
	case bmdMode8kDCI48:
		return NOS_MEDIAIO_FRAME_RATE_48;

	case bmdModeHD1080p50:
	case bmdModeHD1080i50:
	case bmdModeHD720p50:
	case bmdMode2kDCI50:
	case bmdMode4K2160p50:
	case bmdMode4kDCI50:
	case bmdMode8K4320p50:
	case bmdMode8kDCI50:
		return NOS_MEDIAIO_FRAME_RATE_50;

	case bmdModeHD1080p5994:
	case bmdModeHD1080i5994:
	case bmdModeHD720p5994:
	case bmdMode2kDCI5994:
	case bmdMode4K2160p5994:
	case bmdMode4kDCI5994:
	case bmdMode8K4320p5994:
	case bmdMode8kDCI5994:
		return NOS_MEDIAIO_FRAME_RATE_5994;

	case bmdModeHD1080p6000:
	case bmdModeHD1080i6000:
	case bmdModeHD720p60:
	case bmdMode2kDCI60:
	case bmdMode4K2160p60:
	case bmdMode4kDCI60:
	case bmdMode8K4320p60:
		return NOS_MEDIAIO_FRAME_RATE_60;

	case bmdModeHD1080p9590:
	case bmdMode2kDCI9590:
	case bmdMode4K2160p9590:
		return NOS_MEDIAIO_FRAME_RATE_9590;

	case bmdModeHD1080p96:
	case bmdMode2kDCI96:
	case bmdMode4K2160p96:
		return NOS_MEDIAIO_FRAME_RATE_96;

	case bmdModeHD1080p100:
	case bmdMode2kDCI100:
	case bmdMode4K2160p100:
		return NOS_MEDIAIO_FRAME_RATE_100;

	case bmdModeHD1080p11988:
	case bmdMode2kDCI11988:
	case bmdMode4K2160p11988:
		return NOS_MEDIAIO_FRAME_RATE_11988;

	case bmdModeHD1080p120:
	case bmdMode2kDCI120:
	case bmdMode4K2160p120:
		return NOS_MEDIAIO_FRAME_RATE_120;

	default:
		return NOS_MEDIAIO_FRAME_RATE_INVALID;
	}
}

struct FrameGeometryAndRatePair
{
	nosMediaIOFrameGeometry FrameGeometry;
	nosMediaIOFrameRate FrameRate;
};

constexpr FrameGeometryAndRatePair GetFrameGeometryAndRatePairFromDeckLinkDisplayMode(BMDDisplayMode displayMode)
{
	switch (displayMode)
	{
		case bmdModeNTSC: return {NOS_MEDIAIO_FRAME_GEOMETRY_NTSC, NOS_MEDIAIO_FRAME_RATE_2997};
		case bmdModeNTSC2398: return {NOS_MEDIAIO_FRAME_GEOMETRY_NTSC, NOS_MEDIAIO_FRAME_RATE_2398};
		case bmdModePAL: return {NOS_MEDIAIO_FRAME_GEOMETRY_PAL, NOS_MEDIAIO_FRAME_RATE_25};
		case bmdModeNTSCp: return {NOS_MEDIAIO_FRAME_GEOMETRY_NTSC, NOS_MEDIAIO_FRAME_RATE_60};
		case bmdModePALp: return {NOS_MEDIAIO_FRAME_GEOMETRY_PAL, NOS_MEDIAIO_FRAME_RATE_50};
		case bmdModeHD1080p2398: return {NOS_MEDIAIO_FRAME_GEOMETRY_HD1080, NOS_MEDIAIO_FRAME_RATE_2398};
		case bmdModeHD1080p24: return {NOS_MEDIAIO_FRAME_GEOMETRY_HD1080, NOS_MEDIAIO_FRAME_RATE_24};
		case bmdModeHD1080p25: return {NOS_MEDIAIO_FRAME_GEOMETRY_HD1080, NOS_MEDIAIO_FRAME_RATE_25};
		case bmdModeHD1080p2997: return {NOS_MEDIAIO_FRAME_GEOMETRY_HD1080, NOS_MEDIAIO_FRAME_RATE_2997};
		case bmdModeHD1080p30: return {NOS_MEDIAIO_FRAME_GEOMETRY_HD1080, NOS_MEDIAIO_FRAME_RATE_30};
		case bmdModeHD1080p4795: return {NOS_MEDIAIO_FRAME_GEOMETRY_HD1080, NOS_MEDIAIO_FRAME_RATE_4795};
		case bmdModeHD1080p48: return {NOS_MEDIAIO_FRAME_GEOMETRY_HD1080, NOS_MEDIAIO_FRAME_RATE_48};
		case bmdModeHD1080p50: return {NOS_MEDIAIO_FRAME_GEOMETRY_HD1080, NOS_MEDIAIO_FRAME_RATE_50};
		case bmdModeHD1080p5994: return {NOS_MEDIAIO_FRAME_GEOMETRY_HD1080, NOS_MEDIAIO_FRAME_RATE_5994};
		case bmdModeHD1080p6000: return {NOS_MEDIAIO_FRAME_GEOMETRY_HD1080, NOS_MEDIAIO_FRAME_RATE_60};
		case bmdModeHD1080p9590: return {NOS_MEDIAIO_FRAME_GEOMETRY_HD1080, NOS_MEDIAIO_FRAME_RATE_9590};
		case bmdModeHD1080p96: return {NOS_MEDIAIO_FRAME_GEOMETRY_HD1080, NOS_MEDIAIO_FRAME_RATE_96};
		case bmdModeHD1080p100: return {NOS_MEDIAIO_FRAME_GEOMETRY_HD1080, NOS_MEDIAIO_FRAME_RATE_100};
		case bmdModeHD1080p11988: return {NOS_MEDIAIO_FRAME_GEOMETRY_HD1080, NOS_MEDIAIO_FRAME_RATE_11988};
		case bmdModeHD1080p120: return {NOS_MEDIAIO_FRAME_GEOMETRY_HD1080, NOS_MEDIAIO_FRAME_RATE_120};
		case bmdModeHD1080i50: return {NOS_MEDIAIO_FRAME_GEOMETRY_HD1080, NOS_MEDIAIO_FRAME_RATE_50};
		case bmdModeHD1080i5994: return {NOS_MEDIAIO_FRAME_GEOMETRY_HD1080, NOS_MEDIAIO_FRAME_RATE_5994};
		case bmdModeHD1080i6000: return {NOS_MEDIAIO_FRAME_GEOMETRY_HD1080, NOS_MEDIAIO_FRAME_RATE_60};
		case bmdModeHD720p50: return {NOS_MEDIAIO_FRAME_GEOMETRY_HD720, NOS_MEDIAIO_FRAME_RATE_50};
		case bmdModeHD720p5994: return {NOS_MEDIAIO_FRAME_GEOMETRY_HD720, NOS_MEDIAIO_FRAME_RATE_5994};
		case bmdModeHD720p60: return {NOS_MEDIAIO_FRAME_GEOMETRY_HD720, NOS_MEDIAIO_FRAME_RATE_60};
		case bmdMode2k2398: return {NOS_MEDIAIO_FRAME_GEOMETRY_2K, NOS_MEDIAIO_FRAME_RATE_2398};
		case bmdMode2k24: return {NOS_MEDIAIO_FRAME_GEOMETRY_2K, NOS_MEDIAIO_FRAME_RATE_24};
		case bmdMode2k25: return {NOS_MEDIAIO_FRAME_GEOMETRY_2K, NOS_MEDIAIO_FRAME_RATE_25};
		case bmdMode2kDCI2398: return {NOS_MEDIAIO_FRAME_GEOMETRY_2KDCI, NOS_MEDIAIO_FRAME_RATE_2398};
		case bmdMode2kDCI24: return {NOS_MEDIAIO_FRAME_GEOMETRY_2KDCI, NOS_MEDIAIO_FRAME_RATE_24};
		case bmdMode2kDCI25: return {NOS_MEDIAIO_FRAME_GEOMETRY_2KDCI, NOS_MEDIAIO_FRAME_RATE_25};
		case bmdMode2kDCI2997: return {NOS_MEDIAIO_FRAME_GEOMETRY_2KDCI, NOS_MEDIAIO_FRAME_RATE_2997};
		case bmdMode2kDCI30: return {NOS_MEDIAIO_FRAME_GEOMETRY_2KDCI, NOS_MEDIAIO_FRAME_RATE_30};
		case bmdMode2kDCI4795: return {NOS_MEDIAIO_FRAME_GEOMETRY_2KDCI, NOS_MEDIAIO_FRAME_RATE_4795};
		case bmdMode2kDCI48: return {NOS_MEDIAIO_FRAME_GEOMETRY_2KDCI, NOS_MEDIAIO_FRAME_RATE_48};
		case bmdMode2kDCI50: return {NOS_MEDIAIO_FRAME_GEOMETRY_2KDCI, NOS_MEDIAIO_FRAME_RATE_50};
		case bmdMode2kDCI5994: return {NOS_MEDIAIO_FRAME_GEOMETRY_2KDCI, NOS_MEDIAIO_FRAME_RATE_5994};
		case bmdMode2kDCI60: return {NOS_MEDIAIO_FRAME_GEOMETRY_2KDCI, NOS_MEDIAIO_FRAME_RATE_60};
		case bmdMode2kDCI9590: return {NOS_MEDIAIO_FRAME_GEOMETRY_2KDCI, NOS_MEDIAIO_FRAME_RATE_9590};
		case bmdMode2kDCI96: return {NOS_MEDIAIO_FRAME_GEOMETRY_2KDCI, NOS_MEDIAIO_FRAME_RATE_96};
		case bmdMode2kDCI100: return {NOS_MEDIAIO_FRAME_GEOMETRY_2KDCI, NOS_MEDIAIO_FRAME_RATE_100};
		case bmdMode2kDCI11988: return {NOS_MEDIAIO_FRAME_GEOMETRY_2KDCI, NOS_MEDIAIO_FRAME_RATE_11988};
		case bmdMode2kDCI120: return {NOS_MEDIAIO_FRAME_GEOMETRY_2KDCI, NOS_MEDIAIO_FRAME_RATE_120};
		case bmdMode4K2160p2398: return {NOS_MEDIAIO_FRAME_GEOMETRY_4K2160, NOS_MEDIAIO_FRAME_RATE_2398};
		case bmdMode4K2160p24: return {NOS_MEDIAIO_FRAME_GEOMETRY_4K2160, NOS_MEDIAIO_FRAME_RATE_24};
		case bmdMode4K2160p25: return {NOS_MEDIAIO_FRAME_GEOMETRY_4K2160, NOS_MEDIAIO_FRAME_RATE_25};
		case bmdMode4K2160p2997: return {NOS_MEDIAIO_FRAME_GEOMETRY_4K2160, NOS_MEDIAIO_FRAME_RATE_2997};
		case bmdMode4K2160p30: return {NOS_MEDIAIO_FRAME_GEOMETRY_4K2160, NOS_MEDIAIO_FRAME_RATE_30};
		case bmdMode4K2160p4795: return {NOS_MEDIAIO_FRAME_GEOMETRY_4K2160, NOS_MEDIAIO_FRAME_RATE_4795};
		case bmdMode4K2160p48: return {NOS_MEDIAIO_FRAME_GEOMETRY_4K2160, NOS_MEDIAIO_FRAME_RATE_48};
		case bmdMode4K2160p50: return {NOS_MEDIAIO_FRAME_GEOMETRY_4K2160, NOS_MEDIAIO_FRAME_RATE_50};
		case bmdMode4K2160p5994: return {NOS_MEDIAIO_FRAME_GEOMETRY_4K2160, NOS_MEDIAIO_FRAME_RATE_5994};
		case bmdMode4K2160p60: return {NOS_MEDIAIO_FRAME_GEOMETRY_4K2160, NOS_MEDIAIO_FRAME_RATE_60};
		case bmdMode4K2160p9590: return {NOS_MEDIAIO_FRAME_GEOMETRY_4K2160, NOS_MEDIAIO_FRAME_RATE_9590};
		case bmdMode4K2160p96: return {NOS_MEDIAIO_FRAME_GEOMETRY_4K2160, NOS_MEDIAIO_FRAME_RATE_96};
		case bmdMode4K2160p100: return {NOS_MEDIAIO_FRAME_GEOMETRY_4K2160, NOS_MEDIAIO_FRAME_RATE_100};
		case bmdMode4K2160p11988: return {NOS_MEDIAIO_FRAME_GEOMETRY_4K2160, NOS_MEDIAIO_FRAME_RATE_11988};
		case bmdMode4K2160p120: return {NOS_MEDIAIO_FRAME_GEOMETRY_4K2160, NOS_MEDIAIO_FRAME_RATE_120};
		case bmdMode4kDCI2398: return {NOS_MEDIAIO_FRAME_GEOMETRY_4KDCI, NOS_MEDIAIO_FRAME_RATE_2398};
		case bmdMode4kDCI24: return {NOS_MEDIAIO_FRAME_GEOMETRY_4KDCI, NOS_MEDIAIO_FRAME_RATE_24};
		case bmdMode4kDCI25: return {NOS_MEDIAIO_FRAME_GEOMETRY_4KDCI, NOS_MEDIAIO_FRAME_RATE_25};
		case bmdMode4kDCI2997: return {NOS_MEDIAIO_FRAME_GEOMETRY_4KDCI, NOS_MEDIAIO_FRAME_RATE_2997};
		case bmdMode4kDCI30: return {NOS_MEDIAIO_FRAME_GEOMETRY_4KDCI, NOS_MEDIAIO_FRAME_RATE_30};
		case bmdMode4kDCI4795: return {NOS_MEDIAIO_FRAME_GEOMETRY_4KDCI, NOS_MEDIAIO_FRAME_RATE_4795};
		case bmdMode4kDCI48: return {NOS_MEDIAIO_FRAME_GEOMETRY_4KDCI, NOS_MEDIAIO_FRAME_RATE_48};
		case bmdMode4kDCI50: return {NOS_MEDIAIO_FRAME_GEOMETRY_4KDCI, NOS_MEDIAIO_FRAME_RATE_50};
		case bmdMode4kDCI5994: return {NOS_MEDIAIO_FRAME_GEOMETRY_4KDCI, NOS_MEDIAIO_FRAME_RATE_5994};
		case bmdMode4kDCI60: return {NOS_MEDIAIO_FRAME_GEOMETRY_4KDCI, NOS_MEDIAIO_FRAME_RATE_60};
		case bmdModeUnknown: default: return {NOS_MEDIAIO_FRAME_GEOMETRY_INVALID, NOS_MEDIAIO_FRAME_RATE_INVALID};
	}
}

// Function to determine the video scan type based on BMDDisplayMode
inline nosMediaIOVideoScanType GetVideoScanType(BMDDisplayMode displayMode) {
	// Define a map of interlaced modes for quick lookup
	static const std::map<BMDDisplayMode, nosMediaIOVideoScanType> modeScanTypeMap = {
		{bmdModeHD1080i50, NOS_MEDIAIO_VIDEO_INTERLACED_SCAN},
		{bmdModeHD1080i5994, NOS_MEDIAIO_VIDEO_INTERLACED_SCAN},
		{bmdModeHD1080i6000, NOS_MEDIAIO_VIDEO_INTERLACED_SCAN},
		{bmdModeNTSC, NOS_MEDIAIO_VIDEO_INTERLACED_SCAN},
		{bmdModeNTSC2398, NOS_MEDIAIO_VIDEO_INTERLACED_SCAN},
		{bmdModePAL, NOS_MEDIAIO_VIDEO_INTERLACED_SCAN}
	};

	// Check if the mode is in the interlaced map
	if (modeScanTypeMap.find(displayMode) != modeScanTypeMap.end()) {
		return modeScanTypeMap.at(displayMode);
	}

	// Default to progressive for all other modes
	return NOS_MEDIAIO_VIDEO_PROGRESSIVE_SCAN;
}
	
const char* NOSAPI_CALL GetChannelName(nosDeckLinkChannel channel);
}
