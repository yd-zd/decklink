// Copyright MediaZ Teknoloji A.S. All Rights Reserved.
#include <Nodos/PluginHelpers.hpp>

#include <nosDeckLinkSubsystem/nosDeckLinkSubsystem.h>
#include <nosDeviceSubsystem/nosDeviceSubsystem.h>

#include "Conversion_generated.h"
#include "DeckLink_generated.h"
#include "Device_generated.h"

#include <PrefixTree.hpp>

namespace nos::decklink
{

NOS_REGISTER_NAME(Device);
NOS_REGISTER_NAME(ChannelName);
NOS_REGISTER_NAME(IsInput);
NOS_REGISTER_NAME(VideoScanType);
NOS_REGISTER_NAME(Resolution);
NOS_REGISTER_NAME(FrameRate);
NOS_REGISTER_NAME(PixelFormat);
NOS_REGISTER_NAME(IsOpen);
NOS_REGISTER_NAME(ChannelId);
NOS_REGISTER_NAME(ChannelResolution);
NOS_REGISTER_NAME(ChannelPixelFormat);
NOS_REGISTER_NAME(ChannelIsInterlaced);

constexpr auto PIN_VALUE_NONE = "None";

enum class Config : int
{
	IsInput = 0,
	Device,
	ChannelName,
	Resolution,
	FrameRate,
	VideoScanType,
	PixelFormat,
};

enum class ChannelUpdateResult
{
	NothingChanged,
	UnsupportedSettings,
	Opened,
};

struct ConfigStrategy
{
	nos::PrefixTree<std::string> Configs;

	virtual ~ConfigStrategy() = default;
	virtual Config GetNextEntry(Config pin) const = 0;
	virtual std::vector<std::string> BuildSearchKey(Config type, const std::vector<std::string>& allPinValues) const = 0;
	virtual std::vector<std::string> BuildFullKey(const std::vector<std::string>& allPinValues) const = 0;
	virtual bool ShouldCascadeAfter(Config pin) const = 0;
	virtual std::vector<std::string> BuildInsertKey(const std::string& deviceKey, const nosDeckLinkChannelConfig& config) const = 0;
};

// allPinValues layout: {deviceIndex, channelName, resolutionName, frameRateName, videoScanTypeName, pixelFormatName}
//                       [0]          [1]          [2]             [3]            [4]                 [5]

struct InputConfigStrategy : ConfigStrategy
{
	// Tree key: {deviceIndex, channelName, pixelFormatName}
	Config GetNextEntry(Config pin) const override
	{
		if (pin == Config::ChannelName)
			return Config::PixelFormat;
		return static_cast<Config>(static_cast<int>(pin) + 1);
	}

	std::vector<std::string> BuildSearchKey(Config type, const std::vector<std::string>& all) const override
	{
		switch (type)
		{
		case Config::ChannelName: return {all[0]};
		case Config::PixelFormat: return {all[0], all[1]};
		default: return {};
		}
	}

	std::vector<std::string> BuildFullKey(const std::vector<std::string>& all) const override
	{
		return {all[0], all[1], all[5]};
	}

	bool ShouldCascadeAfter(Config pin) const override
	{
		return pin == Config::ChannelName;
	}

	std::vector<std::string> BuildInsertKey(const std::string& deviceKey, const nosDeckLinkChannelConfig& config) const override
	{
		return {deviceKey, nosDeckLink->GetChannelName(config.Channel), nosMediaIO->GetPixelFormatName(config.Input.PixelFormat)};
	}
};

struct OutputConfigStrategy : ConfigStrategy
{
	// Tree key: {deviceIndex, channelName, resolutionName, frameRateName, videoScanTypeName, pixelFormatName}
	Config GetNextEntry(Config pin) const override
	{
		return static_cast<Config>(static_cast<int>(pin) + 1);
	}

	std::vector<std::string> BuildSearchKey(Config type, const std::vector<std::string>& all) const override
	{
		std::vector<std::string> subkey;
		for (int i = 0; i < int(type) - 1; ++i)
			subkey.push_back(all[i]);
		return subkey;
	}

	std::vector<std::string> BuildFullKey(const std::vector<std::string>& all) const override
	{
		return all;
	}

	bool ShouldCascadeAfter(Config pin) const override
	{
		return pin == Config::ChannelName || pin == Config::Resolution ||
		       pin == Config::FrameRate || pin == Config::VideoScanType;
	}

	std::vector<std::string> BuildInsertKey(const std::string& deviceKey, const nosDeckLinkChannelConfig& config) const override
	{
		return {
			deviceKey,
			nosDeckLink->GetChannelName(config.Channel),
			nosMediaIO->GetFrameGeometryName(config.Output.Resolution),
			nosMediaIO->GetFrameRateName(config.Output.FrameRate),
			nosMediaIO->GetVideoScanTypeName(config.Output.ScanType),
			nosMediaIO->GetPixelFormatName(config.Output.PixelFormat)
		};
	}
};

void InputVideoFormatChanged(void* userData, nosMediaIOVideoScanType scanType, nosMediaIOFrameGeometry frameGeometry, nosMediaIOFrameRate frameRate, nosMediaIOPixelFormat pixelFormat);
void FrameResultCallback(void* userData, nosDeckLinkFrameResult result, uint32_t processedFrameNumber);
void DeviceInvalidated(void* userData);
void DeviceStatusCallback(void* userData, const nosDeckLinkDeviceStatus* status);

struct ChannelHandler
{
	class ChannelNode& Node;
	bool ShouldOpen = true;
	bool IsOpen = false;
	bool IsStreamStarted = false;
	uuid IsOpenPinId;
	uuid DevicePinId;
	uuid ChannelNamePinId;
	uuid VideoScanTypePinId;
	uuid OutChannelPinId;
	uuid OutResolutionPinId;
	uuid OutPixelFormatPinId;
	uuid OutChannelIsInterlacedPinId;
	uuid ResolutionPinId;
	uuid FrameRatePinId;
	uuid PixelFormatPinId;
	nosMediaIOVideoScanType VideoScanType = NOS_MEDIAIO_VIDEO_PROGRESSIVE_SCAN; // Default value
	int32_t DeviceIndex = -1;
	std::string ModelName;
	nosMediaIODirection Direction = NOS_MEDIAIO_DIRECTION_OUTPUT;
	nosDeckLinkChannel Channel = NOS_DECKLINK_CHANNEL_INVALID;
	nosMediaIOFrameGeometry Resolution = NOS_MEDIAIO_FRAME_GEOMETRY_INVALID;
	nosMediaIOFrameRate FrameRate = NOS_MEDIAIO_FRAME_RATE_INVALID;
	nosMediaIOPixelFormat PixelFormat = NOS_MEDIAIO_PIXEL_FORMAT_INVALID;
	int32_t VideoInputChangeCallbackId = -1;
	int32_t FrameResultCallbackId = -1;
	int32_t DeviceStatusCallbackId = -1;
	std::optional<nosDeckLinkReferenceStatus> ReferenceStatus;

