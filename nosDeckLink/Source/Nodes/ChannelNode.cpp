// Copyright MediaZ Teknoloji A.S. All Rights Reserved.
#include <Nodos/PluginHelpers.hpp>

#include <nosDeckLinkSubsystem/nosDeckLinkSubsystem.h>

#include "Generated/Conversion_generated.h"
#include "Generated/DeckLink_generated.h"

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
	
struct ChannelHandler
{
	NodeContext* Node;
	bool ShouldOpen = true;
	bool IsOpen = false;
	bool IsStreamStarted = false;
	nosUUID IsOpenPinId;
	nosUUID ChannelNamePinId;
	nosUUID VideoScanTypePinId;
	nosUUID OutChannelPinId;
	nosUUID OutResolutionPinId;
	nosUUID OutPixelFormatPinId;
	nosUUID OutChannelIsInterlacedPinId;
	nosUUID ResolutionPinId;
	nosUUID FrameRatePinId;
	nosUUID PixelFormatPinId;
	nosMediaIOVideoScanType VideoScanType = NOS_MEDIAIO_VIDEO_PROGRESSIVE_SCAN; // Default value
	int32_t DeviceIndex = -1;
	nosMediaIODirection Direction = NOS_MEDIAIO_DIRECTION_OUTPUT;
	nosDeckLinkChannel Channel = NOS_DECKLINK_CHANNEL_INVALID;
	nosMediaIOFrameGeometry Resolution = NOS_MEDIAIO_FRAME_GEOMETRY_INVALID;
	nosMediaIOFrameRate FrameRate = NOS_MEDIAIO_FRAME_RATE_INVALID;
	nosMediaIOPixelFormat PixelFormat = NOS_MEDIAIO_PIXEL_FORMAT_INVALID;
	int32_t VideoInputChangeCallbackId = -1;
	int32_t FrameResultCallbackId = -1;
	int32_t DeviceInvalidatedCallbackId = -1;

	std::atomic_uint32_t DropCount = 0;
	std::mutex DecklinkThreadMutex;
	struct
	{
		bool DropDetectionEnabled = false;
		uint32_t FramesSinceLastDrop = 0;
		bool DropDetected = false;
	} DeckLinkThreadStatus;

	template<auto Member, typename T>
	ChannelUpdateResult Update(const T& value, bool reopen = true)
	{
		if (this->*Member == value)
			return ChannelUpdateResult::NothingChanged;
		if (reopen && IsOpen)
			Close();
		this->*Member = value;
		UpdateChannelStatusAndOutPins();
		if (reopen && !Open())
			return ChannelUpdateResult::UnsupportedSettings;
		return ChannelUpdateResult::Opened;
	}

	ChannelHandler(NodeContext* node) : Node(node)
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

