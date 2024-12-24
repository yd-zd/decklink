// Copyright MediaZ Teknoloji A.S. All Rights Reserved.
#include "Device.hpp"

// Nodos
#include <Nodos/Modules.h>
#include <nosUtil/Stopwatch.hpp>

#include "ChannelMapping.inl"
#include "DeviceManager.hpp"
#include "EnumConversions.hpp"
#include "SubDevice.hpp"

namespace nos::decklink
{

class ProfileChangeCallback : public Object<IDeckLinkProfileCallback>
{
public:
	ProfileChangeCallback(uint32_t deviceIndex, int64_t deviceGroupId) :
		DeviceIndex(deviceIndex), DeviceGroupId(deviceGroupId)
	{
	}

	HRESULT	STDMETHODCALLTYPE ProfileChanging(IDeckLinkProfile* newProfile, dlbool_t streamsWillBeForcedToStop) override
	{
		DeviceLock lock(DeviceIndex, false);
		if (auto device = DeviceManager::Instance()->GetDevice(DeviceIndex))
			device->ClearSubDevices();
		return S_OK;
	}
	HRESULT	STDMETHODCALLTYPE ProfileActivated(IDeckLinkProfile* newProfile) override
	{
		DeviceLock lock(DeviceIndex, false);
		if (auto device = DeviceManager::Instance()->GetDevice(DeviceIndex))
			device->Reinit(DeviceGroupId);
		return S_OK;
	}

