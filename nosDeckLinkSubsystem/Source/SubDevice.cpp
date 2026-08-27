// Copyright MediaZ Teknoloji A.S. All Rights Reserved.
#include "SubDevice.hpp"

#include <Nodos/Modules.h>
#include <EnumConversions.hpp>

#include <string_view>
#include <utility>

#include "DeviceManager.hpp"

namespace nos::decklink
{

namespace
{
constexpr size_t MaxIPFlows = 6;

const char* IPFlowTypeName(int64_t type)
{
	switch (type)
	{
	case bmdDeckLinkIPFlowTypeVideo: return "video";
	case bmdDeckLinkIPFlowTypeAudio: return "audio";
	case bmdDeckLinkIPFlowTypeAncillary: return "ancillary";
	default: return "unknown";
	}
}

bool IsSenderSDP(int64_t type, std::string_view sdp)
{
	switch (type)
	{
	case bmdDeckLinkIPFlowTypeVideo:
		return sdp.find("m=video") != std::string_view::npos;
	case bmdDeckLinkIPFlowTypeAudio:
		return sdp.find("m=audio") != std::string_view::npos;
	case bmdDeckLinkIPFlowTypeAncillary:
		return sdp.find("m=application") != std::string_view::npos || sdp.find("m=ancillary") != std::string_view::npos;
	default:
		return false;
	}
}

HRESULT SetPeerSDP(IDeckLinkIPFlowSetting* setting, std::string const& sdp)
{
#if _WIN32
	auto value = StdToDlString(sdp);
	if (!value)
		return E_FAIL;
	auto result = setting->SetString(bmdDeckLinkIPFlowPeerSDP, value);
	DeleteString(value);
	return result;
#else
	return setting->SetString(bmdDeckLinkIPFlowPeerSDP, sdp.c_str());
#endif
}

std::optional<std::string> GetFlowSDP(IDeckLinkIPFlowStatus* status)
{
	dlstring_t value = nullptr;
	auto result = status->GetString(bmdDeckLinkIPFlowSDP, &value);
	if (result != S_OK || !value)
	{
		if (value)
			DeleteString(value);
		return std::nullopt;
	}
	auto sdp = DlToStdString(value);
	DeleteString(value);
	return sdp;
}

std::optional<BMDVideoConnection> GetSupportedIPVideoConnection(IDeckLinkProfileAttributes* attributes,
	nosMediaIODirection direction, std::string const& modelName)
{
	if (!attributes)
		return std::nullopt;
	auto supportedConnectionsId = direction == NOS_MEDIAIO_DIRECTION_INPUT
		? BMDDeckLinkVideoInputConnections
		: BMDDeckLinkVideoOutputConnections;

	int64_t supportedConnections = 0;
	auto result = attributes->GetInt(supportedConnectionsId, &supportedConnections);
	if (result != S_OK)
	{
		nosEngine.LogE("DeckLinkDevice: Failed to query supported IP video connections for device %s: HRESULT 0x%08X",
			modelName.c_str(), static_cast<unsigned int>(result));
		return std::nullopt;
	}

	if (supportedConnections & bmdVideoConnectionEthernet)
		return bmdVideoConnectionEthernet;
	if (supportedConnections & bmdVideoConnectionOpticalEthernet)
		return bmdVideoConnectionOpticalEthernet;

	nosEngine.LogE("DeckLinkDevice: Device %s advertises no supported IP video connection (mask 0x%llX)",
		modelName.c_str(), static_cast<unsigned long long>(supportedConnections));
	return std::nullopt;
}

bool ApplyIPVideoConnection(IDeckLinkConfiguration* configuration, IDeckLinkProfileAttributes* attributes,
	nosMediaIODirection direction, std::string const& modelName)
{
	if (!configuration)
		return false;
	auto targetConnection = GetSupportedIPVideoConnection(attributes, direction, modelName);
	if (!targetConnection)
		return false;
	auto configurationId = direction == NOS_MEDIAIO_DIRECTION_INPUT
		? bmdDeckLinkConfigVideoInputConnection
		: bmdDeckLinkConfigVideoOutputConnection;

	int64_t currentConnection = bmdVideoConnectionUnspecified;
	if (configuration->GetInt(configurationId, &currentConnection) == S_OK &&
		currentConnection == *targetConnection)
	{
		return true;
	}

	auto result = configuration->SetInt(configurationId, *targetConnection);
	if (result != S_OK)
	{
		nosEngine.LogE("DeckLinkDevice: Failed to select IP video connection 0x%llX for device %s: HRESULT 0x%08X",
			static_cast<unsigned long long>(*targetConnection), modelName.c_str(), static_cast<unsigned int>(result));
		return false;
	}
	return true;
}

}

std::optional<BMDVideoConnection> SubDevice::GetIPVideoConnection(nosMediaIODirection direction) const
{
	return GetSupportedIPVideoConnection(ProfileAttributes, direction, ModelName);
}

SubDevice::SubDevice(IDeckLink* deviceInterface)
	: DLDevice(deviceInterface)
{
	dlstring_t modelName;
	if (!DLDevice || DLDevice->GetModelName(&modelName) != S_OK || !modelName)
	{
		// DeckLink IP 100G firmware exposes a NULL device entry after the
		// last real sub-device. Treat any device we cannot read as invalid
		// instead of crashing on a NULL interface pointer.
		if (modelName)
			DeleteString(modelName);
		Valid = false;
		nosEngine.LogW("DeckLinkDevice: Invalid device entry ignored");
		return;
	}
	ModelName = DlToStdString(modelName);
	nosEngine.LogI("DeckLink Device created: %s", ModelName.c_str());
	DeleteString(modelName);

	auto res = DLDevice->QueryInterface(IID_IDeckLinkInput, (void**)&Input.Interface);
	if (res != S_OK)
		nosEngine.LogE("DeckLinkDevice: Failed to get input interface for device: %s", ModelName.c_str());
	res = DLDevice->QueryInterface(IID_IDeckLinkOutput, (void**)&Output.Interface);
	if (res != S_OK)
		nosEngine.LogE("DeckLinkDevice: Failed to get output interface for device: %s", ModelName.c_str());

	res = DLDevice->QueryInterface(IID_IDeckLinkIPExtensions, (void**)&IPExtensions);
	if (res == S_OK && IPExtensions)
	{
		EnumerateIPFlows();
	}
	else if (res != E_NOINTERFACE)
	{
		nosEngine.LogW("DeckLinkDevice: IP extensions could not be queried for device: %s", ModelName.c_str());
	}

	res = DLDevice->QueryInterface(IID_IDeckLinkProfileAttributes, (void**)&ProfileAttributes);
	if (res != S_OK || !ProfileAttributes)
	{
		nosEngine.LogE("DeckLinkDevice: Failed to get profile attributes for device: %s", ModelName.c_str());
		return;
	}

	res = DLDevice->QueryInterface(IID_IDeckLinkConfiguration, (void**)&Configuration);
	if (res != S_OK || !Configuration)
	{
		nosEngine.LogE("DeckLinkDevice: Failed to get configuration for device: %s", ModelName.c_str());
		return;
	}

	dlstring_t serialNumber{};
	res = Configuration->GetString(bmdDeckLinkConfigDeviceInformationSerialNumber, &serialNumber);
	if (res != S_OK)
		nosEngine.LogE("DeckLinkDevice: Failed to get serial number for device: %s", ModelName.c_str());
	if (serialNumber)
	{
		SerialNumber = DlToStdString(serialNumber);
		DeleteString(serialNumber);
	}

	BOOL supported{};
	res = ProfileAttributes->GetFlag(BMDDeckLinkSupportsInputFormatDetection, &supported);
	if ((res != S_OK) || (supported == false))
	{
		nosEngine.LogW("DeckLinkDevice: Device does not support automatic mode detection for device: %s", ModelName.c_str());
		// SDI devices retain the existing requirement. IP receivers use the
		// same format-detection flag in the normal capture path, but some IP
		// firmware exposes the flow interfaces without advertising this flag.
		if (!IsIPCapable())
			return;
	}

	res = ProfileAttributes->GetInt(BMDDeckLinkSubDeviceIndex, &SubDeviceIndex);
	if (res != S_OK)
		nosEngine.LogE("DeckLinkDevice: Failed to get subdevice index for device: %s", ModelName.c_str());

	dlstring_t handle{};
	res = ProfileAttributes->GetString(BMDDeckLinkDeviceHandle, &handle);
	if (res != S_OK)
		nosEngine.LogE("DeckLinkDevice: Failed to get device handle for device: %s", ModelName.c_str());
	if (handle)
	{
		Handle = DlToStdString(handle);
		DeleteString(handle);
	}
	res = ProfileAttributes->GetInt(BMDDeckLinkProfileID, &ProfileId);
	if (res != S_OK)
		nosEngine.LogE("DeckLinkDevice: Failed to get profile ID for device: %s", ModelName.c_str());

	res = ProfileAttributes->GetInt(BMDDeckLinkDeviceGroupID, &DeviceGroupId);
	if (res != S_OK)
		nosEngine.LogE("DeckLinkDevice: Failed to get device group ID for device: %s", ModelName.c_str());

	res = ProfileAttributes->GetInt(BMDDeckLinkPersistentID, &PersistentId);
	if (res != S_OK)
		nosEngine.LogE("DeckLinkDevice: Failed to get persistent ID for device: %s", ModelName.c_str());

	res = ProfileAttributes->GetInt(BMDDeckLinkTopologicalID, &TopologicalId);
	if (res != S_OK)
		nosEngine.LogE("DeckLinkDevice: Failed to get topological ID for device: %s", ModelName.c_str());

	ApplyIPConfiguration();

	res = DLDevice->QueryInterface(IID_IDeckLinkProfileManager, (void**)&ProfileManager);
	if (res != S_OK || !ProfileManager)
	{
		nosEngine.LogD("DeckLinkDevice: ProfileManager interface is not available for device: %s", ModelName.c_str());
	}
}

SubDevice::~SubDevice()
{
	DisableIPFlows();
	IPFlows.clear();
	Release(IPExtensions);
	Release(Configuration);
	Release(ProfileAttributes);
	Release(ProfileManager);
}

void SubDevice::EnumerateIPFlows()
{
	if (!IPExtensions)
		return;
	IDeckLinkIPFlowIterator* iterator = nullptr;
	auto result = IPExtensions->GetDeckLinkIPFlowIterator(&iterator);
	if (result != S_OK || !iterator)
	{
		nosEngine.LogE("DeckLinkDevice: Failed to get IP flow iterator for device: %s", ModelName.c_str());
		return;
	}

	// Some IP 100G firmware returns a NULL flow entry after the six real
	// flows, and calling Next again after that entry never returns.
	for (size_t i = 0; i < MaxIPFlows; ++i)
	{
		IDeckLinkIPFlow* flow = nullptr;
		auto nextResult = iterator->Next(&flow);
		if (nextResult != S_OK || !flow)
		{
			Release(flow);
			break;
		}

		IDeckLinkIPFlowAttributes* attributes = nullptr;
		if (flow->QueryInterface(IID_IDeckLinkIPFlowAttributes, (void**)&attributes) != S_OK || !attributes)
		{
			Release(attributes);
			Release(flow);
			break;
		}

		IPFlowInfo info;
		info.Flow = flow;
		info.Attributes = attributes;
		if (attributes->GetInt(bmdDeckLinkIPFlowDirection, &info.Direction) != S_OK ||
			attributes->GetInt(bmdDeckLinkIPFlowType, &info.Type) != S_OK)
		{
			nosEngine.LogW("DeckLinkDevice: Invalid IP flow attributes for device: %s", ModelName.c_str());
			Release(info.Attributes);
			Release(info.Flow);
			break;
		}
		IPFlows.push_back(std::move(info));
	}
	Release(iterator);

	if (IPFlows.empty())
		nosEngine.LogW("DeckLinkDevice: IP extensions exposed no usable flows for device: %s", ModelName.c_str());
	else
		nosEngine.LogI("DeckLinkDevice: %zu IP flows detected for device: %s", IPFlows.size(), ModelName.c_str());
}

void SubDevice::ApplyIPConfiguration()
{
	if (!IsIPCapable() || !Configuration)
		return;

	auto settings = DeviceManager::Instance()->GetIPSettings(ModelName, PersistentId);
	int64_t ptpDomain = settings ? settings->PTPDomain : 127;
	if (Configuration->SetInt(bmdDeckLinkConfigEthernetPTPDomain, ptpDomain) != S_OK)
		nosEngine.LogW("DeckLinkDevice: Failed to set Ethernet PTP domain for device: %s", ModelName.c_str());

	if (!settings)
		return;

	for (size_t connectorIndex = 0; connectorIndex < settings->Connectors.size(); ++connectorIndex)
	{
		auto const& connector = settings->Connectors[connectorIndex];
		if (!connector.Configured)
			continue;
		if (Configuration->SetFlagWithParam(bmdDeckLinkConfigParamEthernetUseDHCP, connectorIndex, connector.UseDHCP) != S_OK)
			nosEngine.LogW("DeckLinkDevice: Failed to set DHCP for Ethernet connector %zu on device: %s", connectorIndex, ModelName.c_str());

		auto setString = [&](BMDDeckLinkConfigurationID id, std::string const& value, char const* label)
		{
			if (value.empty())
				return;
#if _WIN32
			auto dlValue = StdToDlString(value);
			if (!dlValue)
			{
				nosEngine.LogW("DeckLinkDevice: Failed to allocate %s for device: %s", label, ModelName.c_str());
				return;
			}
			auto result = Configuration->SetStringWithParam(id, connectorIndex, dlValue);
			DeleteString(dlValue);
#else
			auto result = Configuration->SetStringWithParam(id, connectorIndex, value.c_str());
#endif
			if (result != S_OK)
				nosEngine.LogW("DeckLinkDevice: Failed to set %s for Ethernet connector %zu on device: %s", label, connectorIndex, ModelName.c_str());
		};

		setString(bmdDeckLinkConfigParamEthernetStaticLocalIPAddress, connector.StaticLocalIPAddress, "static local IP address");
		setString(bmdDeckLinkConfigParamEthernetStaticSubnetMask, connector.StaticSubnetMask, "static subnet mask");
		setString(bmdDeckLinkConfigParamEthernetStaticGatewayIPAddress, connector.StaticGatewayIPAddress, "static gateway IP address");
		setString(bmdDeckLinkConfigParamEthernetVideoOutputAddress, connector.VideoOutputAddress, "video output address");
		setString(bmdDeckLinkConfigParamEthernetAudioOutputAddress, connector.AudioOutputAddress, "audio output address");
		setString(bmdDeckLinkConfigParamEthernetAncillaryOutputAddress, connector.AncillaryOutputAddress, "ancillary output address");
	}
}

bool SubDevice::EnableIPReceiverFlows()
{
	if (!IsIPCapable())
		return true;

	DisableIPFlows();
	auto settings = DeviceManager::Instance()->GetIPSettings(ModelName, PersistentId);
	if (!settings || settings->PeerSDP[NOS_DECKLINK_IP_FLOW_VIDEO].empty())
	{
		if (!MissingIPReceiverSDPReported)
		{
			nosEngine.LogW("DeckLinkDevice: IP receiver is not configured; video peer SDP is required for device: %s", ModelName.c_str());
			MissingIPReceiverSDPReported = true;
		}
		return false;
	}
	MissingIPReceiverSDPReported = false;

	std::array<int64_t, 3> flowTypes = {
		bmdDeckLinkIPFlowTypeVideo,
		bmdDeckLinkIPFlowTypeAudio,
		bmdDeckLinkIPFlowTypeAncillary
	};
	for (auto flowType : flowTypes)
	{
		auto const& peerSDP = settings->PeerSDP[flowType];
		if (peerSDP.empty())
			continue;
		if (peerSDP.size() >= 1000)
		{
			nosEngine.LogE("DeckLinkDevice: %s peer SDP must be less than 1000 bytes for device: %s", IPFlowTypeName(flowType), ModelName.c_str());
			DisableIPFlows();
			return false;
		}

		IPFlowInfo* receiver = nullptr;
		for (auto& flow : IPFlows)
		{
			if (flow.Type != flowType)
				continue;
			IDeckLinkIPFlowSetting* setting = nullptr;
			if (flow.Flow->QueryInterface(IID_IDeckLinkIPFlowSetting, (void**)&setting) != S_OK || !setting)
			{
				Release(setting);
				continue;
			}
			auto result = SetPeerSDP(setting, peerSDP);
			Release(setting);
			if (result == S_OK)
			{
				receiver = &flow;
				break;
			}
		}
		if (!receiver)
		{
			nosEngine.LogE("DeckLinkDevice: No receiver %s flow accepted peer SDP for device: %s", IPFlowTypeName(flowType), ModelName.c_str());
			DisableIPFlows();
			return false;
		}
		receiver->Receiver = true;
		receiver->SDP = peerSDP;
		if (receiver->Flow->Enable() != S_OK)
		{
			nosEngine.LogE("DeckLinkDevice: Failed to enable receiver %s flow for device: %s", IPFlowTypeName(flowType), ModelName.c_str());
			DisableIPFlows();
			return false;
		}
		receiver->Enabled = true;
	}
	return true;
}

bool SubDevice::EnableIPSenderFlows()
{
	if (!IsIPCapable())
		return true;

	DisableIPFlows();
	for (auto& flow : IPFlows)
	{
		IDeckLinkIPFlowStatus* status = nullptr;
		if (flow.Flow->QueryInterface(IID_IDeckLinkIPFlowStatus, (void**)&status) != S_OK || !status)
		{
			Release(status);
			continue;
		}
		auto sdp = GetFlowSDP(status);
		Release(status);
		if (!sdp || !IsSenderSDP(flow.Type, *sdp))
			continue;

		flow.Sender = true;
		flow.SDP = *sdp;
		if (flow.Flow->Enable() != S_OK)
		{
			nosEngine.LogE("DeckLinkDevice: Failed to enable sender %s flow for device: %s", IPFlowTypeName(flow.Type), ModelName.c_str());
			DisableIPFlows();
			return false;
		}
		flow.Enabled = true;
		if (flow.Type >= 0 && flow.Type < int64_t(SenderSDPs.size()))
			SenderSDPs[flow.Type] = *sdp;
	}

	if (SenderSDPs[NOS_DECKLINK_IP_FLOW_VIDEO].empty())
	{
		nosEngine.LogE("DeckLinkDevice: No sender video SDP was available after enabling video output for device: %s", ModelName.c_str());
		DisableIPFlows();
		return false;
	}
	return true;
}

void SubDevice::DisableIPFlows()
{
	for (auto& flow : IPFlows)
	{
		if (flow.Enabled)
		{
			if (flow.Flow->Disable() != S_OK)
				nosEngine.LogW("DeckLinkDevice: Failed to disable IP flow for device: %s", ModelName.c_str());
			flow.Enabled = false;
		}
		flow.Receiver = false;
		flow.Sender = false;
		flow.SDP.clear();
	}
	SenderSDPs.fill(std::string{});
}

std::optional<std::string> SubDevice::GetIPFlowSDP(nosDeckLinkIPFlowType flowType) const
{
	if (flowType < NOS_DECKLINK_IP_FLOW_VIDEO || flowType > NOS_DECKLINK_IP_FLOW_ANCILLARY)
		return std::nullopt;
	if (!IsIPCapable())
		return std::nullopt;
	if (SenderSDPs[flowType].empty())
		return std::nullopt;
	return SenderSDPs[flowType];
}

bool SubDevice::IsBusyWith(nosMediaIODirection mode)
{
	switch (mode)
	{
	case NOS_MEDIAIO_DIRECTION_INPUT:
		return Input.IsCurrentlyOpen();
	case NOS_MEDIAIO_DIRECTION_OUTPUT:
		return Output.IsCurrentlyOpen();
	}
	return false;
}

std::map<nosMediaIOFrameGeometry, std::set<nosMediaIOFrameRate>> SubDevice::GetSupportedOutputFrameGeometryAndFrameRates(BMDVideoConnection connection,
	nosMediaIOVideoScanType scanType, std::unordered_set<nosMediaIOPixelFormat> const& pixelFormats)
{
	std::map<nosMediaIOFrameGeometry, std::set<nosMediaIOFrameRate>> supported;
	if (!Output)
	{
		nosEngine.LogE("SubDevice: Output interface is not available for device: %s", ModelName.c_str());
		return supported;
	}
	
	for (auto& pixelFormat : pixelFormats)
	{
		for (int i = NOS_MEDIAIO_FRAME_GEOMETRY_MIN; i < NOS_MEDIAIO_FRAME_GEOMETRY_MAX; ++i)
		{
			auto fg = static_cast<nosMediaIOFrameGeometry>(i);
			for (auto& displayMode : GetDisplayModesForFrameGeometry(fg))
			{
				if (GetVideoScanType(displayMode) == scanType && DoesSupportOutputVideoMode(connection, displayMode, GetDeckLinkPixelFormat(pixelFormat)))
				{
					supported[fg].insert(GetFrameRateFromDisplayMode(displayMode));
				}
			}
		}
	}
	return supported;
}

std::map<nosMediaIOFrameGeometry, std::map<nosMediaIOFrameRate, std::set<nosMediaIOPixelFormat>>> SubDevice::GetSupportedOutputVideoFormats(BMDVideoConnection connection,
	nosMediaIOVideoScanType scanType)
{
	std::map<nosMediaIOFrameGeometry, std::map<nosMediaIOFrameRate, std::set<nosMediaIOPixelFormat>>> supported;
	if (!Output)
	{
		nosEngine.LogE("SubDevice: Output interface is not available for device: %s", ModelName.c_str());
		return supported;
	}

	for (int i = NOS_MEDIAIO_PIXEL_FORMAT_MIN; i <= NOS_MEDIAIO_PIXEL_FORMAT_MAX; ++i)
	{
		auto pixelFormat = static_cast<nosMediaIOPixelFormat>(i);
		if (pixelFormat == NOS_MEDIAIO_PIXEL_FORMAT_INVALID)
			continue;
		for (int i = NOS_MEDIAIO_FRAME_GEOMETRY_MIN; i < NOS_MEDIAIO_FRAME_GEOMETRY_MAX; ++i)
		{
			auto fg = static_cast<nosMediaIOFrameGeometry>(i);
			for (auto& displayMode : GetDisplayModesForFrameGeometry(fg))
			{
				if (GetVideoScanType(displayMode) == scanType && DoesSupportOutputVideoMode(connection, displayMode, GetDeckLinkPixelFormat(pixelFormat)))
				{
					supported[fg][GetFrameRateFromDisplayMode(displayMode)].insert(pixelFormat);
				}
			}
		}
	}
	return supported;
}

int32_t SubDevice::AddInputVideoFormatChangeCallback(nosDeckLinkInputVideoFormatChangeCallback callback, void* userData)
{
	return Input.AddInputVideoFormatChangeCallback(callback, userData);
}

void SubDevice::RemoveInputVideoFormatChangeCallback(uint32_t callbackId)
{
	Input.RemoveInputVideoFormatChangeCallback(callbackId);
}

int32_t SubDevice::AddFrameResultCallback(nosMediaIODirection dir, nosDeckLinkFrameResultCallback callback, void* userData)
{
	return GetIO(dir).AddFrameResultCallback(callback, userData);
}

void SubDevice::RemoveFrameResultCallback(nosMediaIODirection dir, uint32_t callbackId)
{
	GetIO(dir).RemoveFrameResultCallback(callbackId);
}

bool SubDevice::StartStream(nosMediaIODirection mode)
{
	return GetIO(mode).StartStream();
}

bool SubDevice::StopStream(nosMediaIODirection mode)
{
	return GetIO(mode).StopStream();
}

void SubDevice::TagChannel(nosMediaIODirection dir, nosDeckLinkChannel channel)
{
	GetIO(dir).Channel = channel;
}

void SubDevice::TagDevice(uint32_t deviceIndex)
{
	GetIO(NOS_MEDIAIO_DIRECTION_INPUT).DeviceIndex = deviceIndex;
	GetIO(NOS_MEDIAIO_DIRECTION_OUTPUT).DeviceIndex = deviceIndex;
}

bool SubDevice::DoesSupportOutputVideoMode(BMDVideoConnection connection, BMDDisplayMode displayMode, BMDPixelFormat pixelFormat)
{
	if (!Output)
		return false;
	BOOL supported{};
	BMDDisplayMode actualDisplayMode{};
	auto res = Output->DoesSupportVideoMode(connection, displayMode, pixelFormat, bmdNoVideoOutputConversion, bmdSupportedVideoModeDefault, &actualDisplayMode, &supported);
	if (res != S_OK)
	{
		nosEngine.LogE("SubDevice: Failed to check video mode support for device: %s", ModelName.c_str());
		return false;
	}
	if (!supported && actualDisplayMode == displayMode)
	{
		// Supported but with conversion
		nosEngine.LogW("SubDevice: Video mode %d is supported but with conversion for device: %s", displayMode, ModelName.c_str());
		return true;
	}
	return supported;
}

bool SubDevice::OpenOutput(BMDDisplayMode displayMode, BMDPixelFormat pixelFormat, bool useIP)
{
	if (!Output) 
	{
		nosEngine.LogE("SubDevice: Output interface is not available for device: %s", ModelName.c_str());
		return false;
	}
	if (useIP)
	{
		ApplyIPConfiguration();
		if (!ApplyIPVideoConnection(Configuration, ProfileAttributes, NOS_MEDIAIO_DIRECTION_OUTPUT, ModelName))
			return false;
	}
	if (!Output.OpenStream(displayMode, pixelFormat))
		return false;
	if (useIP && !EnableIPSenderFlows())
	{
		Output.CloseStream();
		DisableIPFlows();
		return false;
	}
	return true;
}

bool SubDevice::CloseOutput()
{
	if (!Output)
	{
		nosEngine.LogE("SubDevice: Output interface is not available for device: %s", ModelName.c_str());
		return false;
	}
	auto result = Output.CloseStream();
	DisableIPFlows();
	return result;
}

std::optional<nosDeckLinkReferenceStatus> SubDevice::GetOutputReferenceStatus()
{
	if (!Output)
	{
		nosEngine.LogE("SubDevice: Output interface is not available for device: %s", ModelName.c_str());
		return std::nullopt;
	}
	BMDReferenceStatus status{};
	auto res = Output->GetReferenceStatus(&status);
	if (res != S_OK)
	{
		nosEngine.LogE("SubDevice: Failed to get reference status for device: %s", ModelName.c_str());
		return std::nullopt;
	}
	nosDeckLinkReferenceStatus ref = NOS_DECKLINK_REFERENCE_STATUS_UNKNOWN;
	switch (status)
	{
	case bmdReferenceNotSupportedByHardware:
		ref = NOS_DECKLINK_REFERENCE_STATUS_NOT_SUPPORTED;
		break;
	case bmdReferenceLocked:
		ref = NOS_DECKLINK_REFERENCE_STATUS_LOCKED;
		break;
	case bmdReferenceUnlocked:
		ref = NOS_DECKLINK_REFERENCE_STATUS_UNLOCKED;
		break;
	}
	return ref;
}

bool SubDevice::OpenInput(BMDPixelFormat pixelFormat, bool useIP)
{
	if (!Input)
	{
		nosEngine.LogE("SubDevice: Input interface is not available for device: %s", ModelName.c_str());
		return false;
	}
	if (useIP)
	{
		ApplyIPConfiguration();
		if (!ApplyIPVideoConnection(Configuration, ProfileAttributes, NOS_MEDIAIO_DIRECTION_INPUT, ModelName))
			return false;
		// Receiver flows must be configured and enabled before EnableVideoInput.
		if (!EnableIPReceiverFlows())
			return false;
	}
	if (!Input.OpenStream(bmdModeNTSC, pixelFormat))
	{
		if (useIP)
			DisableIPFlows();
		return false;
	}
	return true; // Display mode is auto-detected
}

bool SubDevice::CloseInput()
{
	if (!Input)
	{
		nosEngine.LogE("SubDevice: Input interface is not available for device: %s", ModelName.c_str());
		return false;
	}
	auto result = Input.CloseStream();
	DisableIPFlows();
	return result;
}

bool SubDevice::ResetInputFrames()
{
	if (!Input)
	{
		nosEngine.LogE("SubDevice: Input interface is not available for device: %s", ModelName.c_str());
		return false;
	}
	return true;
}

bool SubDevice::WaitFrame(nosMediaIODirection dir, std::chrono::milliseconds timeout)
{
	return GetIO(dir).WaitFrame(timeout);
}

void SubDevice::DmaTransfer(nosMediaIODirection dir, void* buffer, size_t size)
{
	GetIO(dir).DmaTransfer(buffer, size);
}

std::optional<nosVec2u> SubDevice::GetDeltaSeconds(nosMediaIODirection dir)
{
	return GetIO(dir).GetDeltaSeconds();
}

}