	void OnFrameEnd_DeckLinkThread(nosDeckLinkFrameResult result, uint32_t processedFrameNumber)
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
				nosEngine.LogW("Requesting path restart due to frame drops");
				nosEngine.SendPathRestart(OutChannelPinId);
			}
			break;
		}
		}
	}

	void DeviceInvalidated()
	{
		nosEngine.SetPinValue(ChannelNamePinId, nos::Buffer("NONE", 5));
	}

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

	bool Open()
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
		DeviceInvalidatedCallbackId = nosDeckLink->RegisterDeviceInvalidatedCallback(DeviceIndex, &decklink::DeviceInvalidated, this);
		auto res = nosDeckLink->OpenChannel(DeviceIndex, &params);
		if (res == NOS_RESULT_SUCCESS)
		{
			IsOpen = true;
			Node->SetPinOrphanState(OutChannelPinId, fb::PinOrphanStateType::ACTIVE, nullptr);
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
		UpdateChannelStatusAndOutPins();
		return res == NOS_RESULT_SUCCESS;
	}

	void StartIfOpen()
	{
		if (IsOpen)
			nosDeckLink->StartStream(DeviceIndex, Channel);
	}

	void StopIfOpen()
	{
		if (IsOpen)
		{
			nosDeckLink->StopStream(DeviceIndex, Channel);
			std::unique_lock lock(DecklinkThreadMutex);
			DeckLinkThreadStatus = {};
		}
	}

	void Close()
	{
		if (IsInput())
			nosDeckLink->UnregisterInputVideoFormatChangeCallback(DeviceIndex, Channel, VideoInputChangeCallbackId);
		nosDeckLink->UnregisterFrameResultCallback(DeviceIndex, Channel, FrameResultCallbackId);
		nosDeckLink->CloseChannel(DeviceIndex, Channel);
		IsOpen = false;
		nosEngine.SetPinValue(OutChannelPinId, nos::Buffer::From(ChannelId(-1, 0, false)));
		nosEngine.SetPinValue(OutResolutionPinId, nos::Buffer::From(nosVec2u{ 0, 0 }));
		nosEngine.SendPathRestart(OutChannelPinId);
		UpdateChannelStatus();
	}

	void UpdateChannelStatus()
	{
		std::stringstream channelString;
		char nameBuffer[256]{};
		auto res = nosDeckLink->GetPortMappedChannelName(DeviceIndex, Channel, nameBuffer, sizeof(nameBuffer));
		if (res != NOS_RESULT_SUCCESS)
			channelString << "Unknown Channel";
		else
			channelString << nameBuffer;
		channelString << " ";
		if (Resolution != NOS_MEDIAIO_FRAME_GEOMETRY_INVALID)
		{
			channelString << nosMediaIO->GetFrameGeometryName(Resolution);
			channelString << " ";
		}
		if (FrameRate != NOS_MEDIAIO_FRAME_RATE_INVALID)
			channelString << nosMediaIO->GetFrameRateName(FrameRate);
		if (VideoScanType != NOS_MEDIAIO_VIDEO_SCAN_TYPE_INVALID)
			channelString << (VideoScanType == NOS_MEDIAIO_VIDEO_PROGRESSIVE_SCAN ? " (p)" : " (i)");
		fb::NodeStatusMessageType type;
		std::string statusText;
		if (ShouldOpen && IsOpen)
		{
			type = fb::NodeStatusMessageType::INFO;
			statusText = channelString.str();
			Node->SetPinOrphanState(OutChannelPinId, fb::PinOrphanStateType::ACTIVE);
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
			Node->SetPinOrphanState(OutChannelPinId, fb::PinOrphanStateType::ORPHAN, statusText.c_str());
		}
		SetStatus(StatusType::Channel, type, statusText);
		UpdateStatus();
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

	void UpdateChannelStatusAndOutPins()
	{
		UpdateChannelStatus();
		UpdateOutPins();
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
	};

	void SetStatus(StatusType statusType, fb::NodeStatusMessageType msgType, std::string text);
	void ClearStatus(StatusType statusType);
	std::map<StatusType, fb::TNodeStatusMessage> StatusMessages;
};

void InputVideoFormatChanged(void* userData, nosMediaIOVideoScanType scanType, nosMediaIOFrameGeometry frameGeometry, nosMediaIOFrameRate frameRate, nosMediaIOPixelFormat pixelFormat)
{
	static_cast<ChannelHandler*>(userData)->OnInputVideoFormatChanged_DeckLinkThread(scanType, frameGeometry, frameRate, pixelFormat);
}

void FrameResultCallback(void* userData, nosDeckLinkFrameResult result, uint32_t processedFrameNumber)
{
	static_cast<ChannelHandler*>(userData)->OnFrameEnd_DeckLinkThread(result, processedFrameNumber);
}

void DeviceInvalidated(void* userData)
{
	static_cast<ChannelHandler*>(userData)->DeviceInvalidated();
}

