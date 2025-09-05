#include "pch.h"

#define __YYDEFINE_EXTENSION_FUNCTIONS__
#include "Extension_Interface.h"
#include "Ref.h"
#include "YYRValue.h"
#define YYEXPORT __declspec(dllexport)

YYRunnerInterface* g_pYYRunnerInterface;

[[noreturn]] FORCEINLINE static void unreachable() { __assume(false); }

#ifndef NDEBUG
#define assert(a) (!!(a) || (__debugbreak(), unreachable(), false))
#else
#define assert(a) (__assume(a))
#endif

#define countof(a) (sizeof(a) / sizeof((a)[0]))

static void trace(const char* format, ...)
{
    va_list args;
    char buffer[0x1000];

    va_start(args, format);
    int length = vsprintf_s(buffer, format, args);
    va_end(args);

    OutputDebugStringA(buffer);
}

namespace
{
    constexpr char c_ExtensionName[] = "PFO";
    constexpr char c_ExtensionVersion[] = (""
#include "../version.h"
        "");

    constexpr int64 c_TimeBeforeResendingMessage = 1'000'000 / 60;
    constexpr int64 c_WaitForInputsDelayTime = 1'000'000 / 60;

    constexpr int c_TargetFPS = 60;
    constexpr int c_MaxInputDelay = 30;
    constexpr int c_MaxDelaySamples = 60 * 20;
    constexpr int c_MinSamplesBeforeDelayCalculation = c_MaxDelaySamples;
    constexpr int c_SamplesToIgnoreAfterChangingDelay = 5 * 20;
    constexpr int c_PercentSamplesBelowDelayToLowerDelay = 99;
    constexpr int c_PercentSamplesWithinDelayToMaintainDelay = 80;
    constexpr int c_PingDelayCalculationSafetyMargin = 10;

    YYRunnerInterface g_RunnerInterface;

    ISteamFriends* g_SteamFriends;
    ISteamMatchmaking* g_SteamMatchmaking;
    ISteamNetworkingSockets* g_SteamNetworkingSockets;
    ISteamNetworkingUtils* g_SteamNetworkingUtils;

    struct PFO* g_pfo;

    int g_GmlChecksumBuffer1;
    int g_GmlChecksumBuffer2;

    int g_gml_random_set_seed;
    int g_gml_random_get_seed;
    int g_gml_randomize;
    int g_gml_file_exists;
    int g_gml_buffer_load;
    int g_gml_buffer_save;
    int g_gml_buffer_delete;
    int g_gml_file_delete;
    int g_gml_buffer_seek;
    int g_gml_file_copy;
    int g_gml_Script_onlineStateChangedCallback;
    int g_gml_Script_getInputCallback;
    int g_gml_Script_getChecksumCallback;

    template<typename T>
    FORCEINLINE void* copy(T& dst, const T& src)
    {
        return memcpy(dst, src, sizeof(T));
    }

    template<typename T>
    FORCEINLINE void* reset(T& dst)
    {
        return new(&dst) T();
    }

    struct SizeGetter
    {
        int m_Size = 0;

        template<typename T>
        constexpr void Write(const T& value)
        {
            m_Size += sizeof(T);
        }

        constexpr void Write(const void* buffer, int size)
        {
            m_Size += size;
        }
    };

    struct Writer
    {
        uint8* m_Buffer;
        int m_Size;
        int m_Offset = 0;

        Writer() = delete;
        Writer(uint8* buffer, int size) : m_Buffer(buffer), m_Size(size) {}

        template<typename T>
        void Write(const T& value)
        {
            assert(m_Offset + sizeof(T) <= m_Size);
            memcpy(m_Buffer + m_Offset, &value, sizeof(T));
            m_Offset += sizeof(T);
        }

        void Write(const void* buffer, int size)
        {
            assert(size >= 0);
            assert(m_Offset + size <= m_Size);
            memcpy(m_Buffer + m_Offset, buffer, size);
            m_Offset += size;
        }
    };

    struct Reader
    {
        const uint8* m_Buffer;
        int m_Size;
        int m_Offset = 0;

        Reader() = delete;
        Reader(const uint8* buffer, int size) : m_Buffer(buffer), m_Size(size) {}

        template<typename T>
        void Write(T& value)
        {
            assert(m_Offset + sizeof(T) <= m_Size);
            memcpy(&value, m_Buffer + m_Offset, sizeof(T));
            m_Offset += sizeof(T);
        }

        void Write(void* buffer, int size)
        {
            assert(size >= 0);
            assert(m_Offset + size <= m_Size);
            memcpy(buffer, m_Buffer + m_Offset, size);
            m_Offset += size;
        }
    };

    template<typename T>
    struct Differ
    {
        const uint8* m_Buffer1;
        const uint8* m_Buffer2;
        int m_Size;
        int m_Offset = 0;
        T m_Diff1;
        T m_Diff2;

        Differ() = delete;
        Differ(Reader& reader1, Reader& reader2) : m_Buffer1(reader1.m_Buffer), m_Buffer2(reader2.m_Buffer), m_Size(reader1.m_Size)
        {
            assert(reader1.m_Size == reader2.m_Size);

            m_Diff1.Serialize(reader1);
            m_Diff2.Serialize(reader2);
        }

        template<typename T>
        void Write(const T& value)
        {
            assert(m_Offset + sizeof(T) <= m_Size);
            assert(memcmp(m_Buffer1 + m_Offset, m_Buffer2 + m_Offset, sizeof(T)) == 0);
            m_Offset += sizeof(T);
        }
    };

    constexpr uint32 rand_spcg32(uint64 s[1])
    {
        constexpr uint64 m = 0x85bad762cae4b329;
        constexpr uint64 a = 0xa12e5b2b500f7add;
        *s = *s * m + a;
        int shift = 29 - (*s >> 61);
        return static_cast<uint32>(*s >> shift);
    }

    constexpr uint32 hash_fnv1a32(const void* data, int size, uint32 hash = 0x811c9dc5)
    {
        constexpr uint32 prime = 0x01000193;
        auto ptr = static_cast<const uint8*>(data);
        auto end = ptr + size;

        while (ptr < end)
        {
            hash ^= *ptr++;
            hash *= prime;
        }

        return hash;
    }

    FORCEINLINE constexpr void init_buffer(RValue& r, int index)
    {
        r.v64 = MAKE_REF(REFID_BUFFER, index);
        r.flags = 0;
        r.kind = VALUE_REF;
    }

    FORCEINLINE constexpr void init_real(RValue& r, double value)
    {
        r.val = value;
        r.flags = 0;
        r.kind = VALUE_REAL;
    }

    FORCEINLINE constexpr void init_int64(RValue& r, int64 value)
    {
        r.v64 = value;
        r.flags = 0;
        r.kind = VALUE_INT64;
    }

    FORCEINLINE constexpr void init_bool(RValue& r, bool value)
    {
        r.val = value;
        r.flags = 0;
        r.kind = VALUE_BOOL;
    }

    int buffer_seek(CInstance* instance, int buffer, int base, int offset)
    {
        RValue result = {};
        RValue arg[3];
        init_buffer(arg[0], buffer);
        init_real(arg[1], base);
        init_real(arg[2], offset);
        Script_Perform(g_gml_buffer_seek, instance, instance, countof(arg), &result, arg);
        return static_cast<int>(result.val);
    }

    using InputFlags_t = uint32;

    enum class EOnlineState
    {
        Offline,
        StartingGame,
        InGame,
        Disconnecting,
        Quitting,
    };

    struct MessageSendInfo
    {
        int64 m_MessageNumber = 0;
        int64 m_MessageSendTime = 0;
        int64 m_MessageAcknowledgeTime = 0;
        uint32 m_InputFrame = 0;
        uint32 m_Frame = 0;
    };

    enum class EInputDelayMode
    {
        ManualAll,
        ManualSelf,
        AutomaticShared,
        AutomaticFavored,

        COUNT
    };

    struct InputDelayChangeRequest
    {
        EInputDelayMode m_InputDelayMode = {};
        uint32 m_InputFrame = 0;
        int m_InputDelay = 0;
    };

    struct MessageInputDelayChangeRequest
    {
        int8 m_ChangeType = 0;
        uint8 m_FrameIndex = 0;
        uint8 m_InputDelay = 0;
    };

    struct Checksum
    {
        uint32 m_Frame = 0;
        uint64 m_RNG[1] = {};
        EInputDelayMode m_InputDelayMode = {};
        int m_InputDelayFavoredPlayerIndex = -1;
        struct
        {
            int m_InputDelay = 0;
            InputFlags_t m_InputBuffer[8] = {};
            InputDelayChangeRequest m_InputDelayChangeRequests[8];
        } m_PlayerData[2];

        template<typename T>
        constexpr void Serialize(T& writer)
        {
            writer.Write(m_Frame);
            writer.Write(m_RNG);
            writer.Write(m_InputDelayMode);
            writer.Write(m_InputDelayFavoredPlayerIndex);

            for (int playerIndex = 0; playerIndex < countof(m_PlayerData); playerIndex++)
            {
                auto& playerData = m_PlayerData[playerIndex];

                writer.Write(playerData.m_InputDelay);

                for (int i = 0; i < countof(playerData.m_InputBuffer); i++)
                {
                    writer.Write(playerData.m_InputBuffer[i]);
                }

                for (int i = 0; i < countof(playerData.m_InputDelayChangeRequests); i++)
                {
                    writer.Write(playerData.m_InputDelayChangeRequests[i].m_InputDelayMode);
                    writer.Write(playerData.m_InputDelayChangeRequests[i].m_InputFrame);
                    writer.Write(playerData.m_InputDelayChangeRequests[i].m_InputDelay);
                }
            }
        }

        static consteval int GetMaxSize()
        {
            SizeGetter size;
            Checksum checksum;
            checksum.Serialize(size);
            return size.m_Size;
        }
    };

    struct ChecksumBuffer
    {
        uint32 m_Frame = 0;
        uint32 m_Checksum = 0;
        int m_PfoBufferSize = 0;
        int m_GmlBufferSize = 0;
        bool m_SentBuffer = false;
        bool m_ReceivedBuffer = false;
        uint8 m_PfoBuffer[Checksum::GetMaxSize()] = {};
        uint8 m_GmlBuffer[1024] = {};

        template<typename T>
        constexpr void Serialize(T& writer)
        {
            writer.Write(m_Frame);
            writer.Write(m_Checksum);
            writer.Write(m_PfoBufferSize);
            writer.Write(m_GmlBufferSize);
            writer.Write(m_PfoBuffer, m_PfoBufferSize);
            writer.Write(m_GmlBuffer, m_GmlBufferSize);
        }
    };

