// Copyright MediaZ Teknoloji A.S. All Rights Reserved.
#include "Device.hpp"

// Nodos
#include <Nodos/Name.hpp>

#include "ChannelMapping.inl"
#include "DeviceManager.hpp"
#include "EnumConversions.hpp"
#include "SubDevice.hpp"

// External
#include <nosSysDevice/nosDeviceSubsystem.h>

namespace nos::decklink
{
std::string ProfileIDToString(BMDProfileID profileId)
{
	switch (profileId)
	{
	case bmdProfileOneSubDeviceFullDuplex:
		return "1 Sub-Device Full Duplex";
	case bmdProfileOneSubDeviceHalfDuplex:
		return "1 Sub-Device Half Duplex";
	case bmdProfileTwoSubDevicesFullDuplex:
		return "2 Sub-Devices Full Duplex";
	case bmdProfileTwoSubDevicesHalfDuplex:
		return "2 Sub-Devices Half Duplex";
	case bmdProfileFourSubDevicesHalfDuplex:
		return "4 Sub-Devices Half Duplex";
	default:
		return std::string("Unknown Profile (") + std::to_string(profileId) + ")";
	}
}

std::vector<std::unique_ptr<SubDevice>> CreateAllSubDevices()
{
	IDeckLinkIterator* deckLinkIterator = nullptr;

	HRESULT result = GetDeckLinkIterator(&deckLinkIterator);
	if (FAILED(result) || deckLinkIterator == nullptr)
	{
		nosEngine.LogE("Could not obtain DeckLink iterator");
		return {};
	}

	IDeckLink* deckLink = NULL;
	uint32_t   deviceNumber = 0;

	// Obtain an IDeckLink instance for each device on the system
	std::vector<std::unique_ptr<SubDevice>> subDevices;
	while (deckLinkIterator->Next(&deckLink) == S_OK)
	{
		auto bmDevice = std::make_unique<SubDevice>(deckLink);
		subDevices.push_back(std::move(bmDevice));
		deviceNumber++;
	}

	if (deckLinkIterator)
		deckLinkIterator->Release();

	return subDevices;
}

std::vector<std::unique_ptr<SubDevice>> CreateSubDevicesForDevice(uint32_t groupId)
{
	auto filtered = CreateAllSubDevices();
	for (auto it = filtered.begin(); it != filtered.end();)
		it = ((*it)->DeviceGroupId == groupId) ? std::next(it) : filtered.erase(it);
	return filtered;
}

std::vector<std::unique_ptr<class Device>> CreateDevices()
{
	auto subDevices = CreateAllSubDevices();

	std::unordered_map<int64_t, std::vector<std::unique_ptr<SubDevice>>> subDevicePerDevice;
	for (auto& subDevice : subDevices)
		subDevicePerDevice[subDevice->DeviceGroupId].push_back(std::move(subDevice));

	std::vector<std::unique_ptr<Device>> devices;
	uint32_t deviceIndex = 0;
	for (auto& [groupId, subDevices] : subDevicePerDevice)
	{
		auto device = std::make_unique<Device>(deviceIndex, std::move(subDevices));
		devices.push_back(std::move(device));
		++deviceIndex;
	}

	return devices;
}

class ProfileChangeCallback : public Object<IDeckLinkProfileCallback>
{
public:
	ProfileChangeCallback(uint32_t deviceIndex, int64_t deviceGroupId) :
		DeviceIndex(deviceIndex), DeviceGroupId(deviceGroupId)
	{
	}

	HRESULT	STDMETHODCALLTYPE ProfileChanging(IDeckLinkProfile* newProfile, dlbool_t streamsWillBeForcedToStop) override
	{
		nosEngine.ReloadPlugin();
		return S_OK;
	}
	HRESULT	STDMETHODCALLTYPE ProfileActivated(IDeckLinkProfile* newProfile) override
	{
		return S_OK;
	}

	uint32_t DeviceIndex;
	int64_t DeviceGroupId;
};

class NotificationCallback : public Object<IDeckLinkNotificationCallback>
{
public:
	friend class Device;
	NotificationCallback(Device* device) : DevicePtr(device), StatusInterface(device->StatusInterface.GetPtr())
	{
		StatusInterface->AddRef();
		ReadStatus(bmdDeckLinkStatusPCIExpressLinkWidth);
		ReadStatus(bmdDeckLinkStatusPCIExpressLinkSpeed);
		ReadStatus(bmdDeckLinkStatusReferenceSignalMode);
		ReadStatus(bmdDeckLinkStatusReferenceSignalLocked);
	}