class ChannelNode : public nos::NodeContext
{
public:
	ChannelNode(const nosFbNode* node) : NodeContext(node), Channel(this)
	{
		SetPinVisualizer(NSN_VideoScanType, {.type = nos::fb::VisualizerType::COMBO_BOX, .name = GetVideoScanTypeStringListName()});
		SetPinVisualizer(NSN_Device, {.type = nos::fb::VisualizerType::COMBO_BOX, .name = GetDeviceStringListName()});
		SetPinVisualizer(NSN_ChannelName, {.type = nos::fb::VisualizerType::COMBO_BOX, .name = GetChannelStringListName()});
		SetPinVisualizer(NSN_Resolution, {.type = nos::fb::VisualizerType::COMBO_BOX, .name = GetResolutionStringListName()});
		SetPinVisualizer(NSN_FrameRate, {.type = nos::fb::VisualizerType::COMBO_BOX, .name = GetFrameRateStringListName()});
		SetPinVisualizer(NSN_PixelFormat, {.type = nos::fb::VisualizerType::COMBO_BOX, .name = GetPixelFormatStringListName()});

		Channel.IsOpenPinId = *GetPinId(NSN_IsOpen);
		Channel.VideoScanTypePinId = *GetPinId(NSN_VideoScanType);
		Channel.ChannelNamePinId = *GetPinId(NSN_ChannelName);
		Channel.OutChannelPinId = *GetPinId(NSN_ChannelId);
		Channel.OutResolutionPinId = *GetPinId(NSN_ChannelResolution);
		Channel.OutPixelFormatPinId = *GetPinId(NSN_ChannelPixelFormat);
		Channel.OutChannelIsInterlacedPinId = *GetPinId(NSN_ChannelIsInterlaced);
		Channel.ResolutionPinId = *GetPinId(NSN_Resolution);
		Channel.FrameRatePinId = *GetPinId(NSN_FrameRate);
		Channel.PixelFormatPinId = *GetPinId(NSN_PixelFormat);

		UpdateStringList(GetVideoScanTypeStringListName(), GetPossibleVideoScanTypes());
		UpdateStringList(GetDeviceStringListName(), GetPossibleDeviceNames());

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
				if (oldVideoScanType != newVideoScanType)
					ResetAfter(ChangedPinType::VideoScanType);
			}
		});
		AddPinValueWatcher(NSN_Device, [this](const nos::Buffer& newVal, std::optional<nos::Buffer> oldValue) {
			DevicePinValue = InterpretPinValue<const char>(newVal);
			uint32_t newDeviceIndex = 0;
			if (NOS_RESULT_SUCCESS != nosDeckLink->GetDeviceByUniqueDisplayName(DevicePinValue.c_str(), &newDeviceIndex))
				newDeviceIndex = -1;
			Channel.Update<&ChannelHandler::DeviceIndex>(newDeviceIndex);
			if (DevicePinValue != "NONE" && Channel.DeviceIndex == -1)
				ResetPin(NSN_Device);
			else
			{
				if (oldValue)
					ResetAfter(ChangedPinType::Device);
				else if (DevicePinValue == "NONE")
					AutoSelectIfSingle(NSN_Device, GetPossibleDeviceNames());
			}
			UpdateAfter(ChangedPinType::Device, !oldValue);
		});
		AddPinValueWatcher(NSN_ChannelName, [this](const nos::Buffer& newVal, std::optional<nos::Buffer> oldValue) {
			ChannelPinValue = InterpretPinValue<const char>(newVal);
			auto newChannel = nosDeckLink->GetChannelFromPortMappedName(Channel.DeviceIndex, ChannelPinValue.c_str());
			Channel.Update<&ChannelHandler::Channel>(newChannel);
			if (ChannelPinValue != "NONE" && newChannel == NOS_DECKLINK_CHANNEL_INVALID)
				ResetPin(NSN_ChannelName);
			else
			{
				if (oldValue)
					ResetAfter(ChangedPinType::ChannelName);
				else if (ChannelPinValue == "NONE")
					AutoSelectIfSingle(NSN_ChannelName, GetPossibleChannelNames());
			}
			UpdateAfter(ChangedPinType::ChannelName, !oldValue);
		});
		AddPinValueWatcher(NSN_Resolution, [this](const nos::Buffer& newVal, std::optional<nos::Buffer> oldValue) {
			ResolutionPinValue = InterpretPinValue<const char>(newVal);
			auto newResolution = nosMediaIO->GetFrameGeometryFromString(ResolutionPinValue.c_str());
			Channel.Update<&ChannelHandler::Resolution>(newResolution, !Channel.IsInput());
			if (ResolutionPinValue != "NONE" && newResolution == NOS_MEDIAIO_FRAME_GEOMETRY_INVALID)
				ResetPin(NSN_Resolution);
			else
			{
				if (oldValue)
					ResetAfter(ChangedPinType::Resolution);
				else if (ResolutionPinValue == "NONE")
					AutoSelectIfSingle(NSN_Resolution, GetPossibleResolutions());
			}
			UpdateAfter(ChangedPinType::Resolution, !oldValue);
		});
		AddPinValueWatcher(NSN_FrameRate, [this](const nos::Buffer& newVal, std::optional<nos::Buffer> oldValue) {
			FrameRatePinValue = InterpretPinValue<const char>(newVal);
			auto newFrameRate = nosMediaIO->GetFrameRateFromString(FrameRatePinValue.c_str());
			Channel.Update<&ChannelHandler::FrameRate>(newFrameRate, !Channel.IsInput());
			if (FrameRatePinValue != "NONE" && newFrameRate == NOS_MEDIAIO_FRAME_RATE_INVALID)
				ResetPin(NSN_FrameRate);
			else
			{
				if (oldValue)
					ResetAfter(ChangedPinType::FrameRate);
				else if (FrameRatePinValue == "NONE")
					AutoSelectIfSingle(NSN_FrameRate, GetPossibleFrameRates());
			}
			UpdateAfter(ChangedPinType::FrameRate, !oldValue);
		});
		AddPinValueWatcher(NSN_PixelFormat, [this](const nos::Buffer& newVal, std::optional<nos::Buffer> oldValue) {
			PixelFormatPinValue = InterpretPinValue<const char>(newVal);
			auto newPixelFormat = nosMediaIO->GetPixelFormatFromString(PixelFormatPinValue.c_str());
			Channel.Update<&ChannelHandler::PixelFormat>(newPixelFormat, !Channel.IsInput());
			if (PixelFormatPinValue != "NONE" && newPixelFormat == NOS_MEDIAIO_PIXEL_FORMAT_INVALID)
				ResetPin(NSN_FrameRate);
			else
			{
				if (oldValue)
					ResetAfter(ChangedPinType::PixelFormat);
				else if (PixelFormatPinValue == "NONE")
					AutoSelectIfSingle(NSN_PixelFormat, GetPossiblePixelFormats());
			}
			UpdateAfter(ChangedPinType::PixelFormat, !oldValue);
		});
	}

	void AutoSelectIfSingle(nosName pinName, std::vector<std::string> const& list)
	{
		if (list.size() == 2)
			SetPinValue(pinName, nosBuffer{.Data = (void*)list[1].c_str(), .Size = list[1].size() + 1});
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
			auto deviceList = GetPossibleDeviceNames();
			UpdateStringList(GetDeviceStringListName(), deviceList);
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
		SetPinValue(name, nosBuffer{.Data = (void*)"NONE", .Size = 5});
	}

	nosResult ExecuteNode(nosNodeExecuteParams* params) override
	{
		params->MarkAllOutsDirty = NOS_FALSE;
		{
			std::unique_lock lock(Channel.DecklinkThreadMutex);
			if (params->IsFreeRun)
				Channel.DeckLinkThreadStatus = {};
			else
				Channel.DeckLinkThreadStatus.DropDetectionEnabled = true;
		}
		return Channel.IsOpen ? NOS_RESULT_SUCCESS : NOS_RESULT_FAILED;
	}
	
	std::string GetDeviceStringListName() { return "decklink.DeviceList." + UUID2STR(NodeId); }
	std::string GetChannelStringListName() { return "decklink.ChannelList." + UUID2STR(NodeId); }
	std::string GetResolutionStringListName() { return "decklink.ResolutionList." + UUID2STR(NodeId); }
	std::string GetFrameRateStringListName() { return "decklink.FrameRateList." + UUID2STR(NodeId); }
	std::string GetPixelFormatStringListName() { return "decklink.PixelFormatList." + UUID2STR(NodeId); }
	std::string GetVideoScanTypeStringListName() { return "decklink.VideoScanList." + UUID2STR(NodeId); }

	std::vector<std::string> GetPossibleDeviceNames()
	{
		std::vector<std::string> devices = {"NONE"};
		size_t count = 0;
		nosDeckLink->GetDevices(&count, nullptr);
		std::vector<nosDeckLinkDeviceDesc> deviceDescs(count);
		nosDeckLink->GetDevices(&count, deviceDescs.data());
		for (auto& deviceDesc : deviceDescs)
		{
			devices.push_back(deviceDesc.UniqueDisplayName);
		}
		return devices;
	}
	std::vector<std::string> GetPossibleChannelNames() 
	{
		std::vector<std::string> channels = {"NONE"};
		if (Channel.DeviceIndex == -1)
			return channels;
		nosDeckLinkChannelList channelList{};
		nosDeckLink->GetAvailableChannels(Channel.DeviceIndex, Channel.Direction, &channelList);
		for (size_t i = 0; i < channelList.Count; i++)
		{
			std::string channelName(256, '\0');
			nosDeckLink->GetPortMappedChannelName(Channel.DeviceIndex, channelList.Channels[i], channelName.data(), channelName.size());
			channels.push_back(channelName);
		}
		return channels;
	}

	std::vector<std::string> GetPossibleVideoScanTypes() 
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
		std::vector<std::string> possibleResolutions = {"NONE"};
		if (Channel.DeviceIndex == -1 || Channel.Channel == NOS_DECKLINK_CHANNEL_INVALID)
			return possibleResolutions;
		nosMediaIOFrameGeometryList frameGeometryList{};
		nosDeckLink->GetSupportedOutputFrameGeometries(Channel.DeviceIndex, Channel.Channel, Channel.VideoScanType, &frameGeometryList);
		for (size_t i = 0; i < frameGeometryList.Count; i++)
		{
			auto& fg = frameGeometryList.Geometries[i];
			auto name = nosMediaIO->GetFrameGeometryName(fg);
			possibleResolutions.push_back(name);
		}
		return possibleResolutions;
	}
	
	std::vector<std::string> GetPossibleFrameRates() 
	{
		std::vector<std::string> possibleFrameRates = {"NONE"};
		if (Channel.DeviceIndex == -1 || Channel.Channel == NOS_DECKLINK_CHANNEL_INVALID || Channel.Resolution == NOS_MEDIAIO_FRAME_GEOMETRY_INVALID)
			return possibleFrameRates;
		nosMediaIOFrameRateList frameRates{};
		nosDeckLink->GetSupportedOutputFrameRatesForGeometry(Channel.DeviceIndex, Channel.Channel, Channel.VideoScanType, Channel.Resolution, &frameRates);
		for (size_t i = 0; i < frameRates.Count; i++)
		{
			auto& rate = frameRates.FrameRates[i];
			auto name = nosMediaIO->GetFrameRateName(rate);
			possibleFrameRates.push_back(name);
		}
		return possibleFrameRates;
	}

	std::vector<std::string> GetPossiblePixelFormats()
	{
		std::vector<std::string> possiblePixelFormats = {"NONE"};
		if (Channel.DeviceIndex == -1 || Channel.FrameRate == NOS_MEDIAIO_FRAME_RATE_INVALID)
			return possiblePixelFormats;
		nosMediaIOPixelFormatList pixelFormats{};
		nosDeckLink->GetSupportedOutputPixelFormats(Channel.DeviceIndex, Channel.Channel, Channel.VideoScanType, Channel.Resolution, Channel.FrameRate, &pixelFormats);
		for (size_t i = 0; i < pixelFormats.Count; i++)
		{
			auto& format = pixelFormats.PixelFormats[i];
			auto name = nosMediaIO->GetPixelFormatName(format);
			possiblePixelFormats.push_back(name);
		}
		return possiblePixelFormats;
	}

	std::string VideoScanTypePinValue;
	std::string DevicePinValue = "NONE";
	std::string ChannelPinValue = "NONE";
	std::string ResolutionPinValue = "NONE";
	std::string FrameRatePinValue = "NONE";
	std::string PixelFormatPinValue = "NONE";

	ChannelHandler Channel;


	void OnPathStartInitiated() override
	{
		if(Channel.IsInput())
			Channel.StartIfOpen();
	}

	void OnPathStart() override
	{
		if (!Channel.IsOpen)
			return;
		if (Channel.IsInput())
			nosDeckLink->ResetInputFrames(Channel.DeviceIndex, Channel.Channel);
		// Output is started here since decklink api counts drops
		// and we need to start the stream when we are sure that frames will arrive
		else
			Channel.StartIfOpen();
	}

	void OnPathStop() override
	{
		Channel.StopIfOpen();
	}
};