    struct PlayerData
    {
        int64 m_LastSentTime = 0;
        int64 m_LastSentMessageNumber = 0;
        int64 m_LastReceivedMessageNumber = 0;
        int64 m_LastAcknowledgedMessageNumber = 0;
        int64 m_PreviousFrameLastAcknowledgedMessageNumber = 0;
        int64 m_PreviousFrameSampleRTT = 0;
        int m_InputDelay = 0;
        uint32 m_LastSentInputFrame = 0;
        uint32 m_LastInputFrame = 0;
        uint32 m_LastAcknowledgedInputFrame = 0;
        int m_SampleCount = 0;
        int m_SampleHead = 0;
        HSteamNetConnection m_ConnectSocket = k_HSteamNetConnection_Invalid;
        InputFlags_t m_InputBuffer[128] = {};
        InputDelayChangeRequest m_InputDelayChangeRequests[countof(m_InputBuffer)];
        MessageSendInfo m_MessageSendData[512];
        int m_DelaySamples[c_MaxDelaySamples] = {};
        int m_DelayHistogram[c_MaxInputDelay + 1] = {};
        ChecksumBuffer m_ChecksumData[128];
    };

    enum class EFileStatus
    {
        None,
        Owned,
        UnownedUnknown,
        UnownedLoading,
        UnownedDoesNotExist,
        UnownedExists,
    };

    struct FileData
    {
        const char* m_Filename = nullptr;
        const void* m_Data = nullptr;
        EFileStatus m_Status = EFileStatus::None;
        int m_OwnerPlayerIndex = 0;
        int m_Size = 0;
    };

    enum class EReliableMessageType : int8
    {
        File,
        FileDoesNotExist,
        ChecksumBuffer,
    };

    struct ReliableMessage
    {
        EReliableMessageType m_Type;
        char m_Filename[MAX_PATH + 1];
        union
        {
            int m_Size;
            uint32 m_Frame;
        };

        template<typename T>
        constexpr void Serialize(T& writer)
        {
            writer.Write(m_Type);

            if (m_Type == EReliableMessageType::File || m_Type == EReliableMessageType::FileDoesNotExist)
            {
                int i = 0;
                do
                {
                    assert(i < countof(m_Filename));
                    writer.Write(m_Filename[i]);
                } while (m_Filename[i++] != '\0');
            }

            if (m_Type == EReliableMessageType::File)
            {
                writer.Write(m_Size);
            }

            if (m_Type == EReliableMessageType::ChecksumBuffer)
            {
                writer.Write(m_Frame);
            }
        }
    };

    struct Message
    {
        int64 m_LastReceivedMessageNumber = 0;
        uint32 m_FirstSentFrame = 0;
        uint8 m_FramesSentCount = 0;
        int8 m_ChecksumFrameDelta = 0;
        uint32 m_Checksum = 0;

        InputFlags_t m_Inputs[countof(PlayerData::m_InputBuffer)] = {};
        MessageInputDelayChangeRequest m_InputDelayChangeRequests[countof(m_Inputs)];

        template<typename T>
        constexpr void Serialize(T& writer)
        {
            writer.Write(m_LastReceivedMessageNumber);
            writer.Write(m_FirstSentFrame);
            writer.Write(m_FramesSentCount);
            writer.Write(m_ChecksumFrameDelta);
            writer.Write(m_Checksum);

            assert(m_FramesSentCount <= countof(m_Inputs));
            for (int i = 0; i < m_FramesSentCount; i++)
            {
                writer.Write(m_Inputs[i]);
            }

            for (int i = 0; i < countof(m_InputDelayChangeRequests); i++)
            {
                writer.Write(m_InputDelayChangeRequests[i].m_ChangeType);
                if (m_InputDelayChangeRequests[i].m_ChangeType != 0)
                {
                    writer.Write(m_InputDelayChangeRequests[i].m_FrameIndex);
                    writer.Write(m_InputDelayChangeRequests[i].m_InputDelay);
                }
                else
                {
                    break;
                }
            }
        }

        static consteval int GetMaxSize()
        {
            SizeGetter size;
            Message message;
            ChecksumBuffer checksumBuffer;
            checksumBuffer.m_PfoBufferSize = sizeof(checksumBuffer.m_PfoBuffer);
            checksumBuffer.m_GmlBufferSize = sizeof(checksumBuffer.m_GmlBuffer);
            message.m_FramesSentCount = countof(message.m_Inputs);

            for (int i = 0; i < countof(message.m_InputDelayChangeRequests); i++)
            {
                message.m_InputDelayChangeRequests[i].m_ChangeType = static_cast<int8>(EInputDelayMode::AutomaticFavored) + 1;
            }

            message.Serialize(size);
            return size.m_Size;
        }
    };

    struct PFO
    {
        CCallResult<PFO, LobbyCreated_t> m_SteamCallResultLobbyCreated;
        CCallResult<PFO, LobbyEnter_t> m_SteamCallResultLobbyEntered;
        CCallResult<PFO, LobbyMatchList_t> m_SteamCallResultLobbyMatchList;

        HSteamListenSocket m_ListenSocket = k_HSteamListenSocket_Invalid;

        EOnlineState m_OnlineState = EOnlineState::Offline;
        uint32 m_Frame = 0;
        int m_PlayerIndex = 0;

        EInputDelayMode m_InputDelayMode = EInputDelayMode::ManualAll;
        EInputDelayMode m_RequestedInputDelayMode = m_InputDelayMode;
        uint32 m_RequestedInputDelayFrame = 0;
        int m_RequestedInputDelay = 0;
        int m_InputDelayFavoredPlayerIndex = -1;
        int m_RequestedInputDelayFavoredPlayerIndex = -1;

        uint64 m_RNG[1] = {};

        PlayerData m_PlayerData[2];
        FileData m_FileData[32];

        STEAM_CALLBACK(PFO, OnNetConnectionStatusChanged, SteamNetConnectionStatusChangedCallback_t)
        {
            auto hConn = pParam->m_hConn;
            auto& info = pParam->m_info;
            auto eOldState = pParam->m_eOldState;

            ReleaseConsoleOutput("Connection status changed conn: %u, state %d -> %d, end: %d %s\n", hConn, eOldState, info.m_eState, info.m_eEndReason, info.m_szEndDebug);

            if (info.m_eState == k_ESteamNetworkingConnectionState_ClosedByPeer || info.m_eState == k_ESteamNetworkingConnectionState_ProblemDetectedLocally)
            {
                if (info.m_eState == k_ESteamNetworkingConnectionState_ClosedByPeer)
                {
                    g_SteamNetworkingSockets->CloseConnection(hConn, k_ESteamNetConnectionEnd_App_Min, nullptr, false);
                }
                else
                {
                    g_SteamNetworkingSockets->CloseConnection(hConn, info.m_eEndReason, info.m_szEndDebug, false);
                }

                for (int playerIndex = 0; playerIndex < countof(m_PlayerData); playerIndex++)
                {
                    auto& playerData = m_PlayerData[playerIndex];
                    if (playerData.m_ConnectSocket == hConn)
                    {
                        playerData.m_ConnectSocket = k_HSteamNetConnection_Invalid;
                    }
                }

                if (info.m_eState == k_ESteamNetworkingConnectionState_ProblemDetectedLocally &&
                    (info.m_eEndReason == k_ESteamNetConnectionEnd_Remote_Timeout || info.m_eEndReason == k_ESteamNetConnectionEnd_Misc_Timeout))
                {
                    ReleaseConsoleOutput("Let's try to reconnect!\n");
                    //m_OnlineState = EOnlineState::Reconnecting;
                    m_OnlineState = EOnlineState::Disconnecting;
                }
                else
                {
                    m_OnlineState = EOnlineState::Disconnecting;
                }
            }

            if (info.m_hListenSocket != k_HSteamListenSocket_Invalid &&
                eOldState == k_ESteamNetworkingConnectionState_None &&
                info.m_eState == k_ESteamNetworkingConnectionState_Connecting)
            {
                if (m_OnlineState == EOnlineState::Offline || m_OnlineState == EOnlineState::InGame)
                {
                    if (m_PlayerData[1].m_ConnectSocket != k_HSteamNetConnection_Invalid)
                    {
                        g_SteamNetworkingSockets->CloseConnection(hConn, k_ESteamNetConnectionEnd_AppException_Generic + 2, "New connection received", false);
                    }

                    EResult res = g_SteamNetworkingSockets->AcceptConnection(hConn);
                    if (res == k_EResultOK)
                    {
                        AddConnection(1, hConn);
                    }
                    else
                    {
                        YYError("AcceptConnection returned %d", res);
                        g_SteamNetworkingSockets->CloseConnection(hConn, k_ESteamNetConnectionEnd_AppException_Generic, "Failed to accept connection", false);
                    }
                }
                else
                {
                    g_SteamNetworkingSockets->CloseConnection(hConn, k_ESteamNetConnectionEnd_AppException_Generic + 1, "Game no longer available", false);
                }
            }

            if (info.m_eState == k_ESteamNetworkingConnectionState_Connected)
            {
                if (m_OnlineState == EOnlineState::Offline)
                {
                    m_OnlineState = EOnlineState::StartingGame;
                }
            }
        }

        void AddConnection(int playerIndex, HSteamNetConnection hConn)
        {
            constexpr int lanePriorities[] = { 0, 1 }; // lowest number drains first
            g_SteamNetworkingSockets->ConfigureConnectionLanes(hConn, countof(lanePriorities), lanePriorities, nullptr);
            m_PlayerData[playerIndex].m_ConnectSocket = hConn;
        }

        STEAM_CALLBACK(PFO, OnLobbyDataUpdate, LobbyDataUpdate_t)
        {
            auto map = CreateDsMap(0, 0);
            DsMapAddString(map, "event_type", "lobby_data_update");
            DsMapAddBool(map, "success", pParam->m_bSuccess);
            DsMapAddInt64(map, "lobby_id", pParam->m_ulSteamIDLobby);
            DsMapAddInt64(map, "member_id", pParam->m_ulSteamIDMember);
            CreateAsyncEventWithDSMap(map, 69);
        }

        STEAM_CALLBACK(PFO, OnLobbyGameCreated, LobbyGameCreated_t)
        {
            auto map = CreateDsMap(0, 0);
            DsMapAddString(map, "event_type", "lobby_game_created");
            DsMapAddInt64(map, "lobby_id", pParam->m_ulSteamIDLobby);
            DsMapAddInt64(map, "server_id", pParam->m_ulSteamIDGameServer);
            DsMapAddDouble(map, "ip", pParam->m_unIP);
            DsMapAddDouble(map, "port", pParam->m_usPort);
            CreateAsyncEventWithDSMap(map, 69);
        }

        void Quit()
        {
            if (m_OnlineState >= EOnlineState::InGame)
            {
                m_OnlineState = EOnlineState::Quitting;
            }
        }