	uint32_t DeviceIndex;
	int64_t DeviceGroupId;
};

std::vector<std::unique_ptr<class Device>> CreateDevices(std::optional<uint32_t> optGroupId)
{
	IDeckLinkIterator* deckLinkIterator = nullptr;

	HRESULT result = E_FAIL;
#ifdef _WIN32
	result = CoInitialize(NULL);
	if (FAILED(result))
	{
		nosEngine.LogE("DeckLinkDevice: Initialization of COM failed with error: %s", _com_error(result).ErrorMessage());
		return {};
	}
#endif

	result = GetDeckLinkIterator(&deckLinkIterator);
	if (FAILED(result) || deckLinkIterator == nullptr)
	{
		nosEngine.LogE("DeckLinkDevice: Could not obtain DeckLink iterator");
		return {};
	}

	IDeckLink* deckLink = NULL;
	uint32_t   deviceNumber = 0;

	// Obtain an IDeckLink instance for each device on the system
	std::vector<std::unique_ptr<SubDevice>> subDevices;
	while (deckLinkIterator->Next(&deckLink) == S_OK)
	{
		auto bmDevice = std::make_unique<SubDevice>(deckLink);
		if (optGroupId && bmDevice->DeviceGroupId != *optGroupId)
		{
			bmDevice.reset();
			Release(deckLink);
			continue;
		}
		subDevices.push_back(std::move(bmDevice));
		deviceNumber++;
	}

	if (deckLinkIterator)
		deckLinkIterator->Release();

	std::unordered_map<int64_t, std::vector<std::unique_ptr<SubDevice>>> subDevicePerDevice;
	for (auto& subDevice : subDevices)
		subDevicePerDevice[subDevice->DeviceGroupId].push_back(std::move(subDevice));

	std::vector<std::unique_ptr<Device>> devices;
	uint32_t deviceIndex = 0;
	for (auto& [groupId, subDevices] : subDevicePerDevice)
	{
		devices.push_back(std::make_unique<Device>(deviceIndex, std::move(subDevices)));
		++deviceIndex;
	}

	return devices;
}

Device::Device(uint32_t index, std::vector<std::unique_ptr<SubDevice>>&& subDevices)
	: Index(index), SubDevices(std::move(subDevices)), DeviceInvalidatedCallbacksMutex(new std::mutex)
{
	if (SubDevices.empty())
		nosEngine.LogE("No sub-device provided for device index: %d", index);
	else
	{
		ModelName = SubDevices[0]->ModelName;
		GroupId = SubDevices[0]->DeviceGroupId;
	}

	for (auto& subDevice : SubDevices)
		subDevice->TagDevice(Index);
	
	// Determine which sub-devices are capable of opening the channel
	auto& channelMap = GetChannelMap();
	auto modelIt = channelMap.find(ModelName);
	if (modelIt == channelMap.end())
	{
		nosEngine.LogE("No channel map found for device: %s", ModelName.c_str());
		return;
	}
	auto& mapping = modelIt->second;
	for (auto& [profile, rest2] : mapping)
	{
		if (profile != bmdProfileFourSubDevicesHalfDuplex)
			continue; // TODO.
		for (auto& [subDeviceIndex, rest3] : rest2)
		{
			for (auto& [curChannel, modes] : rest3)
			{
				if (auto subDevice = GetSubDevice(subDeviceIndex))
				{
					for (auto mode : modes)
					{
						Channel2SubDevice[mode][curChannel] = subDevice;
						subDevice->TagChannel(mode, curChannel);
					}
				}
			}
		}
	}

	auto onProfileChange = new ProfileChangeCallback(Index, GroupId);
	if (onProfileChange == nullptr)
		nosEngine.LogE("Could not create profile change callback");
	else
		GetProfileManager()->SetCallback(onProfileChange);
	Release(onProfileChange);
}

void Device::Reinit(uint32_t groupId)
{
	{
		IDeckLink* dlDevice = nullptr;
		if (auto mainSubDevice = GetSubDevice(0))
			dlDevice = mainSubDevice->DLDevice;
		ClearSubDevices();
		Release(dlDevice);
	}
	auto devices = CreateDevices(groupId);
	if (devices.empty())
	{
		nosEngine.LogE("DeckLinkDevice: Failed to reinitialize device with index: %d", Index);
		return;
	}
	if (devices.size() > 1)
	{
		nosEngine.LogE("DeckLinkDevice: Reinitialized device with index: %d, but found more than one device with same group ID!", Index);
	}
	*this = std::move(*devices[0]);
}

std::string Device::GetUniqueDisplayName() const
{
	return ModelName + " - " + std::to_string(Index);
}

std::vector<nosDeckLinkChannel> Device::GetAvailableChannels(nosMediaIODirection mode)
{
	std::vector<nosDeckLinkChannel> channels;
	static std::array allChannels {
		NOS_DECKLINK_CHANNEL_SINGLE_LINK_1,
		NOS_DECKLINK_CHANNEL_SINGLE_LINK_2,
		NOS_DECKLINK_CHANNEL_SINGLE_LINK_3,
		NOS_DECKLINK_CHANNEL_SINGLE_LINK_4,
		NOS_DECKLINK_CHANNEL_SINGLE_LINK_5,
		NOS_DECKLINK_CHANNEL_SINGLE_LINK_6,
		NOS_DECKLINK_CHANNEL_SINGLE_LINK_7,
		NOS_DECKLINK_CHANNEL_SINGLE_LINK_8
	};
	for (auto& channel : allChannels)
	{
		if (CanOpenChannel(mode, channel))
			channels.push_back(channel);
	}
	return channels;
}

bool Device::CanOpenChannel(nosMediaIODirection dir, nosDeckLinkChannel channel, SubDevice** outSubDevice) const
{
	auto dit = Channel2SubDevice.find(dir);
	if (dit != Channel2SubDevice.end())
	{
		auto cit = dit->second.find(channel);
		if (cit != dit->second.end())
		{
			auto* subDevice = cit->second;
			if (!subDevice->IsBusy())
			{
				if (outSubDevice)
					*outSubDevice = subDevice;
				return true;
			}
		}
	}
	return false;
}

SubDevice* Device::GetSubDeviceOfChannel(nosMediaIODirection dir, nosDeckLinkChannel channel) const
{
	auto dit = Channel2SubDevice.find(dir);
	if (dit != Channel2SubDevice.end())
	{
		auto cit = dit->second.find(channel);
		if (cit != dit->second.end())
			return cit->second;
	}
	return nullptr;
}

std::pair<SubDevice*, nosMediaIODirection> Device::GetSubDeviceOfOpenChannel(nosDeckLinkChannel channel) const
{
	auto it = OpenChannels.find(channel);
	if (it != OpenChannels.end())
		return it->second;
	return {nullptr, {}};
}

SubDevice* Device::GetSubDevice(int64_t index) const
{
	if (index < SubDevices.size())
		return SubDevices[index].get();
	return nullptr;
}

IDeckLinkProfileManager* Device::GetProfileManager() const
{
	if (SubDevices.empty())
		return nullptr;
	return SubDevices[0]->ProfileManager;
}

std::optional<BMDProfileID> Device::GetActiveProfile() const
{
	if (SubDevices.empty())
		return std::nullopt;
	auto& subDevice = SubDevices[0];
	IDeckLinkProfileIterator* profileIterator = nullptr;
	auto res = subDevice->ProfileManager->GetProfiles(&profileIterator);
	if (res != S_OK)
	{
		nosEngine.LogE("DeckLinkDevice: Failed to get profile iterator for device: %s", ModelName.c_str());
		return std::nullopt;
	}
	IDeckLinkProfile* profile = nullptr;
	while (profileIterator->Next(&profile) == S_OK)
	{
		BOOL active = false;
		if (profile->IsActive(&active) == S_OK && active)
		{
			IDeckLinkProfileAttributes* profileAttributes = nullptr;
			if (profile->QueryInterface(IID_IDeckLinkProfileAttributes, (void**)&profileAttributes) == S_OK)
			{
				int64_t profileId{};
				if (profileAttributes->GetInt(BMDDeckLinkProfileID, &profileId) == S_OK)
				{
					Release(profileAttributes);
					Release(profileIterator);
					return BMDProfileID(profileId);
				}
				Release(profileAttributes);
			}
		}
		Release(profile);
	}
	return std::nullopt;
}

void Device::UpdateProfile(BMDProfileID newProfileId)
{
	if (SubDevices.empty())
		return;
	auto& subDevice = SubDevices[0];
	IDeckLinkProfileIterator* profileIter = nullptr;
	auto res = subDevice->ProfileManager->GetProfiles(&profileIter);
	if (res != S_OK || !profileIter)
	{
		nosEngine.LogE("DeckLinkDevice: Failed to get profile iterator for device: %s", ModelName.c_str());
		return;
	}
	IDeckLinkProfile* profile = nullptr;
	while (profileIter->Next(&profile) == S_OK)
	{
		bool exit = false;
		IDeckLinkProfileAttributes* profileAttr = nullptr;
		if (profile->QueryInterface(IID_IDeckLinkProfileAttributes, (void**)&profileAttr) == S_OK)
		{
			int64_t profileId{};
			if (profileAttr->GetInt(BMDDeckLinkProfileID, &profileId) == S_OK)
			{
				if (profileId == newProfileId)
				{
					exit = true;
					if (profile->SetActive() != S_OK)
						nosEngine.LogE("DeckLinkDevice: Failed to set profile for device: %s", ModelName.c_str());
				}
			}
			Release(profileAttr);
		}
		Release(profile);
		if (exit)
			break;
	}
	Release(profileIter);
}

bool Device::OpenOutput(nosDeckLinkChannel channel, BMDDisplayMode displayMode, BMDPixelFormat pixelFormat)
{
	auto subDevice = GetSubDeviceOfChannel(NOS_MEDIAIO_DIRECTION_OUTPUT, channel);
	if (!subDevice)
	{
		nosEngine.LogE("No sub-device found for channel %s", GetChannelName(channel));
		return false;
	}
	if (subDevice->OpenOutput(displayMode, pixelFormat))
	{
		OpenChannels[channel] = { subDevice, NOS_MEDIAIO_DIRECTION_OUTPUT };
		return true;
	}
	return false;
}

bool Device::CloseChannel(nosDeckLinkChannel channel)
{
	auto it = OpenChannels.find(channel);
	if (it == OpenChannels.end())
	{
		nosEngine.LogE("No open channel found for channel %s", GetChannelName(channel));
		return false;
	}
	auto [subDevice, mode] = it->second;
	if (mode == NOS_MEDIAIO_DIRECTION_INPUT)
	{
		if (!subDevice->CloseInput())
			return false;
	}
	else
	{
		if (!subDevice->CloseOutput())
			return false;
	}
	OpenChannels.erase(it);
	return true;
}

std::optional<nosVec2u> Device::GetCurrentDeltaSecondsOfChannel(nosDeckLinkChannel channel)
{
	auto it = OpenChannels.find(channel);
	if (it == OpenChannels.end())
	{
		nosEngine.LogE("No open channel found for channel %s", GetChannelName(channel));
		return std::nullopt;
	}
	auto [subDevice, mode] = it->second;
	return subDevice->GetDeltaSeconds(mode);
}

bool Device::WaitFrame(nosDeckLinkChannel channel, std::chrono::milliseconds timeout, nosMediaIOInterlacedFieldType optFieldType)
{
	auto it = OpenChannels.find(channel);
	if (it == OpenChannels.end())
	{
		nosEngine.LogE("No open channel found for channel %s", GetChannelName(channel));
		return false;
	}
	auto [subDevice, mode] = it->second;
	return subDevice->WaitFrame(mode, timeout, optFieldType);
}

bool Device::DmaTransfer(nosDeckLinkChannel channel, void* buffer, size_t size)
{
	auto it = OpenChannels.find(channel);
	if (it == OpenChannels.end())
	{
		nosEngine.LogE("No open channel found for channel %s", GetChannelName(channel));
		return false;
	}
	auto [subDevice, mode] = it->second;
	subDevice->DmaTransfer(mode, buffer, size);
	return true;
}

bool Device::OpenInput(nosDeckLinkChannel channel, BMDPixelFormat pixelFormat)
{
	auto subDevice = GetSubDeviceOfChannel(NOS_MEDIAIO_DIRECTION_INPUT, channel);
	if (!subDevice)
	{
		nosEngine.LogE("No sub-device found for channel %s", GetChannelName(channel));
		return false;
	}
	if (subDevice->OpenInput(pixelFormat))
	{
		OpenChannels[channel] = { subDevice, NOS_MEDIAIO_DIRECTION_INPUT };
		return true;
	}
	return false;
}

bool Device::StartStream(nosDeckLinkChannel channel)
{
	auto it = OpenChannels.find(channel);
	if (it == OpenChannels.end())
	{
		nosEngine.LogE("No open channel found for channel %s", GetChannelName(channel));
		return false;
	}
	auto [subDevice, mode] = it->second;
	return subDevice->StartStream(mode);
}

bool Device::StopStream(nosDeckLinkChannel channel)
{
	auto it = OpenChannels.find(channel);
	if (it == OpenChannels.end())
	{
		nosEngine.LogE("No open channel found for channel %s", GetChannelName(channel));
		return false;
	}
	auto [subDevice, mode] = it->second;
	return subDevice->StopStream(mode);
}

bool Device::ResetInputFrames(nosDeckLinkChannel channel)
{
	auto it = OpenChannels.find(channel);
	if (it == OpenChannels.end())
	{
		nosEngine.LogE("No open channel found for channel %s", GetChannelName(channel));
		return false;
	}
	auto [subDevice, mode] = it->second;
	return subDevice->ResetInputFrames();
}

void Device::ClearSubDevices()
{
	Channel2SubDevice.clear();
	OpenChannels.clear();
	std::vector<IDeckLink*> siblings;
	auto* mainSubDevice = GetSubDevice(0);
	for (auto& subDevice : SubDevices)
	{
		if (mainSubDevice && mainSubDevice->DLDevice == subDevice->DLDevice)
			// Main sub-device will be released later to avoid deadlock during profile change callback. 
			continue;
		siblings.push_back(subDevice->DLDevice);
	}
	SubDevices.clear();
	for (auto sibling : siblings)
		Release(sibling);
	{
		std::unique_lock lock(*DeviceInvalidatedCallbacksMutex);
		for (auto& [callbackId, pair] : DeviceInvalidatedCallbacks)
		{
			auto& [callback, userData] = pair;
			callback(userData);
		}
	}
}

int32_t Device::AddDeviceInvalidatedCallback(nosDeckLinkDeviceInvalidatedCallback callback, void* userData)
{
	std::unique_lock lock(*DeviceInvalidatedCallbacksMutex);
	DeviceInvalidatedCallbacks[NextDeviceInvalidatedCallbackId] = { callback, userData };
	return NextDeviceInvalidatedCallbackId++;
}

void Device::RemoveDeviceInvalidatedCallback(int32_t callbackId)
{
	std::unique_lock lock(*DeviceInvalidatedCallbacksMutex);
	DeviceInvalidatedCallbacks.erase(callbackId);
}
}