	std::atomic_uint32_t DropCount = 0;
	std::mutex DecklinkThreadMutex;
	struct
	{
		uint32_t DeviceIndex = 0;
		std::string ChannelName = "Unknown Channel";
		bool DropDetectionEnabled = false;
		uint32_t FramesSinceLastDrop = 0;
		bool DropDetected = false;
		bool PathRestartRequested = false;
	} DeckLinkThreadStatus;

	template<auto Member, typename T>
	ChannelUpdateResult Update(const T& value, bool reopen = true)
	{
		if (this->*Member == value)
			return ChannelUpdateResult::NothingChanged;
		if (reopen && IsOpen)
			Close();
		this->*Member = value;
		UpdateStatusAndOutPins();
		if (reopen && !Open())
			return ChannelUpdateResult::UnsupportedSettings;
		if (IsOpen && !StartIfOpen())
		{
			Close();
			return ChannelUpdateResult::UnsupportedSettings;
		}
		return ChannelUpdateResult::Opened;
	}

	ChannelHandler(ChannelNode& node) : Node(node) {}

	~ChannelHandler()
	{
		Close();
	}

	void OnInputVideoFormatChanged_DeckLinkThread(nosMediaIOVideoScanType scanType, nosMediaIOFrameGeometry frameGeometry, nosMediaIOFrameRate frameRate, nosMediaIOPixelFormat pixelFormat)
	{
		const char* scanTypeCstr = nosMediaIO->GetVideoScanTypeName(scanType);
		const char* frameGeometryCstr = nosMediaIO->GetFrameGeometryName(frameGeometry);
		const char* frameRateCstr = nosMediaIO->GetFrameRateName(frameRate);
		const char* pixelFormatCstr = nosMediaIO->GetPixelFormatName(pixelFormat);
		VideoScanType = scanType;
		Resolution = frameGeometry;
		FrameRate = frameRate;
		PixelFormat = pixelFormat;
		nosEngine.SetPinValue(ResolutionPinId, nos::Buffer(frameGeometryCstr, strlen(frameGeometryCstr) + 1));
		nosEngine.SetPinValue(FrameRatePinId, nos::Buffer(frameRateCstr, strlen(frameRateCstr) + 1));
		nosEngine.SetPinValue(VideoScanTypePinId, nos::Buffer(scanTypeCstr, strlen(scanTypeCstr) + 1));
		nosEngine.SetPinValue(PixelFormatPinId, nos::Buffer(pixelFormatCstr, strlen(pixelFormatCstr) + 1));
		UpdateChannelStatus();
		UpdateOutPins();
	}

	void OnFrameEnd(nosDeckLinkFrameResult result, uint32_t processedFrameNumber)
	{
		std::unique_lock lock(DecklinkThreadMutex);
		if (!DeckLinkThreadStatus.DropDetectionEnabled)
			return;
		switch (result)
		{
		case NOS_DECKLINK_FRAME_DROPPED:
		{
			++DropCount;
			DeckLinkThreadStatus.FramesSinceLastDrop = 0;
			DeckLinkThreadStatus.DropDetected = true;
			nosEngine.LogD("(Device %d, %s) dropped a frame", DeckLinkThreadStatus.DeviceIndex, DeckLinkThreadStatus.ChannelName.c_str());
			SetStatus(StatusType::DropCount, fb::NodeStatusMessageType::WARNING, "Drop Count: " + std::to_string(DropCount));
			UpdateStatus();
			break;
		}
		case NOS_DECKLINK_FRAME_COMPLETED:
		{
			if (DeckLinkThreadStatus.DropDetected)
				++DeckLinkThreadStatus.FramesSinceLastDrop;
			if (DeckLinkThreadStatus.FramesSinceLastDrop > 50)
			{
				if (std::string_view(DeckLinkThreadStatus.ChannelName).starts_with("IP "))
				{
					// An IP channel is paced by PTP. Restarting its Nodos path after
					// a recovered gap creates another gap and an endless restart loop.
					DeckLinkThreadStatus.DropDetected = false;
					DeckLinkThreadStatus.FramesSinceLastDrop = 0;
				}
				else if (!DeckLinkThreadStatus.PathRestartRequested)
				{
					nosEngine.LogW("Requesting path restart due to frame drops");
					nosEngine.SendPathRestart(OutChannelPinId);
					DeckLinkThreadStatus.PathRestartRequested = true;
				}
			}
			break;
		}
		}
	}

	void UnregisterDeviceCallbacks()
	{
		if (DeviceIndex != -1)
		{
			if (DeviceStatusCallbackId != -1)
			{
				nosDeckLink->UnregisterDeviceStatusCallback(DeviceIndex, DeviceStatusCallbackId);
				DeviceStatusCallbackId = -1;
			}
		}
	}

