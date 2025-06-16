// Copyright MediaZ Teknoloji A.S. All Rights Reserved.
#include <Nodos/SubsystemAPI.h>
#include <Nodos/Name.hpp>

#include "EnumConversions.hpp"

NOS_INIT();

#include <nosMediaIO/nosMediaIO.h>
#include <nosDeviceSubsystem/nosDeviceSubsystem.h>

NOS_MEDIAIO_SUBSYSTEM_INIT()
NOS_DEVICE_SUBSYSTEM_INIT()

NOS_BEGIN_IMPORT_DEPS()
	NOS_MEDIAIO_SUBSYSTEM_IMPORT()
	NOS_DEVICE_SUBSYSTEM_IMPORT()
NOS_END_IMPORT_DEPS()

#include "nosDeckLinkSubsystem/nosDeckLinkSubsystem.h"
#include "Device.hpp"
#include "SubDevice.hpp"
#include "DeviceManager.hpp"
#include <Nodos/Helpers.hpp>

namespace nos::decklink
{
std::unordered_map<uint32_t, nosDeckLinkSubsystem*> GExportedSubsystemVersions;

nosResult NOSAPI_CALL UnloadSubsystem()
{
	DeviceManager::Destroy();
	return NOS_RESULT_SUCCESS;
}

const char* NOSAPI_CALL GetChannelName(nosDeckLinkChannel channel)
{
	if (channel < NOS_DECKLINK_CHANNEL_MIN || channel > NOS_DECKLINK_CHANNEL_MAX)
		return NOS_DECKLINK_CHANNEL_NAMES[NOS_DECKLINK_CHANNEL_INVALID];
	return NOS_DECKLINK_CHANNEL_NAMES[channel];
}

namespace internal
{
SubDevice* GetSubDevice(uint32_t deviceIndex, nosMediaIODirection dir, nosDeckLinkChannel channel)
{
	auto* device = DeviceManager::Instance()->GetDevice(deviceIndex);
	if (!device)
	{
		nosEngine.LogE("No such device with index %d", deviceIndex);
		return nullptr;
	}
	auto* subDevice = device->GetSubDeviceOfChannel(dir, channel);
	if (!subDevice)
	{
		nosEngine.LogE("No such sub-device for channel %s", GetChannelName(channel));
		return nullptr;
	}
	return subDevice;
}
}

void NOSAPI_CALL GetDevices(size_t* outCount, nosDeckLinkDeviceDesc* outDeviceDescriptors)
{
	if (outCount == nullptr)
		return;
	auto& devices = DeviceManager::Instance()->GetDevices();
	*outCount = devices.size();
	if (outDeviceDescriptors == nullptr)
		return;
	for (size_t i = 0; i < *outCount; i++)
	{
		DeviceLock lock(i);
		auto& device = devices[i];
		outDeviceDescriptors[i].DeviceIndex = device->Index;
		auto uniqueDisplayName = device->GetUniqueDisplayName();
		size_t maxSize = sizeof(outDeviceDescriptors[i].UniqueDisplayName);
		strncpy(outDeviceDescriptors[i].UniqueDisplayName, uniqueDisplayName.c_str(), std::min(uniqueDisplayName.size() + 1, maxSize));
		outDeviceDescriptors[i].UniqueDisplayName[maxSize - 1] = '\0';
	}
}

nosResult NOSAPI_CALL GetAvailableChannels(uint32_t deviceIndex, nosMediaIODirection dir, nosDeckLinkChannelList* outChannels)
{
	DeviceLock lock(deviceIndex);
	auto device = DeviceManager::Instance()->GetDevice(deviceIndex);
	if (device == nullptr)
	{
		nosEngine.LogE("No such device with index %d", deviceIndex);
		return NOS_RESULT_NOT_FOUND;
	}
	if (!outChannels)
		return NOS_RESULT_INVALID_ARGUMENT;

	auto availableChannels = device->GetAvailableChannels(dir);
	outChannels->Count = availableChannels.size();
	for (size_t i = 0; i < availableChannels.size(); i++)
	{
		outChannels->Channels[i] = availableChannels[i];
	}
	return NOS_RESULT_SUCCESS;
}

nosDeckLinkChannel NOSAPI_CALL GetChannelFromName(const char* channelName)
{
	for (int i = 0; i <= NOS_DECKLINK_CHANNEL_COUNT; i++)
	{
		if (strcmp(NOS_DECKLINK_CHANNEL_NAMES[i], channelName) == 0)
			return nosDeckLinkChannel(i);
	}
	return NOS_DECKLINK_CHANNEL_INVALID;
}

nosResult NOSAPI_CALL GetDeviceByUniqueDisplayName(const char* uniqueDisplayName, uint32_t* outDeviceIndex)
{
	auto& devices = DeviceManager::Instance()->GetDevices();
	for (size_t i = 0; i < devices.size(); i++)
	{
		auto& device = devices[i];
		if (device->GetUniqueDisplayName() == uniqueDisplayName)
		{
			*outDeviceIndex = device->Index;
			return NOS_RESULT_SUCCESS;
		}
	}
	return NOS_RESULT_NOT_FOUND;
}
	
nosResult NOSAPI_CALL GetDeviceInfoByIndex(uint32_t deviceIndex, nosDeckLinkDeviceInfo* outInfo)
{
	DeviceLock lock(deviceIndex);
	auto& devices = DeviceManager::Instance()->GetDevices();
	if (deviceIndex >= devices.size())
	{
		nosEngine.LogE("No such device with index %d", deviceIndex);
		return NOS_RESULT_NOT_FOUND;
	}
	auto& device = devices[deviceIndex];
	strncpy(outInfo->ModelName, device->ModelName.c_str(), device->ModelName.size() + 1);
	outInfo->Desc.DeviceIndex = device->Index;
	auto uniqueDisplayName = device->GetUniqueDisplayName();
	size_t maxSize = sizeof(outInfo->Desc.UniqueDisplayName);
	strncpy(outInfo->Desc.UniqueDisplayName, uniqueDisplayName.c_str(), std::min(uniqueDisplayName.size() + 1, maxSize));
	return NOS_RESULT_SUCCESS;
}


nosResult NOSAPI_CALL GetSupportedOutputFrameGeometries(uint32_t deviceIndex, nosDeckLinkChannel channel, nosMediaIOVideoScanType scanType, nosMediaIOFrameGeometryList* outGeometries)
{
	if (!outGeometries)
	{
		nosEngine.LogE("Invalid argument: outGeometries is nullptr");
		return NOS_RESULT_INVALID_ARGUMENT;
	}
	DeviceLock lock(deviceIndex);
	auto* subDevice = internal::GetSubDevice(deviceIndex, NOS_MEDIAIO_DIRECTION_OUTPUT, channel);
	if (!subDevice)
		return NOS_RESULT_NOT_FOUND;
	auto supported = subDevice->GetSupportedOutputFrameGeometryAndFrameRates(scanType, {NOS_MEDIAIO_PIXEL_FORMAT_YCBCR_8BIT, NOS_MEDIAIO_PIXEL_FORMAT_YCBCR_10BIT});
	outGeometries->Count = supported.size();
	int i = 0;
	for (auto& [fg, _] : supported)
		outGeometries->Geometries[i++] = fg;
	return NOS_RESULT_SUCCESS;
}

nosResult NOSAPI_CALL GetSupportedOutputFrameRatesForGeometry(uint32_t deviceIndex, nosDeckLinkChannel channel, nosMediaIOVideoScanType scanType, nosMediaIOFrameGeometry frameGeo, nosMediaIOFrameRateList* outFrameRates)
{
	if (!outFrameRates)
	{
		nosEngine.LogE("Invalid argument: outFrameRates is nullptr");
		return NOS_RESULT_INVALID_ARGUMENT;
	}
	DeviceLock lock(deviceIndex);
	auto* subDevice = internal::GetSubDevice(deviceIndex, NOS_MEDIAIO_DIRECTION_OUTPUT, channel);
	if (!subDevice)
		return NOS_RESULT_NOT_FOUND;
	auto supported = subDevice->GetSupportedOutputFrameGeometryAndFrameRates(scanType, {NOS_MEDIAIO_PIXEL_FORMAT_YCBCR_8BIT, NOS_MEDIAIO_PIXEL_FORMAT_YCBCR_10BIT});
	std::set<nosMediaIOFrameRate> frameRates;
	for (auto& [curFrameGeo, frs] : supported)
	{
		if (curFrameGeo == frameGeo)
		{
			frameRates.insert(frs.begin(), frs.end());
			break;
		}
	}
	int i = 0;
	outFrameRates->Count = frameRates.size();
	for (auto it = frameRates.rbegin(); it != frameRates.rend(); ++it)
		outFrameRates->FrameRates[i++] = *it;
	return NOS_RESULT_SUCCESS;
}

nosResult NOSAPI_CALL GetSupportedOutputPixelFormats(uint32_t deviceIndex, nosDeckLinkChannel channel, nosMediaIOVideoScanType scanType, nosMediaIOFrameGeometry frameGeo, nosMediaIOFrameRate frameRate, nosMediaIOPixelFormatList* outList)
{
	if (!outList)
	{
		nosEngine.LogE("Invalid argument: outList is nullptr");
		return NOS_RESULT_INVALID_ARGUMENT;
	}
	DeviceLock lock(deviceIndex);
	auto* subDevice = internal::GetSubDevice(deviceIndex, NOS_MEDIAIO_DIRECTION_OUTPUT, channel);
	if (!subDevice)
		return NOS_RESULT_NOT_FOUND;
	auto supported = subDevice->GetSupportedOutputVideoFormats(scanType);
	auto& pixelFormats = supported[frameGeo][frameRate];
	outList->Count = pixelFormats.size();
	int i = 0;
	for (auto& fmt : pixelFormats)
		outList->PixelFormats[i++] = fmt;
	return NOS_RESULT_SUCCESS;
}

nosResult NOSAPI_CALL OpenChannel(uint32_t deviceIndex, nosDeckLinkOpenChannelParams* params)
{
	DeviceLock lock(deviceIndex);
	auto* device = DeviceManager::Instance()->GetDevice(deviceIndex);
	if (!device)
	{
		nosEngine.LogE("No such device with index %d", deviceIndex);
		return NOS_RESULT_NOT_FOUND;
	}
	if (params->Direction == NOS_MEDIAIO_DIRECTION_OUTPUT)
	{
		if (!device->OpenOutput(params->Channel, GetDeckLinkDisplayMode(params->Output.ScanType, params->Output.Geometry, params->Output.FrameRate), GetDeckLinkPixelFormat(params->PixelFormat)))
		{
			nosEngine.LogE("Failed to open output for channel %s", GetChannelName(params->Channel));
			return NOS_RESULT_FAILED;
		}
	}
	else
	{
		if (!device->OpenInput(params->Channel, GetDeckLinkPixelFormat(params->PixelFormat)))
		{
			nosEngine.LogE("Failed to open input for channel %s", GetChannelName(params->Channel));
			return NOS_RESULT_FAILED;
		}
	}
	return NOS_RESULT_SUCCESS;
}

nosResult NOSAPI_CALL CloseChannel(uint32_t deviceIndex, nosDeckLinkChannel channel)
{
	DeviceLock lock(deviceIndex);
	auto* device = DeviceManager::Instance()->GetDevice(deviceIndex);
	if (!device)
	{
		nosEngine.LogE("No such device with index %d", deviceIndex);
		return NOS_RESULT_NOT_FOUND;
	}
	if (!device->CloseChannel(channel))
	{
		nosEngine.LogE("Failed to close channel %s", GetChannelName(channel));
		return NOS_RESULT_FAILED;
	}
	return NOS_RESULT_SUCCESS;
}

nosResult NOSAPI_CALL GetCurrentDeltaSecondsOfChannel(uint32_t deviceIndex, nosDeckLinkChannel channel, nosVec2u* outDeltaSeconds)
{
	DeviceLock lock(deviceIndex);
	auto* device = DeviceManager::Instance()->GetDevice(deviceIndex);
	if (!device)
	{
		nosEngine.LogE("No such device with index %d", deviceIndex);
		return NOS_RESULT_NOT_FOUND;
	}
	auto deltaSeconds = device->GetCurrentDeltaSecondsOfChannel(channel);
	if (!deltaSeconds)
	{
		nosEngine.LogE("Failed to get delta seconds for channel %s", GetChannelName(channel));
		return NOS_RESULT_FAILED;
	}
	*outDeltaSeconds = *deltaSeconds;
	return NOS_RESULT_SUCCESS;
}

nosResult NOSAPI_CALL WaitFrame(uint32_t deviceIndex, nosDeckLinkChannel channel, uint32_t timeoutMs)
{
	DeviceLock lock(deviceIndex);
	auto* device = DeviceManager::Instance()->GetDevice(deviceIndex);
	if (!device)
	{
		nosEngine.LogE("No such device with index %d", deviceIndex);
		return NOS_RESULT_NOT_FOUND;
	}
	if (!device->WaitFrame(channel, std::chrono::milliseconds(timeoutMs)))
		return NOS_RESULT_FAILED;
	return NOS_RESULT_SUCCESS;
}

nosResult NOSAPI_CALL DMATransfer(uint32_t deviceIndex, nosDeckLinkChannel channel, void* data, size_t size)
{
	DeviceLock lock(deviceIndex);
	auto* device = DeviceManager::Instance()->GetDevice(deviceIndex);
	if (!device)
	{
		nosEngine.LogE("No such device with index %d", deviceIndex);
		return NOS_RESULT_NOT_FOUND;
	}
	if (!device->DmaTransfer(channel, data, size))
		return NOS_RESULT_FAILED;
	return NOS_RESULT_SUCCESS;
}

int32_t NOSAPI_CALL RegisterInputVideoFormatChangeCallback(uint32_t deviceIndex, nosDeckLinkChannel channel, nosDeckLinkInputVideoFormatChangeCallback callback, void* userData)
{
	DeviceLock lock(deviceIndex);
	auto* device = DeviceManager::Instance()->GetDevice(deviceIndex);
	if (!device)
	{
		nosEngine.LogE("No such device with index %d", deviceIndex);
		return NOS_RESULT_NOT_FOUND;
	}
	auto* subDevice = device->GetSubDeviceOfChannel(NOS_MEDIAIO_DIRECTION_INPUT, channel);
	if (!subDevice)
	{
		nosEngine.LogE("No sub-device found for channel %s", GetChannelName(channel));
		return -1;
	}
	return subDevice->AddInputVideoFormatChangeCallback(callback, userData);
}

nosResult NOSAPI_CALL UnregisterInputVideoFormatChangeCallback(uint32_t deviceIndex, nosDeckLinkChannel channel, int32_t callbackId)
{
	DeviceLock lock(deviceIndex);
	auto* device = DeviceManager::Instance()->GetDevice(deviceIndex);
	if (!device)
	{
		nosEngine.LogE("No such device with index %d", deviceIndex);
		return NOS_RESULT_NOT_FOUND;
	}
	auto* subDevice = device->GetSubDeviceOfChannel(NOS_MEDIAIO_DIRECTION_INPUT, channel);
	if (!subDevice)
	{
		nosEngine.LogE("No sub-device found for channel %s", GetChannelName(channel));
		return NOS_RESULT_NOT_FOUND;
	}
	subDevice->RemoveInputVideoFormatChangeCallback(callbackId);
	return NOS_RESULT_SUCCESS;	
}

nosResult NOSAPI_CALL StartStream(uint32_t deviceIndex, nosDeckLinkChannel channel)
{
	DeviceLock lock(deviceIndex);
	auto* device = DeviceManager::Instance()->GetDevice(deviceIndex);
	if (!device)
	{
		nosEngine.LogE("No such device with index %d", deviceIndex);
		return NOS_RESULT_NOT_FOUND;
	}
	return device->StartStream(channel) ? NOS_RESULT_SUCCESS : NOS_RESULT_FAILED;
}

nosResult NOSAPI_CALL StopStream(uint32_t deviceIndex, nosDeckLinkChannel channel)
{
	DeviceLock lock(deviceIndex);
	auto* device = DeviceManager::Instance()->GetDevice(deviceIndex);
	if (!device)
	{
		nosEngine.LogE("No such device with index %d", deviceIndex);
		return NOS_RESULT_NOT_FOUND;
	}
	return device->StopStream(channel) ? NOS_RESULT_SUCCESS : NOS_RESULT_FAILED;
}

nosResult NOSAPI_CALL ResetInputFrames(uint32_t deviceIndex, nosDeckLinkChannel channel)
{
	DeviceLock lock(deviceIndex);
	auto* device = DeviceManager::Instance()->GetDevice(deviceIndex);
	if (!device)
	{
		nosEngine.LogE("No such device with index %d", deviceIndex);
		return NOS_RESULT_NOT_FOUND;
	}
	return device->ResetInputFrames(channel) ? NOS_RESULT_SUCCESS : NOS_RESULT_FAILED;
}

int32_t NOSAPI_CALL RegisterFrameResultCallback(uint32_t deviceIndex, nosDeckLinkChannel channel, nosDeckLinkFrameResultCallback callback, void* userData)
{
	DeviceLock lock(deviceIndex);
	auto* device = DeviceManager::Instance()->GetDevice(deviceIndex);
	if (!device)
	{
		nosEngine.LogE("No such device with index %d", deviceIndex);
		return NOS_RESULT_NOT_FOUND;
	}
	auto [subDevice, dir] = device->GetSubDeviceOfOpenChannel(channel);
	if (!subDevice)
	{
		nosEngine.LogE("No sub-device found open channel %s", GetChannelName(channel));
		return -1;
	}
	return subDevice->AddFrameResultCallback(dir, callback, userData);
}

nosResult NOSAPI_CALL UnregisterFrameResultCallback(uint32_t deviceIndex, nosDeckLinkChannel channel, int32_t callbackId)
{
	DeviceLock lock(deviceIndex);
	auto* device = DeviceManager::Instance()->GetDevice(deviceIndex);
	if (!device)
	{
		nosEngine.LogE("No such device with index %d", deviceIndex);
		return NOS_RESULT_NOT_FOUND;
	}
	auto [subDevice, dir] = device->GetSubDeviceOfOpenChannel(channel);
	if (!subDevice)
	{
		nosEngine.LogE("No sub-device found open channel %s", GetChannelName(channel));
		return NOS_RESULT_NOT_FOUND;
	}
	subDevice->RemoveFrameResultCallback(dir, callbackId);
	return NOS_RESULT_SUCCESS;	
}

int32_t NOSAPI_CALL RegisterDeviceInvalidatedCallback(uint32_t deviceIndex, nosDeckLinkDeviceInvalidatedCallback callback, void* userData)
{
	DeviceLock lock(deviceIndex);
	auto* device = DeviceManager::Instance()->GetDevice(deviceIndex);
	if (!device)
	{
		nosEngine.LogE("No such device with index %d", deviceIndex);
		return NOS_RESULT_NOT_FOUND;
	}
	return device->AddDeviceInvalidatedCallback(callback, userData);
}

nosResult NOSAPI_CALL UnregisterDeviceInvalidatedCallback(uint32_t deviceIndex, int32_t callbackId)
{
	DeviceLock lock(deviceIndex);
	auto* device = DeviceManager::Instance()->GetDevice(deviceIndex);
	if (!device)
	{
		nosEngine.LogE("No such device with index %d", deviceIndex);
		return NOS_RESULT_NOT_FOUND;
	}
	device->RemoveDeviceInvalidatedCallback(callbackId);
	return NOS_RESULT_SUCCESS;
}

int32_t NOSAPI_CALL RegisterDeviceStatusCallback(uint32_t deviceIndex, nosDeckLinkDeviceStatusCallback callback, void* userData)
{
	DeviceLock lock(deviceIndex);
	auto* device = DeviceManager::Instance()->GetDevice(deviceIndex);
	if (!device)
	{
		nosEngine.LogE("No such device with index %d", deviceIndex);
		return NOS_RESULT_NOT_FOUND;
	}
	return device->AddDeviceStatusCallback(callback, userData);
}

nosResult NOSAPI_CALL UnregisterDeviceStatusCallback(uint32_t deviceIndex, int32_t callbackId)
{
	DeviceLock lock(deviceIndex);
	auto* device = DeviceManager::Instance()->GetDevice(deviceIndex);
	if (!device)
	{
		nosEngine.LogE("No such device with index %d", deviceIndex);
		return NOS_RESULT_NOT_FOUND;
	}
	device->RemoveDeviceStatusCallback(callbackId);
	return NOS_RESULT_SUCCESS;
}

nosResult NOSAPI_CALL GetPortMappedChannelName(uint32_t deviceIndex, nosDeckLinkChannel channel, char* outName, size_t maxSize)
{
	auto mappedName = DeviceManager::Instance()->GetPortMappedChannelName(deviceIndex, channel);
	if (!mappedName)
		return NOS_RESULT_FAILED;
	size_t len = mappedName->size() + 1;
	if (maxSize < len)
	{
		nosEngine.LogE("Buffer size is not enough for the mapped channel name");
		return NOS_RESULT_INSUFFICIENT_BUFFER_SIZE;
	}
	strncpy(outName, mappedName->c_str(), len);
	return NOS_RESULT_SUCCESS;
}

nosDeckLinkChannel NOSAPI_CALL GetChannelFromPortMappedName(uint32_t deviceIndex, const char* portMappedName)
{
	return DeviceManager::Instance()->GetChannelFromPortMappedName(deviceIndex, portMappedName);
}

nosResult NOSAPI_CALL GetOutputReferenceStatus(uint32_t deviceIndex, nosDeckLinkChannel channel, nosDeckLinkReferenceStatus* outReferenceStatus)
{
	DeviceLock lock(deviceIndex);
	auto* device = DeviceManager::Instance()->GetDevice(deviceIndex);
	if (!device)
	{
		nosEngine.LogE("No such device with index %d", deviceIndex);
		return NOS_RESULT_NOT_FOUND;
	}
	auto [subDevice, dir] = device->GetSubDeviceOfOpenChannel(channel);
	if (!subDevice)
	{
		nosEngine.LogE("No sub-device found open channel %s", GetChannelName(channel));
		return NOS_RESULT_NOT_FOUND;
	}
	auto ref = subDevice->GetOutputReferenceStatus();
	if (!ref)
	{
		nosEngine.LogE("Failed to get output reference status for channel %s", GetChannelName(channel));
		return NOS_RESULT_FAILED;
	}
	*outReferenceStatus = *ref;
	return NOS_RESULT_SUCCESS;
}

nosResult NOSAPI_CALL GetChannelState(uint32_t deviceIndex, nosDeckLinkChannel channel, nosDeckLinkChannelState* outState)
{
	DeviceLock lock(deviceIndex);
	auto* device = DeviceManager::Instance()->GetDevice(deviceIndex);
	if (!device)
	{
		nosEngine.LogE("No such device with index %d", deviceIndex);
		return NOS_RESULT_NOT_FOUND;
	}
	auto [subDevice, dir] = device->GetSubDeviceOfOpenChannel(channel);
	if (!subDevice)
	{
		outState->IsOpen = false;
		outState->IsStreaming = false;
		outState->LastFrameInfo = {};
		return NOS_RESULT_SUCCESS;
	}
	auto& ioHandler = subDevice->GetIO(dir);
	outState->IsOpen = ioHandler.IsCurrentlyOpen();
	outState->IsStreaming = ioHandler.IsCurrentlyRunning();
	outState->LastFrameInfo = ioHandler.LastFrameInfo;
	return NOS_RESULT_SUCCESS;
}

nosResult NOSAPI_CALL SetAutoSchedulingEnabled(uint32_t deviceIndex, nosDeckLinkChannel channel, nosBool isEnabled)
{
	DeviceLock lock(deviceIndex);
	auto* device = DeviceManager::Instance()->GetDevice(deviceIndex);
	if (!device)
	{
		nosEngine.LogE("No such device with index %d", deviceIndex);
		return NOS_RESULT_NOT_FOUND;
	}
	auto [subDevice, dir] = device->GetSubDeviceOfOpenChannel(channel);
	if (!subDevice)
	{
		nosEngine.LogE("No sub-device found open channel %s", GetChannelName(channel));
		return NOS_RESULT_NOT_FOUND;
	}
	if (dir != NOS_MEDIAIO_DIRECTION_OUTPUT)
	{
		nosEngine.LogE("Auto-schedule only meant for output channels");
		return NOS_RESULT_INVALID_ARGUMENT;
	}
	static_cast<OutputHandler*>(&subDevice->GetIO(dir))->AutoSchedulingEnabled = true;
	return NOS_RESULT_SUCCESS;
}

nosResult NOSAPI_CALL Export(uint32_t minorVersion, void** outSubsystemContext)
{
	auto it = GExportedSubsystemVersions.find(minorVersion);
	if (it != GExportedSubsystemVersions.end())
	{
		*outSubsystemContext = it->second;
		return NOS_RESULT_SUCCESS;
	}
	nosDeckLinkSubsystem* subsystem = new nosDeckLinkSubsystem();
	subsystem->GetDevices = GetDevices;
	subsystem->GetAvailableChannels = GetAvailableChannels;
	subsystem->GetChannelName = GetChannelName;
	subsystem->GetChannelFromName = GetChannelFromName;
	subsystem->GetDeviceByUniqueDisplayName = GetDeviceByUniqueDisplayName;
	subsystem->GetDeviceInfoByIndex = GetDeviceInfoByIndex;
	subsystem->GetSupportedOutputFrameGeometries = GetSupportedOutputFrameGeometries;
	subsystem->GetSupportedOutputFrameRatesForGeometry = GetSupportedOutputFrameRatesForGeometry;
	subsystem->GetSupportedOutputPixelFormats = GetSupportedOutputPixelFormats;
	subsystem->OpenChannel = OpenChannel;
	subsystem->CloseChannel = CloseChannel;
	subsystem->GetCurrentDeltaSecondsOfChannel = GetCurrentDeltaSecondsOfChannel;
	subsystem->WaitFrame = WaitFrame;
	subsystem->DMATransfer = DMATransfer;
	subsystem->RegisterInputVideoFormatChangeCallback = RegisterInputVideoFormatChangeCallback;
	subsystem->UnregisterInputVideoFormatChangeCallback = UnregisterInputVideoFormatChangeCallback;
	subsystem->StartStream = StartStream;
	subsystem->StopStream = StopStream;
	subsystem->ResetInputFrames = ResetInputFrames;
	subsystem->RegisterFrameResultCallback = RegisterFrameResultCallback;
	subsystem->UnregisterFrameResultCallback = UnregisterFrameResultCallback;
	subsystem->RegisterDeviceInvalidatedCallback = RegisterDeviceInvalidatedCallback;
	subsystem->UnregisterDeviceInvalidatedCallback = UnregisterDeviceInvalidatedCallback;
	subsystem->GetPortMappedChannelName = GetPortMappedChannelName;
	subsystem->GetChannelFromPortMappedName = GetChannelFromPortMappedName;
	subsystem->GetOutputReferenceStatus = GetOutputReferenceStatus;
	subsystem->RegisterDeviceStatusCallback = RegisterDeviceStatusCallback;
	subsystem->UnregisterDeviceStatusCallback = UnregisterDeviceStatusCallback;
	subsystem->GetChannelState = GetChannelState;
	subsystem->SetAutoSchedulingEnabled = SetAutoSchedulingEnabled;
	*outSubsystemContext = subsystem;
	GExportedSubsystemVersions[minorVersion] = subsystem;
	return NOS_RESULT_SUCCESS;
}

nosResult NOSAPI_CALL Initialize()
{
	// Get the settings
	std::filesystem::path relativeSettingsPath = "Config/Settings.json";
	auto settingsFilePath = std::filesystem::path(nosEngine.Module->RootFolderPath) / relativeSettingsPath;
	bool settingsLoaded = false;
	std::string messageString;
	nosModuleStatusMessage msg {
		.ModuleId = nosEngine.Module->Id,
		.UpdateType = NOS_MODULE_STATUS_MESSAGE_UPDATE_TYPE_APPEND,
		.MessageType = NOS_MODULE_STATUS_MESSAGE_TYPE_WARNING
	};
	if (!std::filesystem::exists(settingsFilePath))
	{
		messageString = "Settings file at " + settingsFilePath.string() + " not found. Using default settings.";
	}
	else
	{
		try
		{
			settingsFilePath = std::filesystem::canonical(settingsFilePath);
		}
		catch (std::exception& e)
		{
		}
		if (auto settingsBuffer = nos::GetAssetAsType(settingsFilePath.string().c_str(),
													  NOS_NAME(sys::decklink::Settings::GetFullyQualifiedName())))
		{
			DeviceManager::Instance()->LoadSettings(*settingsBuffer->As<sys::decklink::Settings>());
			settingsLoaded = true;
			msg.MessageType = NOS_MODULE_STATUS_MESSAGE_TYPE_INFO;
			messageString = "Using SDI port mappings from " + settingsFilePath.string();
		}
		else
			messageString =
				"Failed to load settings file at " + settingsFilePath.string() + ". Using default settings.";

	}
	msg.Message = messageString.c_str();
	nosEngine.SendModuleStatusMessageUpdate(&msg);
	if (!settingsLoaded)
	{
		DeviceManager::Instance()->LoadDefaultSettings();
	}
	DeviceManager::Instance()->InitializeDeviceList();
	return NOS_RESULT_SUCCESS;
}

extern "C"
{
NOSAPI_ATTR nosResult NOSAPI_CALL nosExportSubsystem(nosSubsystemFunctions* subsystemFunctions)
{
	subsystemFunctions->OnRequest = Export;
	subsystemFunctions->Initialize = Initialize;
	subsystemFunctions->OnPreUnloadSubsystem = UnloadSubsystem;
	return NOS_RESULT_SUCCESS;
}
}
}
