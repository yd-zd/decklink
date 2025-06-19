// Copyright MediaZ Teknoloji A.S. All Rights Reserved.
#include <Nodos/PluginHelpers.hpp>

#include <nosDeckLinkSubsystem/nosDeckLinkSubsystem.h>
#include <nosDeviceSubsystem/nosDeviceSubsystem.h>

#include "Generated/Conversion_generated.h"
#include "Generated/DeckLink_generated.h"
#include "Generated/Device_generated.h"

#include "Helpers/Trie.hpp"

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

enum class ChangedPinType
{
	IsInput,
	Device,
	ChannelName,
	Resolution,
	FrameRate,
	PixelFormat,
	VideoScanType,
};

enum class ChannelUpdateResult
{
	NothingChanged,
	UnsupportedSettings,
	Opened,
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
	int32_t DeviceInvalidatedCallbackId = -1;
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
		StartIfOpen();
		return ChannelUpdateResult::Opened;
	}

	ChannelHandler(ChannelNode& node) : Node(node)
	{
		UpdateChannelStatus();
	}

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
		nosEngine.SetPinValue(VideoScanTypePinId, nos::Buffer(scanTypeCstr, strlen(scanTypeCstr) + 1));
		nosEngine.SetPinValue(ResolutionPinId, nos::Buffer(frameGeometryCstr, strlen(frameGeometryCstr) + 1));
		nosEngine.SetPinValue(FrameRatePinId, nos::Buffer(frameRateCstr, strlen(frameRateCstr) + 1));
		nosEngine.SetPinValue(PixelFormatPinId, nos::Buffer(pixelFormatCstr, strlen(pixelFormatCstr) + 1));
		UpdateChannelStatus();
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
			if (!DeckLinkThreadStatus.PathRestartRequested && DeckLinkThreadStatus.FramesSinceLastDrop > 50)
			{
				nosEngine.LogW("Requesting path restart due to frame drops");
				nosEngine.SendPathRestart(OutChannelPinId);
				DeckLinkThreadStatus.PathRestartRequested = true;
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
			if (DeviceInvalidatedCallbackId != -1)
			{
				nosDeckLink->UnregisterDeviceInvalidatedCallback(DeviceIndex, DeviceInvalidatedCallbackId);
				DeviceInvalidatedCallbackId = -1;
			}
		}
	}

	void RegisterDeviceCallbacks()
	{
		if (DeviceIndex != -1)
		{
			DeviceStatusCallbackId = nosDeckLink->RegisterDeviceStatusCallback(DeviceIndex, &DeviceStatusCallback, this);
			DeviceInvalidatedCallbackId = nosDeckLink->RegisterDeviceInvalidatedCallback(DeviceIndex, &decklink::DeviceInvalidated, this);
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

	void StartIfOpen()
	{
		if (IsOpen)
		{
			nosDeckLink->StartStream(DeviceIndex, Channel);
			ResetDropState();
		}
	}

	void StopIfOpen()
	{
		if (IsOpen)
		{
			nosDeckLink->StopStream(DeviceIndex, Channel);
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
		auto res = nosDeckLink->GetPortMappedChannelName(DeviceIndex, Channel, nameBuffer, sizeof(nameBuffer));
		if (res != NOS_RESULT_SUCCESS)
			channelString << "Unknown Channel";
		else
			channelString << nameBuffer;
		return channelString.str();
	}

	void UpdateChannelStatus();

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
		Firmware,
		DropCount,
		Profile
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
	ChannelNode(nosFbNodePtr node) : NodeContext(node), Channel(*this)
	{
		SetPinVisualizer(NSN_VideoScanType, {.type = nos::fb::VisualizerType::COMBO_BOX, .name = GetVideoScanTypeStringListName()});
		SetPinVisualizer(NSN_Device, {.type = nos::fb::VisualizerType::NAMED_VALUE, .name = sys::device::GetDeviceListNameForVendor(NOS_NAME(NOS_DECKLINK_VENDOR_NAME)), .hide_value = true});
		SetPinVisualizer(NSN_ChannelName, {.type = nos::fb::VisualizerType::COMBO_BOX, .name = GetChannelStringListName()});
		SetPinVisualizer(NSN_Resolution, {.type = nos::fb::VisualizerType::COMBO_BOX, .name = GetResolutionStringListName()});
		SetPinVisualizer(NSN_FrameRate, {.type = nos::fb::VisualizerType::COMBO_BOX, .name = GetFrameRateStringListName()});
		SetPinVisualizer(NSN_PixelFormat, {.type = nos::fb::VisualizerType::COMBO_BOX, .name = GetPixelFormatStringListName()});

		Channel.IsOpenPinId = *GetPinId(NSN_IsOpen);
		Channel.VideoScanTypePinId = *GetPinId(NSN_VideoScanType);
		Channel.DevicePinId = *GetPinId(NSN_Device);
		Channel.ChannelNamePinId = *GetPinId(NSN_ChannelName);
		Channel.OutChannelPinId = *GetPinId(NSN_ChannelId);
		Channel.OutResolutionPinId = *GetPinId(NSN_ChannelResolution);
		Channel.OutPixelFormatPinId = *GetPinId(NSN_ChannelPixelFormat);
		Channel.OutChannelIsInterlacedPinId = *GetPinId(NSN_ChannelIsInterlaced);
		Channel.ResolutionPinId = *GetPinId(NSN_Resolution);
		Channel.FrameRatePinId = *GetPinId(NSN_FrameRate);
		Channel.PixelFormatPinId = *GetPinId(NSN_PixelFormat);

		GetAllPossibleConfigurations();

		UpdateStringList(GetVideoScanTypeStringListName(), GetPossibleVideoScanTypes());

		AddPinValueWatcher(NSN_IsOpen, [this](const nos::Buffer& newVal, std::optional<nos::Buffer> oldValue) {
			Channel.ShouldOpen = *InterpretPinValue<bool>(newVal);
			if (!Channel.ShouldOpen)
				Channel.Close();
			else
				Channel.Open();
		});
		AddPinValueWatcher(NSN_IsInput, [this](const nos::Buffer& newVal, std::optional<nos::Buffer> oldValue) {
			auto newValue = *InterpretPinValue<bool>(newVal) ? NOS_MEDIAIO_DIRECTION_INPUT : NOS_MEDIAIO_DIRECTION_OUTPUT;
			Channel.Update<&ChannelHandler::Direction>(newValue);
			UpdateAfter(ChangedPinType::IsInput, !oldValue);
		});
		AddPinValueWatcher(NSN_VideoScanType, [this](const nos::Buffer& newVal, std::optional<nos::Buffer> oldValue) {
			VideoScanTypePinValue = InterpretPinValue<const char>(newVal);
			auto newVideoScanType = nosMediaIO->GetVideoScanTypeFromString(VideoScanTypePinValue.c_str());
			if (newVideoScanType == NOS_MEDIAIO_VIDEO_SCAN_TYPE_INVALID)
			{
				// Initial value
				std::string_view scanTypeCstr = nosMediaIO->GetVideoScanTypeName(Channel.VideoScanType);
				SetPinValue(NSN_VideoScanType, nos::Buffer(scanTypeCstr.data(), scanTypeCstr.size() + 1));
				return;
			}
			Channel.Update<&ChannelHandler::VideoScanType>(newVideoScanType, Channel.Direction != NOS_MEDIAIO_DIRECTION_INPUT);
			if (oldValue)
			{
				auto oldVideoScanType = nosMediaIO->GetVideoScanTypeFromString(InterpretPinValue<const char>(*oldValue));
				if (oldVideoScanType != newVideoScanType && Channel.Direction != NOS_MEDIAIO_DIRECTION_INPUT)
					ResetAfter(ChangedPinType::VideoScanType);
			}
		});
		AddPinValueWatcher(NSN_Device, [this](const nos::Buffer& newVal, std::optional<nos::Buffer> oldValue) {
			DevicePinValue = newVal.As<sys::device::TDeviceInfo>();
			Channel.ModelName = DevicePinValue.model_name;

			int32_t newDeviceIndex = -1;
			if (auto deviceIndex = GetDeviceHandle(DevicePinValue))
				newDeviceIndex = *deviceIndex;
			else
				nosEngine.LogE("Failed to get suitable device");

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
					ResetAfter(ChangedPinType::Device);
				else if (DevicePinValue.vendor_name == PIN_VALUE_NONE)
					AutoSelectIfSingle(NSN_Device, GetPossibleDevices());
			}
			UpdateAfter(ChangedPinType::Device, !oldValue);
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
					ResetAfter(ChangedPinType::ChannelName);
				else if (ChannelPinValue == PIN_VALUE_NONE)
					AutoSelectIfSingle(NSN_ChannelName, GetPossibleChannelNames());
			}
			UpdateAfter(ChangedPinType::ChannelName, !oldValue);
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
					ResetAfter(ChangedPinType::Resolution);
				else if (ResolutionPinValue == PIN_VALUE_NONE)
					AutoSelectIfSingle(NSN_Resolution, GetPossibleResolutions());
			}
			UpdateAfter(ChangedPinType::Resolution, !oldValue);
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
					ResetAfter(ChangedPinType::FrameRate);
				else if (FrameRatePinValue == PIN_VALUE_NONE)
					AutoSelectIfSingle(NSN_FrameRate, GetPossibleFrameRates());
			}
			UpdateAfter(ChangedPinType::FrameRate, !oldValue);
		});
		AddPinValueWatcher(NSN_PixelFormat, [this](const nos::Buffer& newVal, std::optional<nos::Buffer> oldValue) {
			PixelFormatPinValue = InterpretPinValue<const char>(newVal);
			auto newPixelFormat = nosMediaIO->GetPixelFormatFromString(PixelFormatPinValue.c_str());
			Channel.Update<&ChannelHandler::PixelFormat>(newPixelFormat, !Channel.IsInput());
			if (PixelFormatPinValue != PIN_VALUE_NONE && newPixelFormat == NOS_MEDIAIO_PIXEL_FORMAT_INVALID)
				ResetPin(NSN_FrameRate);
			else
			{
				if (oldValue)
					ResetAfter(ChangedPinType::PixelFormat);
				else if (PixelFormatPinValue == PIN_VALUE_NONE)
					AutoSelectIfSingle(NSN_PixelFormat, GetPossiblePixelFormats());
			}
			UpdateAfter(ChangedPinType::PixelFormat, !oldValue);
		});
	}

	static std::optional<uint32_t> GetDeviceHandle(sys::device::TDeviceInfo const& deviceInfo)
	{
		nosDeviceInfo info = sys::device::ConvertDeviceInfo(deviceInfo);
		nosDeviceId deviceId{};
		int32_t newDeviceIndex = -1;
		auto res = nosDevice->GetSuitableDevice(&info, &deviceId);
		if (res != NOS_RESULT_SUCCESS)
		{
			return std::nullopt;
		}
		else
		{
			uint64_t handle{};
			res = nosDevice->GetDeviceHandle(deviceId, &handle);
			assert(res == NOS_RESULT_SUCCESS);
			return uint32_t(handle);
		}
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

	void UpdateAfter(ChangedPinType pin, bool first)
	{
		bool isInput = Channel.IsInput();
		switch (pin)
		{
		case ChangedPinType::IsInput: {
			ChangePinReadOnly(NSN_VideoScanType, isInput);
			ChangePinReadOnly(NSN_Resolution, isInput);
			ChangePinReadOnly(NSN_FrameRate, isInput);
			ChangePinReadOnly(NSN_PixelFormat, isInput);
			auto deviceList = GetPossibleDevices();
			if (!first)
				AutoSelectIfSingle(NSN_Device, deviceList);
			break;
		}
		case ChangedPinType::Device: {
			auto channelList = GetPossibleChannelNames();
			UpdateStringList(GetChannelStringListName(), channelList);
			if (!first)
				AutoSelectIfSingle(NSN_ChannelName, channelList);
			break;
		}
		case ChangedPinType::ChannelName: {
			auto resolutionList = GetPossibleResolutions();
			UpdateStringList(GetResolutionStringListName(), resolutionList);
			if(!isInput && !first)
				AutoSelectIfSingle(NSN_Resolution, resolutionList);
			break;
		}
		case ChangedPinType::Resolution: {
			auto frameRateList = GetPossibleFrameRates();
			UpdateStringList(GetFrameRateStringListName(), frameRateList);
			if (!first)
				AutoSelectIfSingle(NSN_FrameRate, frameRateList);
			break;
		}
		case ChangedPinType::FrameRate: {
			auto pixelFormatList = GetPossiblePixelFormats();
			UpdateStringList(GetPixelFormatStringListName(), pixelFormatList);
			if (!first)
				AutoSelectIfSingle(NSN_PixelFormat, pixelFormatList);
			break;
		}
		}
	}

	void ResetAfter(ChangedPinType pin)
	{
		nos::Name pinToSet;
		switch (pin)
		{
		case ChangedPinType::IsInput: 
		case ChangedPinType::VideoScanType: 
			pinToSet = NSN_Device;
			break;
		case ChangedPinType::Device: 
			pinToSet = NSN_ChannelName; 
			break;
		case ChangedPinType::ChannelName: 
			pinToSet = NSN_Resolution; 
			break;
		case ChangedPinType::Resolution: 
			pinToSet = NSN_FrameRate; 
			break;
		case ChangedPinType::FrameRate: 
			pinToSet = NSN_PixelFormat; 
			break;
		}
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
		return Channel.IsOpen ? NOS_RESULT_SUCCESS : NOS_RESULT_FAILED;
	}
	
	std::string GetChannelStringListName() { return "decklink.ChannelList." + std::string(NodeId); }
	std::string GetResolutionStringListName() { return "decklink.ResolutionList." + std::string(NodeId); }
	std::string GetFrameRateStringListName() { return "decklink.FrameRateList." + std::string(NodeId); }
	std::string GetPixelFormatStringListName() { return "decklink.PixelFormatList." + std::string(NodeId); }
	std::string GetVideoScanTypeStringListName() { return "decklink.VideoScanList." + std::string(NodeId); }
	
	Trie<std::string, bool> PossibleOutputConfigurations;

	void GetAllPossibleConfigurations()
	{
		// [(Device, Channel, Video Scan Type, Resolution, Frame Rate, Pixel Format)]
		auto devices = GetPossibleDevices();
		for (auto& device : devices)
		{
			auto deviceIndex = GetDeviceHandle(device);
			if (!deviceIndex)
				continue;
			auto channels = GetPossibleChannels(*deviceIndex, NOS_MEDIAIO_DIRECTION_OUTPUT);
			for (auto& channel : channels)
			{
				for (int i = NOS_MEDIAIO_VIDEO_PROGRESSIVE_SCAN; i <= NOS_MEDIAIO_VIDEO_SCAN_TYPE_MAX; i++)
				{
					auto videoScanType = (nosMediaIOVideoScanType)i;
					auto resolutions = GetPossibleResolutions(*deviceIndex, channel, videoScanType);
					for (auto& resolution : resolutions)
					{
						auto frameRates = GetPossibleFrameRates(*deviceIndex, channel, videoScanType, resolution);
						for (auto& frameRate : frameRates)
						{
							auto pixelFormats = GetPossiblePixelFormats(*deviceIndex, channel, videoScanType, resolution, frameRate);
							for (auto& pixelFormat : pixelFormats)
							{
								auto deviceKey = std::to_string(*deviceIndex);
								std::string channelName = nosDeckLink->GetChannelName(channel);
								std::string videoScanTypeName = nosMediaIO->GetVideoScanTypeName(videoScanType);
								std::string resolutionName = nosMediaIO->GetFrameGeometryName(resolution);
								std::string frameRateName = nosMediaIO->GetFrameRateName(frameRate);
								std::string pixelFormatName = nosMediaIO->GetPixelFormatName(pixelFormat);
								std::vector<std::string> key = {
									deviceKey, channelName, videoScanTypeName, resolutionName, frameRateName, pixelFormatName
								};
								PossibleOutputConfigurations.Insert(key, true);
							}
						}
					}
				}
			}
		}
	}

	std::vector<sys::device::TDeviceInfo> GetPossibleDevices()
	{
		return sys::device::GetDevicesWithVendor(NOS_NAME(NOS_DECKLINK_VENDOR_NAME));
	}

	std::vector<std::string> GetPossibleChannelNames() 
	{
		std::vector<std::string> channels = {PIN_VALUE_NONE};
		if (Channel.DeviceIndex == -1)
			return channels;
		return GetPossibleChannelNames(Channel.DeviceIndex, Channel.Direction);
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

	static std::vector<std::string> GetPossibleVideoScanTypes() 
	{
		std::vector<std::string> videoScanTypeNames;
		for (int i = NOS_MEDIAIO_VIDEO_PROGRESSIVE_SCAN; i <= NOS_MEDIAIO_VIDEO_SCAN_TYPE_MAX; i++)
		{
			auto name = nosMediaIO->GetVideoScanTypeName((nosMediaIOVideoScanType)i);
			videoScanTypeNames.push_back(name);
		}
		return videoScanTypeNames;
	}
	
	std::vector<std::string> GetPossibleResolutions() 
	{
		std::vector<std::string> possibleResolutions = {PIN_VALUE_NONE};
		if (Channel.DeviceIndex == -1 || Channel.Channel == NOS_DECKLINK_CHANNEL_INVALID)
			return possibleResolutions;
		return GetPossibleResolutionNames(Channel.DeviceIndex, Channel.Channel, Channel.VideoScanType);
	}

	static std::vector<std::string> GetPossibleResolutionNames(uint32_t deviceIndex, nosDeckLinkChannel channel, nosMediaIOVideoScanType videoScanType)
	{
		std::vector<std::string> resolutionNames = {PIN_VALUE_NONE};
		auto resolutions = GetPossibleResolutions(deviceIndex, channel, videoScanType);
		for (auto& resolution : resolutions)
		{
			auto name = nosMediaIO->GetFrameGeometryName(resolution);
			resolutionNames.push_back(name);
		}
		return resolutionNames;
	}

	static std::vector<nosMediaIOFrameGeometry> GetPossibleResolutions(uint32_t deviceIndex, nosDeckLinkChannel channel, nosMediaIOVideoScanType videoScanType)
	{
		std::vector<nosMediaIOFrameGeometry> resolutions;
		nosMediaIOFrameGeometryList frameGeometryList{};
		nosDeckLink->GetSupportedOutputFrameGeometries(deviceIndex, channel, videoScanType, &frameGeometryList);
		for (size_t i = 0; i < frameGeometryList.Count; i++)
		{
			auto& fg = frameGeometryList.Geometries[i];
			resolutions.push_back(fg);
		}
		return resolutions;
	}
	
	std::vector<std::string> GetPossibleFrameRates() 
	{
		std::vector<std::string> possibleFrameRates = {PIN_VALUE_NONE};
		if (Channel.DeviceIndex == -1 || Channel.Channel == NOS_DECKLINK_CHANNEL_INVALID || Channel.Resolution == NOS_MEDIAIO_FRAME_GEOMETRY_INVALID)
			return possibleFrameRates;
		return GetPossibleFrameRateNames(Channel.DeviceIndex, Channel.Channel, Channel.VideoScanType, Channel.Resolution);
	}

	static std::vector<std::string> GetPossibleFrameRateNames(uint32_t deviceIndex, nosDeckLinkChannel channel, nosMediaIOVideoScanType videoScanType, nosMediaIOFrameGeometry resolution)
	{
		std::vector<std::string> frameRateNames = {PIN_VALUE_NONE};
		auto frameRates = GetPossibleFrameRates(deviceIndex, channel, videoScanType, resolution);
		for (auto& frameRate : frameRates)
		{
			auto name = nosMediaIO->GetFrameRateName(frameRate);
			frameRateNames.push_back(name);
		}
		return frameRateNames;
	}

	static std::vector<nosMediaIOFrameRate> GetPossibleFrameRates(uint32_t deviceIndex, nosDeckLinkChannel channel, nosMediaIOVideoScanType videoScanType, nosMediaIOFrameGeometry resolution)
	{
		std::vector<nosMediaIOFrameRate> frameRates;
		nosMediaIOFrameRateList frameRateList{};
		nosDeckLink->GetSupportedOutputFrameRatesForGeometry(deviceIndex, channel, videoScanType, resolution, &frameRateList);
		for (size_t i = 0; i < frameRateList.Count; i++)
			frameRates.push_back(frameRateList.FrameRates[i]);
		return frameRates;
	}

	std::vector<std::string> GetPossiblePixelFormats()
	{
		std::vector<std::string> possiblePixelFormats = {PIN_VALUE_NONE};
		if (Channel.DeviceIndex == -1 || Channel.FrameRate == NOS_MEDIAIO_FRAME_RATE_INVALID)
			return possiblePixelFormats;
		return GetPossiblePixelFormatNames(Channel.DeviceIndex, Channel.Channel, Channel.VideoScanType, Channel.Resolution, Channel.FrameRate);
	}

	static std::vector<std::string> GetPossiblePixelFormatNames(uint32_t deviceIndex, nosDeckLinkChannel channel, nosMediaIOVideoScanType videoScanType,
		nosMediaIOFrameGeometry resolution, nosMediaIOFrameRate frameRate)
	{
		std::vector<std::string> pixelFormatNames = {PIN_VALUE_NONE};
		auto pixelFormats = GetPossiblePixelFormats(deviceIndex, channel, videoScanType, resolution, frameRate);
		for (auto& pixelFormat : pixelFormats)
		{
			auto name = nosMediaIO->GetPixelFormatName(pixelFormat);
			pixelFormatNames.push_back(name);
		}
		return pixelFormatNames;
	}

	static std::vector<nosMediaIOPixelFormat> GetPossiblePixelFormats(uint32_t deviceIndex, nosDeckLinkChannel channel, nosMediaIOVideoScanType videoScanType,
		nosMediaIOFrameGeometry resolution, nosMediaIOFrameRate frameRate)
	{
		std::vector<nosMediaIOPixelFormat> pixelFormats;
		nosMediaIOPixelFormatList pixelFormatList{};
		nosDeckLink->GetSupportedOutputPixelFormats(deviceIndex, channel, videoScanType, resolution, frameRate, &pixelFormatList);
		for (size_t i = 0; i < pixelFormatList.Count; i++)
			pixelFormats.push_back(pixelFormatList.PixelFormats[i]);
		return pixelFormats;
	}

	std::string VideoScanTypePinValue;
	sys::device::TDeviceInfo DevicePinValue;
	std::string ChannelPinValue = PIN_VALUE_NONE;
	std::string ResolutionPinValue = PIN_VALUE_NONE;
	std::string FrameRatePinValue = PIN_VALUE_NONE;
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
			.PixelFormat = IsInput() ? NOS_MEDIAIO_PIXEL_FORMAT_YCBCR_8BIT : PixelFormat,
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
			Node.SetPinOrphanState(OutChannelPinId, fb::PinOrphanStateType::ACTIVE, nullptr);
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
		UpdateStatusAndOutPins();
		return res == NOS_RESULT_SUCCESS;
	}
}

void ChannelHandler::Close()
{
	nosDeckLink->UnregisterFrameResultCallback(DeviceIndex, Channel, FrameResultCallbackId);
	UnregisterDeviceCallbacks();
	ClearMiscMessages();
	if (IsInput())
		nosDeckLink->UnregisterInputVideoFormatChangeCallback(DeviceIndex, Channel, VideoInputChangeCallbackId);
	nosDeckLink->CloseChannel(DeviceIndex, Channel);
	IsOpen = false;
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
		Node.SetPinOrphanState(OutChannelPinId, fb::PinOrphanStateType::ACTIVE);
	}
	else
	{
		if (ShouldOpen && !IsOpen && CanOpen())
		{
			type = fb::NodeStatusMessageType::FAILURE;
			statusText = "Failed to open: " + channelString.str();
				
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
		Node.SetPinOrphanState(OutChannelPinId, fb::PinOrphanStateType::ORPHAN, statusText.c_str());
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