	void RegisterDeviceCallbacks()
	{
		if (DeviceIndex != -1)
		{
			DeviceStatusCallbackId = nosDeckLink->RegisterDeviceStatusCallback(DeviceIndex, &DeviceStatusCallback, this);
		}
	}

	void OnDeviceStatusUpdate_DeckLinkThread(const nosDeckLinkDeviceStatus* status)
	{
		char watchLogKey[256], statusStr[256];
		std::snprintf(watchLogKey, sizeof(watchLogKey), "DeckLink Device %d: PCIe Status", DeviceIndex);
		std::snprintf(statusStr, sizeof(statusStr), "Gen%lld x%lld", status->PCIeLink.Speed, status->PCIeLink.Width);		
		nosEngine.WatchLog(watchLogKey, statusStr);

		std::snprintf(watchLogKey, sizeof(watchLogKey), "DeckLink Device %d: On-Board Temperature", DeviceIndex);
		std::snprintf(statusStr, sizeof(statusStr), "%lld Celcius", status->Temperature);
		nosEngine.WatchLog(watchLogKey, statusStr);

		MiscMessages.clear();
		for (int i = 0; i < int(status->Messages.Count); ++i)
		{
			auto& message = status->Messages.List[i];
			fb::TNodeStatusMessage msg {
				.text = message.Message,
				.type = (fb::NodeStatusMessageType)message.Type
			};
			MiscMessages.emplace_back(std::move(msg));
		}
		UpdateStatus();
	}

	void DeviceInvalidated();

	bool CanOpen()
	{
		if (!ShouldOpen || DeviceIndex == -1 || Channel == NOS_DECKLINK_CHANNEL_INVALID)
			return false;
		if (!IsInput())
			return Resolution != NOS_MEDIAIO_FRAME_GEOMETRY_INVALID && FrameRate != NOS_MEDIAIO_FRAME_RATE_INVALID && PixelFormat != NOS_MEDIAIO_PIXEL_FORMAT_INVALID;
		return true;
	}
	
	bool IsInput()
	{
		return Direction == NOS_MEDIAIO_DIRECTION_INPUT;
	}

	bool Open();

	bool StartIfOpen()
	{
		if (!IsOpen)
			return false;
		if (IsStreamStarted)
			return true;
		IsStreamStarted = nosDeckLink->StartStream(DeviceIndex, Channel) == NOS_RESULT_SUCCESS;
		if (IsStreamStarted)
			ResetDropState();
		else
			nosEngine.LogE("Failed to start stream for device %d, channel %s", DeviceIndex, GetChannelName().c_str());
		return IsStreamStarted;
	}

	void StopIfOpen()
	{
		if (IsOpen && IsStreamStarted)
		{
			nosDeckLink->StopStream(DeviceIndex, Channel);
			IsStreamStarted = false;
			ResetDropState();
		}
	}

	void ResetDropState()
	{
		std::unique_lock lock(DecklinkThreadMutex);
		DeckLinkThreadStatus = {};
	}

	void Close();

	std::string GetChannelName()
	{
		std::stringstream channelString;
		char nameBuffer[256]{};
		if (DeviceIndex == -1 ||
			Channel == NOS_DECKLINK_CHANNEL_INVALID ||
			NOS_RESULT_SUCCESS != nosDeckLink->GetPortMappedChannelName(DeviceIndex, Channel, nameBuffer, sizeof(nameBuffer)))
			channelString << "Unknown Channel";
		else
			channelString << nameBuffer;
		return channelString.str();
	}

	void UpdateChannelStatus();

	bool IsIPInput()
	{
		return IsInput() && GetChannelName().starts_with("IP ");
	}

	void UpdateOutPins()
	{
		nosVec2u resolution{};
		nosMediaIO->Get2DFrameResolution(Resolution, &resolution);
		nosEngine.SetPinValue(OutResolutionPinId, nos::Buffer::From(resolution));
		nos::mediaio::YCbCrPixelFormat ycbcrFormat{};
		switch (PixelFormat)
		{
		case NOS_MEDIAIO_PIXEL_FORMAT_YCBCR_8BIT:
			ycbcrFormat = nos::mediaio::YCbCrPixelFormat::YUV8;
			break;
		case NOS_MEDIAIO_PIXEL_FORMAT_YCBCR_10BIT:
			ycbcrFormat = nos::mediaio::YCbCrPixelFormat::V210;
			break;
		}
		nosEngine.SetPinValue(OutPixelFormatPinId, nos::Buffer::From(ycbcrFormat));
		nosEngine.SetPinValue(OutChannelIsInterlacedPinId, nos::Buffer::From(VideoScanType == NOS_MEDIAIO_VIDEO_INTERLACED_SCAN));
	}

	void UpdateReferenceStatus()
	{
		std::string refString;
		fb::NodeStatusMessageType type = fb::NodeStatusMessageType::WARNING;
		if (ReferenceStatus)
		{
			switch (*ReferenceStatus)
			{
			case NOS_DECKLINK_REFERENCE_STATUS_LOCKED:
				type = fb::NodeStatusMessageType::INFO;
				refString = "Reference: Locked";
				break;
			case NOS_DECKLINK_REFERENCE_STATUS_UNLOCKED:
				refString = "Reference: Unlocked";
				break;
			case NOS_DECKLINK_REFERENCE_STATUS_UNKNOWN:
				refString = "Reference Lock: Unknown";
				break;
			}
		}
		if (refString.empty())
			ClearStatus(StatusType::Reference);
		else
			SetStatus(StatusType::Reference, type, refString);
	}

	void UpdateStatusAndOutPins()
	{
		UpdateReferenceStatus();
		UpdateChannelStatus();
		UpdateOutPins();
	}

	void ClearMiscMessages()
	{
		MiscMessages.clear();
		UpdateStatus();
	}
	
