/*
 * Copyright MediaZ Teknoloji A.S. All Rights Reserved.
 */

#ifndef NOS_DECKLINK_DEVICE_SUBSYSTEM_H_INCLUDED
#define NOS_DECKLINK_DEVICE_SUBSYSTEM_H_INCLUDED

#if __cplusplus
extern "C"
{
#endif

#include <Nodos/Types.h>

// nos.sys.mediaio
#include <nosMediaIO/nosMediaIO.h>

typedef struct nosDeckLinkDeviceDesc {
	uint32_t DeviceIndex;
	char UniqueDisplayName[256];
} nosDeckLinkDeviceDesc;

typedef struct nosDeckLinkDeviceInfo
{
	nosDeckLinkDeviceDesc Desc;
	char ModelName[256];
} nosDeckLinkDeviceInfo;
	
typedef enum nosDeckLinkChannel
{
	NOS_DECKLINK_CHANNEL_INVALID,
	NOS_DECKLINK_CHANNEL_MIN = NOS_DECKLINK_CHANNEL_INVALID,
	NOS_DECKLINK_CHANNEL_SINGLE_LINK_1,
	NOS_DECKLINK_CHANNEL_SINGLE_LINK_2,
	NOS_DECKLINK_CHANNEL_SINGLE_LINK_3,
	NOS_DECKLINK_CHANNEL_SINGLE_LINK_4,
	NOS_DECKLINK_CHANNEL_SINGLE_LINK_5,
	NOS_DECKLINK_CHANNEL_SINGLE_LINK_6,
	NOS_DECKLINK_CHANNEL_SINGLE_LINK_7,
	NOS_DECKLINK_CHANNEL_SINGLE_LINK_8,
	NOS_DECKLINK_CHANNEL_MAX = NOS_DECKLINK_CHANNEL_SINGLE_LINK_8
} nosDeckLinkChannel;

#define NOS_DECKLINK_CHANNEL_COUNT (NOS_DECKLINK_CHANNEL_SINGLE_LINK_8 - NOS_DECKLINK_CHANNEL_INVALID)

inline const char* NOS_DECKLINK_CHANNEL_NAMES[] = {
	"INVALID",
	"Single Link 1",
	"Single Link 2",
	"Single Link 3",
	"Single Link 4",
	"Single Link 5",
	"Single Link 6",
	"Single Link 7",
	"Single Link 8",
};

typedef struct nosDeckLinkChannelList {
	size_t Count;
	nosDeckLinkChannel Channels[NOS_DECKLINK_CHANNEL_COUNT];
} nosDeckLinkAvailableChannels;

typedef struct nosDeckLinkOpenChannelParams
{
	nosMediaIODirection Direction;
	nosDeckLinkChannel Channel;
	nosMediaIOPixelFormat PixelFormat;
	struct
	{
		nosMediaIOFrameGeometry Geometry;
		nosMediaIOFrameRate FrameRate;
		nosMediaIOVideoScanType ScanType;
	} Output; // Don't care if Direction == NOS_MEDIAIO_DIRECTION_INPUT
} nosDeckLinkOpenOutputParams;

typedef enum nosDeckLinkFrameResult
{
	NOS_DECKLINK_FRAME_COMPLETED, // Frame arrived if it's an input channel, frame displayed if it's an output channel
	NOS_DECKLINK_FRAME_DROPPED,
} nosDeckLinkFrameResult;

typedef void (NOSAPI_CALL* nosDeckLinkInputVideoFormatChangeCallback)(void* userData, nosMediaIOVideoScanType scanType, nosMediaIOFrameGeometry geometry, nosMediaIOFrameRate frameRate, nosMediaIOPixelFormat pixelFormat);
typedef void (NOSAPI_CALL* nosDeckLinkFrameResultCallback)(void* userData, nosDeckLinkFrameResult result, uint32_t processedFrameNumber);
typedef void (NOSAPI_CALL* nosDeckLinkDeviceInvalidatedCallback)(void* userData);

typedef struct nosDeckLinkSubsystem {
	void				(NOSAPI_CALL* GetDevices)(size_t *inoutCount, nosDeckLinkDeviceDesc* outDeviceDescriptors);
	nosResult			(NOSAPI_CALL* GetAvailableChannels)(uint32_t deviceIndex, nosMediaIODirection direction, nosDeckLinkChannelList* outChannels);
	const char*			(NOSAPI_CALL* GetChannelName)(nosDeckLinkChannel channel);
	nosDeckLinkChannel	(NOSAPI_CALL* GetChannelFromName)(const char* channelName);
	nosResult			(NOSAPI_CALL* GetDeviceByUniqueDisplayName)(const char* uniqueDisplayName, uint32_t* outDeviceIndex);
	nosResult			(NOSAPI_CALL* GetDeviceInfoByIndex)(uint32_t deviceIndex, nosDeckLinkDeviceInfo* outInfo);

	// Channels
	nosResult (NOSAPI_CALL* GetSupportedOutputFrameGeometries)(uint32_t deviceIndex, nosDeckLinkChannel channel, nosMediaIOVideoScanType scanType, nosMediaIOFrameGeometryList* outGeometries);
	nosResult (NOSAPI_CALL* GetSupportedOutputFrameRatesForGeometry)(uint32_t deviceIndex, nosDeckLinkChannel channel, nosMediaIOVideoScanType scanType, nosMediaIOFrameGeometry geometry, nosMediaIOFrameRateList* outFrameRates);
	nosResult (NOSAPI_CALL* GetSupportedOutputPixelFormats)(uint32_t deviceIndex, nosDeckLinkChannel channel, nosMediaIOVideoScanType scanType, nosMediaIOFrameGeometry geometry, nosMediaIOFrameRate frameRate, nosMediaIOPixelFormatList* outPixelFormats);
	nosResult (NOSAPI_CALL* OpenChannel)(uint32_t deviceIndex, nosDeckLinkOpenChannelParams* params);
	nosResult (NOSAPI_CALL* CloseChannel)(uint32_t deviceIndex, nosDeckLinkChannel channel);
	nosResult (NOSAPI_CALL* GetCurrentDeltaSecondsOfChannel)(uint32_t deviceIndex, nosDeckLinkChannel channel, nosVec2u* outDeltaSeconds);
	int32_t   (NOSAPI_CALL* RegisterInputVideoFormatChangeCallback)(uint32_t deviceIndex, nosDeckLinkChannel channel, nosDeckLinkInputVideoFormatChangeCallback callback, void* userData);
	nosResult (NOSAPI_CALL* UnregisterInputVideoFormatChangeCallback)(uint32_t deviceIndex, nosDeckLinkChannel channel, int32_t callbackId);
	int32_t   (NOSAPI_CALL* RegisterFrameResultCallback)(uint32_t deviceIndex, nosDeckLinkChannel channel, nosDeckLinkFrameResultCallback callback, void* userData);
	nosResult (NOSAPI_CALL* UnregisterFrameResultCallback)(uint32_t deviceIndex, nosDeckLinkChannel channel, int32_t callbackId);

	// I/O
	nosResult (NOSAPI_CALL* WaitFrame)(uint32_t deviceIndex, nosDeckLinkChannel channel, uint32_t timeoutMs, nosMediaIOInterlacedFieldType fieldType);
	nosResult (NOSAPI_CALL* DMATransfer)(uint32_t deviceIndex, nosDeckLinkChannel channel, void* data, size_t size);
	nosResult (NOSAPI_CALL* StartStream)(uint32_t deviceIndex, nosDeckLinkChannel channel);
	nosResult (NOSAPI_CALL* StopStream)(uint32_t deviceIndex, nosDeckLinkChannel channel);

	int32_t   (NOSAPI_CALL* RegisterDeviceInvalidatedCallback)(uint32_t deviceIndex, nosDeckLinkDeviceInvalidatedCallback callback, void* userData);
	nosResult (NOSAPI_CALL* UnregisterDeviceInvalidatedCallback)(uint32_t deviceIndex, int32_t callbackId);

	/// Port mapped channel name. If a custom port mapping is used (in Config/Settings.json), this function will return the mapped name.
	/// Example:
	///  - If SDI 1 is mapped to SDI 3:
	///		- "Single Link 1" -> "Single Link 3"
	///		- "Dual Link 1-2" -> "Dual Link 1-3"
	nosResult			(NOSAPI_CALL* GetPortMappedChannelName)(uint32_t deviceIndex, nosDeckLinkChannel channel, char* outName, size_t maxSize);
	nosDeckLinkChannel	(NOSAPI_CALL* GetChannelFromPortMappedName)(uint32_t deviceIndex, const char* portMappedChannelName);
	nosResult (NOSAPI_CALL* ResetInputFrames)(uint32_t deviceIndex, nosDeckLinkChannel channel);
} nosDeckLinkSubsystem;

#pragma region Helper Declarations & Macros

// Make sure these are same with nossys file.
#define NOS_DECKLINK_DEVICE_SUBSYSTEM_NAME "nos.sys.decklink"
#define NOS_DECKLINK_DEVICE_SUBSYSTEM_VERSION_MAJOR 1
#define NOS_DECKLINK_DEVICE_SUBSYSTEM_VERSION_MINOR 0

extern struct nosModuleInfo nosDeckLinkSubsystemModuleInfo;
extern nosDeckLinkSubsystem* nosDeckLink;

#define NOS_DECKLINK_DEVICE_SUBSYSTEM_INIT()      \
	nosModuleInfo nosDeckLinkSubsystemModuleInfo; \
	nosDeckLinkSubsystem* nosDeckLink = nullptr;

#define NOS_DECKLINK_DEVICE_SUBSYSTEM_IMPORT() NOS_IMPORT_DEP(NOS_DECKLINK_DEVICE_SUBSYSTEM_NAME, nosDeckLinkSubsystemModuleInfo, nosDeckLink)

#pragma endregion

#if __cplusplus
}
#endif

#endif