	virtual ~NotificationCallback()
	{
		StatusInterface->Release();
	}

	void ReadStatus(BMDDeckLinkStatusID statusId)
	{
		bool updated = false;
		{
			std::unique_lock lock(StatusMutex);
			switch (statusId)
			{
			case bmdDeckLinkStatusDeviceTemperature:
				{
					StatusInterface->GetInt(statusId, &Status.Temperature);
					updated = true;
					break;
				}
			case bmdDeckLinkStatusReferenceSignalLocked:
				{
					BOOL referenceSignalLocked;
					auto res = StatusInterface->GetFlag(statusId, &referenceSignalLocked);
					if (res != S_OK)
					{
						nosEngine.LogE("NotificationCallback: Failed to get reference lock status");
						Status.ReferenceStatus = NOS_DECKLINK_REFERENCE_STATUS_UNKNOWN;
					}
					else if (referenceSignalLocked)
						Status.ReferenceStatus = NOS_DECKLINK_REFERENCE_STATUS_LOCKED;
					else
						Status.ReferenceStatus = NOS_DECKLINK_REFERENCE_STATUS_UNLOCKED;
					updated = true;
					break;
				}
			case bmdDeckLinkStatusReferenceSignalMode:
				{
					int64_t mode;
					auto res = StatusInterface->GetInt(statusId, &mode);
					if (res != S_OK)
						nosEngine.LogE("NotificationCallback: Failed to get reference signal mode");
					ReferenceSignalMode = (BMDDisplayMode)mode;
					updated = true;
					break;
				}
			case bmdDeckLinkStatusPCIExpressLinkWidth:
				{
					StatusInterface->GetInt(statusId, &Status.PCIeLink.Width);
					updated = true;
					break;
				}
			case bmdDeckLinkStatusPCIExpressLinkSpeed:
				{
					StatusInterface->GetInt(statusId, &Status.PCIeLink.Speed);
					updated = true;
					break;
				}
			default:
				{
					break;
				}
			}
		}
		if (updated)
			NotifyStatusChange();
	}

	// Implement the IDeckLinkNotificationCallback interface
	HRESULT STDMETHODCALLTYPE Notify(BMDNotifications topic, uint64_t param1, uint64_t param2)
	{
		// Check whether the notification we received is a status notification
		if (topic != bmdStatusChanged)
			return S_OK;

		// TODO: Add & call necessary callbacks for these status changes.
		BMDDeckLinkStatusID statusId = (BMDDeckLinkStatusID)param1;
		ReadStatus(statusId);

		return S_OK;
	}

	void NotifyStatusChange()
	{
		std::shared_lock lock(*DevicePtr->StatusCallbacksMutex);
		std::shared_lock statusLock(StatusMutex);
		for (auto& [callback, userData] : DevicePtr->DeviceCallbacks.Status.Map | std::views::values)
			callback(userData, &Status);
	}

	void CheckProfileSupportAndNotify(std::optional<BMDProfileID> const& currentProfile, std::unordered_set<std::optional<BMDProfileID>> const& supportedProfiles)
	{
		if (supportedProfiles.empty())
		{
			ProfileStatusMessageIndex = AddMessage("Device is not supported yet!", NOS_DECKLINK_DEVICE_MESSAGE_TYPE_ERROR);
		}
		else if (!supportedProfiles.contains(std::nullopt) && !supportedProfiles.contains(currentProfile))
		{
			std::stringstream ss;
			ss << "Active device profile is not supported yet!\n";
			if (currentProfile)
			{
				ss << "\tCurrent Profile: " << ProfileIDToString(*currentProfile) << "\n";
			}
			ss << "\tSupported Profiles:\n";
			for (auto& profileId : supportedProfiles)
			{
				if (profileId)
				{
					ss << "\t\t" << ProfileIDToString(*profileId) << "\n";
				}
			}
			ss << "Configure connector mapping from BlackMagic DeckLink software.";
			ProfileStatusMessageIndex = AddMessage(ss.str(), NOS_DECKLINK_DEVICE_MESSAGE_TYPE_ERROR);
		}
		else if (ProfileStatusMessageIndex != -1)
		{
			RemoveMessage(ProfileStatusMessageIndex);
		}
		NotifyStatusChange();
	}