	void UpdateStatus();

	enum class StatusType
	{
		Channel,
		Reference,
		ReferenceInvalid,
		DeltaSecondsCompatible,
		DropCount,
		Profile,
		Diagnostics
	};

	void SetStatus(StatusType statusType, fb::NodeStatusMessageType msgType, std::string text);
	void ClearStatus(StatusType statusType);
	
	std::map<StatusType, fb::TNodeStatusMessage> StatusMessages;
	std::vector<fb::TNodeStatusMessage> MiscMessages;
	std::recursive_mutex StatusMutex;
};

void InputVideoFormatChanged(void* userData, nosMediaIOVideoScanType scanType, nosMediaIOFrameGeometry frameGeometry, nosMediaIOFrameRate frameRate, nosMediaIOPixelFormat pixelFormat)
{
	static_cast<ChannelHandler*>(userData)->OnInputVideoFormatChanged_DeckLinkThread(scanType, frameGeometry, frameRate, pixelFormat);
}

void FrameResultCallback(void* userData, nosDeckLinkFrameResult result, uint32_t processedFrameNumber)
{
	static_cast<ChannelHandler*>(userData)->OnFrameEnd(result, processedFrameNumber);
}

void DeviceInvalidated(void* userData)
{
	static_cast<ChannelHandler*>(userData)->DeviceInvalidated();
}

void DeviceStatusCallback(void* userData, const nosDeckLinkDeviceStatus* status)
{
	static_cast<ChannelHandler*>(userData)->OnDeviceStatusUpdate_DeckLinkThread(status);
}

class ChannelNode : public nos::NodeContext
{
public:
	bool OnlyUpdateDevicePinValue = false;
	ChannelNode(nosFbNodePtr node) : NodeContext(node), Channel(*this)
	{
		SetPinVisualizer(NSN_Device, {.type = nos::fb::VisualizerType::NAMED_VALUE, .name = sys::device::GetDeviceListNameForVendor(NOS_NAME(NOS_DECKLINK_VENDOR_NAME)), .hide_value = true});
		SetPinVisualizer(NSN_ChannelName, {.type = nos::fb::VisualizerType::COMBO_BOX, .name = GetStringListName(Config::ChannelName)});
		SetPinVisualizer(NSN_Resolution, {.type = nos::fb::VisualizerType::COMBO_BOX, .name = GetStringListName(Config::Resolution)});
		SetPinVisualizer(NSN_FrameRate, {.type = nos::fb::VisualizerType::COMBO_BOX, .name = GetStringListName(Config::FrameRate)});
		SetPinVisualizer(NSN_VideoScanType, {.type = nos::fb::VisualizerType::COMBO_BOX, .name = GetStringListName(Config::VideoScanType)});
		SetPinVisualizer(NSN_PixelFormat, {.type = nos::fb::VisualizerType::COMBO_BOX, .name = GetStringListName(Config::PixelFormat)});

		Channel.IsOpenPinId = *GetPinId(NSN_IsOpen);
		Channel.DevicePinId = *GetPinId(NSN_Device);
		Channel.ChannelNamePinId = *GetPinId(NSN_ChannelName);
		Channel.OutChannelPinId = *GetPinId(NSN_ChannelId);
		Channel.OutResolutionPinId = *GetPinId(NSN_ChannelResolution);
		Channel.OutPixelFormatPinId = *GetPinId(NSN_ChannelPixelFormat);
		Channel.OutChannelIsInterlacedPinId = *GetPinId(NSN_ChannelIsInterlaced);
		Channel.ResolutionPinId = *GetPinId(NSN_Resolution);
		Channel.FrameRatePinId = *GetPinId(NSN_FrameRate);
		Channel.VideoScanTypePinId = *GetPinId(NSN_VideoScanType);
		Channel.PixelFormatPinId = *GetPinId(NSN_PixelFormat);
		Channel.UpdateChannelStatus();

		BuildPossibleConfigs();

		// TODO: Refactor repetitive code in pin value watchers.
		AddPinValueWatcher(NSN_IsOpen, [this](const nos::Buffer& newVal, std::optional<nos::Buffer> oldValue) {
			Channel.ShouldOpen = *InterpretPinValue<bool>(newVal);
			if (!Channel.ShouldOpen)
				Channel.Close();
			else if (Channel.Open() && !Channel.StartIfOpen())
				Channel.Close();
		});
		AddPinValueWatcher(NSN_IsInput, [this](const nos::Buffer& newVal, std::optional<nos::Buffer> oldValue) {
			auto newValue = *InterpretPinValue<bool>(newVal) ? NOS_MEDIAIO_DIRECTION_INPUT : NOS_MEDIAIO_DIRECTION_OUTPUT;
			Channel.Update<&ChannelHandler::Direction>(newValue);
			UpdateAfter(Config::IsInput, !oldValue);
		});
		AddPinValueWatcher(NSN_Device, [this](const nos::Buffer& newVal, std::optional<nos::Buffer> oldValue) {
			DevicePinValue = newVal.As<sys::device::TDeviceInfo>();
			Channel.ModelName = DevicePinValue.model_name;
			if (OnlyUpdateDevicePinValue)
			{
				OnlyUpdateDevicePinValue = false;
				return;
			}

			int32_t newDeviceIndex = -1;
			nosDeviceId newDeviceId = 0;
			if (auto deviceIdPair = GetDeviceIndex(DevicePinValue))
			{
				newDeviceIndex = deviceIdPair->first;
				newDeviceId = deviceIdPair->second;
			}

			auto updated = Channel.DeviceIndex != newDeviceIndex;
			if (updated)
				Channel.UnregisterDeviceCallbacks();
			Channel.Update<&ChannelHandler::DeviceIndex>(newDeviceIndex);
			if (updated)
				Channel.RegisterDeviceCallbacks();
			if (DevicePinValue.vendor_name != PIN_VALUE_NONE && Channel.DeviceIndex == -1)
				ResetPin(NSN_Device);
			else
			{
				if (oldValue)
					ResetAfter(Config::Device);
				else if (Channel.DeviceIndex != -1)
				{
					nosDeviceInfo foundDeviceInfo{};
					auto res = nosDevice->GetDeviceInfo(newDeviceId, &foundDeviceInfo);
					NOS_SOFT_CHECK(res == NOS_RESULT_SUCCESS, "Device must be found at this point")
						if (NOS_RESULT_SUCCESS == res)
						{
							auto foundDeviceObj = sys::device::ConvertDeviceInfo(foundDeviceInfo);
							if (DevicePinValue != foundDeviceObj)
							{
								OnlyUpdateDevicePinValue = true;
								SetPinValue(NSN_Device, nos::Buffer::From(foundDeviceObj));
							}
						}
				}
				else if (DevicePinValue.vendor_name == PIN_VALUE_NONE)
					AutoSelectIfSingle(NSN_Device, GetPossibleDevices());
			}
			UpdateAfter(Config::Device, !oldValue);
		});
		AddPinValueWatcher(NSN_ChannelName, [this](const nos::Buffer& newVal, std::optional<nos::Buffer> oldValue) {
			ChannelPinValue = InterpretPinValue<const char>(newVal);
			auto newChannel = nosDeckLink->GetChannelFromPortMappedName(Channel.DeviceIndex, ChannelPinValue.c_str());
			Channel.Update<&ChannelHandler::Channel>(newChannel);
			if (ChannelPinValue != PIN_VALUE_NONE && newChannel == NOS_DECKLINK_CHANNEL_INVALID)
				ResetPin(NSN_ChannelName);
			else
			{
				if (oldValue)
					ResetAfter(Config::ChannelName);
				else if (ChannelPinValue == PIN_VALUE_NONE)
					AutoSelectIfSingle(NSN_ChannelName, GetPossibleValues(Config::ChannelName));
			}
			UpdateAfter(Config::ChannelName, !oldValue);
		});
		AddPinValueWatcher(NSN_Resolution, [this](const nos::Buffer& newVal, std::optional<nos::Buffer> oldValue) {
			ResolutionPinValue = InterpretPinValue<const char>(newVal);
			auto newResolution = nosMediaIO->GetFrameGeometryFromString(ResolutionPinValue.c_str());
			Channel.Update<&ChannelHandler::Resolution>(newResolution, !Channel.IsInput());
			if (ResolutionPinValue != PIN_VALUE_NONE && newResolution == NOS_MEDIAIO_FRAME_GEOMETRY_INVALID)
				ResetPin(NSN_Resolution);
			else
			{
				if (oldValue)
					ResetAfter(Config::Resolution);
				else if (ResolutionPinValue == PIN_VALUE_NONE)
					AutoSelectIfSingle(NSN_Resolution, GetPossibleValues(Config::Resolution));
			}
			UpdateAfter(Config::Resolution, !oldValue);
		});
		AddPinValueWatcher(NSN_FrameRate, [this](const nos::Buffer& newVal, std::optional<nos::Buffer> oldValue) {
			FrameRatePinValue = InterpretPinValue<const char>(newVal);
			auto newFrameRate = nosMediaIO->GetFrameRateFromString(FrameRatePinValue.c_str());
			Channel.Update<&ChannelHandler::FrameRate>(newFrameRate, !Channel.IsInput());
			if (FrameRatePinValue != PIN_VALUE_NONE && newFrameRate == NOS_MEDIAIO_FRAME_RATE_INVALID)
				ResetPin(NSN_FrameRate);
			else
			{
				if (oldValue)
					ResetAfter(Config::FrameRate);
				else if (FrameRatePinValue == PIN_VALUE_NONE)
					AutoSelectIfSingle(NSN_FrameRate, GetPossibleValues(Config::FrameRate));
			}
			UpdateAfter(Config::FrameRate, !oldValue);
		});
		AddPinValueWatcher(NSN_VideoScanType, [this](const nos::Buffer& newVal, std::optional<nos::Buffer> oldValue) {
			VideoScanTypePinValue = InterpretPinValue<const char>(newVal);
			auto newVideoScanType = nosMediaIO->GetVideoScanTypeFromString(VideoScanTypePinValue.c_str());
			Channel.Update<&ChannelHandler::VideoScanType>(newVideoScanType, !Channel.IsInput());
			if (VideoScanTypePinValue != PIN_VALUE_NONE && newVideoScanType == NOS_MEDIAIO_VIDEO_SCAN_TYPE_INVALID)
				ResetPin(NSN_VideoScanType);
			else
			{
				if (oldValue)
					ResetAfter(Config::VideoScanType);
				else if (VideoScanTypePinValue == PIN_VALUE_NONE)
					AutoSelectIfSingle(NSN_VideoScanType, GetPossibleValues(Config::VideoScanType));
			}
			UpdateAfter(Config::VideoScanType, !oldValue);
		});
		AddPinValueWatcher(NSN_PixelFormat, [this](const nos::Buffer& newVal, std::optional<nos::Buffer> oldValue) {
			PixelFormatPinValue = InterpretPinValue<const char>(newVal);
			auto newPixelFormat = nosMediaIO->GetPixelFormatFromString(PixelFormatPinValue.c_str());
			Channel.Update<&ChannelHandler::PixelFormat>(newPixelFormat);
			if (PixelFormatPinValue != PIN_VALUE_NONE && newPixelFormat == NOS_MEDIAIO_PIXEL_FORMAT_INVALID)
				ResetPin(NSN_FrameRate);
			else
			{
				if (oldValue)
					ResetAfter(Config::PixelFormat);
				else if (PixelFormatPinValue == PIN_VALUE_NONE)
					AutoSelectIfSingle(NSN_PixelFormat, GetPossibleValues(Config::PixelFormat));
			}
			UpdateAfter(Config::PixelFormat, !oldValue);
		});
	}
	
