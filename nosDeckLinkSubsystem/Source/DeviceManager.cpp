// Copyright MediaZ Teknoloji A.S. All Rights Reserved.
#include "DeviceManager.hpp"

#include "ChannelMapping.inl"
#include "Device.hpp"

#include <cstdio>

namespace nos::decklink
{

const char* NOSAPI_CALL GetChannelName(nosDeckLinkChannel channel);
nosDeckLinkChannel NOSAPI_CALL GetChannelFromName(const char* channelName);

DeviceManager::DeviceManager()
{
#ifdef _WIN32
	HRESULT result = CoInitialize(NULL);
	if (FAILED(result))
	{
		nosEngine.LogE("Initialization of COM failed with error: %s", _com_error(result).ErrorMessage());
	}
#endif
	FetchApiVersion();
}

DeviceManager::~DeviceManager()
{
	ClearDeviceList();
}

void DeviceManager::LoadDefaultSettings()
{
	Settings = {};
}

void DeviceManager::LoadSettings(sys::decklink::Settings const& settings)
{
	settings.UnPackTo(&Settings);
	if (!ValidatePortMappings() || !ValidateIPSettings())
	{
		nosEngine.LogE("DeviceManager: Invalid settings. Loading default settings.");
		LoadDefaultSettings();
	}
}

bool DeviceManager::ValidatePortMappings()
{
	bool success = true;
	char errorMsg[256];
	for (auto& portMappingSetting : Settings.sdi_port_mappings)
	{
		auto& modelName = portMappingSetting->model_name;
		if (modelName.empty())
		{
			std::snprintf(errorMsg, sizeof(errorMsg), "Empty model name in port mappings.");
			success = false;
			break;
		}
		auto it = GetSDIPortCounts().find(modelName);
		if (it == GetSDIPortCounts().end())
		{
			std::snprintf(errorMsg, sizeof(errorMsg), "Device model not supported: %s", modelName.c_str());
			success = false;
			break;
		}
		auto portCount = it->second;
		auto& portMapping = portMappingSetting->sdi_port_mapping;
		std::unordered_set<int> targetPorts, sourcePorts;
		for (auto& entry : portMapping)
		{
			auto src = entry.original_port();
			auto dst = entry.new_port();
			if (src < 1 || src > portCount || dst < 1 || dst > portCount)
			{
				std::snprintf(errorMsg, sizeof(errorMsg), "Invalid port mapping for device %s: %d > %d", modelName.c_str(), src, dst);
				success = false;
				break;
			}
			if (targetPorts.find(dst) != targetPorts.end() || sourcePorts.find(src) != sourcePorts.end())
			{
				std::snprintf(errorMsg, sizeof(errorMsg), "Duplicate source or target port in port mapping for device %s: %d > %d", modelName.c_str(), src, dst);
				success = false;
				break;
			}
			targetPorts.insert(dst);
			sourcePorts.insert(src);
		}
		if (!success)
			break;
	}
	if (!success)
	{
		nosEngine.LogE("DeviceManager: %s", errorMsg);
		char completeMsg[256];
		std::snprintf(completeMsg, sizeof(completeMsg), "%s. Using default settings.", errorMsg);
		nosModuleStatusMessage msg {
			.ModuleId = nosEngine.Module->Id,
			.UpdateType = NOS_MODULE_STATUS_MESSAGE_UPDATE_TYPE_APPEND,
			.MessageType = NOS_MODULE_STATUS_MESSAGE_TYPE_ERROR,
			.Message = completeMsg
		};
		nosEngine.SendModuleStatusMessageUpdate(&msg);
	}
	return success;
}

bool DeviceManager::ValidateIPSettings()
{
	for (auto& ipSetting : Settings.ip_device_settings)
	{
		if (!ipSetting)
			continue;
		if (ipSetting->model_name.empty())
		{
			nosEngine.LogE("DeviceManager: Empty model name in IP device settings.");
			return false;
		}
		if (ipSetting->persistent_id < -1)
		{
			nosEngine.LogE("DeviceManager: Invalid persistent ID %lld for IP device %s.", ipSetting->persistent_id, ipSetting->model_name.c_str());
			return false;
		}
		if (ipSetting->ptp_domain < 0 || ipSetting->ptp_domain > 127)
		{
			nosEngine.LogE("DeviceManager: Invalid PTP domain %lld for IP device %s.", ipSetting->ptp_domain, ipSetting->model_name.c_str());
			return false;
		}
		if (ipSetting->video_peer_sdp.size() >= 1000 || ipSetting->audio_peer_sdp.size() >= 1000 || ipSetting->ancillary_peer_sdp.size() >= 1000)
		{
			nosEngine.LogE("DeviceManager: Peer SDP must be less than 1000 bytes for IP device %s.", ipSetting->model_name.c_str());
			return false;
		}

		std::unordered_set<int32_t> connectorIndices;
		for (auto& connector : ipSetting->ethernet_connectors)
		{
			if (!connector)
				continue;
			if (connector->connector_index < 0 || connector->connector_index >= 2 || !connectorIndices.insert(connector->connector_index).second)
			{
				nosEngine.LogE("DeviceManager: Invalid or duplicate Ethernet connector index for IP device %s.", ipSetting->model_name.c_str());
				return false;
			}
		}
	}
	return true;
}

std::optional<IPRuntimeSettings> DeviceManager::GetIPSettings(std::string_view modelName, int64_t persistentId) const
{
	for (auto& ipSetting : Settings.ip_device_settings)
	{
		if (!ipSetting)
			continue;
		std::string configuredModel = ipSetting->model_name;
		bool modelMatches = modelName == configuredModel || modelName.starts_with(configuredModel + " (");
		if (!modelMatches)
			continue;
		if (ipSetting->persistent_id >= 0 && ipSetting->persistent_id != persistentId)
			continue;

		IPRuntimeSettings result;
		result.Configured = true;
		result.PersistentId = ipSetting->persistent_id;
		result.PTPDomain = ipSetting->ptp_domain;
		result.PeerSDP[NOS_DECKLINK_IP_FLOW_VIDEO] = ipSetting->video_peer_sdp;
		result.PeerSDP[NOS_DECKLINK_IP_FLOW_AUDIO] = ipSetting->audio_peer_sdp;
		result.PeerSDP[NOS_DECKLINK_IP_FLOW_ANCILLARY] = ipSetting->ancillary_peer_sdp;
		for (auto& connector : ipSetting->ethernet_connectors)
		{
			if (!connector || connector->connector_index < 0 || connector->connector_index >= int(result.Connectors.size()))
				continue;
			auto& target = result.Connectors[connector->connector_index];
			target.Configured = true;
			target.UseDHCP = connector->use_dhcp;
			target.StaticLocalIPAddress = connector->static_local_ip_address;
			target.StaticSubnetMask = connector->static_subnet_mask;
			target.StaticGatewayIPAddress = connector->static_gateway_ip_address;
			target.VideoOutputAddress = connector->video_output_address;
			target.AudioOutputAddress = connector->audio_output_address;
			target.AncillaryOutputAddress = connector->ancillary_output_address;
		}
		return result;
	}
	return std::nullopt;
}

void DeviceManager::InitializeDeviceList()
{
	Devices = CreateDevices();
	for (auto& device : Devices)
		DeviceMutexes[device->Index] = std::make_unique<std::shared_mutex>();
}

void DeviceManager::FetchApiVersion()
{
	ApiVersion = std::nullopt;

	IDeckLinkAPIInformation* api = nullptr;
	HRESULT result = S_OK;

	// Create an IDeckLinkIterator object to enumerate all DeckLink cards in the system
	result = CoCreateInstance(CLSID_CDeckLinkAPIInformation, NULL, CLSCTX_ALL, IID_IDeckLinkAPIInformation, (void**)&api);
	if (FAILED(result))
	{
		nosModuleStatusMessage msg {
			.ModuleId = nosEngine.Module->Id,
			.UpdateType = NOS_MODULE_STATUS_MESSAGE_UPDATE_TYPE_APPEND,
			.MessageType = NOS_MODULE_STATUS_MESSAGE_TYPE_ERROR,
			.Message = "DeckLink API information interface could not be fetched. Drivers may not be installed."
		};
		nosEngine.SendModuleStatusMessageUpdate(&msg);
		return;
	}

	long long apiVersion = 0;
	result = api->GetInt(BMDDeckLinkAPIVersion, &apiVersion);
	if (FAILED(result))
	{
		nosModuleStatusMessage msg{
			.ModuleId = nosEngine.Module->Id,
			.UpdateType = NOS_MODULE_STATUS_MESSAGE_UPDATE_TYPE_APPEND,
			.MessageType = NOS_MODULE_STATUS_MESSAGE_TYPE_ERROR,
			.Message = "DeckLink API version could not be read."
		};
		nosEngine.SendModuleStatusMessageUpdate(&msg);
		api->Release();
		return;
	}

	// Word, decreasing address order:
	// Byte 4: Major
	// Byte 3: Minor
	// Byte 2: Sub Version
	// Byte 1: Extra
	uint8_t major = (apiVersion >> 24) & 0xFF;
	uint8_t minor = (apiVersion >> 16) & 0xFF;
	uint8_t subVersion = (apiVersion >> 8) & 0xFF;
	uint8_t extra = apiVersion & 0xFF;
	nosEngine.LogI("DeckLink API Version: %d.%d.%d.%d", major, minor, subVersion, extra);

	api->Release();

	ApiVersion = std::array<int, 2>{ major, minor };

	bool incompatible = (*ApiVersion)[0] != NOS_DECKLINK_USED_DRIVER_API_VERSION_MAJOR || (*ApiVersion)[1] < NOS_DECKLINK_USED_DRIVER_API_VERSION_MINOR;
	bool warn = (*ApiVersion)[0] == NOS_DECKLINK_USED_DRIVER_API_VERSION_MAJOR && (*ApiVersion)[1] > NOS_DECKLINK_USED_DRIVER_API_VERSION_MINOR;
	if (incompatible)
	{
		std::stringstream errorMsg;
		errorMsg << "Your driver version " << (*ApiVersion)[0] << "." << (*ApiVersion)[1] << " is incompatible, please install version " <<  NOS_DECKLINK_USED_DRIVER_API_VERSION << "!";
		auto msg = errorMsg.str();
		nosModuleStatusMessage status{
			.ModuleId = nosEngine.Module->Id,
			.UpdateType = NOS_MODULE_STATUS_MESSAGE_UPDATE_TYPE_APPEND,
			.MessageType = NOS_MODULE_STATUS_MESSAGE_TYPE_ERROR,
			.Message = msg.c_str()
		};
		nosEngine.SendModuleStatusMessageUpdate(&status);
	}
	else if (warn)
	{
		std::stringstream warnMsg;
		warnMsg << "Your driver version " << (*ApiVersion)[0] << "." << (*ApiVersion)[1] << " has not been tested, you could install version " NOS_DECKLINK_USED_DRIVER_API_VERSION;
		auto msg = warnMsg.str();
		nosModuleStatusMessage status{
			.ModuleId = nosEngine.Module->Id,
			.UpdateType = NOS_MODULE_STATUS_MESSAGE_UPDATE_TYPE_APPEND,
			.MessageType = NOS_MODULE_STATUS_MESSAGE_TYPE_WARNING,
			.Message = msg.c_str()
		};
		nosEngine.SendModuleStatusMessageUpdate(&status);
	}
}

std::string SimultaneousReplace(std::string_view input, const std::map<std::string, std::string>& transformations)
{
	// A vector to store matches (start position, end position, replacement)
	std::vector<std::tuple<size_t, size_t, std::string>> matches;

	// Collect all matches from the input string for all transformations
	for (const auto& [oldStr, newStr] : transformations) {
		size_t start = 0;
		while ((start = input.find(oldStr, start)) != std::string::npos) {
			matches.emplace_back(start, start + oldStr.length(), newStr);
			start += oldStr.length(); // Move past the current match
		}
	}

	// Sort matches by start position (to handle multiple replacements in order)
	std::sort(matches.begin(), matches.end(), [](const auto& a, const auto& b) {
		return std::get<0>(a) < std::get<0>(b); // Compare start positions
	});

	// Rebuild the string with simultaneous replacements
	std::string result;
	size_t lastPos = 0;

	for (const auto& [start, end, replacement] : matches) {
		if (start >= lastPos) { // Ensure no overlap
			result.append(input.substr(lastPos, start - lastPos)); // Append unchanged part
			result.append(replacement); // Append replacement
			lastPos = end; // Update the last processed position
		}
	}

	// Append any remaining part of the original string
	result.append(input.substr(lastPos));

	return result;
}

std::optional<std::string> DeviceManager::GetPortMappedChannelName(uint32_t deviceIndex, nosDeckLinkChannel channel)
{
	std::string modelName;
	{
		DeviceLock lock(deviceIndex);
		auto* device = GetDevice(deviceIndex);
		if (!device)
		{
			nosEngine.LogE("DeviceManager: No such device with index %d", deviceIndex);
			return std::nullopt;
		}
		modelName = device->ModelName;
	}
	std::string originalChannelName = GetChannelName(channel);
	std::map<std::string, std::string> transformations;
	for (auto& portMappingSetting : Settings.sdi_port_mappings)
	{
		if (portMappingSetting->model_name != modelName)
			continue;
		for (auto& entry : portMappingSetting->sdi_port_mapping)
		{
			std::string sourcePortStr = std::to_string(entry.original_port());
			std::string targetPortStr = std::to_string(entry.new_port());
			transformations[sourcePortStr] = targetPortStr;
		}
	}
	return SimultaneousReplace(originalChannelName, transformations);
}

nosDeckLinkChannel DeviceManager::GetChannelFromPortMappedName(uint32_t deviceIndex, std::string_view portMappedName)
{
	std::string modelName;
	{
		DeviceLock lock(deviceIndex);
		auto* device = GetDevice(deviceIndex);
		if (!device)
		{
			nosEngine.LogE("DeviceManager: No such device with index %d", deviceIndex);
			return NOS_DECKLINK_CHANNEL_INVALID;
		}
		modelName = device->ModelName;
	}
	std::map<std::string, std::string> transformations;
	for (auto& portMappingSetting : Settings.sdi_port_mappings)
	{
		if (portMappingSetting->model_name != modelName)
			continue;
		for (auto& entry : portMappingSetting->sdi_port_mapping)
		{
			std::string sourcePortStr = std::to_string(entry.original_port());
			std::string targetPortStr = std::to_string(entry.new_port());
			transformations[targetPortStr] = sourcePortStr; // reverse
		}
	}
	auto originalName = SimultaneousReplace(portMappedName, transformations);
	return GetChannelFromName(originalName.c_str());
}

void DeviceManager::ClearDeviceList()
{
	for (auto it = Devices.begin(); it != Devices.end(); ++it)
	{
		auto device = it->get();
		auto index = it->get()->Index;
		LockDevice(index, false);
		device->Destroy();
		it->reset();
		UnlockDevice(index, false);
	}
	Devices.clear();
}

Device* DeviceManager::GetDevice(int64_t groupId)
{
	for (auto& device : Devices)
	{
		if (device->GroupId == groupId)
			return device.get();
	}
	return nullptr;
}

Device* DeviceManager::GetDevice(uint32_t deviceIndex)
{
	if (deviceIndex < Devices.size())
		return Devices[deviceIndex].get();
	return nullptr;
}

thread_local std::unordered_map<uint32_t, DeviceManager::RecursiveMutex> DeviceManager::LockedByThisThread = {};

void DeviceManager::LockDevice(uint32_t deviceIndex, bool shared)
{
	auto& [lockCount, isSharedLock] = LockedByThisThread[deviceIndex];
	if (lockCount > 0)
	{
		lockCount++;
		if (isSharedLock)
			assert(shared);
		return;
	}
	// Not locked yet
	auto it = DeviceMutexes.find(deviceIndex);
	if (it == DeviceMutexes.end())
		return;
	if (shared)
		it->second->lock_shared();
	else
		it->second->lock();
	// Mark as locked
	++lockCount;
	isSharedLock = shared;
}

void DeviceManager::UnlockDevice(uint32_t deviceIndex, bool shared)
{
	auto& [lockCount, isSharedLock] = LockedByThisThread[deviceIndex];
	if (lockCount == 0)
		return;
	if (--lockCount == 0)
	{
		if (isSharedLock)
			assert(shared);
		auto it = DeviceMutexes.find(deviceIndex);
		if (it == DeviceMutexes.end())
			return;
		if (shared)
			it->second->unlock_shared();
		else
			it->second->unlock();
	}
}

DeviceManager* DeviceManager::Instance()
{
	if (!SingleInstance)
		SingleInstance = new DeviceManager;
	return SingleInstance;
}

void DeviceManager::Destroy()
{
	delete SingleInstance;
	SingleInstance = nullptr;
}

DeviceManager* DeviceManager::SingleInstance = nullptr;
}