        void Update(CInstance* instance)
        {
            if (m_OnlineState == EOnlineState::StartingGame)
            {
                m_OnlineState = EOnlineState::InGame;

                RequestInputDelayChange(EInputDelayMode::AutomaticShared);

                {
                    RValue result = {};
                    RValue arg;
                    init_int64(arg, 1);
                    Script_Perform(g_gml_Script_onlineStateChangedCallback, instance, instance, 1, &result, &arg);
                }
            }

            if (m_OnlineState == EOnlineState::InGame)
            {
                m_Frame++;

                // write checksum
                {
                    Checksum checksum;
                    checksum.m_Frame = m_Frame;
                    copy(checksum.m_RNG, m_RNG);
                    checksum.m_InputDelayMode = m_InputDelayMode;
                    checksum.m_InputDelayFavoredPlayerIndex = m_InputDelayFavoredPlayerIndex;

                    for (int playerIndex = 0; playerIndex < countof(checksum.m_PlayerData); playerIndex++)
                    {
                        auto& playerData = m_PlayerData[playerIndex];
                        auto& checksumPlayerData = checksum.m_PlayerData[playerIndex];

                        checksumPlayerData.m_InputDelay = playerData.m_InputDelay;

                        for (int i = 0; i < countof(checksumPlayerData.m_InputBuffer); i++)
                        {
                            checksumPlayerData.m_InputBuffer[i] = playerData.m_InputBuffer[(m_Frame - i - 1) & (countof(playerData.m_InputBuffer) - 1)];
                        }

                        for (int i = 0; i < countof(checksumPlayerData.m_InputDelayChangeRequests); i++)
                        {
                            auto& changeRequest = playerData.m_InputDelayChangeRequests[(m_Frame - i - 1) & (countof(playerData.m_InputDelayChangeRequests) - 1)];
                            if (changeRequest.m_InputFrame == m_Frame - i - 1)
                            {
                                checksumPlayerData.m_InputDelayChangeRequests[i] = changeRequest;
                            }
                        }
                    }

                    auto& myPlayerData = m_PlayerData[m_PlayerIndex];
                    auto& checksumBuffer = myPlayerData.m_ChecksumData[m_Frame & (countof(myPlayerData.m_ChecksumData) - 1)];
                    reset(checksumBuffer);
                    checksumBuffer.m_Frame = m_Frame;

                    {
                        Writer writer(checksumBuffer.m_PfoBuffer, sizeof(checksumBuffer.m_PfoBuffer));
                        checksum.Serialize(writer);
                        checksumBuffer.m_PfoBufferSize = writer.m_Offset;
                    }

                    {
                        auto ibuffer = BufferGetFromGML(g_GmlChecksumBuffer1);
                        assert(ibuffer != nullptr);

                        buffer_seek(instance, g_GmlChecksumBuffer1, 0, 0);

                        {
                            RValue result = {};
                            RValue arg;
                            init_buffer(arg, g_GmlChecksumBuffer1);
                            Script_Perform(g_gml_Script_getChecksumCallback, instance, instance, 1, &result, &arg);
                        }

                        int dataSize = BufferTELL(ibuffer);
                        assert(dataSize <= sizeof(checksumBuffer.m_GmlBuffer));
                        uint8* data = BufferGet(ibuffer);

                        checksumBuffer.m_GmlBufferSize = dataSize;
                        memcpy(checksumBuffer.m_GmlBuffer, data, checksumBuffer.m_GmlBufferSize);
                    }

                    checksumBuffer.m_Checksum = hash_fnv1a32(checksumBuffer.m_PfoBuffer, checksumBuffer.m_PfoBufferSize);
                    checksumBuffer.m_Checksum = hash_fnv1a32(checksumBuffer.m_GmlBuffer, checksumBuffer.m_GmlBufferSize, checksumBuffer.m_Checksum);

                    CheckChecksum(checksumBuffer.m_Frame, instance);
                }

                // get local player input
                {
                    auto& myPlayerData = m_PlayerData[m_PlayerIndex];
                    uint32 frame = myPlayerData.m_LastInputFrame + 1;
                    uint32 lastFrame = m_Frame + myPlayerData.m_InputDelay;
                    InputFlags_t input;

                    {
                        RValue result = {};
                        RValue arg;
                        init_real(arg, frame);
                        Script_Perform(g_gml_Script_getInputCallback, instance, instance, 1, &result, &arg);
                        input = static_cast<InputFlags_t>(YYGetInt64(&result, 0));
                        assert(static_cast<int64>(input) == result.v64);
                    }

                    // if we have more inputs than we need, only keep the input if it's different than the previous
                    bool keepInput = true;
                    if (frame > lastFrame)
                    {
                        auto previousInput = myPlayerData.m_InputBuffer[(frame - 1) & (countof(myPlayerData.m_InputBuffer) - 1)];

                        {
                            RValue result = {};
                            RValue args[3];
                            init_real(args[0], frame);
                            init_int64(args[1], previousInput);
                            init_int64(args[2], input);
                            Script_Perform(g_gml_Script_getInputCallback, instance, instance, countof(args), &result, args);
                            keepInput = YYGetBool(&result, 0);
                        }
                    }

                    if (keepInput)
                    {
                        while (true)
                        {
                            assert(frame - m_Frame + 1 < countof(myPlayerData.m_InputBuffer));
                            myPlayerData.m_InputBuffer[frame & (countof(myPlayerData.m_InputBuffer) - 1)] = input;
                            myPlayerData.m_LastInputFrame = frame;

                            if (++frame > lastFrame)
                            {
                                break;
                            }

                            {
                                RValue result = {};
                                RValue args[2];
                                init_real(args[0], frame);
                                init_int64(args[1], input);
                                Script_Perform(g_gml_Script_getInputCallback, instance, instance, countof(args), &result, args);
                                input = static_cast<InputFlags_t>(YYGetInt64(&result, 0));
                                assert(static_cast<int64>(input) == result.v64);
                            }
                        }
                    }

                    assert(myPlayerData.m_LastInputFrame >= m_Frame);
                }

                while (true)
                {
                    // receive all messages
                    {
                        SteamNetworkingMessage_t* netMessages[32];

                        for (int playerIndex = 0; playerIndex < countof(m_PlayerData); playerIndex++)
                        {
                            auto& playerData = m_PlayerData[playerIndex];
                            if (playerData.m_ConnectSocket != k_HSteamNetConnection_Invalid)
                            {
                                int count = SteamNetworkingSockets()->ReceiveMessagesOnConnection(playerData.m_ConnectSocket, netMessages, countof(netMessages));

                                for (int i = 0; i < count; i++)
                                {
                                    auto netMessage = netMessages[i];

                                    switch (netMessage->m_idxLane)
                                    {
                                    case 0:
                                    {
                                        assert(netMessage->m_cbSize <= Message::GetMaxSize());

                                        Message message;

                                        Reader reader(static_cast<uint8*>(netMessage->m_pData), netMessage->m_cbSize);
                                        message.Serialize(reader);

                                        if (netMessage->m_nMessageNumber > playerData.m_LastReceivedMessageNumber)
                                        {
                                            playerData.m_LastReceivedMessageNumber = netMessage->m_nMessageNumber;
                                        }

                                        auto& acknowledgedMessage = playerData.m_MessageSendData[message.m_LastReceivedMessageNumber & (countof(playerData.m_MessageSendData) - 1)];
                                        if (acknowledgedMessage.m_MessageNumber == message.m_LastReceivedMessageNumber)
                                        {
                                            if (acknowledgedMessage.m_MessageAcknowledgeTime == 0)
                                            {
                                                acknowledgedMessage.m_MessageAcknowledgeTime = netMessage->m_usecTimeReceived;
                                            }

                                            if (message.m_LastReceivedMessageNumber > playerData.m_LastAcknowledgedMessageNumber)
                                            {
                                                playerData.m_LastAcknowledgedMessageNumber = message.m_LastReceivedMessageNumber;
                                                playerData.m_LastAcknowledgedInputFrame = acknowledgedMessage.m_InputFrame;
                                            }
                                        }

                                        uint32 checksumFrame = message.m_FirstSentFrame + message.m_ChecksumFrameDelta;
                                        assert(message.m_ChecksumFrameDelta >= 0 || checksumFrame < message.m_FirstSentFrame); // detect unsigned overflow
                                        auto& checksumBuffer = playerData.m_ChecksumData[checksumFrame & (countof(playerData.m_ChecksumData) - 1)];

                                        if (checksumBuffer.m_Frame != checksumFrame)
                                        {
                                            reset(checksumBuffer);
                                            checksumBuffer.m_Frame = checksumFrame;
                                            checksumBuffer.m_Checksum = message.m_Checksum;

                                            CheckChecksum(checksumBuffer.m_Frame, instance, playerIndex);
                                        }

                                        netMessage->Release();

                                        assert(message.m_FramesSentCount <= countof(message.m_Inputs));
                                        if (message.m_FramesSentCount > 0)
                                        {
                                            uint32 lastFrame = message.m_FirstSentFrame + message.m_FramesSentCount - 1;
                                            if (lastFrame > playerData.m_LastInputFrame)
                                            {
                                                uint32 firstFrame = playerData.m_LastInputFrame + 1;

                                                int firstMessageIndex = firstFrame - message.m_FirstSentFrame;
                                                int count = lastFrame - firstFrame + 1;

                                                assert(firstMessageIndex >= 0 && firstMessageIndex <= message.m_FramesSentCount);
                                                assert(count > 0 && count <= message.m_FramesSentCount);

                                                for (int i = 0; i < count; i++)
                                                {
                                                    playerData.m_InputBuffer[(i + firstFrame) & (countof(playerData.m_InputBuffer) - 1)] = message.m_Inputs[i + firstMessageIndex];
                                                }

                                                for (int i = 0; i < countof(message.m_InputDelayChangeRequests); i++)
                                                {
                                                    auto& messageChangeRequest = message.m_InputDelayChangeRequests[i];
                                                    if (messageChangeRequest.m_ChangeType != 0)
                                                    {
                                                        assert(messageChangeRequest.m_ChangeType > 0 && messageChangeRequest.m_ChangeType <= static_cast<int8>(EInputDelayMode::COUNT));
                                                        assert(messageChangeRequest.m_FrameIndex < message.m_FramesSentCount);
                                                        assert(messageChangeRequest.m_InputDelay <= c_MaxInputDelay);

                                                        if (messageChangeRequest.m_FrameIndex >= firstMessageIndex)
                                                        {
                                                            uint32 frame = messageChangeRequest.m_FrameIndex + message.m_FirstSentFrame;
                                                            auto& changeRequest = playerData.m_InputDelayChangeRequests[frame & (countof(playerData.m_InputDelayChangeRequests) - 1)];
                                                            changeRequest.m_InputDelayMode = static_cast<EInputDelayMode>(messageChangeRequest.m_ChangeType - 1);
                                                            changeRequest.m_InputFrame = frame;
                                                            changeRequest.m_InputDelay = messageChangeRequest.m_InputDelay;
                                                        }
                                                    }
                                                    else
                                                    {
                                                        break;
                                                    }
                                                }

                                                playerData.m_LastInputFrame = lastFrame;

                                                //trace("Received frame: %u\n", lastFrame);
                                            }
                                            else
                                            {
                                                //trace("Received frame: %u, but it wasn't newer than last received frame: %u\n", lastFrame, playerData.m_LastInputFrame);
                                            }
                                        }
                                    } break;
                                    case 1:
                                    {
                                        ReceiveReliableMessage(netMessage, playerIndex, instance);
                                    } break;
                                    }
                                }
                            }
                        }
                    }

                    // send all messages
                    {
                        int messageCount = 0;
                        SteamNetworkingMessage_t* netMessages[countof(m_PlayerData) - 1];
                        PlayerData* netMessagePlayers[countof(netMessages)];

                        auto& myPlayerData = m_PlayerData[m_PlayerIndex];

                        for (int playerIndex = 0; playerIndex < countof(m_PlayerData); playerIndex++)
                        {
                            auto& playerData = m_PlayerData[playerIndex];
                            if (playerData.m_ConnectSocket != k_HSteamNetConnection_Invalid)
                            {
                                int64 time = g_SteamNetworkingUtils->GetLocalTimestamp();

                                if (myPlayerData.m_LastInputFrame > playerData.m_LastSentInputFrame || time >= playerData.m_LastSentTime + c_TimeBeforeResendingMessage)
                                {
                                    netMessagePlayers[messageCount] = &playerData;
                                    auto netMessage = netMessages[messageCount++] = g_SteamNetworkingUtils->AllocateMessage(Message::GetMaxSize());
                                    netMessage->m_conn = playerData.m_ConnectSocket;
                                    netMessage->m_nFlags = k_nSteamNetworkingSend_UnreliableNoDelay;

                                    Message message;

                                    uint32 first = playerData.m_LastAcknowledgedInputFrame + 1;
                                    int count = myPlayerData.m_LastInputFrame - playerData.m_LastAcknowledgedInputFrame;
                                    assert(count >= 0);

                                    if (count > countof(message.m_Inputs))
                                    {
                                        count = countof(message.m_Inputs);

                                        trace("Failed to send all inputs for frame: %u\n", myPlayerData.m_LastInputFrame);
                                    }

                                    message.m_LastReceivedMessageNumber = playerData.m_LastReceivedMessageNumber;
                                    message.m_FirstSentFrame = first;

                                    assert(count <= countof(message.m_Inputs));
                                    message.m_FramesSentCount = static_cast<uint8>(count);

                                    auto& checksumBuffer = myPlayerData.m_ChecksumData[m_Frame & (countof(myPlayerData.m_ChecksumData) - 1)];
                                    assert(checksumBuffer.m_Frame == m_Frame);
                                    int checksumFrameDelta = m_Frame - first;
                                    assert(checksumFrameDelta >= INT8_MIN && checksumFrameDelta <= INT8_MAX);
                                    message.m_ChecksumFrameDelta = static_cast<int8>(checksumFrameDelta);
                                    message.m_Checksum = checksumBuffer.m_Checksum;

                                    int changeRequestIndex = 0;
                                    for (int i = 0; i < count; i++)
                                    {
                                        uint32 frame = i + first;
                                        message.m_Inputs[i] = myPlayerData.m_InputBuffer[frame & (countof(myPlayerData.m_InputBuffer) - 1)];

                                        auto& changeRequest = myPlayerData.m_InputDelayChangeRequests[frame & (countof(myPlayerData.m_InputDelayChangeRequests) - 1)];
                                        if (changeRequest.m_InputFrame == frame)
                                        {
                                            auto& messageChangeRequest = message.m_InputDelayChangeRequests[changeRequestIndex++];
                                            messageChangeRequest.m_ChangeType = static_cast<int8>(changeRequest.m_InputDelayMode) + 1;
                                            messageChangeRequest.m_FrameIndex = static_cast<uint8>(i);
                                            messageChangeRequest.m_InputDelay = static_cast<uint8>(changeRequest.m_InputDelay);
                                        }
                                    }

                                    Writer writer(static_cast<uint8*>(netMessage->m_pData), netMessage->m_cbSize);
                                    message.Serialize(writer);
                                    netMessage->m_cbSize = writer.m_Offset;

                                    if (myPlayerData.m_LastInputFrame <= playerData.m_LastSentInputFrame)
                                    {
                                        //trace("Resending inputs for frame: %u\n", myPlayerData.m_LastInputFrame);
                                    }

                                    playerData.m_LastSentInputFrame = myPlayerData.m_LastInputFrame;
                                    playerData.m_LastSentTime = time;
                                }
                            }
                        }

                        if (messageCount > 0)
                        {
                            int64 time = g_SteamNetworkingUtils->GetLocalTimestamp();
                            int64 outResults[countof(netMessages)];
                            g_SteamNetworkingSockets->SendMessages(messageCount, netMessages, outResults);

                            //trace("Sending inputs for frame: %u\n", myPlayerData.m_LastInputFrame);

                            for (int i = 0; i < messageCount; i++)
                            {
                                int64 messageNumber = outResults[i];
                                if (messageNumber > 0)
                                {
                                    auto& playerData = *netMessagePlayers[i];
                                    playerData.m_LastSentMessageNumber = messageNumber;
                                    playerData.m_MessageSendData[messageNumber & (countof(playerData.m_MessageSendData) - 1)] =
                                    {
                                        .m_MessageNumber = messageNumber,
                                        .m_MessageSendTime = time,
                                        .m_InputFrame = myPlayerData.m_LastInputFrame,
                                        .m_Frame = m_Frame,
                                    };
                                }
                            }
                        }
                    }

                    bool hasAllInput = true;
                    for (int i = 0; i < countof(m_PlayerData); i++)
                    {
                        if (m_PlayerData[i].m_LastInputFrame < m_Frame)
                        {
                            hasAllInput = false;
                            break;
                        }
                    }

                    if (hasAllInput)
                    {
                        break;
                    }

                    Timing_Sleep(c_WaitForInputsDelayTime);
                    //trace("SLOWING DOWN!\n");

                    MSG msg;
                    while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
                    {
                        TranslateMessage(&msg);
                        DispatchMessageW(&msg);

                        if (msg.message == WM_QUIT || msg.message == WM_CLOSE)
                        {
                            PostMessageW(msg.hwnd, msg.message, msg.wParam, msg.lParam);
                            m_OnlineState = EOnlineState::Quitting;
                            break;
                        }
                    }

                    SteamAPI_RunCallbacks();

                    if (m_OnlineState != EOnlineState::InGame)
                    {
                        m_Frame--;
                        break;
                    }
                }

                if (m_OnlineState == EOnlineState::InGame)
                {
                    // process input delay change requests for this frame
                    {
                        InputDelayChangeRequest bestChangeRequest;

                        // the "best" change request is just the one from the lowest index player
                        for (int playerIndex = countof(m_PlayerData) - 1; playerIndex >= 0; playerIndex--)
                        {
                            auto& playerData = m_PlayerData[playerIndex];
                            auto& changeRequest = playerData.m_InputDelayChangeRequests[m_Frame & (countof(playerData.m_InputDelayChangeRequests) - 1)];
                            if (changeRequest.m_InputFrame == m_Frame)
                            {
                                bestChangeRequest = changeRequest;
                            }
                        }

                        // we need to generate a best change request if we need to switch our favored player index
                        if (bestChangeRequest.m_InputFrame == 0
                            && (m_InputDelayMode == EInputDelayMode::AutomaticShared || m_InputDelayMode == EInputDelayMode::AutomaticFavored)
                            && m_InputDelayFavoredPlayerIndex != m_RequestedInputDelayFavoredPlayerIndex)
                        {
                            bestChangeRequest.m_InputDelayMode = m_InputDelayMode;
                            bestChangeRequest.m_InputFrame = m_Frame;

                            int maxDelay = 0;
                            for (int playerIndex = 0; playerIndex < countof(m_PlayerData); playerIndex++)
                            {
                                auto& playerData = m_PlayerData[playerIndex];
                                maxDelay = max(maxDelay, playerData.m_InputDelay);
                            }
                            bestChangeRequest.m_InputDelay = maxDelay;
                        }

                        m_InputDelayFavoredPlayerIndex = m_RequestedInputDelayFavoredPlayerIndex;

                        if (bestChangeRequest.m_InputFrame != 0)
                        {
                            // if multiple players request the same mode on the same frame, then find the one with the highest delay
                            for (int playerIndex = 0; playerIndex < countof(m_PlayerData); playerIndex++)
                            {
                                auto& playerData = m_PlayerData[playerIndex];
                                auto& changeRequest = playerData.m_InputDelayChangeRequests[m_Frame & (countof(playerData.m_InputDelayChangeRequests) - 1)];
                                if (changeRequest.m_InputFrame == m_Frame && changeRequest.m_InputDelayMode == bestChangeRequest.m_InputDelayMode && changeRequest.m_InputDelay > bestChangeRequest.m_InputDelay)
                                {
                                    bestChangeRequest.m_InputDelay = changeRequest.m_InputDelay;
                                }
                            }

                            // fix the input delay if it's in the wrong shared/favored mode
                            if (bestChangeRequest.m_InputDelayMode == EInputDelayMode::AutomaticShared || bestChangeRequest.m_InputDelayMode == EInputDelayMode::AutomaticFavored)
                            {
                                if (m_InputDelayFavoredPlayerIndex < 0)
                                {
                                    if (bestChangeRequest.m_InputDelayMode == EInputDelayMode::AutomaticFavored) bestChangeRequest.m_InputDelay = (bestChangeRequest.m_InputDelay + 1) / 2;
                                    bestChangeRequest.m_InputDelayMode = EInputDelayMode::AutomaticShared;
                                }
                                else
                                {
                                    if (bestChangeRequest.m_InputDelayMode == EInputDelayMode::AutomaticShared) bestChangeRequest.m_InputDelay = bestChangeRequest.m_InputDelay * 2;
                                    bestChangeRequest.m_InputDelayMode = EInputDelayMode::AutomaticFavored;
                                }
                            }

                            m_InputDelayMode = bestChangeRequest.m_InputDelayMode;

                            // apply the input delay to the players according to the mode
                            bool anyPlayerChangedInputDelay = false;
                            for (int playerIndex = 0; playerIndex < countof(m_PlayerData); playerIndex++)
                            {
                                auto& playerData = m_PlayerData[playerIndex];

                                int delay;
                                switch (m_InputDelayMode)
                                {
                                case EInputDelayMode::ManualSelf:
                                {
                                    auto& changeRequest = playerData.m_InputDelayChangeRequests[m_Frame & (countof(playerData.m_InputDelayChangeRequests) - 1)];
                                    if (changeRequest.m_InputFrame == m_Frame && changeRequest.m_InputDelayMode == m_InputDelayMode)
                                    {
                                        delay = changeRequest.m_InputDelay;
                                    }
                                    else
                                    {
                                        continue;
                                    }
                                } break;
                                case EInputDelayMode::ManualAll:
                                case EInputDelayMode::AutomaticShared:
                                {
                                    delay = bestChangeRequest.m_InputDelay;
                                } break;
                                case EInputDelayMode::AutomaticFavored:
                                {
                                    if (playerIndex == m_InputDelayFavoredPlayerIndex)
                                    {
                                        delay = 0;
                                    }
                                    else
                                    {
                                        delay = bestChangeRequest.m_InputDelay;
                                    }
                                } break;
                                default:
                                    assert(false);
                                }

                                assert(delay >= 0 && delay <= c_MaxInputDelay);
                                if (delay != playerData.m_InputDelay)
                                {
                                    playerData.m_InputDelay = delay;
                                    anyPlayerChangedInputDelay = true;
                                }
                            }

                            // clear our samples if any player changed their input delay
                            if (anyPlayerChangedInputDelay)
                            {
                                for (int playerIndex = 0; playerIndex < countof(m_PlayerData); playerIndex++)
                                {
                                    auto& playerData = m_PlayerData[playerIndex];
                                    if (playerData.m_ConnectSocket != k_HSteamNetConnection_Invalid)
                                    {
                                        playerData.m_PreviousFrameLastAcknowledgedMessageNumber = playerData.m_LastSentMessageNumber;
                                        playerData.m_SampleCount = 0;
                                        playerData.m_SampleHead = 0;
                                        playerData.m_PreviousFrameSampleRTT = 0;
                                        memset(playerData.m_DelayHistogram, 0, sizeof(playerData.m_DelayHistogram));
                                    }
                                }
                            }

                            // make sure our requested input delay is up to date, so we can always request a change if it's different from this
                            if (m_Frame >= m_RequestedInputDelayFrame)
                            {
                                m_RequestedInputDelayMode = m_InputDelayMode;
                                m_RequestedInputDelay = m_InputDelayMode == EInputDelayMode::ManualSelf ? m_PlayerData[m_PlayerIndex].m_InputDelay : bestChangeRequest.m_InputDelay;
                            }
                        }
                    }

                    // calculate delay
                    {
                        int maxDelay = 0;
                        for (int playerIndex = 0; playerIndex < countof(m_PlayerData); playerIndex++)
                        {
                            auto& playerData = m_PlayerData[playerIndex];
                            maxDelay = max(maxDelay, playerData.m_InputDelay);
                        }

                        for (int playerIndex = 0; playerIndex < countof(m_PlayerData); playerIndex++)
                        {
                            auto& playerData = m_PlayerData[playerIndex];
                            if (playerData.m_ConnectSocket != k_HSteamNetConnection_Invalid)
                            {
                                int64 rtt = 0;

                                // for all acked messages since last frame, get the max time since one was acknowledged
                                int64 first = playerData.m_PreviousFrameLastAcknowledgedMessageNumber;
                                int64 last = playerData.m_LastAcknowledgedMessageNumber;
                                int64 acknowledgeTime = 0;
                                for (int64 i = last; i > first; i--)
                                {
                                    auto& messageInfo = playerData.m_MessageSendData[i & (countof(playerData.m_MessageSendData) - 1)];
                                    if (messageInfo.m_MessageNumber == i)
                                    {
                                        // if the message hasn't been acked, we just use the ack time of the one after it that got acked
                                        if (messageInfo.m_MessageAcknowledgeTime != 0)
                                        {
                                            acknowledgeTime = messageInfo.m_MessageAcknowledgeTime;
                                        }

                                        assert(acknowledgeTime != 0);
                                        rtt = max(rtt, acknowledgeTime - messageInfo.m_MessageSendTime);
                                    }
                                }

                                // no acks since last frame, so just use last sample rtt
                                if (rtt == 0)
                                {
                                    rtt = playerData.m_PreviousFrameSampleRTT;
                                }

                                // use the current time since the first non-acked message if that's greater
                                int64 firstNonAcked = playerData.m_LastAcknowledgedMessageNumber + 1;
                                auto& messageInfo = playerData.m_MessageSendData[firstNonAcked & (countof(playerData.m_MessageSendData) - 1)];
                                if (messageInfo.m_MessageNumber == firstNonAcked)
                                {
                                    rtt = max(rtt, g_SteamNetworkingUtils->GetLocalTimestamp() - messageInfo.m_MessageSendTime);
                                }

                                // add the sample if we have one
                                if (rtt != 0)
                                {
                                    assert(rtt > 0);
                                    rtt = min(rtt, INT32_MAX);

                                    if (playerData.m_SampleCount >= countof(playerData.m_DelaySamples))
                                    {
                                        int oldDelay = playerData.m_DelaySamples[playerData.m_SampleHead];
                                        assert(playerData.m_DelayHistogram[oldDelay] > 0);
                                        playerData.m_DelayHistogram[oldDelay]--;
                                    }
                                    else
                                    {
                                        playerData.m_SampleCount++;
                                    }

                                    int delay = static_cast<int>((rtt * c_TargetFPS + 1'000'000 - 1) / 1'000'000);
                                    assert(delay >= 0);
                                    if (delay >= countof(playerData.m_DelayHistogram))
                                    {
                                        delay = countof(playerData.m_DelayHistogram) - 1;
                                    }

                                    playerData.m_DelaySamples[playerData.m_SampleHead++] = delay;
                                    if (playerData.m_SampleHead >= countof(playerData.m_DelaySamples))
                                    {
                                        playerData.m_SampleHead = 0;
                                    }

                                    playerData.m_DelayHistogram[delay]++;

                                    //trace("Added sample: %d, %d\n", static_cast<int>(rtt), delay);
                                }

                                playerData.m_PreviousFrameLastAcknowledgedMessageNumber = playerData.m_LastAcknowledgedMessageNumber;
                                playerData.m_PreviousFrameSampleRTT = rtt;

                                if (playerData.m_SampleCount >= c_MinSamplesBeforeDelayCalculation)
                                {
                                    // this prints out some useful info while testing, but it only really works for shared mode
                                    //{
                                    //    int delayUpperCheck = playerData.m_InputDelay * 2;
                                    //    int delayLowerCheck = (playerData.m_InputDelay - 1) * 2;
                                    //    assert(delayUpperCheck < countof(playerData.m_DelayHistogram));

                                    //    int upperTotal = 0;
                                    //    int lowerTotal = 0;
                                    //    for (int i = 0; i <= delayUpperCheck; i++)
                                    //    {
                                    //        upperTotal += playerData.m_DelayHistogram[i];
                                    //        if (i <= delayLowerCheck)
                                    //        {
                                    //            lowerTotal += playerData.m_DelayHistogram[i];
                                    //        }
                                    //    }

                                    //    {
                                    //        int medianDelay = (CalculateDelayPercentile(playerData, 50) + 1) / 2;
                                    //        int belowOrEqualPercent = (upperTotal * 100 + 50) / playerData.m_SampleCount;
                                    //        int belowPercent = (lowerTotal * 100 + 50) / playerData.m_SampleCount;
                                    //        int abovePercent = 100 - belowOrEqualPercent;

                                    //        //trace("Sample median: %d, current:%d, <%d%%, <=%d%%, >%d%%\n", medianDelay, playerData.m_InputDelay, belowPercent, belowOrEqualPercent, abovePercent);
                                    //    }
                                    //}

                                    if (m_InputDelayMode == EInputDelayMode::AutomaticShared || m_InputDelayMode == EInputDelayMode::AutomaticFavored)
                                    {
                                        int delay = CalculateDelayPercentile(playerData, c_PercentSamplesWithinDelayToMaintainDelay);
                                        if (m_InputDelayMode == EInputDelayMode::AutomaticShared) delay = (delay + 1) / 2;

                                        if (delay > maxDelay)
                                        {
                                            RequestInputDelayChange(m_InputDelayMode, delay);
                                        }
                                        else if (delay < maxDelay)
                                        {
                                            delay = CalculateDelayPercentile(playerData, c_PercentSamplesBelowDelayToLowerDelay);
                                            if (m_InputDelayMode == EInputDelayMode::AutomaticShared) delay = (delay + 1) / 2;

                                            if (delay < maxDelay)
                                            {
                                                RequestInputDelayChange(m_InputDelayMode, delay);
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            if (m_OnlineState == EOnlineState::Disconnecting)
            {
                {
                    RValue result = {};
                    RValue arg;
                    init_int64(arg, 2);
                    Script_Perform(g_gml_Script_onlineStateChangedCallback, instance, instance, 1, &result, &arg);
                }

                m_OnlineState = EOnlineState::Offline;

                {
                    RValue result = {};
                    RValue arg;
                    init_int64(arg, 0);
                    Script_Perform(g_gml_Script_onlineStateChangedCallback, instance, instance, 1, &result, &arg);
                }
            }
        }

        InputFlags_t GetInput(CInstance* instance, int playerIndex)
        {
            InputFlags_t in = 0;

            if (m_OnlineState == EOnlineState::InGame)
            {
                assert(playerIndex >= 0 && playerIndex < countof(m_PlayerData));
                auto& playerData = m_PlayerData[playerIndex];
                assert(playerData.m_LastInputFrame >= m_Frame);
                in = playerData.m_InputBuffer[m_Frame & (countof(playerData.m_InputBuffer) - 1)];

                //ReleaseConsoleOutput("Frame %u: Input for player %d = [ %x ]\n", m_Frame, playerIndex, in);
            }

            return in;
        }

        void Randomize(RValue& result, CInstance* instance)
        {
            if (m_OnlineState >= EOnlineState::InGame)
            {
                uint32 seed = rand_spcg32(m_RNG);

                RValue tmp = {};
                RValue arg;
                init_real(arg, seed);
                Script_Perform(g_gml_random_set_seed, instance, instance, 1, &tmp, &arg);
                init_real(result, seed);

                ReleaseConsoleOutput("Frame: %u: Randomize: %llx\n", m_Frame, m_RNG[0]);
            }
            else
            {
                Script_Perform(g_gml_randomize, instance, instance, 0, &result, nullptr);
                assert(KIND_RValue(&result) == VALUE_REAL);
            }
        }

        void CheckChecksum(uint32 frame, CInstance* instance, int checkPlayerIndex = -1)
        {
            auto& myPlayerData = m_PlayerData[m_PlayerIndex];
            auto& a = myPlayerData.m_ChecksumData[frame & (countof(myPlayerData.m_ChecksumData) - 1)];

            if (a.m_Frame == frame)
            {
                for (int playerIndex = 0; playerIndex < countof(m_PlayerData); playerIndex++)
                {
                    if (checkPlayerIndex < 0 || playerIndex == checkPlayerIndex)
                    {
                        auto& playerData = m_PlayerData[playerIndex];
                        auto& b = playerData.m_ChecksumData[frame & (countof(playerData.m_ChecksumData) - 1)];

                        if (a.m_Frame == b.m_Frame && a.m_Checksum != b.m_Checksum)
                        {
                            if (!b.m_SentBuffer)
                            {
                                // send checksum message
                                ReliableMessage reliableMessage;
                                reliableMessage.m_Type = EReliableMessageType::ChecksumBuffer;
                                reliableMessage.m_Frame = a.m_Frame;
                                SendReliableMessage(reliableMessage, instance, nullptr, &a);
                                b.m_SentBuffer = true;
                            }

                            if (b.m_ReceivedBuffer)
                            {
                                // compare pfo checksum
                                if (a.m_PfoBufferSize != b.m_PfoBufferSize || memcmp(a.m_PfoBuffer, b.m_PfoBuffer, a.m_PfoBufferSize) != 0)
                                {
                                    Reader readerA(a.m_PfoBuffer, a.m_PfoBufferSize);
                                    Reader readerB(b.m_PfoBuffer, b.m_PfoBufferSize);

                                    Differ<Checksum> differ(readerA, readerB);
                                    differ.m_Diff1.Serialize(differ);
                                }

                                // compare gml checksum
                                if (a.m_GmlBufferSize != b.m_GmlBufferSize || memcmp(a.m_GmlBuffer, b.m_GmlBuffer, a.m_GmlBufferSize) != 0)
                                {
                                    BufferWriteContent(g_GmlChecksumBuffer1, 0, a.m_GmlBuffer, a.m_GmlBufferSize);
                                    BufferWriteContent(g_GmlChecksumBuffer2, 0, b.m_GmlBuffer, b.m_GmlBufferSize);

                                    buffer_seek(instance, g_GmlChecksumBuffer1, 0, 0);
                                    buffer_seek(instance, g_GmlChecksumBuffer2, 0, 0);

                                    {
                                        RValue result = {};
                                        RValue arg[2];
                                        init_buffer(arg[0], g_GmlChecksumBuffer1);
                                        init_buffer(arg[1], g_GmlChecksumBuffer2);
                                        Script_Perform(g_gml_Script_getChecksumCallback, instance, instance, countof(arg), &result, arg);
                                    }
                                }

                                // we quit when a checksum failure happens, could change this in the future
                                SteamAPI_RunCallbacks();
                                PostMessageW(NULL, WM_QUIT, 0, 0);
                                m_OnlineState = EOnlineState::Quitting;
                            }
                        }
                    }
                }
            }
        }

        int CalculateDelayPercentile(PlayerData& playerData, int percentile)
        {
            assert(percentile >= 0 && percentile <= 100);
            assert(playerData.m_SampleCount > 0);

            int target = playerData.m_SampleCount * percentile / 100;
            int cumulative = 0;

            for (int i = 0; i < countof(playerData.m_DelayHistogram); i++)
            {
                if (playerData.m_DelayHistogram[i] > 0)
                {
                    cumulative += playerData.m_DelayHistogram[i];
                    if (cumulative >= target)
                    {
                        return i;
                    }
                }
            }

            assert(false);
        }

        void RequestInputDelayChange(EInputDelayMode mode, int delay = -1)
        {
            auto& myPlayerData = m_PlayerData[m_PlayerIndex];
            uint32 inputFrame = myPlayerData.m_LastInputFrame + 1;
            auto& changeRequest = myPlayerData.m_InputDelayChangeRequests[inputFrame & (countof(myPlayerData.m_InputDelayChangeRequests) - 1)];

            switch (mode)
            {
            case EInputDelayMode::ManualAll:
            case EInputDelayMode::ManualSelf:
                break;
            case EInputDelayMode::AutomaticShared:
            case EInputDelayMode::AutomaticFavored:
            {
                if (delay < 0)
                {
                    mode = m_InputDelayFavoredPlayerIndex < 0 ? EInputDelayMode::AutomaticShared : EInputDelayMode::AutomaticFavored;

                    for (int playerIndex = 0; playerIndex < countof(m_PlayerData); playerIndex++)
                    {
                        auto& playerData = m_PlayerData[playerIndex];
                        if (playerData.m_ConnectSocket != k_HSteamNetConnection_Invalid)
                        {
                            if (playerData.m_SampleCount >= c_MinSamplesBeforeDelayCalculation)
                            {
                                int newDelay = CalculateDelayPercentile(playerData, c_PercentSamplesWithinDelayToMaintainDelay);
                                delay = max(delay, newDelay);
                            }
                        }
                    }

                    if (delay < 0)
                    {
                        for (int playerIndex = 0; playerIndex < countof(m_PlayerData); playerIndex++)
                        {
                            auto& playerData = m_PlayerData[playerIndex];
                            if (playerData.m_ConnectSocket != k_HSteamNetConnection_Invalid)
                            {
                                SteamNetConnectionRealTimeStatus_t status;
                                g_SteamNetworkingSockets->GetConnectionRealTimeStatus(playerData.m_ConnectSocket, &status, 0, nullptr);
                                if (status.m_nPing >= 0)
                                {
                                    int newDelay = ((status.m_nPing + c_PingDelayCalculationSafetyMargin) * c_TargetFPS + 1000 - 1) / 1000;
                                    newDelay = min(newDelay, c_MaxInputDelay);
                                    delay = max(delay, newDelay);
                                }
                            }
                        }

                        if (delay < 0)
                        {
                            delay = 10;
                        }
                    }

                    if (mode == EInputDelayMode::AutomaticShared)
                    {
                        delay = (delay + 1) / 2;
                    }
                }
                else
                {
                    if (m_InputDelayFavoredPlayerIndex < 0)
                    {
                        if (mode == EInputDelayMode::AutomaticFavored) delay = (delay + 1) / 2;
                        mode = EInputDelayMode::AutomaticShared;
                    }
                    else
                    {
                        if (mode == EInputDelayMode::AutomaticShared) delay = delay * 2;
                        mode = EInputDelayMode::AutomaticFavored;
                    }
                }
            } break;
            default:
                assert(false);
            }

            assert(delay >= 0 && delay <= c_MaxInputDelay);

            if (mode != m_RequestedInputDelayMode || delay != m_RequestedInputDelay)
            {
                m_RequestedInputDelayFrame = changeRequest.m_InputFrame = inputFrame;
                m_RequestedInputDelayMode = changeRequest.m_InputDelayMode = mode;
                m_RequestedInputDelay = changeRequest.m_InputDelay = delay;

                ReleaseConsoleOutput("Requested Input Delay Change on frame: %u, mode: %d, delay: %d\n", inputFrame, mode, delay);
            }
        }

        FileData* FindFileData(const char* filename, int playerIndex = -1)
        {
            assert(filename != nullptr);

            FileData* ret = nullptr;
            FileData* firstEmpty = nullptr;

            for (int i = 0; i < countof(m_FileData); i++)
            {
                auto fileData = &m_FileData[i];
                if (fileData->m_Filename == nullptr)
                {
                    if (firstEmpty == nullptr)
                    {
                        firstEmpty = fileData;
                    }
                }
                else if (strcmp(fileData->m_Filename, filename) == 0)
                {
                    ret = fileData;
                    break;
                }
            }

            if (playerIndex >= 0)
            {
                if (ret == nullptr)
                {
                    assert(firstEmpty != nullptr);
                    ret = firstEmpty;
                    ret->m_Filename = YYStrDup(filename);

                    ret->m_OwnerPlayerIndex = playerIndex;
                }
                else if (ret->m_Status == EFileStatus::None)
                {
                    ret->m_OwnerPlayerIndex = playerIndex;
                }
                else
                {
                    assert(ret->m_OwnerPlayerIndex == playerIndex);
                }
            }
            else if (playerIndex < 0)
            {
                assert(ret != nullptr);
                if (ret->m_Status == EFileStatus::None)
                {
                    assert(m_OnlineState < EOnlineState::InGame);
                }
            }

            return ret;
        }

        void FileExists(RValue& result, CInstance* instance, RValue* arg, int playerIndex)
        {
            assert(playerIndex >= 0 && playerIndex < countof(m_PlayerData));

            auto filename = YYGetString(arg, 0);
            auto fileData = FindFileData(filename, playerIndex);

            if (m_OnlineState >= EOnlineState::InGame)
            {
                if (playerIndex == m_PlayerIndex)
                {
                    Script_Perform(g_gml_file_exists, instance, instance, 1, &result, arg);
                    assert(KIND_RValue(&result) == VALUE_REAL);
                    bool exists = BOOL_RValue(&result);

                    if (fileData->m_Status == EFileStatus::None)
                    {
                        fileData->m_Status = EFileStatus::Owned;

                        if (m_OnlineState == EOnlineState::InGame)
                        {
                            ReliableMessage reliableMessage;
                            reliableMessage.m_Type = exists ? EReliableMessageType::File : EReliableMessageType::FileDoesNotExist;
                            strncpy_s(reliableMessage.m_Filename, filename, countof(reliableMessage.m_Filename) - 1);
                            SendReliableMessage(reliableMessage, instance, arg);
                        }
                    }
                    else
                    {
                        assert(fileData->m_Status == EFileStatus::Owned);
                    }
                }
                else
                {
                    switch (fileData->m_Status)
                    {
                    case EFileStatus::None:
                    {
                        fileData->m_Status = EFileStatus::UnownedUnknown;
                    } [[fallthrough]];
                    case EFileStatus::UnownedUnknown:
                    {
                        init_int64(result, -2);
                    } break;
                    case EFileStatus::UnownedLoading:
                    {
                        init_int64(result, -1);
                    } break;
                    case EFileStatus::UnownedDoesNotExist:
                    {
                        init_int64(result, 0);
                    } break;
                    case EFileStatus::UnownedExists:
                    {
                        init_int64(result, 1);
                    } break;
                    default:
                        assert(false);
                    }
                }
            }
            else
            {
                if (fileData->m_Status != EFileStatus::None)
                {
                    fileData->m_Status = EFileStatus::None;
                    fileData->m_Size = 0;

                    if (fileData->m_Data != nullptr)
                    {
                        YYFree(fileData->m_Data);
                        fileData->m_Data = nullptr;
                    }
                }

                Script_Perform(g_gml_file_exists, instance, instance, 1, &result, arg);
                assert(KIND_RValue(&result) == VALUE_REAL);
            }
        }

        void SendReliableMessage(ReliableMessage& reliableMessage, CInstance* instance, RValue* arg = nullptr, ChecksumBuffer* checksumBuffer = nullptr)
        {
            auto netMessage = g_SteamNetworkingUtils->AllocateMessage(k_cbMaxSteamNetworkingSocketsMessageSizeSend);

            Writer writer(static_cast<uint8*>(netMessage->m_pData), netMessage->m_cbSize);

            if (reliableMessage.m_Type == EReliableMessageType::File)
            {
                RValue result = {};
                Script_Perform(g_gml_buffer_load, instance, instance, 1, &result, arg);
                assert(KIND_RValue(&result) == VALUE_REF);
                assert(GET_REF_TYPE(result.v64) == REFID_BUFFER);

                void* data = nullptr;
                int dataSize = 0;
                auto success = BufferGetContent(result.v32, &data, &dataSize);
                assert(success);
                assert(data != nullptr);
                assert(dataSize >= 0);

                reliableMessage.m_Size = dataSize;
                reliableMessage.Serialize(writer);
                writer.Write(data, dataSize);

                YYFree(data);

                RValue tmp = {};
                Script_Perform(g_gml_buffer_delete, instance, instance, 1, &tmp, &result);
            }
            else if (reliableMessage.m_Type == EReliableMessageType::FileDoesNotExist)
            {
                reliableMessage.Serialize(writer);
            }
            else if (reliableMessage.m_Type == EReliableMessageType::ChecksumBuffer)
            {
                reliableMessage.Serialize(writer);
                checksumBuffer->Serialize(writer);
            }

            for (int playerIndex = 0; playerIndex < countof(m_PlayerData); playerIndex++)
            {
                if (playerIndex != m_PlayerIndex)
                {
                    netMessage->m_conn = m_PlayerData[playerIndex].m_ConnectSocket;
                }
            }

            netMessage->m_cbSize = writer.m_Offset;
            netMessage->m_nFlags = k_nSteamNetworkingSend_Reliable;
            netMessage->m_idxLane = 1;

            int64 outResult;
            g_SteamNetworkingSockets->SendMessages(1, &netMessage, &outResult);
            while (outResult == -k_EResultLimitExceeded)
            {
                Timing_Sleep(1'000'000 / 120);
                trace("Resending file message\n");
                g_SteamNetworkingSockets->SendMessages(1, &netMessage, &outResult);
            }
        }

        void ReceiveReliableMessage(SteamNetworkingMessage_t* netMessage, int playerIndex, CInstance* instance)
        {
            ReliableMessage reliableMessage;
            Reader reader(static_cast<uint8*>(netMessage->m_pData), netMessage->m_cbSize);
            reliableMessage.Serialize(reader);

            if (reliableMessage.m_Type == EReliableMessageType::File || reliableMessage.m_Type == EReliableMessageType::FileDoesNotExist)
            {
                auto fileData = FindFileData(reliableMessage.m_Filename, playerIndex);

                assert(fileData->m_Status == EFileStatus::None || fileData->m_Status == EFileStatus::UnownedUnknown);
                assert(fileData->m_Data == nullptr);
                assert(fileData->m_Size == 0);

                if (reliableMessage.m_Type == EReliableMessageType::File)
                {
                    assert(reliableMessage.m_Size <= reader.m_Size);
                    auto data = YYAlloc(reliableMessage.m_Size);
                    assert(data != nullptr);
                    reader.Write(data, reliableMessage.m_Size);
                    assert(reader.m_Offset == reader.m_Size);
                    fileData->m_Data = data;
                    fileData->m_Size = reliableMessage.m_Size;
                    fileData->m_Status = EFileStatus::UnownedExists;
                }
                else
                {
                    fileData->m_Status = EFileStatus::UnownedDoesNotExist;
                }
            }
            else if (reliableMessage.m_Type == EReliableMessageType::ChecksumBuffer)
            {
                auto& playerData = m_PlayerData[playerIndex];
                auto& checksumBuffer = playerData.m_ChecksumData[reliableMessage.m_Frame & (countof(playerData.m_ChecksumData) - 1)];
                if (checksumBuffer.m_Frame != reliableMessage.m_Frame)
                {
                    reset(checksumBuffer);
                }

                checksumBuffer.Serialize(reader);
                assert(checksumBuffer.m_Frame == reliableMessage.m_Frame);

                checksumBuffer.m_ReceivedBuffer = true;
                CheckChecksum(checksumBuffer.m_Frame, instance, playerIndex);
            }

            netMessage->Release();
        }

        void BufferLoad(RValue& result, CInstance* instance, RValue* arg)
        {
            auto filename = YYGetString(arg, 0);
            auto fileData = FindFileData(filename);

            switch (fileData->m_Status)
            {
            case EFileStatus::None:
            case EFileStatus::Owned:
            {
                Script_Perform(g_gml_buffer_load, instance, instance, 1, &result, arg);
            } break;
            case EFileStatus::UnownedDoesNotExist:
            {
                init_real(result, -1);
            } break;
            case EFileStatus::UnownedExists:
            {
                assert(fileData->m_Data != nullptr);
                auto bufferIndex = CreateBuffer(fileData->m_Size, eBuffer_Format_Fixed, 1);
                assert(bufferIndex >= 0);
                BufferWriteContent(bufferIndex, 0, fileData->m_Data, fileData->m_Size);
                init_buffer(result, bufferIndex);
            } break;
            default:
                assert(false);
            }
        }

        void BufferSave(RValue& result, CInstance* instance, RValue* arg)
        {
            auto filename = YYGetString(arg, 1);
            auto fileData = FindFileData(filename);

            switch (fileData->m_Status)
            {
            case EFileStatus::None:
            case EFileStatus::Owned:
            {
                Script_Perform(g_gml_buffer_save, instance, instance, 2, &result, arg);
            } break;
            case EFileStatus::UnownedDoesNotExist:
            case EFileStatus::UnownedExists:
            {
                fileData->m_Status = EFileStatus::UnownedDoesNotExist;
                fileData->m_Size = 0;

                if (fileData->m_Data != nullptr)
                {
                    YYFree(fileData->m_Data);
                    fileData->m_Data = nullptr;
                }

                void* data = nullptr;
                int dataSize = 0;
                auto success = BufferGetContent(arg[0].v32, &data, &dataSize);
                assert(success);
                assert(data != nullptr);
                assert(dataSize >= 0);

                fileData->m_Status = EFileStatus::UnownedExists;
                fileData->m_Size = dataSize;
                fileData->m_Data = data;
            } break;
            default:
                assert(false);
            }
        }

        void FileDelete(RValue& result, CInstance* instance, RValue* arg)
        {
            auto filename = YYGetString(arg, 0);
            auto fileData = FindFileData(filename);

            switch (fileData->m_Status)
            {
            case EFileStatus::None:
            case EFileStatus::Owned:
            {
                Script_Perform(g_gml_file_delete, instance, instance, 1, &result, arg);
            } break;
            case EFileStatus::UnownedDoesNotExist:
            {
                init_real(result, 0);
            } break;
            case EFileStatus::UnownedExists:
            {
                fileData->m_Status = EFileStatus::UnownedDoesNotExist;
                fileData->m_Size = 0;

                assert(fileData->m_Data != nullptr);
                YYFree(fileData->m_Data);
                fileData->m_Data = nullptr;

                init_real(result, 1);
            } break;
            default:
                assert(false);
            }
        }

        void FileCopy(RValue& result, CInstance* instance, RValue* arg)
        {
            auto filename = YYGetString(arg, 0);
            auto newname = YYGetString(arg, 1);

            auto fileData = FindFileData(filename);
            auto newData = FindFileData(newname);
            assert(fileData != newData);

            switch (fileData->m_Status)
            {
            case EFileStatus::None:
            case EFileStatus::Owned:
            {
                assert(newData->m_Status == fileData->m_Status);
                Script_Perform(g_gml_file_copy, instance, instance, 2, &result, arg);
            } break;
            case EFileStatus::UnownedDoesNotExist:
            {
            } break;
            case EFileStatus::UnownedExists:
            {
                assert(newData->m_Status == EFileStatus::UnownedDoesNotExist);
                assert(newData->m_Data == nullptr);
                assert(fileData->m_Data != nullptr);

                auto dataSize = fileData->m_Size;
                auto data = YYAlloc(dataSize);
                assert(data != nullptr);
                memcpy(data, fileData->m_Data, dataSize);
                newData->m_Status = EFileStatus::UnownedExists;
                newData->m_Size = dataSize;
                newData->m_Data = data;
            } break;
            default:
                assert(false);
            }
        }

        void FileStatus(RValue& result, const char* filename)
        {
            auto fileData = FindFileData(filename, -2);
            auto status = fileData != nullptr ? fileData->m_Status : EFileStatus::None;

            switch (status)
            {
            case EFileStatus::None:
            {
                init_bool(result, 0);
            } break;
            case EFileStatus::Owned:
            {
                init_real(result, 0);
            } break;
            case EFileStatus::UnownedUnknown:
            case EFileStatus::UnownedLoading:
            case EFileStatus::UnownedDoesNotExist:
            case EFileStatus::UnownedExists:
            {
                init_int64(result, 0);
            } break;
            default:
                assert(false);
            }
        }
    };
}

YYEXPORT void YYExtensionInitialise(const struct YYRunnerInterface* _pFunctions, size_t _functions_size)
{
    if (_functions_size < sizeof(YYRunnerInterface))
    {
        YYError("YYRunnerInterface size mismatch in extension DLL!");
        return;
    }

    memcpy(&g_RunnerInterface, _pFunctions, sizeof(YYRunnerInterface));
    g_pYYRunnerInterface = &g_RunnerInterface;

    auto version = extGetVersion(c_ExtensionName);
    if (strncmp(version, c_ExtensionVersion, countof(c_ExtensionVersion) - 1) != 0)
    {
        char msg[1024];
        sprintf_s(msg, "Error: Version mismatch detected between PFO.dll (v%s) and data.win (v%s).\n\nPlease make sure you have copied PFO.dll into the same folder as data.win, and that both are the from the same version of the mod.", c_ExtensionVersion, version);
        ShowMessage(msg);
        ExitProcess(0);
    }

    constexpr auto get_builtin_function = [](const char* name) -> int
        {
            int ret;
            Code_Function_Find(name, &ret);
            assert(ret != -1);
            return ret;
        };

    g_gml_random_set_seed = get_builtin_function("random_set_seed");
    g_gml_random_get_seed = get_builtin_function("random_get_seed");
    g_gml_randomize = get_builtin_function("randomize");
    g_gml_file_exists = get_builtin_function("file_exists");
    g_gml_buffer_load = get_builtin_function("buffer_load");
    g_gml_buffer_save = get_builtin_function("buffer_save");
    g_gml_buffer_delete = get_builtin_function("buffer_delete");
    g_gml_buffer_seek = get_builtin_function("buffer_seek");
    g_gml_file_delete = get_builtin_function("file_delete");
    g_gml_file_copy = get_builtin_function("file_copy");

    g_SteamFriends = SteamFriends();
    g_SteamMatchmaking = SteamMatchmaking();
    g_SteamNetworkingSockets = SteamNetworkingSockets();
    g_SteamNetworkingUtils = SteamNetworkingUtils();

    g_pfo = new PFO();

    g_GmlChecksumBuffer1 = CreateBuffer(sizeof(ChecksumBuffer::m_GmlBuffer), eBuffer_Format_Fixed, 1);
    g_GmlChecksumBuffer2 = CreateBuffer(sizeof(ChecksumBuffer::m_GmlBuffer), eBuffer_Format_Fixed, 1);

    g_SteamNetworkingUtils->InitRelayNetworkAccess();
    g_SteamNetworkingSockets->InitAuthentication();

    DebugConsoleOutput("PFO 50 YYExtensionInitialise CONFIGURED\n");
}

YYEXPORT void pfo_update(RValue& result, CInstance* selfinst, CInstance* otherinst, int argc, RValue* arg)
{
    assert(argc == 0);

    g_pfo->Update(selfinst);
    init_bool(result, true);
}

YYEXPORT void pfo_get_input(RValue& result, CInstance* selfinst, CInstance* otherinst, int argc, RValue* arg)
{
    assert(argc == 1);

    int playerIndex = YYGetInt32(arg, 0);
    init_int64(result, g_pfo->GetInput(selfinst, playerIndex));
}

YYEXPORT void pfo_is_online(RValue& result, CInstance* selfinst, CInstance* otherinst, int argc, RValue* arg)
{
    assert(argc == 0);

    init_bool(result, g_pfo->m_OnlineState >= EOnlineState::InGame);
}

YYEXPORT void pfo_get_input_delay(RValue& result, CInstance* selfinst, CInstance* otherinst, int argc, RValue* arg)
{
    assert(argc <= 1);

    auto playerIndex = argc > 0 ? YYGetInt32(arg, 0) : g_pfo->m_PlayerIndex;
    assert(playerIndex >= 0 && playerIndex < countof(g_pfo->m_PlayerData));

    init_real(result, g_pfo->m_PlayerData[playerIndex].m_InputDelay);
}

YYEXPORT void pfo_get_input_delay_mode(RValue& result, CInstance* selfinst, CInstance* otherinst, int argc, RValue* arg)
{
    assert(argc == 0);

    init_real(result, static_cast<int>(g_pfo->m_InputDelayMode));
}

YYEXPORT void pfo_request_input_delay_change(RValue& result, CInstance* selfinst, CInstance* otherinst, int argc, RValue* arg)
{
    assert(argc >= 1 && argc <= 2);

    auto mode = YYGetInt64(arg, 0);
    auto delay = argc > 1 ? YYGetInt32(arg, 1) : -1;

    g_pfo->RequestInputDelayChange(static_cast<EInputDelayMode>(mode), delay);
}

YYEXPORT void pfo_set_input_delay_favored_player_index(RValue& result, CInstance* selfinst, CInstance* otherinst, int argc, RValue* arg)
{
    assert(argc == 1);

    int playerIndex = static_cast<int>(YYGetInt32(arg, 0));
    assert(playerIndex >= -1 && playerIndex < static_cast<int>(countof(g_pfo->m_PlayerData)));

    g_pfo->m_RequestedInputDelayFavoredPlayerIndex = playerIndex;
}

YYEXPORT void pfo_randomize(RValue& result, CInstance* selfinst, CInstance* otherinst, int argc, RValue* arg)
{
    assert(argc == 0);

    g_pfo->Randomize(result, selfinst);
}

YYEXPORT void pfo_get_frame(RValue& result, CInstance* selfinst, CInstance* otherinst, int argc, RValue* arg)
{
    assert(argc == 0);

    init_real(result, g_pfo->m_Frame);
}

YYEXPORT void pfo_init(RValue& result, CInstance* selfinst, CInstance* otherinst, int argc, RValue* arg)
{
    assert(argc == 0);

    constexpr auto get_script_callback = [](const char* optionName) -> int
        {
            auto name = extOptGetString(c_ExtensionName, optionName);
            if (name == nullptr || name[0] == '\0' || strncmp(name, "undefined", 9) == 0)
            {
                YYError("Missing extension option '%s'", optionName);
            }

            int ret = Script_Find_Id(name);
            if (ret == -1)
            {
                YYError("Failed to find script callback '%s' for extension option '%s'", name, optionName);
            }
            return ret;
        };

    g_gml_Script_onlineStateChangedCallback = get_script_callback("onlineStateChangedCallback");
    g_gml_Script_getInputCallback = get_script_callback("getInputCallback");
    g_gml_Script_getChecksumCallback = get_script_callback("getChecksumCallback");

    init_bool(result, 1);
}

YYEXPORT void pfo_get_online_player_index(RValue& result, CInstance* selfinst, CInstance* otherinst, int argc, RValue* arg)
{
    assert(argc == 0);

    if (g_pfo->m_OnlineState >= EOnlineState::InGame)
    {
        init_real(result, g_pfo->m_PlayerIndex);
    }
    else
    {
        init_real(result, -1);
    }
}

YYEXPORT void pfo_file_exists(RValue& result, CInstance* selfinst, CInstance* otherinst, int argc, RValue* arg)
{
    assert(argc > 0 && argc <= 2);

    int playerIndex = argc > 1 ? YYGetInt32(arg, 1) : 0;

    g_pfo->FileExists(result, selfinst, arg, playerIndex);
}

YYEXPORT void pfo_buffer_load(RValue& result, CInstance* selfinst, CInstance* otherinst, int argc, RValue* arg)
{
    assert(argc == 1);

    g_pfo->BufferLoad(result, selfinst, arg);
}

YYEXPORT void pfo_buffer_save(RValue& result, CInstance* selfinst, CInstance* otherinst, int argc, RValue* arg)
{
    assert(argc == 2);
    assert(KIND_RValue(&arg[0]) == VALUE_REF);
    assert(GET_REF_TYPE(arg[0].v64) == REFID_BUFFER);

    g_pfo->BufferSave(result, selfinst, arg);
}

YYEXPORT void pfo_file_delete(RValue& result, CInstance* selfinst, CInstance* otherinst, int argc, RValue* arg)
{
    assert(argc == 1);

    g_pfo->FileDelete(result, selfinst, arg);
}

YYEXPORT void pfo_file_copy(RValue& result, CInstance* selfinst, CInstance* otherinst, int argc, RValue* arg)
{
    assert(argc == 2);

    g_pfo->FileCopy(result, selfinst, arg);
}

YYEXPORT void pfo_file_status(RValue& result, CInstance* selfinst, CInstance* otherinst, int argc, RValue* arg)
{
    assert(argc == 1);

    g_pfo->FileStatus(result, YYGetString(arg, 0));
}

YYEXPORT void pfo_quit(RValue& result, CInstance* selfinst, CInstance* otherinst, int argc, RValue* arg)
{
    assert(argc == 0);

    g_pfo->Quit();
}

YYEXPORT void pfo_steam_lobby_get_member_data(RValue& result, CInstance* selfinst, CInstance* otherinst, int argc, RValue* arg)
{
    assert(argc == 3);
    auto lobbyId = CSteamID(static_cast<uint64>(YYGetInt64(arg, 0)));
    auto steamId = CSteamID(static_cast<uint64>(YYGetInt64(arg, 1)));
    auto key = YYGetString(arg, 2);

    auto value = g_SteamMatchmaking->GetLobbyMemberData(lobbyId, steamId, key);
    YYCreateString(&result, value);
}

YYEXPORT void pfo_steam_lobby_set_member_data(RValue& result, CInstance* selfinst, CInstance* otherinst, int argc, RValue* arg)
{
    assert(argc == 3);
    auto lobbyId = CSteamID(static_cast<uint64>(YYGetInt64(arg, 0)));
    auto key = YYGetString(arg, 1);
    auto value = YYGetString(arg, 2);

    g_SteamMatchmaking->SetLobbyMemberData(lobbyId, key, value);
}

YYEXPORT void pfo_steam_lobby_set_game_server(RValue& result, CInstance* selfinst, CInstance* otherinst, int argc, RValue* arg)
{
    assert(argc == 4);
    auto lobbyId = CSteamID(static_cast<uint64>(YYGetInt64(arg, 0)));
    auto serverIp = static_cast<uint32>(YYGetInt32(arg, 1));
    auto serverPort = static_cast<uint16>(YYGetInt32(arg, 2));
    auto serverId = CSteamID(static_cast<uint64>(YYGetInt64(arg, 3)));

    g_SteamMatchmaking->SetLobbyGameServer(lobbyId, serverIp, serverPort, serverId);
}

YYEXPORT void pfo_create_listen_socket(RValue& result, CInstance* selfinst, CInstance* otherinst, int argc, RValue* arg)
{
    assert(argc == 0);

    if (g_pfo->m_ListenSocket != k_HSteamListenSocket_Invalid)
    {
        g_SteamNetworkingSockets->CloseListenSocket(g_pfo->m_ListenSocket);
        g_pfo->m_ListenSocket = k_HSteamListenSocket_Invalid;
    }

    g_pfo->m_ListenSocket = g_SteamNetworkingSockets->CreateListenSocketP2P(0, 0, nullptr);
    if (g_pfo->m_ListenSocket == k_HSteamListenSocket_Invalid) YYError("Listen socket invalid");
}

YYEXPORT void pfo_connect(RValue& result, CInstance* selfinst, CInstance* otherinst, int argc, RValue* arg)
{
    assert(argc == 1);
    SteamNetworkingIdentity identity;
    identity.SetSteamID64(YYGetInt64(arg, 0));

    g_pfo->m_PlayerIndex = 1;
    auto hConn = g_SteamNetworkingSockets->ConnectP2P(identity, 0, 0, nullptr);
    if (hConn != k_HSteamNetConnection_Invalid)
    {
        g_pfo->AddConnection(0, hConn);
    }
    else
    {
        YYError("Connect socket invalid");
    }
}

YYEXPORT void pfo_set_seed(RValue& result, CInstance* selfinst, CInstance* otherinst, int argc, RValue* arg)
{
    assert(argc == 1);
    auto seed = YYGetInt64(arg, 0);

    g_pfo->m_RNG[0] = seed;
    ReleaseConsoleOutput("Set random seed: %llx\n", seed);
}

YYEXPORT void pfo_get_ping(RValue& result, CInstance* selfinst, CInstance* otherinst, int argc, RValue* arg)
{
    assert(argc == 0);

    int ping = -2;
    for (int i = 0; i < countof(g_pfo->m_PlayerData); i++)
    {
        auto& playerData = g_pfo->m_PlayerData[i];
        if (playerData.m_ConnectSocket != k_HSteamNetConnection_Invalid)
        {
            SteamNetConnectionRealTimeStatus_t status;
            g_SteamNetworkingSockets->GetConnectionRealTimeStatus(playerData.m_ConnectSocket, &status, 0, nullptr);
            ping = status.m_nPing;
            break;
        }
    }

    init_real(result, ping);
}