	static std::optional<std::pair<uint32_t, nosDeviceId>> GetDeviceIndex(sys::device::TDeviceInfo const& deviceInfo)
	{
		nosDeviceInfo info = sys::device::ConvertDeviceInfo(deviceInfo);
		nosDeviceId deviceId{};
		int32_t newDeviceIndex = -1;
		auto res = nosDevice->GetSuitableDevice(&info, &deviceId);
		if (res != NOS_RESULT_SUCCESS)
		{
			return std::nullopt;
		}
		uint64_t handle{};
		res = nosDevice->GetDeviceHandle(deviceId, &handle);
		assert(res == NOS_RESULT_SUCCESS);
		return std::make_pair(uint32_t(handle), deviceId);
	}

	template <typename T>
	void AutoSelectIfSingle(nosName pinName, std::vector<T> const& list)
	{
		if constexpr (std::is_same_v<T, std::string>)
		{
			if (list.size() == 2)
				SetPinValue(pinName, nosBuffer{.Data = (void*)list[1].c_str(), .Size = list[1].size() + 1});
		}
		if constexpr (std::is_same_v<T, sys::device::TDeviceInfo>)
		{
			if (list.size() == 1)
				SetPinValue(pinName, nos::Buffer::From(list[0]));
		}
	}

	void UpdateAfter(Config pin, bool first)
	{
		bool isInput = Channel.IsInput();
		switch (pin)
		{
		case Config::IsInput: {
			ChangePinReadOnly(NSN_VideoScanType, isInput);
			ChangePinReadOnly(NSN_Resolution, isInput);
			ChangePinReadOnly(NSN_FrameRate, isInput);
			ChangePinReadOnly(NSN_PixelFormat, false);
			auto deviceList = GetPossibleDevices();
			if (!first)
				AutoSelectIfSingle(GetPinName(GetNextEntry(pin)), deviceList);
			break;
		}
		case Config::Device: {
			auto next = GetNextEntry(pin);
			auto values = GetPossibleValues(next);
			UpdateStringList(GetStringListName(next), values);
			if (!first)
				AutoSelectIfSingle(GetPinName(next), values);
			break;
		}
		case Config::ChannelName:
		case Config::Resolution:
		case Config::FrameRate:
		case Config::VideoScanType:
		{
			if (!GetActiveStrategy().ShouldCascadeAfter(pin))
				break;
			auto next = GetNextEntry(pin);
			auto values = GetPossibleValues(next);
			UpdateStringList(GetStringListName(next), values);
			if (!first)
				AutoSelectIfSingle(GetPinName(next), values);
			break;
		}
		}
	}