void ChannelHandler::UpdateStatus()
{
	std::vector<fb::TNodeStatusMessage> messages;
	if (DeviceIndex == -1)
		messages.push_back(fb::TNodeStatusMessage{{}, "No device selected", fb::NodeStatusMessageType::WARNING});
	else
	{
		nosDeckLinkDeviceInfo deviceInfo{};
		nosDeckLink->GetDeviceInfoByIndex(DeviceIndex, &deviceInfo);
		messages.push_back(fb::TNodeStatusMessage{{}, deviceInfo.ModelName, fb::NodeStatusMessageType::INFO});
	}
	for (auto& [type, message] : StatusMessages)
		messages.push_back(message);
	Node->SetNodeStatusMessages(messages);
}

void ChannelHandler::SetStatus(StatusType statusType, fb::NodeStatusMessageType msgType, std::string text)
{
	StatusMessages[statusType] = fb::TNodeStatusMessage{{}, std::move(text), msgType};
	UpdateStatus();
}

void ChannelHandler::ClearStatus(StatusType statusType)
{
	StatusMessages.erase(statusType);
	UpdateStatus();
}

nosResult RegisterChannelNode(nosNodeFunctions* funcs)
{
	NOS_BIND_NODE_CLASS(NOS_NAME_STATIC("Channel"), ChannelNode, funcs)
	return NOS_RESULT_SUCCESS;
}
}