	void CheckApiVersionAndNotify()
	{
		auto& apiVersion = DeviceManager::Instance()->ApiVersion;
		if (ApiVersionStatusMessageIndex != -1)
		{
			RemoveMessage(ApiVersionStatusMessageIndex);
			ApiVersionStatusMessageIndex = -1;
		}
		if (!apiVersion)
		{
			ApiVersionStatusMessageIndex = AddMessage("Driver API version could not be read, please install version " NOS_DECKLINK_USED_DRIVER_API_VERSION "!", NOS_DECKLINK_DEVICE_MESSAGE_TYPE_ERROR);
		}
		else
		{
			bool incompatible = (*apiVersion)[0] != NOS_DECKLINK_USED_DRIVER_API_VERSION_MAJOR || (*apiVersion)[1] < NOS_DECKLINK_USED_DRIVER_API_VERSION_MINOR;
			bool warn = (*apiVersion)[0] == NOS_DECKLINK_USED_DRIVER_API_VERSION_MAJOR && (*apiVersion)[1] > NOS_DECKLINK_USED_DRIVER_API_VERSION_MINOR;
			if (incompatible)
			{
				std::stringstream errorMsg;
				errorMsg << "Your driver version " << (*apiVersion)[0] << "." << (*apiVersion)[1] << " is incompatible, please install version " NOS_DECKLINK_USED_DRIVER_API_VERSION "!";
				ApiVersionStatusMessageIndex = AddMessage(errorMsg.str(), NOS_DECKLINK_DEVICE_MESSAGE_TYPE_ERROR);
			}
			else if (warn)
			{
				std::stringstream warnMsg;
				warnMsg << "Your driver version " << (*apiVersion)[0] << "." << (*apiVersion)[1] << " has not been tested, you could install version " NOS_DECKLINK_USED_DRIVER_API_VERSION;
				ApiVersionStatusMessageIndex = AddMessage(warnMsg.str(), NOS_DECKLINK_DEVICE_MESSAGE_TYPE_WARNING);
			}
		}
		NotifyStatusChange();
	}

protected:
	int AddMessage(std::string const& message, nosDeckLinkDeviceMessageType type)
	{
		std::unique_lock lock(StatusMutex);
		auto currentCount = Status.Messages.Count;
		if (currentCount >= std::size(Status.Messages.List))
		{
			nosEngine.LogE("NotificationCallback: Message list is full");
			return -1;
		}
		auto& messageList = Status.Messages.List[currentCount];
		strncpy(messageList.Message, message.c_str(), std::size(messageList.Message));
		messageList.Type = type;
		Status.Messages.Count = currentCount + 1;
		return currentCount;
	}

	void RemoveMessage(int index)
	{
		std::unique_lock lock(StatusMutex);
		if (index < 0 || index >= Status.Messages.Count)
		{
			nosEngine.LogE("NotificationCallback: Invalid message index to remove: %d", index);
			return;
		}
		auto& messageList = Status.Messages.List;
		for (int i = index; i < Status.Messages.Count - 1; ++i)
			messageList[i] = messageList[i + 1];
		Status.Messages.Count -= 1;
	}
	