	nos::Name GetPinName(Config pin)
	{
		switch (pin)
		{
			case Config::IsInput: return NSN_IsInput;
			case Config::Device: return NSN_Device;
			case Config::ChannelName: return NSN_ChannelName;
			case Config::Resolution: return NSN_Resolution;
			case Config::FrameRate: return NSN_FrameRate;
			case Config::VideoScanType: return NSN_VideoScanType;
			case Config::PixelFormat: return NSN_PixelFormat;
			default:
				NOS_SOFT_CHECK(false, "Invalid ChannelConfigEntry");
				return nos::Name();
		}
	}

	Config GetNextEntry(Config pin)
	{
		return GetActiveStrategy().GetNextEntry(pin);
	}

	void ResetAfter(Config pin)
	{
		if (pin == Config::PixelFormat)
			return;
		auto pinToSet = GetPinName(GetNextEntry(pin));
		auto& strategy = GetActiveStrategy();
		auto currentConfig = strategy.BuildFullKey(GetAllPinValues());
		std::vector<std::vector<std::string>> results;
		strategy.Configs.Search(currentConfig, results);
		if (results.empty())
			ResetPin(pinToSet);
	}

	void ResetPin(nosName name)
	{
		if (name == NSN_Device)
			SetPinValue(name, nos::Buffer::From(sys::device::NoneDeviceInfo()));
		else
			SetPinValue(name, nosBuffer{.Data = (void*)PIN_VALUE_NONE, .Size = 5});
	}

	nosResult ExecuteNode(nosNodeExecuteParams* params) override
	{
		params->MarkAllOutsDirty = NOS_FALSE;
		{
			std::unique_lock lock(Channel.DecklinkThreadMutex);
			if (Channel.DeckLinkThreadStatus.DropDetectionEnabled == (params->TimingInfo.TimingMode == NOS_EXECUTION_TIMING_MODE_VARIABLE_STEP))
			{
				if (params->TimingInfo.TimingMode == NOS_EXECUTION_TIMING_MODE_VARIABLE_STEP)
					Channel.DeckLinkThreadStatus = {};
				else
				{
					Channel.DeckLinkThreadStatus.DropDetectionEnabled = true;
					Channel.DeckLinkThreadStatus.DeviceIndex = Channel.DeviceIndex;
					Channel.DeckLinkThreadStatus.ChannelName = Channel.GetChannelName();
				}
			}
		}
		if (Channel.IsOpen)
		{
			if (Channel.Direction == NOS_MEDIAIO_DIRECTION_OUTPUT)
			{
				// Get Reference Status and update status
				nosDeckLinkReferenceStatus refStatus;	
				if (NOS_RESULT_SUCCESS == nosDeckLink->GetOutputReferenceStatus(Channel.DeviceIndex, Channel.Channel, &refStatus))
				{
					Channel.Update<&ChannelHandler::ReferenceStatus>(refStatus);
				}
			}
		}
		// A disabled or temporarily unavailable channel is an expected idle state.
		// Keep this path asleep until a ChannelId update restarts it instead of
		// failing the runner shared by the other DeckLink channels.
		return Channel.IsOpen && Channel.IsStreamStarted ? NOS_RESULT_SUCCESS : NOS_RESULT_PENDING;
	}

	std::string GetStringListName(Config type)
	{
		std::stringstream prefix;
		prefix << "decklink.";
		switch (type)
		{
		case Config::ChannelName: prefix << "ChannelList"; break;
		case Config::Resolution: prefix << "ResolutionList"; break;
		case Config::FrameRate: prefix << "FrameRateList"; break;
		case Config::VideoScanType: prefix << "VideoScanList"; break;
		case Config::PixelFormat: prefix << "PixelFormatList"; break;
		default:
			NOS_SOFT_CHECK(false, "Invalid ChannelConfigEntry");
			break;
		}
		prefix << "." << std::string(NodeId);
		return prefix.str();
	}
	
	InputConfigStrategy InputStrategy;
	OutputConfigStrategy OutputStrategy;

	ConfigStrategy& GetActiveStrategy()
	{
		return Channel.IsInput() ? static_cast<ConfigStrategy&>(InputStrategy) : static_cast<ConfigStrategy&>(OutputStrategy);
	}

	std::vector<std::string> GetAllPinValues()
	{
		return {
			std::to_string(Channel.DeviceIndex), ChannelPinValue, ResolutionPinValue,
			FrameRatePinValue, VideoScanTypePinValue, PixelFormatPinValue
		};
	}

	void BuildPossibleConfigs()
	{
		auto devices = GetPossibleDevices();
		for (auto& device : devices)
		{
			auto deviceIdIndexPair = GetDeviceIndex(device);
			if (!deviceIdIndexPair)
				continue;
			auto& deviceIndex = deviceIdIndexPair->first;
			size_t count = 0;
			if (NOS_RESULT_SUCCESS != nosDeckLink->GetAvailableChannelConfigurations(deviceIndex, nullptr, &count) || count == 0)
				continue;
			std::vector<nosDeckLinkChannelConfig> configs(count);
			if (NOS_RESULT_SUCCESS != nosDeckLink->GetAvailableChannelConfigurations(deviceIndex, configs.data(), &count))
				continue;
			auto deviceKey = std::to_string(deviceIndex);
			for (auto& config : configs)
			{
				auto& strategy = config.Direction == NOS_MEDIAIO_DIRECTION_INPUT
					? static_cast<ConfigStrategy&>(InputStrategy)
					: static_cast<ConfigStrategy&>(OutputStrategy);
				strategy.Configs.Insert(strategy.BuildInsertKey(deviceKey, config));
			}
		}
	}

	std::vector<sys::device::TDeviceInfo> GetPossibleDevices()
	{
		return sys::device::GetDevicesWithVendor(NOS_NAME(NOS_DECKLINK_VENDOR_NAME));
	}

	static std::vector<nosDeckLinkChannel> GetPossibleChannels(uint32_t deviceIndex, nosMediaIODirection dir)
	{
		std::vector<nosDeckLinkChannel> channels;
		nosDeckLinkChannelList channelList{};
		nosDeckLink->GetAvailableChannels(deviceIndex, dir, &channelList);
		for (size_t i = 0; i < channelList.Count; i++)
			channels.push_back(channelList.Channels[i]);
		return channels;
	}

	static std::vector<std::string> GetPossibleChannelNames(uint32_t deviceIndex, nosMediaIODirection direction)
	{
		std::vector<std::string> channelNames = {PIN_VALUE_NONE};
		auto channels = GetPossibleChannels(deviceIndex, direction);
		for (auto& channel : channels)
		{
			std::string channelName(256, '\0');
			nosDeckLink->GetPortMappedChannelName(deviceIndex, channel, channelName.data(), channelName.size());
			channelNames.push_back(channelName);
		}
		return channelNames;
	}

	std::vector<std::string> GetPossibleValues(Config type)
	{
		if (type == Config::IsInput || type == Config::Device)
			return {};
		auto& strategy = GetActiveStrategy();
		auto allPinValues = GetAllPinValues();
		auto searchKey = strategy.BuildSearchKey(type, allPinValues);
		std::vector<std::vector<std::string>> results;
		strategy.Configs.Search(searchKey, results);
		std::set<std::string> uniqueValues;
		for (auto& result : results)
			if (result.size() > searchKey.size())
				uniqueValues.insert(result[searchKey.size()]);
		std::vector<std::string> values = { PIN_VALUE_NONE };
		for (auto& value : uniqueValues)
			values.push_back(value);
		return values;
	}

	sys::device::TDeviceInfo DevicePinValue;
	std::string ChannelPinValue = PIN_VALUE_NONE;
	std::string ResolutionPinValue = PIN_VALUE_NONE;
	std::string FrameRatePinValue = PIN_VALUE_NONE;
	std::string VideoScanTypePinValue = PIN_VALUE_NONE;
	std::string PixelFormatPinValue = PIN_VALUE_NONE;

	ChannelHandler Channel;