	Device* DevicePtr;
	IDeckLinkStatus* StatusInterface;
	BMDDisplayMode ReferenceSignalMode = bmdModeUnknown;
	nosDeckLinkDeviceStatus Status{};
	std::shared_mutex StatusMutex;
	int ProfileStatusMessageIndex = -1;
	int ApiVersionStatusMessageIndex = -1;
};
	
std::vector<std::unique_ptr<class Device>> CreateDevices(std::optional<uint32_t> optGroupId)
{
	IDeckLinkIterator* deckLinkIterator = nullptr;

	HRESULT result = GetDeckLinkIterator(&deckLinkIterator);
	if (FAILED(result) || deckLinkIterator == nullptr)
	{
		nosEngine.LogE("Could not obtain DeckLink iterator");
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
		auto device = std::make_unique<Device>(deviceIndex, std::move(subDevices));
		devices.push_back(std::move(device));
		++deviceIndex;
	}

	return devices;
}

void Device::SetupFromMainSubDevice()
{
	auto mainSubDevice = GetMainSubDevice();
	ModelName = mainSubDevice->ModelName;
	GroupId = mainSubDevice->DeviceGroupId;
}

Device::Device(uint32_t index, std::vector<std::unique_ptr<SubDevice>>&& subDevices)
	: Index(index)
	, SubDevices(std::move(subDevices))
	, InvalidatedCallbacksMutex(new std::shared_mutex)
	, StatusCallbacksMutex(new std::shared_mutex)
{
	if (SubDevices.empty())
		nosEngine.LogE("No sub-device provided for device index: %d", index);
	else
	{
		SetupFromMainSubDevice();
	}

	for (auto& subDevice : SubDevices)
		subDevice->TagDevice(Index);

	auto [profile, deviceInterface] = GetActiveProfileAndInterface();
	ActiveProfile = profile;
	DeviceInterfaceType = deviceInterface;

	// Determine which sub-devices are capable of opening the channel
	PrepareChannelSubDeviceMap();

	if (auto profileManager = GetProfileManager())
	{
		auto onProfileChange = new ProfileChangeCallback(Index, GroupId);
		if (onProfileChange == nullptr)
			nosEngine.LogE("Could not create profile change callback");
		else
			profileManager->SetCallback(onProfileChange);
		Release(onProfileChange);
	}

	SetupNotifications();

	RegisterDevice();

	SwitchToSupportedProfile();
}

void Device::Destroy()
{
	nosDevice->UnregisterDevice(Id);
	Id = 0;
	IDeckLink* mainDlDevice = nullptr;
	if (auto mainSubDevice = GetMainSubDevice())
		mainDlDevice = mainSubDevice->DLDevice;
	ClearSubDevices();
	RemoveNotifications();
	Release(mainDlDevice);
	InvalidatedCallbacksMutex.reset();
	StatusCallbacksMutex.reset();
}

void Device::SetupNotifications()
{
	auto mainSubDevice = GetMainSubDevice();
	IDeckLinkStatus* statI = nullptr;
	auto res = mainSubDevice->DLDevice->QueryInterface(IID_IDeckLinkStatus, (void**)&statI);
	StatusInterface = statI;
	if (res != S_OK)
		nosEngine.LogE("Failed to get status interface for device: %s", ModelName.c_str());
	IDeckLinkNotification* notifI = nullptr;
	res = mainSubDevice->DLDevice->QueryInterface(IID_IDeckLinkNotification, (void**)&notifI);
	NotificationInterface = notifI;
	if (res != S_OK)
		nosEngine.LogE("Failed to get notification interface for device: %s", ModelName.c_str());
	if (StatusInterface && NotificationInterface)
	{
		NotifCallback = new NotificationCallback(this);
		if (!NotifCallback)
			nosEngine.LogE("Failed to create notification callback for device: %s", ModelName.c_str());
		else
		{
			res = NotificationInterface->Subscribe(bmdStatusChanged, NotifCallback.GetPtr());
			if (res != S_OK)
				nosEngine.LogE("Failed to subscribe to status change for device: %s", ModelName.c_str());
			NotifCallback->CheckApiVersionAndNotify();
			NotifCallback->CheckProfileSupportAndNotify(ActiveProfile, SupportedProfiles);
		}
	}
}

void Device::RemoveNotifications()
{
	if (NotificationInterface && NotifCallback)
	{
		auto res = NotificationInterface->Unsubscribe(bmdStatusChanged, NotifCallback.GetPtr());
		if (res != S_OK)
			nosEngine.LogE("Failed to unsubscribe from status change for device: %s", ModelName.c_str());
	}
	Release(NotifCallback);
	Release(StatusInterface);
	Release(NotificationInterface);
}

std::string Device::GetUniqueDisplayName() const
{
	return ModelName + " - " + std::to_string(Index);
}

std::vector<nosDeckLinkChannel> Device::GetAvailableChannels(nosMediaIODirection mode)
{
	std::vector<nosDeckLinkChannel> channels;
	static std::vector<nosDeckLinkChannel> allChannels = {};
	if (allChannels.empty())
	{
		for (int i = NOS_DECKLINK_CHANNEL_MIN; i <= NOS_DECKLINK_CHANNEL_MAX; ++i)
			allChannels.push_back(nosDeckLinkChannel(i));
	}
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
	if (dit == Channel2SubDevice.end())
		return false;
	auto cit = dit->second.find(channel);

	if (cit == dit->second.end())
		return false;
		
	auto& [subDeviceIndex, subDevice] = cit->second;
	auto& chMap = GetChannelMap();
	auto modelIt = chMap.find(ModelName);
	if (modelIt == chMap.end())
		return false;
	auto profileIt = modelIt->second.find(ActiveProfile);
	if (profileIt == modelIt->second.end())
		if (modelIt->second.find(std::nullopt) == modelIt->second.end())
			// No profile support for this device
			return false;
		else
			profileIt = modelIt->second.begin();
	auto subDeviceIt = profileIt->second.find(subDeviceIndex);
	if (subDeviceIt == profileIt->second.end())
		return false;

	std::unordered_set<nosMediaIODirection> supportedDirections; // For this channel and subdevice
	auto channelIt = subDeviceIt->second.find(channel);
	if (channelIt == subDeviceIt->second.end())
		return false;

	for (auto& dirForCh : channelIt->second)
		supportedDirections.insert(dirForCh);

	if (supportedDirections.find(dir) == supportedDirections.end())
		return false;

	// Now, check subdevice is busy with our direction.
	// TODO: Full-duplexity?
	if (subDevice->IsBusyWith(dir))
		return false;

	if (outSubDevice)
		*outSubDevice = subDevice;
	return true;
}

SubDevice* Device::GetSubDeviceOfChannel(nosMediaIODirection dir, nosDeckLinkChannel channel) const
{
	auto dit = Channel2SubDevice.find(dir);
	if (dit != Channel2SubDevice.end())
	{
		auto cit = dit->second.find(channel);
		if (cit != dit->second.end())
			return cit->second.second;
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

SubDevice* Device::GetMainSubDevice() const
{
	return GetSubDevice(0);
}

Nullable<IDeckLinkProfileManager> Device::GetProfileManager() const
{
	if (SubDevices.empty())
		return nullptr;
	return SubDevices[0]->ProfileManager;
}

std::pair<std::optional<BMDProfileID>, std::optional<BMDDeviceInterface>> GetProfileIdAndDeviceInterface(IDeckLinkProfileAttributes* profileAttributes)
{
	int64_t profileId{};
	std::pair<std::optional<BMDProfileID>, std::optional<BMDDeviceInterface>> result;
	if (profileAttributes->GetInt(BMDDeckLinkProfileID, &profileId) == S_OK)
		result.first = BMDProfileID(profileId);
	int64_t deviceInterface{};
	if (profileAttributes->GetInt(BMDDeckLinkDeviceInterface, &deviceInterface) == S_OK)
		result.second = BMDDeviceInterface(deviceInterface);
	return result;
}

std::pair<std::optional<BMDProfileID>, std::optional<BMDDeviceInterface>> Device::GetActiveProfileAndInterface() const
{
	if (SubDevices.empty())
		return {std::nullopt, std::nullopt};
	auto mainSubDevice = GetMainSubDevice();
	if (!mainSubDevice)
		return {std::nullopt, std::nullopt};
	if (!mainSubDevice->ProfileManager)
		return GetProfileIdAndDeviceInterface(mainSubDevice->ProfileAttributes);
	IDeckLinkProfileIterator* profileIterator = nullptr;
	auto res = mainSubDevice->ProfileManager->GetProfiles(&profileIterator);
	if (res != S_OK)
	{
		nosEngine.LogE("Failed to get profile iterator for device: %s", ModelName.c_str());
		return {std::nullopt, std::nullopt};
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
				auto result = GetProfileIdAndDeviceInterface(profileAttributes);
				Release(profileAttributes);
				Release(profile);
				Release(profileIterator);
				return result;
			}
		}
		Release(profile);
	}
	Release(profileIterator);
	return {std::nullopt, std::nullopt};
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
		nosEngine.LogE("Failed to get profile iterator for device: %s", ModelName.c_str());
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
						nosEngine.LogE("Failed to set profile for device: %s", ModelName.c_str());
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
		subDevice->TagChannel(NOS_MEDIAIO_DIRECTION_OUTPUT, channel);
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
	subDevice->TagChannel(NOS_MEDIAIO_DIRECTION_OUTPUT, NOS_DECKLINK_CHANNEL_INVALID);
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

bool Device::WaitFrame(nosDeckLinkChannel channel, std::chrono::milliseconds timeout)
{
	auto it = OpenChannels.find(channel);
	if (it == OpenChannels.end())
	{
		nosEngine.LogE("No open channel found for channel %s", GetChannelName(channel));
		return false;
	}
	auto [subDevice, mode] = it->second;
	return subDevice->WaitFrame(mode, timeout);
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
		subDevice->TagChannel(NOS_MEDIAIO_DIRECTION_INPUT, channel);
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
	auto* mainSubDevice = GetMainSubDevice();
	for (auto& subDevice : SubDevices)
	{
		if (mainSubDevice->DLDevice == subDevice->DLDevice)
			// Main sub-device will be released later to avoid deadlock during profile change callback. 
			continue;
		siblings.push_back(subDevice->DLDevice);
	}
	SubDevices.clear();
	for (auto sibling : siblings)
		Release(sibling);
	{
		std::unique_lock lock(*InvalidatedCallbacksMutex);
		for (auto& [callbackId, pair] : DeviceCallbacks.Invalidated)
		{
			auto& [callback, userData] = pair;
			callback(userData);
		}
	}
}

int32_t Device::AddDeviceInvalidatedCallback(nosDeckLinkDeviceInvalidatedCallback callback, void* userData)
{
	std::unique_lock lock(*InvalidatedCallbacksMutex);
	return DeviceCallbacks.Invalidated.Add(callback, userData);
}

void Device::RemoveDeviceInvalidatedCallback(int32_t callbackId)
{
	std::unique_lock lock(*InvalidatedCallbacksMutex);
	DeviceCallbacks.Invalidated.Remove(callbackId);
}

int32_t Device::AddDeviceStatusCallback(nosDeckLinkDeviceStatusCallback callback, void* user_data)
{
	int32_t id;
	{
		std::unique_lock lock(*StatusCallbacksMutex);
		id = DeviceCallbacks.Status.Add(callback, user_data);
	}
	if (NotifCallback)
		NotifCallback->NotifyStatusChange();
	else
		nosEngine.LogE("Notification callback is not available for device: %s", ModelName.c_str());
	return id;
}

void Device::RemoveDeviceStatusCallback(int32_t callbackId)
{
	std::unique_lock lock(*StatusCallbacksMutex);
	DeviceCallbacks.Status.Remove(callbackId);
}

void Device::RegisterDevice()
{
	uint32_t deviceFlags = NOS_DEVICE_FLAG_VIDEO_IO;
	if (DeviceInterfaceType && *DeviceInterfaceType == bmdDeviceInterfacePCI)
		deviceFlags |= NOS_DEVICE_FLAG_PCI;
	std::string serialNumber = std::to_string(GetMainSubDevice()->PersistentId);
	nosRegisterDeviceParams params = {
		.Device = {
			.VendorName = NOS_NAME(NOS_DECKLINK_VENDOR_NAME),
			.ModelName = nos::Name(ModelName),
			.TopologicalId = uint64_t(GetMainSubDevice()->TopologicalId),
			.SerialNumber = nos::Name(serialNumber),
			.Flags = nosDeviceFlags(deviceFlags),
		},
		.DisplayName = nos::Name(ModelName),
		.Handle = Index
	};
	auto res = nosDevice->RegisterDevice(&params, &Id);
	if (res != NOS_RESULT_SUCCESS)
		nosEngine.LogE("Failed to register device: %s", ModelName.c_str());
}

void Device::PrepareChannelSubDeviceMap()
{
	auto& channelMap = GetChannelMap();
	auto modelIt = channelMap.find(ModelName);
	if (modelIt == channelMap.end())
	{
		nosEngine.LogE("No channel map found for device: %s", ModelName.c_str());
		return;
	}
	auto& mapping = modelIt->second;

	SupportedProfiles.clear();
	for (auto& [profile, rest2] : mapping)
	{
		SupportedProfiles.insert(profile);
		if (profile && ActiveProfile != profile)
			continue;
		for (auto& [subDeviceIndex, rest3] : rest2)
		{
			for (auto& [curChannel, modes] : rest3)
			{
				if (auto subDevice = GetSubDevice(subDeviceIndex))
				{
					for (auto mode : modes)
					{
						Channel2SubDevice[mode][curChannel] = {subDeviceIndex, subDevice};
					}
				}
			}
		}
	}
}

void Device::SwitchToSupportedProfile()
{
	if (SupportedProfiles.empty())
		return;
	if (ActiveProfile && SupportedProfiles.contains(ActiveProfile))
		return;
	auto& firstProfile = *SupportedProfiles.begin();
	if (firstProfile)
	{
		nosEngine.LogI("Switching to supported profile: %s for device: %s", ProfileIDToString(*firstProfile).c_str(), ModelName.c_str());
		UpdateProfile(*firstProfile);
	}
}

}