	void OnPathStart() override
	{
		if (!Channel.IsOpen)
			return;
		if (Channel.IsInput())
			nosDeckLink->ResetInputFrames(Channel.DeviceIndex, Channel.Channel);
		Channel.ResetDropState();
	}
};

void ChannelHandler::DeviceInvalidated()
{
	nosEngine.SetPinValue(ChannelNamePinId, nos::Buffer(PIN_VALUE_NONE, 5));
}

bool ChannelHandler::Open()
{
	{
		if (IsOpen)
			return true;
		if (!CanOpen())
			return false;
		nosDeckLinkOpenChannelParams params {
			.Direction = Direction,
			.Channel = Channel,
			.PixelFormat = IsInput() && PixelFormat == NOS_MEDIAIO_PIXEL_FORMAT_INVALID ? NOS_MEDIAIO_PIXEL_FORMAT_YCBCR_10BIT : PixelFormat,
			.Output = {}
		};
		if (!IsInput())
		{
			params.Output.Geometry = Resolution;
			params.Output.FrameRate = FrameRate;
			params.Output.ScanType = VideoScanType;
		}
		else
		{
			VideoInputChangeCallbackId = nosDeckLink->RegisterInputVideoFormatChangeCallback(DeviceIndex, Channel, &InputVideoFormatChanged, this);
		}
		auto res = nosDeckLink->OpenChannel(DeviceIndex, &params);
		if (res == NOS_RESULT_SUCCESS)
		{
			IsOpen = true;
			ChannelId id(DeviceIndex, Channel, Direction);
			nosEngine.SetPinValue(OutChannelPinId, nos::Buffer::From(id));
			nosEngine.SendPathRestart(OutChannelPinId);
			FrameResultCallbackId = nosDeckLink->RegisterFrameResultCallback(DeviceIndex, Channel, &FrameResultCallback, this);
			{
				std::unique_lock lock(DecklinkThreadMutex);
				DropCount = 0;
				DeckLinkThreadStatus = {};
				ClearStatus(StatusType::DropCount);
			}
			UpdateStatus();
		}
		else if (IsInput() && VideoInputChangeCallbackId != -1)
		{
			nosDeckLink->UnregisterInputVideoFormatChangeCallback(DeviceIndex, Channel, VideoInputChangeCallbackId);
			VideoInputChangeCallbackId = -1;
		}
		UpdateStatusAndOutPins();
		return res == NOS_RESULT_SUCCESS;
	}
}

void ChannelHandler::Close()
{
	if (FrameResultCallbackId != -1)
	{
		nosDeckLink->UnregisterFrameResultCallback(DeviceIndex, Channel, FrameResultCallbackId);
		FrameResultCallbackId = -1;
	}
	UnregisterDeviceCallbacks();
	ClearMiscMessages();
	if (IsOpen)
	{
		if (IsInput() && VideoInputChangeCallbackId != -1)
		{
			nosDeckLink->UnregisterInputVideoFormatChangeCallback(DeviceIndex, Channel, VideoInputChangeCallbackId);
			VideoInputChangeCallbackId = -1;
		}
		StopIfOpen();
		nosDeckLink->CloseChannel(DeviceIndex, Channel);
	}
	IsOpen = false;
	IsStreamStarted = false;
	ReferenceStatus = std::nullopt;
	nosEngine.SetPinValue(OutChannelPinId, nos::Buffer::From(ChannelId(-1, 0, false)));
	nosEngine.SetPinValue(OutResolutionPinId, nos::Buffer::From(nosVec2u{ 0, 0 }));
	nosEngine.SendPathRestart(OutChannelPinId);
	UpdateChannelStatus();
	UpdateReferenceStatus();
}

void ChannelHandler::UpdateChannelStatus()
{
	std::stringstream channelString;
	channelString << GetChannelName();
	channelString << " ";
	if (Resolution != NOS_MEDIAIO_FRAME_GEOMETRY_INVALID)
	{
		channelString << nosMediaIO->GetFrameGeometryName(Resolution);
		channelString << " ";
	}
	if (FrameRate != NOS_MEDIAIO_FRAME_RATE_INVALID)
		channelString << nosMediaIO->GetFrameRateName(FrameRate);
	if (VideoScanType != NOS_MEDIAIO_VIDEO_SCAN_TYPE_INVALID)
		channelString << (VideoScanType == NOS_MEDIAIO_VIDEO_PROGRESSIVE_SCAN ? "p" : "i");
	fb::NodeStatusMessageType type;
	std::string statusText;
	if (ShouldOpen && IsOpen)
	{
		type = fb::NodeStatusMessageType::INFO;
		statusText = channelString.str();
	}
	else
	{
		if (ShouldOpen && !IsOpen && CanOpen())
		{
			type = IsIPInput() ? fb::NodeStatusMessageType::WARNING : fb::NodeStatusMessageType::FAILURE;
			statusText = (IsIPInput() ? "Waiting for IP receiver configuration/stream: " : "Failed to open: ") + channelString.str();
				
		}
		else if (!ShouldOpen)
		{
			type = fb::NodeStatusMessageType::WARNING;
			statusText = "Channel closed";
		}
		else
		{
			type = fb::NodeStatusMessageType::WARNING;
			statusText = "Idle";
		}
	}
	SetStatus(StatusType::Channel, type, statusText);
	UpdateStatus();
}

void ChannelHandler::UpdateStatus()
{
	std::vector<fb::TNodeStatusMessage> messages;
	if (DeviceIndex == -1)
		messages.push_back(fb::TNodeStatusMessage{{}, "No device selected", fb::NodeStatusMessageType::WARNING});
	else
	{
		messages.push_back(fb::TNodeStatusMessage{ {}, ModelName, fb::NodeStatusMessageType::INFO });
	}
	{
		std::unique_lock lock(StatusMutex);
		for (auto& [type, message] : StatusMessages)
			messages.push_back(message);
		for (auto& message : MiscMessages)
			messages.push_back(message);
	}
	Node.SetNodeStatusMessages(messages);
}

void ChannelHandler::SetStatus(StatusType statusType, fb::NodeStatusMessageType msgType, std::string text)
{
	std::unique_lock lock(StatusMutex);
	StatusMessages[statusType] = fb::TNodeStatusMessage{{}, std::move(text), msgType};
	UpdateStatus();
}

void ChannelHandler::ClearStatus(StatusType statusType)
{
	std::unique_lock lock(StatusMutex);
	StatusMessages.erase(statusType);
	UpdateStatus();
}

nosResult RegisterChannelNode(nosNodeFunctions* funcs)
{
	NOS_BIND_NODE_CLASS(NOS_NAME_STATIC("Channel"), ChannelNode, funcs)
	return NOS_RESULT_SUCCESS;
}
}
