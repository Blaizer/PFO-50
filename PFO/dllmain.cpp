#include "pch.h"

#define __YYDEFINE_EXTENSION_FUNCTIONS__
#include "Extension_Interface.h"
#include "Ref.h"
#include "YYRValue.h"
#define YYEXPORT __declspec(dllexport)

#pragma warning(default: 4365)
#pragma warning(default: 4388)
#pragma warning(default: 4800)
//#pragma warning(default: 4820)

#include "../version.h"

YYRunnerInterface* g_pYYRunnerInterface;

static char g_TempBuffer[8192];

#if !defined(NDEBUG) || 1
#define breakpoint() (IsDebuggerPresent() && (__debugbreak(), false))
#define assert(a) if (!(a)) { [[unlikely]] handle_assert(#a, __FILE__, __FUNCTION__, __LINE__); __debugbreak(); unreachable(); } else { [[likely]]; }
#else
#define breakpoint() (false)
#define assert(a) (__assume(a))
#endif

#define countof(a) static_cast<intptr_t>((sizeof(a) / sizeof((a)[0])))

template<typename T>
FORCEINLINE static constexpr T min(T a, T b)
{
    return (a < b) ? a : b;
}
template<typename T>
FORCEINLINE static constexpr T max(T a, T b)
{
    return (a > b) ? a : b;
}

[[noreturn]] FORCEINLINE static void unreachable() { __assume(false); }

static void trace(const char* format, ...)
{
    va_list args;

    va_start(args, format);
    int length = vsprintf_s(g_TempBuffer, format, args);
    va_end(args);

    OutputDebugStringA(g_TempBuffer);
}

static void handle_assert(const char* assertion, const char* file, const char* function, int line)
{
    file = max(file, strrchr(file, '/') + 1);
    file = max(file, strrchr(file, '\\') + 1);

    constexpr char anonymous[] = "`anonymous-namespace'::";
    constexpr size_t anonymousLength = countof(anonymous) - 1;
    if (strncmp(function, anonymous, anonymousLength) == 0)
    {
        function += anonymousLength;
    }

    trace("Assertion failed (%s) at %s %s() line %d\n", assertion, file, function, line);

    if (!IsDebuggerPresent())
    {
        sprintf_s(g_TempBuffer, "Assertion failed! (%s)\n\nAt %s %s() line %d", assertion, file, function, line);
        ShowMessage(g_TempBuffer);
    }
}

template<typename T>
FORCEINLINE static constexpr T narrow_cast(auto a)
{
    static_assert(sizeof(T) < sizeof(a));
    assert(a == static_cast<decltype(a)>(static_cast<T>(a)));
    return static_cast<T>(a);
}

namespace
{
    constexpr char c_ExtensionName[] = "PFO";
    constexpr char c_ExtensionVersion[] = MOD_VERSION;

    constexpr int64 c_TimeBeforeResendingMessage = 1'000'000 / 60;
    constexpr int64 c_WaitForInputsDelayTime = 1'000'000 / 60;

    constexpr int c_TargetFPS = 60;
    constexpr int c_MaxInputDelay = 20;
    constexpr int c_SamplesNeededBeforeRaisingDelay = 60 * 15;
    constexpr int c_SamplesNeededBeforeLoweringDelay = 60 * 45;
    constexpr int c_PercentSamplesWithinDelayToMaintainDelay = 92;
    constexpr int c_PercentSamplesBelowDelayToLowerDelay = 99;
    constexpr int c_PingDelayCalculationSafetyMargin = 30;
    constexpr int c_FavoredModeExtraInputDelay = 2;
    constexpr int c_MessageInputCountBits = 6;
    constexpr int c_FramesToRunBehindUpperLimit = 70;
    constexpr int c_FramesToRunBehindLowerLimit = 40;

    YYRunnerInterface g_RunnerInterface;

    ISteamFriends* g_SteamFriends;
    ISteamMatchmaking* g_SteamMatchmaking;
    ISteamNetworkingSockets* g_SteamNetworkingSockets;
    ISteamNetworkingUtils* g_SteamNetworkingUtils;

    int g_GmlChecksumBuffer1;
    int g_GmlChecksumBuffer2;

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
        size_t m_Size = 0;

        template<typename T>
        constexpr void Write(const T& value)
        {
            m_Size += sizeof(T);
        }

        constexpr void Write(const void* buffer, size_t size)
        {
            m_Size += size;
        }

        constexpr void SetName(const char* name) {}
        constexpr void SetName(const char* name, int i, const char* iName) {}
    };

    struct Writer
    {
        uint8* m_Buffer;
        size_t m_Size;
        size_t m_Offset = 0;

        Writer() = delete;
        Writer(uint8* buffer, size_t size) : m_Buffer(buffer), m_Size(size) {}

        template<typename T>
        void Write(const T& value)
        {
            assert(m_Offset + sizeof(T) <= m_Size);
            memcpy(m_Buffer + m_Offset, &value, sizeof(T));
            m_Offset += sizeof(T);
        }

        void Write(const void* buffer, size_t size)
        {
            assert(m_Offset + size <= m_Size);
            memcpy(m_Buffer + m_Offset, buffer, size);
            m_Offset += size;
        }

        constexpr void SetName(const char* name) {}
        constexpr void SetName(const char* name, int i, const char* iName) {}
    };

    struct Reader
    {
        const uint8* m_Buffer;
        size_t m_Size;
        size_t m_Offset = 0;

        Reader() = delete;
        Reader(const uint8* buffer, size_t size) : m_Buffer(buffer), m_Size(size) {}

        template<typename T>
        void Write(T& value)
        {
            assert(m_Offset + sizeof(T) <= m_Size);
            memcpy(&value, m_Buffer + m_Offset, sizeof(T));
            m_Offset += sizeof(T);
        }

        void Write(void* buffer, size_t size)
        {
            assert(m_Offset + size <= m_Size);
            memcpy(buffer, m_Buffer + m_Offset, size);
            m_Offset += size;
        }

        constexpr void SetName(const char* name) {}
        constexpr void SetName(const char* name, int i, const char* iName) {}
    };

    struct StringWriter
    {
        char* m_String;
        size_t m_Size;
        size_t m_Offset = 0;

        StringWriter() = delete;
        StringWriter(char* str, size_t count) : m_String(str), m_Size(count) {}

        template<typename T>
        void Write(const T& value)
        {
            m_Offset += sprintf_s(m_String + m_Offset, m_Size - m_Offset, "%d\n", value);
        }

        void SetName(const char* name)
        {
            m_Offset += sprintf_s(m_String + m_Offset, m_Size - m_Offset, "  %s: ", name);
        }

        void SetName(const char* name, int i, const char* iName)
        {
            m_Offset += sprintf_s(m_String + m_Offset, m_Size - m_Offset, "  ");
            m_Offset += WriteName(m_String + m_Offset, m_Size - m_Offset, name, i, iName);
            m_Offset += sprintf_s(m_String + m_Offset, m_Size - m_Offset, ": ");
        }

        static int WriteName(char* str, size_t count, const char* name, int i, const char* iName)
        {
            auto p = name;
            size_t iNameLength = strlen(iName);

            while (true)
            {
                p = strstr(p, "[");
                if (p == nullptr)
                {
                    break;
                }

                p++;
                if (strncmp(p, iName, iNameLength) == 0)
                {
                    return sprintf_s(str, count, "%.*s%d%s", narrow_cast<uint32>(p - name), name, i, p + iNameLength);
                }
            }

            assert(false);
        }
    };

    template<typename T>
    struct Differ
    {
        const uint8* m_Buffer1;
        const uint8* m_Buffer2;
        const char* m_Name = nullptr;
        const char* m_IteratorName = nullptr;
        size_t m_Size;
        size_t m_Offset = 0;
        T m_Diff1;
        T m_Diff2;
        int m_Iterator = 0;

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

            if (memcmp(m_Buffer1 + m_Offset, m_Buffer2 + m_Offset, sizeof(T)) != 0)
            {
                char* str = g_TempBuffer;
                char* end = g_TempBuffer + countof(g_TempBuffer);

                str += sprintf_s(str, end - str, "Desync detected!\n\nDifference in PFO value: ");
                
                if (m_IteratorName != nullptr)
                {
                    str += StringWriter::WriteName(str, end - str, m_Name, m_Iterator, m_IteratorName);
                }
                else
                {
                    str += sprintf_s(str, end - str, "%s", m_Name);
                }

                str += sprintf_s(str, end - str, "\n\nOurs:\n");

                StringWriter nameWriter1(str, static_cast<int>(end - str));
                m_Diff1.Serialize(nameWriter1);
                str += nameWriter1.m_Offset;

                str += sprintf_s(str, end - str, "\nTheirs:\n");

                StringWriter nameWriter2(str, static_cast<int>(end - str));
                m_Diff2.Serialize(nameWriter2);
                str += nameWriter2.m_Offset;

                ShowMessage(g_TempBuffer);

                breakpoint();
            }

            m_Offset += sizeof(T);
        }

        constexpr void SetName(const char* name)
        {
            m_Name = name;
            m_IteratorName = nullptr;
        }

        constexpr void SetName(const char* name, int i, const char* iName)
        {
            m_Name = name;
            m_Iterator = i;
            m_IteratorName = iName;
        }
    };

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
        Automatic,
        Manual,

        COUNT
    };

    struct AutomaticInputDelayChange
    {
        uint32 m_InputFrame = 0;
        int m_InputDelay = 0;
    };

    struct MessageAutomaticInputDelayChange
    {
        uint8 m_InputFrameIndex = 0;
        uint8 m_InputDelay = 0;
    };

    struct Checksum
    {
        uint32 m_Frame = 0;
        EInputDelayMode m_InputDelayMode = {};
        int m_InputDelayFavoredClientIndex = 0;
        int m_MinAutomaticInputDelay = 0;
        int m_MaxAutomaticInputDelay = 0;
        int m_PlayerIndexToClientIndexMap[2] = {};

        struct
        {
            int m_InputDelay = 0;
            int m_AutomaticInputDelay = 0;
            InputFlags_t m_InputBuffer[2] = {};
            AutomaticInputDelayChange m_AutomaticInputDelayChanges[countof(m_InputBuffer)];
        } m_ClientData[2];

        template<typename T>
        constexpr void Serialize(T& writer)
        {
            #define WRITE_NAMED(val) writer.SetName(#val); writer.Write(val);
            #define WRITE_NAMED_INDEX(val, i) writer.SetName(#val, i, #i); writer.Write(val);

            WRITE_NAMED(m_Frame);
            WRITE_NAMED(m_InputDelayMode);
            WRITE_NAMED(m_InputDelayFavoredClientIndex);
            WRITE_NAMED(m_MinAutomaticInputDelay);
            WRITE_NAMED(m_MaxAutomaticInputDelay);

            for (int i = 0; i < countof(m_PlayerIndexToClientIndexMap); i++)
            {
                WRITE_NAMED_INDEX(m_PlayerIndexToClientIndexMap[i], i);
            }

            for (int clientIndex = 0; clientIndex < countof(m_ClientData); clientIndex++)
            {
                WRITE_NAMED_INDEX(m_ClientData[clientIndex].m_InputDelay, clientIndex);
                WRITE_NAMED_INDEX(m_ClientData[clientIndex].m_AutomaticInputDelay, clientIndex);

                auto& clientData = m_ClientData[clientIndex];

                for (int i = 0; i < countof(clientData.m_InputBuffer); i++)
                {
                    WRITE_NAMED_INDEX(clientData.m_InputBuffer[i], i);
                }

                for (int i = 0; i < countof(clientData.m_AutomaticInputDelayChanges); i++)
                {
                    WRITE_NAMED_INDEX(clientData.m_AutomaticInputDelayChanges[i].m_InputFrame, i);
                    WRITE_NAMED_INDEX(clientData.m_AutomaticInputDelayChanges[i].m_InputDelay, i);
                }
            }

            #undef WRITE_NAMED
            #undef WRITE_NAMED_INDEX
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
        bool m_HasBuffer = false;
        uint8 m_PfoBuffer[Checksum::GetMaxSize()] = {};
        uint8 m_GmlBuffer[64] = {};

        constexpr void Reset()
        {
            m_Frame = 0;
            m_Checksum = 0;
            m_PfoBufferSize = 0;
            m_GmlBufferSize = 0;
            m_HasBuffer = false;
        }

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

    ChecksumBuffer g_tempChecksumBuffer;

    template<int NSampleCount, int NBucketCount>
    struct Histogram
    {
        int m_SampleCount = 0;
        int m_SampleHead = 0;
        int m_Samples[NSampleCount] = {};
        int m_Buckets[NBucketCount] = {};

        void AddSample(int sample)
        {
            if (m_SampleCount >= countof(m_Samples))
            {
                int oldSample = m_Samples[m_SampleHead];
                assert(m_Buckets[oldSample] > 0);
                m_Buckets[oldSample]--;
            }
            else
            {
                m_SampleCount++;
            }

            if (sample < 0)
            {
                sample = 0;
            }
            else if (sample >= countof(m_Buckets))
            {
                sample = countof(m_Buckets) - 1;
            }

            m_Samples[m_SampleHead++] = sample;
            if (m_SampleHead >= countof(m_Samples))
            {
                m_SampleHead = 0;
            }

            m_Buckets[sample]++;
        }

        int CalculatePercentile(int percentile)
        {
            assert(percentile >= 0 && percentile <= 100);
            assert(m_SampleCount > 0);

            int target = m_SampleCount * percentile / 100;
            int cumulative = 0;

            for (int i = 0; i < countof(m_Buckets); i++)
            {
                if (m_Buckets[i] > 0)
                {
                    cumulative += m_Buckets[i];
                    if (cumulative >= target)
                    {
                        return i;
                    }
                }
            }

            assert(false);
        }

        void CalculateDistributionAt(int sample, int* belowPercent, int* belowOrEqualPercent)
        {
            assert(sample < countof(m_Buckets));
            assert(m_SampleCount > 0);

            int total = 0;
            for (int i = 0; i <= sample; i++)
            {
                total += m_Buckets[i];
            }
            int belowTotal = total - m_Buckets[sample];

            *belowOrEqualPercent = (total * 100 + 50) / m_SampleCount;
            *belowPercent = (belowTotal * 100 + 50) / m_SampleCount;
        }
    };

    struct PlayerData
    {
        int m_PlayerIndex = -1;
        uint32 m_TailInputFrame = 0;
        InputFlags_t m_InputBuffer[128] = {};
    };

    struct ClientData
    {
        int64 m_LastSentTime = 0;
        int64 m_LastSentMessageNumber = 0;
        int64 m_LastReceivedMessageNumber = 0;
        int64 m_LastAcknowledgedMessageNumber = 0;
        int64 m_PreviousFrameLastAcknowledgedMessageNumber = 0;
        int64 m_PreviousFrameSampleRTT = 0;
        MessageSendInfo m_MessageSendData[512];
        HSteamNetConnection m_ConnectSocket = k_HSteamNetConnection_Invalid;
        uint32 m_LastInputFrame = 0;
        uint32 m_LastAcknowledgedInputFrame = 0;
        uint32 m_LastSentFrame = 0;
        uint32 m_LastReceivedChecksumFrame = 0;
        int m_InputDelay = 0;
        int m_AutomaticInputDelay = 0;
        PlayerData m_PlayerData;
        AutomaticInputDelayChange m_AutomaticInputDelayChanges[countof(m_PlayerData.m_InputBuffer)];
        Histogram<c_SamplesNeededBeforeRaisingDelay, c_MaxInputDelay + 1> m_RaiseDelayHistogram;
        Histogram<c_SamplesNeededBeforeLoweringDelay, c_MaxInputDelay + 1> m_LowerDelayHistogram;
        ChecksumBuffer m_ChecksumData[256] = { { .m_Frame = 1 } };
        bool m_HasReceivedUnacknowledgedInputFrames = false;
        bool m_IsRunningTooFarBehind;
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
        int m_OwnerClientIndex = 0;
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
        union
        {
            int m_Size;
            uint32 m_Frame;
        };
        EReliableMessageType m_Type;
        char m_Filename[MAX_PATH + 1];

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
        uint32 m_FirstInputFrame = 0;
        uint32 m_Checksum = 0;
        InputFlags_t m_Inputs[1 << c_MessageInputCountBits] = {};
        MessageAutomaticInputDelayChange m_AutomaticInputDelayChanges[countof(m_Inputs)];
        uint8 m_InputFrameCount = 0;
        int8 m_ChecksumFrameDelta = 0;

        template<typename T>
        constexpr void Serialize(T& writer)
        {
            writer.Write(m_LastReceivedMessageNumber);
            writer.Write(m_FirstInputFrame);
            writer.Write(m_InputFrameCount);
            writer.Write(m_ChecksumFrameDelta);
            writer.Write(m_Checksum);

            assert(m_InputFrameCount <= countof(m_Inputs));
            for (int i = 0; i < m_InputFrameCount; i++)
            {
                writer.Write(m_Inputs[i]);
            }

            for (int i = 0; i < countof(m_AutomaticInputDelayChanges); i++)
            {
                writer.Write(m_AutomaticInputDelayChanges[i].m_InputFrameIndex);
                if (m_AutomaticInputDelayChanges[i].m_InputFrameIndex != 0)
                {
                    writer.Write(m_AutomaticInputDelayChanges[i].m_InputDelay);
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
            message.m_InputFrameCount = countof(message.m_Inputs);

            for (int i = 0; i < countof(message.m_AutomaticInputDelayChanges); i++)
            {
                message.m_AutomaticInputDelayChanges[i].m_InputFrameIndex = 0xff;
            }

            message.Serialize(size);
            return size.m_Size;
        }
    };

    struct ClientPlayer
    {
        ClientData* m_ClientData = nullptr;
        PlayerData* m_PlayerData = nullptr;
    };

    struct PFO
    {
        ClientData m_ClientData[2];
        int m_PlayerIndexToClientIndexMap[2] = { -1, -1 };
        FileData m_FileData[32];
        
        HSteamListenSocket m_ListenSocket = k_HSteamListenSocket_Invalid;

        EOnlineState m_OnlineState = EOnlineState::Offline;
        uint32 m_Frame = 0;
        int m_ClientIndex = 0;
        int m_AssignedClientsCount = 0;
        int m_PreviousFrameAssignedClientsCount = 0;

        uint32 m_LastSentChecksumFrame = 0;

        EInputDelayMode m_InputDelayMode = {};
        uint32 m_RequestedAutomaticInputDelayFrame = 0;
        int m_RequestedAutomaticInputDelay = 0;
        int m_InputDelayFavoredClientIndex = -1;
        int m_MinAutomaticInputDelay = 0;
        int m_MaxAutomaticInputDelay = c_MaxInputDelay;

        void Update(CInstance* instance, bool advanceFrame)
        {
            if (m_OnlineState == EOnlineState::StartingGame)
            {
                m_OnlineState = EOnlineState::InGame;

                {
                    RValue result = {};
                    RValue arg;
                    init_int64(arg, 1);
                    Script_Perform(g_gml_Script_onlineStateChangedCallback, instance, instance, 1, &result, &arg);
                }
            }

            if (m_OnlineState == EOnlineState::InGame)
            {
                if (advanceFrame)
                {
                    m_Frame++;
                }

                // write checksum
                {
                    ChecksumBuffer* pChecksumBuffer;
                    if (advanceFrame)
                    {
                        auto& myClientData = m_ClientData[m_ClientIndex];
                        pChecksumBuffer = &myClientData.m_ChecksumData[m_Frame & (countof(myClientData.m_ChecksumData) - 1)];
                    }
                    else
                    {
                        pChecksumBuffer = &g_tempChecksumBuffer;
                    }

                    auto& checksumBuffer = *pChecksumBuffer;
                    checksumBuffer.Reset();
                    checksumBuffer.m_Frame = m_Frame;
                    checksumBuffer.m_HasBuffer = true;

                    {
                        Checksum checksum;
                        checksum.m_Frame = m_Frame;
                        checksum.m_InputDelayMode = m_InputDelayMode;
                        checksum.m_InputDelayFavoredClientIndex = m_InputDelayFavoredClientIndex;
                        checksum.m_MinAutomaticInputDelay = m_MinAutomaticInputDelay;
                        checksum.m_MaxAutomaticInputDelay = m_MaxAutomaticInputDelay;
                        copy(checksum.m_PlayerIndexToClientIndexMap, m_PlayerIndexToClientIndexMap);

                        for (int clientIndex = 0; clientIndex < countof(checksum.m_ClientData); clientIndex++)
                        {
                            auto& clientData = m_ClientData[clientIndex];
                            auto& playerData = clientData.m_PlayerData;
                            auto& checksumPlayerData = checksum.m_ClientData[clientIndex];

                            checksumPlayerData.m_InputDelay = clientData.m_InputDelay;
                            checksumPlayerData.m_AutomaticInputDelay = clientData.m_AutomaticInputDelay;

                            for (int i = 0; i < countof(checksumPlayerData.m_InputBuffer); i++)
                            {
                                checksumPlayerData.m_InputBuffer[i] = playerData.m_InputBuffer[(m_Frame - i - 1) & (countof(playerData.m_InputBuffer) - 1)];
                            }

                            for (int i = 0; i < countof(checksumPlayerData.m_AutomaticInputDelayChanges); i++)
                            {
                                auto& changeRequest = clientData.m_AutomaticInputDelayChanges[(m_Frame - i - 1) & (countof(clientData.m_AutomaticInputDelayChanges) - 1)];
                                if (changeRequest.m_InputFrame == m_Frame - i - 1)
                                {
                                    checksumPlayerData.m_AutomaticInputDelayChanges[i] = changeRequest;
                                }
                            }
                        }

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

                    CheckChecksum(checksumBuffer, instance);
                }

                // handle unasigned players
                if (advanceFrame)
                {
                    auto& myClientData = m_ClientData[m_ClientIndex];
                    auto& myPlayerData = myClientData.m_PlayerData;

                    for (int clientIndex = 0; clientIndex < countof(m_ClientData); clientIndex++)
                    {
                        auto& clientData = m_ClientData[clientIndex];
                        auto& playerData = clientData.m_PlayerData;

                        // if all our inputs have been acknowledged while we were assigned, then we can automatically acknowledge unasigned inputs up to this frame
                        if (myPlayerData.m_PlayerIndex < 0)
                        {
                            if (clientData.m_LastAcknowledgedInputFrame >= myClientData.m_LastInputFrame && clientData.m_LastAcknowledgedInputFrame < m_Frame)
                            {
                                clientData.m_LastAcknowledgedInputFrame = m_Frame;
                                //trace("auto acked frame %u for client %d\n", m_Frame, clientIndex);
                            }
                        }

                        if (playerData.m_PlayerIndex < 0)
                        {
                            // zero input of any unasigned players
                            playerData.m_InputBuffer[m_Frame & (countof(playerData.m_InputBuffer) - 1)] = 0;
                            if (playerData.m_TailInputFrame < m_Frame)
                            {
                                assert(playerData.m_TailInputFrame == m_Frame - 1);
                                playerData.m_TailInputFrame = m_Frame;
                            }
                            //trace("zeroed input on frame %u for client %d\n", m_Frame, clientIndex);

                            // invalidate delay changes for unasigned players
                            auto& delayChange = clientData.m_AutomaticInputDelayChanges[m_Frame & (countof(clientData.m_AutomaticInputDelayChanges) - 1)];
                            if (delayChange.m_InputFrame == m_Frame)
                            {
                                delayChange.m_InputFrame++;
                                //trace("invalidated delay change on frame %u for client %d\n", m_Frame, clientIndex);
                            }
                        }
                    }
                }

                // get local player input
                if (advanceFrame)
                {
                    auto& myClientData = m_ClientData[m_ClientIndex];
                    auto& myPlayerData = myClientData.m_PlayerData;

                    if (myPlayerData.m_PlayerIndex >= 0)
                    {
                        uint32 frame = myPlayerData.m_TailInputFrame + 1;
                        uint32 lastFrame = m_Frame + myClientData.m_InputDelay;
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
                                assert(frame - m_Frame < countof(myPlayerData.m_InputBuffer));
                                myPlayerData.m_InputBuffer[frame & (countof(myPlayerData.m_InputBuffer) - 1)] = input;
                                myPlayerData.m_TailInputFrame = frame;

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

                            assert(myClientData.m_LastInputFrame < myPlayerData.m_TailInputFrame);
                            myClientData.m_LastInputFrame = myPlayerData.m_TailInputFrame;
                        }

                        assert(myPlayerData.m_TailInputFrame >= m_Frame);
                    }
                }

                bool firstTime = true;
                while (true)
                {
                    // receive all messages
                    {
                        SteamNetworkingMessage_t* netMessages[1000];

                        for (int clientIndex = 0; clientIndex < countof(m_ClientData); clientIndex++)
                        {
                            auto& clientData = m_ClientData[clientIndex];
                            auto& playerData = clientData.m_PlayerData;

                            if (clientData.m_ConnectSocket != k_HSteamNetConnection_Invalid)
                            {
                                int netMessageCount = g_SteamNetworkingSockets->ReceiveMessagesOnConnection(clientData.m_ConnectSocket, netMessages, countof(netMessages));
                                //trace("Received %d messages\n", count);

                                for (int netMessageIndex = 0; netMessageIndex < netMessageCount; netMessageIndex++)
                                {
                                    auto netMessage = netMessages[netMessageIndex];

                                    switch (netMessage->m_idxLane)
                                    {
                                    case 0:
                                    {
                                        assert(netMessage->m_cbSize <= Message::GetMaxSize());

                                        Message message;

                                        Reader reader(static_cast<uint8*>(netMessage->m_pData), netMessage->m_cbSize);
                                        message.Serialize(reader);

                                        if (netMessage->m_nMessageNumber > clientData.m_LastReceivedMessageNumber)
                                        {
                                            clientData.m_LastReceivedMessageNumber = netMessage->m_nMessageNumber;
                                            clientData.m_HasReceivedUnacknowledgedInputFrames = message.m_InputFrameCount != 0;
                                        }

                                        auto& acknowledgedMessage = clientData.m_MessageSendData[message.m_LastReceivedMessageNumber & (countof(clientData.m_MessageSendData) - 1)];
                                        if (acknowledgedMessage.m_MessageNumber == message.m_LastReceivedMessageNumber)
                                        {
                                            if (acknowledgedMessage.m_MessageAcknowledgeTime == 0)
                                            {
                                                acknowledgedMessage.m_MessageAcknowledgeTime = netMessage->m_usecTimeReceived;
                                            }

                                            if (message.m_LastReceivedMessageNumber > clientData.m_LastAcknowledgedMessageNumber)
                                            {
                                                clientData.m_LastAcknowledgedMessageNumber = message.m_LastReceivedMessageNumber;

                                                if (acknowledgedMessage.m_InputFrame > clientData.m_LastAcknowledgedInputFrame)
                                                {
                                                    clientData.m_LastAcknowledgedInputFrame = acknowledgedMessage.m_InputFrame;
                                                }
                                            }
                                        }

                                        netMessage->Release();
                                        netMessage = nullptr;

                                        uint32 checksumFrame = message.m_FirstInputFrame + message.m_ChecksumFrameDelta;
                                        assert(message.m_ChecksumFrameDelta >= 0 || checksumFrame < message.m_FirstInputFrame); // detect unsigned overflow
                                        auto& checksumBuffer = clientData.m_ChecksumData[checksumFrame & (countof(clientData.m_ChecksumData) - 1)];

                                        if (checksumBuffer.m_Frame < checksumFrame)
                                        {
                                            if (checksumFrame > clientData.m_LastReceivedChecksumFrame)
                                            {
                                                clientData.m_LastReceivedChecksumFrame = checksumFrame;
                                            }

                                            checksumBuffer.Reset();
                                            checksumBuffer.m_Frame = checksumFrame;
                                            checksumBuffer.m_Checksum = message.m_Checksum;

                                            CheckChecksum(checksumBuffer, instance);
                                        }

                                        assert(message.m_InputFrameCount <= countof(message.m_Inputs));
                                        if (message.m_InputFrameCount > 0)
                                        {
                                            uint32 lastFrame = message.m_FirstInputFrame + message.m_InputFrameCount - 1;
                                            if (lastFrame > playerData.m_TailInputFrame)
                                            {
                                                uint32 firstFrame = playerData.m_TailInputFrame + 1;
                                                int firstMessageIndex = firstFrame - message.m_FirstInputFrame;

                                                // skipping over inputs is possible if they remove themselves as a player before we do, this is fine and we will fill these in with zero as we get to them
                                                if (firstMessageIndex < 0)
                                                {
                                                    firstFrame = message.m_FirstInputFrame;
                                                    firstMessageIndex = 0;
                                                }

                                                int count = lastFrame - firstFrame + 1;

                                                assert(firstMessageIndex >= 0 && firstMessageIndex < message.m_InputFrameCount);
                                                assert(count > 0 && count <= message.m_InputFrameCount);
                                                assert(lastFrame - m_Frame < countof(playerData.m_InputBuffer));

                                                for (int i = 0; i < count; i++)
                                                {
                                                    playerData.m_InputBuffer[(i + firstFrame) & (countof(playerData.m_InputBuffer) - 1)] = message.m_Inputs[i + firstMessageIndex];
                                                }
                                                playerData.m_TailInputFrame = lastFrame;

                                                assert(clientData.m_LastInputFrame < playerData.m_TailInputFrame);
                                                clientData.m_LastInputFrame = lastFrame;

                                                for (int i = 0; i < countof(message.m_AutomaticInputDelayChanges); i++)
                                                {
                                                    auto& messageDelayChange = message.m_AutomaticInputDelayChanges[i];
                                                    if (messageDelayChange.m_InputFrameIndex != 0)
                                                    {
                                                        int messageInputFrameIndex = messageDelayChange.m_InputFrameIndex & ((1 << c_MessageInputCountBits) - 1);
                                                        assert(messageInputFrameIndex < message.m_InputFrameCount);
                                                        assert(messageDelayChange.m_InputDelay <= c_MaxInputDelay);

                                                        if (messageInputFrameIndex >= firstMessageIndex)
                                                        {
                                                            uint32 frame = messageInputFrameIndex + message.m_FirstInputFrame;
                                                            auto& delayChange = clientData.m_AutomaticInputDelayChanges[frame & (countof(clientData.m_AutomaticInputDelayChanges) - 1)];
                                                            delayChange.m_InputFrame = frame;
                                                            delayChange.m_InputDelay = messageDelayChange.m_InputDelay;
                                                        }
                                                    }
                                                    else
                                                    {
                                                        break;
                                                    }
                                                }

                                                //trace("Received frame: %u\n", lastFrame);
                                            }
                                            else
                                            {
                                                //trace("Received frame: %u, but it wasn't newer than last received frame: %u\n", lastFrame, playerData.m_TailInputFrame);
                                            }
                                        }
                                    } break;
                                    case 1:
                                    {
                                        ReceiveReliableMessage(netMessage, clientIndex, instance);
                                    } break;
                                    }
                                }
                            }
                        }
                    }

                    bool canMoveToNextFrame = true;

                    // send all messages
                    {
                        int messageCount = 0;
                        SteamNetworkingMessage_t* netMessages[countof(m_ClientData) - 1];
                        ClientData* netMessageToClientDataMap[countof(netMessages)];

                        auto& myClientData = m_ClientData[m_ClientIndex];
                        auto& myPlayerData = myClientData.m_PlayerData;

                        for (int clientIndex = 0; clientIndex < countof(m_ClientData); clientIndex++)
                        {
                            auto& clientData = m_ClientData[clientIndex];

                            if (clientData.m_ConnectSocket != k_HSteamNetConnection_Invalid)
                            {
                                // log some networking info to debug startup issues
                                //if (m_AssignedClientsCount <= 0)
                                //{
                                //    SteamNetConnectionRealTimeLaneStatus_t lanes[2];
                                //    g_SteamNetworkingSockets->GetConnectionRealTimeStatus(clientData.m_ConnectSocket, nullptr, countof(lanes), lanes);

                                //    trace("[0].PendingUnreliable: %d, [0].QueueTime: %lld, [1].PendingReliable: %d, [1].SentUnackedReliable: %d, [1].QueueTime: %lld\n",
                                //        lanes[0].m_cbPendingUnreliable, lanes[0].m_usecQueueTime,
                                //        lanes[1].m_cbPendingReliable, lanes[1].m_cbSentUnackedReliable, lanes[1].m_usecQueueTime);
                                //}

                                // we can't have the client run too far behind, or the inputs we're sending them will roll off the end of the input buffer
                                int countSinceChecksumFrame = myClientData.m_LastInputFrame - clientData.m_LastReceivedChecksumFrame;
                                if (countSinceChecksumFrame > c_FramesToRunBehindUpperLimit)
                                {
                                    clientData.m_IsRunningTooFarBehind = true;
                                    canMoveToNextFrame = false;
                                }
                                else if (clientData.m_IsRunningTooFarBehind && countSinceChecksumFrame > c_FramesToRunBehindLowerLimit)
                                {
                                    canMoveToNextFrame = false;
                                }
                                else
                                {
                                    clientData.m_IsRunningTooFarBehind = false;
                                }

                                Message message;
                                int count = max(0, static_cast<int>(myClientData.m_LastInputFrame - clientData.m_LastAcknowledgedInputFrame));
                                if (count > countof(message.m_Inputs))
                                {
                                    count = countof(message.m_Inputs);

                                    //trace("Failed to send all inputs for frame: %u\n", myClientData.m_LastInputFrame);
                                    canMoveToNextFrame = false;
                                }

                                int64 time = g_SteamNetworkingUtils->GetLocalTimestamp();

                                if ( // if we are sending a new frame, or we are allowed to resend it
                                    (m_Frame > clientData.m_LastSentFrame || time >= clientData.m_LastSentTime + c_TimeBeforeResendingMessage)
                                    && // ... and if we have no frames to send them, and they had no frames to send us, and we're not recording samples, there's no need to send a message
                                    (m_AssignedClientsCount > 0 || count > 0 || clientData.m_HasReceivedUnacknowledgedInputFrames))
                                {
                                    netMessageToClientDataMap[messageCount] = &clientData;
                                    auto netMessage = netMessages[messageCount++] = g_SteamNetworkingUtils->AllocateMessage(Message::GetMaxSize());
                                    netMessage->m_conn = clientData.m_ConnectSocket;
                                    netMessage->m_nFlags = k_nSteamNetworkingSend_UnreliableNoDelay;

                                    uint32 first = clientData.m_LastAcknowledgedInputFrame + 1;
                                    message.m_LastReceivedMessageNumber = clientData.m_LastReceivedMessageNumber;
                                    message.m_FirstInputFrame = first;

                                    assert(count <= countof(message.m_Inputs));
                                    message.m_InputFrameCount = static_cast<uint8>(count);

                                    auto& checksumBuffer = myClientData.m_ChecksumData[m_Frame & (countof(myClientData.m_ChecksumData) - 1)];
                                    assert(checksumBuffer.m_Frame == m_Frame);
                                    int checksumFrameDelta = m_Frame - first;
                                    assert(checksumFrameDelta >= INT8_MIN && checksumFrameDelta <= INT8_MAX);
                                    message.m_ChecksumFrameDelta = static_cast<int8>(checksumFrameDelta);
                                    message.m_Checksum = checksumBuffer.m_Checksum;

                                    {
                                        int delayChangeIndex = 0;
                                        for (int i = 0; i < count; i++)
                                        {
                                            uint32 frame = i + first;

                                            assert(frame <= myPlayerData.m_TailInputFrame);
                                            message.m_Inputs[i] = myPlayerData.m_InputBuffer[frame & (countof(myPlayerData.m_InputBuffer) - 1)];

                                            auto& delayChange = myClientData.m_AutomaticInputDelayChanges[frame & (countof(myClientData.m_AutomaticInputDelayChanges) - 1)];
                                            if (delayChange.m_InputFrame == frame)
                                            {
                                                auto& messageDelayChange = message.m_AutomaticInputDelayChanges[delayChangeIndex++];
                                                messageDelayChange.m_InputFrameIndex = static_cast<uint8>(i | (1 << c_MessageInputCountBits));
                                                messageDelayChange.m_InputDelay = static_cast<uint8>(delayChange.m_InputDelay);
                                            }
                                        }
                                    }

                                    Writer writer(static_cast<uint8*>(netMessage->m_pData), netMessage->m_cbSize);
                                    message.Serialize(writer);
                                    netMessage->m_cbSize = writer.m_Offset;

                                    //if (m_Frame <= clientData.m_LastSentFrame)
                                    //{
                                    //    trace("Resending %d inputs for frame: %u\n", count, myClientData.m_LastInputFrame);
                                    //}
                                    //else
                                    //{
                                    //    trace("Sending %d inputs for frame: %u\n", count, myClientData.m_LastInputFrame);
                                    //}

                                    clientData.m_LastSentFrame = m_Frame;
                                    clientData.m_LastSentTime = time;
                                }
                            }
                        }

                        if (messageCount > 0)
                        {
                            int64 time = g_SteamNetworkingUtils->GetLocalTimestamp();
                            int64 outResults[countof(netMessages)];
                            
                            g_SteamNetworkingSockets->SendMessages(messageCount, netMessages, outResults);
                            for (int i = 0; i < countof(netMessages); i++)
                            {
                                netMessages[i] = nullptr;
                            }

                            for (int i = 0; i < messageCount; i++)
                            {
                                int64 messageNumber = outResults[i];
                                if (messageNumber > 0)
                                {
                                    auto& clientData = *netMessageToClientDataMap[i];
                                    clientData.m_LastSentMessageNumber = messageNumber;
                                    clientData.m_MessageSendData[messageNumber & (countof(clientData.m_MessageSendData) - 1)] =
                                    {
                                        .m_MessageNumber = messageNumber,
                                        .m_MessageSendTime = time,
                                        .m_InputFrame = myClientData.m_LastInputFrame,
                                        .m_Frame = m_Frame,
                                    };
                                }
                            }
                        }
                    }

                    for (int i = 0; i < countof(m_ClientData); i++)
                    {
                        if (m_ClientData[i].m_PlayerData.m_TailInputFrame < m_Frame)
                        {
                            canMoveToNextFrame = false;
                            break;
                        }
                    }

                    if (!advanceFrame || canMoveToNextFrame)
                    {
                        break;
                    }

                    if (firstTime)
                    {
                        firstTime = false;
                        Timing_Sleep(1'000 * 4);
                        //trace("Doing little wait\n");
                    }
                    else
                    {
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
                    }

                    if (m_OnlineState != EOnlineState::InGame)
                    {
                        m_Frame--;
                        break;
                    }
                }

                if (m_OnlineState == EOnlineState::InGame)
                {
                    // update automatic input delay
                    if (advanceFrame)
                    {
                        // process automatic input delay changes for this frame
                        for (int clientIndex = 0; clientIndex < countof(m_ClientData); clientIndex++)
                        {
                            auto& clientData = m_ClientData[clientIndex];
                            auto& delayChange = clientData.m_AutomaticInputDelayChanges[m_Frame & (countof(clientData.m_AutomaticInputDelayChanges) - 1)];
                            if (delayChange.m_InputFrame == m_Frame)
                            {
                                clientData.m_AutomaticInputDelay = delayChange.m_InputDelay;
                                //trace("Set Automatic Input Delay for client %d on frame %u, delay: %d\n", clientIndex, m_Frame, clientData.m_AutomaticInputDelay);
                            }
                        }

                        // make sure our requested input delay is up to date, so we can always request a change if it's different from this
                        if (m_Frame >= m_RequestedAutomaticInputDelayFrame)
                        {
                            auto& myClientData = m_ClientData[m_ClientIndex];
                            m_RequestedAutomaticInputDelay = myClientData.m_AutomaticInputDelay;
                        }

                        UpdateInputDelay();
                    }

                    // calculate new automatic input delay
                    if (advanceFrame)
                    {
                        int newDelay = 0;
                        bool hasDelay = false;

                        for (int clientIndex = 0; clientIndex < countof(m_ClientData); clientIndex++)
                        {
                            auto& clientData = m_ClientData[clientIndex];
                            if (clientData.m_ConnectSocket != k_HSteamNetConnection_Invalid)
                            {
                                int64 rtt = 0;

                                // for all acked messages since last frame, get the max time since one was acknowledged
                                int64 first = clientData.m_PreviousFrameLastAcknowledgedMessageNumber;
                                int64 last = clientData.m_LastAcknowledgedMessageNumber;
                                int64 acknowledgeTime = 0;
                                for (int64 i = last; i > first; i--)
                                {
                                    auto& messageInfo = clientData.m_MessageSendData[i & (countof(clientData.m_MessageSendData) - 1)];
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
                                    rtt = clientData.m_PreviousFrameSampleRTT;
                                }

                                // use the current time since the first non-acked message if that's greater
                                int64 firstNonAcked = clientData.m_LastAcknowledgedMessageNumber + 1;
                                auto& messageInfo = clientData.m_MessageSendData[firstNonAcked & (countof(clientData.m_MessageSendData) - 1)];
                                if (messageInfo.m_MessageNumber == firstNonAcked)
                                {
                                    rtt = max(rtt, g_SteamNetworkingUtils->GetLocalTimestamp() - messageInfo.m_MessageSendTime);
                                }

                                // add the sample
                                if (rtt != 0 && m_AssignedClientsCount > 0 && m_PreviousFrameAssignedClientsCount)
                                {
                                    assert(rtt > 0);
                                    rtt = min(rtt, static_cast<int64>(INT32_MAX));

                                    int delay = static_cast<int>((rtt * c_TargetFPS + 1'000'000 - 1) / 1'000'000);
                                    delay = (delay + 1) / 2;
                                    assert(delay >= 0);

                                    clientData.m_RaiseDelayHistogram.AddSample(delay);
                                    clientData.m_LowerDelayHistogram.AddSample(delay);

                                    //trace("Added sample: %d, %d\n", static_cast<int>(rtt), delay);
                                }

                                m_PreviousFrameAssignedClientsCount = m_AssignedClientsCount;
                                clientData.m_PreviousFrameLastAcknowledgedMessageNumber = clientData.m_LastAcknowledgedMessageNumber;
                                clientData.m_PreviousFrameSampleRTT = rtt;

                                if (clientData.m_PlayerData.m_PlayerIndex >= 0)
                                {
                                    // prints out some useful info while testing
                                    //if (clientData.m_RaiseDelayHistogram.m_SampleCount >= c_SamplesNeededBeforeRaisingDelay)
                                    //{
                                    //    int raiseBelowPercent;
                                    //    int raiseBelowOrEqualPercent;
                                    //    int lowerBelowPercent;
                                    //    int lowerBelowOrEqualPercent;
                                    //    clientData.m_RaiseDelayHistogram.CalculateDistributionAt(m_RequestedAutomaticInputDelay, &raiseBelowPercent, &raiseBelowOrEqualPercent);
                                    //    clientData.m_LowerDelayHistogram.CalculateDistributionAt(m_RequestedAutomaticInputDelay, &lowerBelowPercent, &lowerBelowOrEqualPercent);
                                    //    int raiseMedianDelay = clientData.m_RaiseDelayHistogram.CalculatePercentile(50);
                                    //    int lowerMedianDelay = clientData.m_LowerDelayHistogram.CalculatePercentile(50);

                                    //    trace("Raise median: %d, current:%d, <%d%%, <=%d%%, >%d%% | Lower median: %d, current:%d, <%d%%, <=%d%%, >%d%%\n", raiseMedianDelay, m_RequestedAutomaticInputDelay, raiseBelowPercent, raiseBelowOrEqualPercent, 100 - raiseBelowOrEqualPercent, lowerMedianDelay, m_RequestedAutomaticInputDelay, lowerBelowPercent, lowerBelowOrEqualPercent, 100 - lowerBelowOrEqualPercent);
                                    //}

                                    if (clientData.m_RaiseDelayHistogram.m_SampleCount >= c_SamplesNeededBeforeRaisingDelay)
                                    {
                                        int raiseDelay = clientData.m_RaiseDelayHistogram.CalculatePercentile(c_PercentSamplesWithinDelayToMaintainDelay);
                                        if (raiseDelay >= m_RequestedAutomaticInputDelay)
                                        {
                                            newDelay = max(newDelay, raiseDelay);
                                            hasDelay = true;
                                        }
                                    }

                                    if (newDelay < m_RequestedAutomaticInputDelay && clientData.m_LowerDelayHistogram.m_SampleCount >= c_SamplesNeededBeforeLoweringDelay)
                                    {
                                        int lowerDelay = clientData.m_LowerDelayHistogram.CalculatePercentile(c_PercentSamplesBelowDelayToLowerDelay);
                                        if (lowerDelay <= m_RequestedAutomaticInputDelay)
                                        {
                                            newDelay = max(newDelay, lowerDelay);
                                            hasDelay = true;
                                        }
                                    }
                                }
                            }
                        }

                        if (hasDelay)
                        {
                            RequestAutomaticInputDelayChange(newDelay);
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

        InputFlags_t PlayerGetInput(CInstance* instance, int playerIndex)
        {
            InputFlags_t in = 0;

            assert(playerIndex >= 0 && playerIndex < countof(m_PlayerIndexToClientIndexMap));
            int clientIndex = m_PlayerIndexToClientIndexMap[playerIndex];
            if (clientIndex >= 0)
            {
                auto& playerData = m_ClientData[clientIndex].m_PlayerData;
                assert(playerData.m_TailInputFrame >= m_Frame);
                in = playerData.m_InputBuffer[m_Frame & (countof(playerData.m_InputBuffer) - 1)];

                //ReleaseConsoleOutput("Frame %u: Input for player %d = [ %x ]\n", m_Frame, playerIndex, in);
            }

            return in;
        }

        void CheckChecksum(ChecksumBuffer& a, CInstance* instance)
        {
            for (int clientIndex = 0; clientIndex < countof(m_ClientData); clientIndex++)
            {
                auto& clientData = m_ClientData[clientIndex];
                auto& b = clientData.m_ChecksumData[a.m_Frame & (countof(clientData.m_ChecksumData) - 1)];

                if (a.m_Frame == b.m_Frame && a.m_Checksum != b.m_Checksum)
                {
                    if (a.m_Frame > m_LastSentChecksumFrame)
                    {
                        // send checksum message
                        auto& myClientData = m_ClientData[m_ClientIndex];
                        auto& mine = myClientData.m_ChecksumData[a.m_Frame & (countof(clientData.m_ChecksumData) - 1)];

                        if (mine.m_Frame == a.m_Frame)
                        {
                            ReliableMessage reliableMessage;
                            reliableMessage.m_Type = EReliableMessageType::ChecksumBuffer;
                            reliableMessage.m_Frame = mine.m_Frame;
                            SendReliableMessage(reliableMessage, instance, nullptr, &mine);
                            m_LastSentChecksumFrame = mine.m_Frame;
                        }
                    }

                    if (a.m_HasBuffer && b.m_HasBuffer)
                    {
                        auto& myClientData = m_ClientData[m_ClientIndex];
                        auto startPtr = myClientData.m_ChecksumData;
                        auto endPtr = startPtr + countof(myClientData.m_ChecksumData);

                        if (&b >= startPtr && &b < endPtr)
                        {
                            DiffChecksums(b, a, instance);
                        }
                        else
                        {
                            DiffChecksums(a, b, instance);
                        }

                        // we quit when a checksum failure happens, could change this in the future
                        PostMessageW(NULL, WM_QUIT, 0, 0);
                        m_OnlineState = EOnlineState::Quitting;
                    }
                }
            }
        }

        void DiffChecksums(ChecksumBuffer& a, ChecksumBuffer& b, CInstance* instance)
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
        }

        void OnNetConnectionStatusChanged(SteamNetConnectionStatusChangedCallback_t* pParam)
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

                for (int clientIndex = 0; clientIndex < countof(m_ClientData); clientIndex++)
                {
                    auto& clientData = m_ClientData[clientIndex];
                    if (clientData.m_ConnectSocket == hConn)
                    {
                        clientData.m_ConnectSocket = k_HSteamNetConnection_Invalid;
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
                    if (m_ClientData[1].m_ConnectSocket != k_HSteamNetConnection_Invalid)
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
            constexpr int lanePriorities[] = { 1, 1 }; // lowest number drains first
            constexpr uint16 laneWeights[] = { 1, 1 }; // higher number means more relative bandwidth
            g_SteamNetworkingSockets->ConfigureConnectionLanes(hConn, countof(lanePriorities), lanePriorities, laneWeights);
            m_ClientData[playerIndex].m_ConnectSocket = hConn;
        }

        void Quit()
        {
            if (m_OnlineState >= EOnlineState::InGame)
            {
                m_OnlineState = EOnlineState::Quitting;
            }
        }

        void SetPlayers(int (&map)[countof(m_PlayerIndexToClientIndexMap)])
        {
            int anyAssignedClient = -1;
            int oldClientToPlayerIndexMap[countof(m_ClientData)];

            for (int clientIndex = 0; clientIndex < countof(m_ClientData); clientIndex++)
            {
                auto& playerData = m_ClientData[clientIndex].m_PlayerData;
                oldClientToPlayerIndexMap[clientIndex] = playerData.m_PlayerIndex;
                playerData.m_PlayerIndex = -1;
            }

            for (int playerIndex = countof(map) - 1; playerIndex >= 0; playerIndex--)
            {
                int clientIndex = map[playerIndex];
                assert(clientIndex >= -1 && clientIndex < static_cast<int>(countof(m_ClientData)));
                m_PlayerIndexToClientIndexMap[playerIndex] = clientIndex;
                if (clientIndex >= 0)
                {
                    anyAssignedClient = clientIndex;
                    auto& playerData = m_ClientData[clientIndex].m_PlayerData;
                    playerData.m_PlayerIndex = playerIndex;
                }
            }

            bool changedPlayerAssignment = false;
            m_AssignedClientsCount = 0;
            for (int clientIndex = 0; clientIndex < countof(m_ClientData); clientIndex++)
            {
                auto& clientData = m_ClientData[clientIndex];
                auto& playerData = clientData.m_PlayerData;

                if (playerData.m_PlayerIndex >= 0)
                {
                    m_AssignedClientsCount++;
                }

                if ((playerData.m_PlayerIndex >= 0) != (oldClientToPlayerIndexMap[clientIndex] >= 0))
                {
                    changedPlayerAssignment = true;
                }

                // if a player becomes newly assigned, it needs to start sending again from the tail of its input
                if (playerData.m_PlayerIndex >= 0 && oldClientToPlayerIndexMap[clientIndex] < 0)
                {
                    assert(clientData.m_LastInputFrame <= playerData.m_TailInputFrame);
                    clientData.m_LastInputFrame = playerData.m_TailInputFrame;
                }
            }

            // make sure the favored client index is always valid
            if (m_AssignedClientsCount <= 1)
            {
                m_InputDelayFavoredClientIndex = anyAssignedClient;
            }
            else if (m_InputDelayFavoredClientIndex >= 0 && m_ClientData[m_InputDelayFavoredClientIndex].m_PlayerData.m_PlayerIndex < 0)
            {
                m_InputDelayFavoredClientIndex = -1;
            }

            ReleaseConsoleOutput("We set the players [ %d, %d ] on frame: %u\n", map[0], map[1], m_Frame);

            if (changedPlayerAssignment)
            {
                UpdateInputDelay();
                RequestAutomaticInputDelayChange();
            }
        }

        void UpdateInputDelay()
        {
            if (m_InputDelayMode == EInputDelayMode::Automatic)
            {
                // get the highest automatic input delay of all assigned players
                int automaticInputDelay = 0;
                if (m_AssignedClientsCount > 1)
                {
                    for (int clientIndex = 0; clientIndex < countof(m_ClientData); clientIndex++)
                    {
                        auto& clientData = m_ClientData[clientIndex];
                        if (clientData.m_PlayerData.m_PlayerIndex >= 0)
                        {
                            automaticInputDelay = max(automaticInputDelay, clientData.m_AutomaticInputDelay);
                        }
                    }
                }

                automaticInputDelay = min(automaticInputDelay, m_MaxAutomaticInputDelay);
                automaticInputDelay = max(automaticInputDelay, m_MinAutomaticInputDelay);

                // set input delay based on the current favored player
                for (int clientIndex = 0; clientIndex < countof(m_ClientData); clientIndex++)
                {
                    int delay = 0;
                    if (m_InputDelayFavoredClientIndex < 0)
                    {
                        delay = automaticInputDelay;
                    }
                    else if (m_InputDelayFavoredClientIndex == clientIndex)
                    {
                        delay = 0;
                    }
                    else
                    {
                        delay = automaticInputDelay * 2 + c_FavoredModeExtraInputDelay;
                    }
                    assert(delay >= 0 && delay <= c_MaxInputDelay * 2 + c_FavoredModeExtraInputDelay);

                    auto& clientData = m_ClientData[clientIndex];
                    clientData.m_InputDelay = delay;
                }
            }
        }

        void RequestAutomaticInputDelayChange(int delay = -1)
        {
            if (m_InputDelayMode == EInputDelayMode::Automatic && m_AssignedClientsCount > 1)
            {
                auto& myClientData = m_ClientData[m_ClientIndex];
                if (myClientData.m_PlayerData.m_PlayerIndex >= 0)
                {
                    if (delay < 0)
                    {
                        for (int clientIndex = 0; clientIndex < countof(m_ClientData); clientIndex++)
                        {
                            auto& clientData = m_ClientData[clientIndex];
                            if (clientData.m_ConnectSocket != k_HSteamNetConnection_Invalid && clientData.m_PlayerData.m_PlayerIndex >= 0)
                            {
                                if (clientData.m_RaiseDelayHistogram.m_SampleCount >= c_SamplesNeededBeforeRaisingDelay)
                                {
                                    int newDelay = clientData.m_RaiseDelayHistogram.CalculatePercentile(c_PercentSamplesWithinDelayToMaintainDelay);
                                    delay = max(delay, newDelay);
                                }
                            }
                        }

                        if (delay < 0)
                        {
                            for (int clientIndex = 0; clientIndex < countof(m_ClientData); clientIndex++)
                            {
                                auto& clientData = m_ClientData[clientIndex];
                                if (clientData.m_ConnectSocket != k_HSteamNetConnection_Invalid && clientData.m_PlayerData.m_PlayerIndex >= 0)
                                {
                                    SteamNetConnectionRealTimeStatus_t status;
                                    g_SteamNetworkingSockets->GetConnectionRealTimeStatus(clientData.m_ConnectSocket, &status, 0, nullptr);
                                    if (status.m_nPing >= 0)
                                    {
                                        int newDelay = ((status.m_nPing + c_PingDelayCalculationSafetyMargin) * c_TargetFPS + 1000 - 1) / 1000;
                                        newDelay = (newDelay + 1) / 2;
                                        newDelay = min(newDelay, c_MaxInputDelay);
                                        delay = max(delay, newDelay);
                                    }
                                }
                            }

                            if (delay < 0)
                            {
                                delay = 5;
                            }
                        }
                    }

                    assert(delay >= 0 && delay <= c_MaxInputDelay);

                    if (delay != m_RequestedAutomaticInputDelay)
                    {
                        uint32 inputFrame = myClientData.m_LastInputFrame + 1;
                        auto& changeRequest = myClientData.m_AutomaticInputDelayChanges[inputFrame & (countof(myClientData.m_AutomaticInputDelayChanges) - 1)];

                        m_RequestedAutomaticInputDelayFrame = changeRequest.m_InputFrame = inputFrame;
                        m_RequestedAutomaticInputDelay = changeRequest.m_InputDelay = delay;

                        //trace("Requested Automatic Input Delay Change on frame: %u, delay: %d\n", inputFrame, delay);
                    }
                }
            }
        }

        FileData* FindFileData(const char* filename, int clientIndex = -1)
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

            if (clientIndex >= 0)
            {
                if (ret == nullptr)
                {
                    assert(firstEmpty != nullptr);
                    ret = firstEmpty;
                    ret->m_Filename = YYStrDup(filename);

                    ret->m_OwnerClientIndex = clientIndex;
                }
                else if (ret->m_Status == EFileStatus::None)
                {
                    ret->m_OwnerClientIndex = clientIndex;
                }
                else
                {
                    assert(ret->m_OwnerClientIndex == clientIndex);
                }
            }
            else if (clientIndex == -1)
            {
                assert(ret != nullptr);
                if (ret->m_Status == EFileStatus::None)
                {
                    assert(m_OnlineState < EOnlineState::InGame);
                }
            }

            return ret;
        }

        void FileExists(RValue& result, CInstance* instance, RValue* arg, int clientIndex)
        {
            assert(clientIndex >= 0 && clientIndex < countof(m_ClientData));

            auto filename = YYGetString(arg, 0);
            auto fileData = FindFileData(filename, clientIndex);

            if (m_OnlineState >= EOnlineState::InGame)
            {
                if (clientIndex == m_ClientIndex)
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
        RETRY:
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

            for (int clientIndex = 0; clientIndex < countof(m_ClientData); clientIndex++)
            {
                if (m_ClientData[clientIndex].m_ConnectSocket != k_HSteamNetConnection_Invalid)
                {
                    netMessage->m_conn = m_ClientData[clientIndex].m_ConnectSocket;
                }
            }

            netMessage->m_cbSize = writer.m_Offset;
            netMessage->m_nFlags = k_nSteamNetworkingSend_Reliable;
            netMessage->m_idxLane = 1;

            int64 outResult;
            g_SteamNetworkingSockets->SendMessages(1, &netMessage, &outResult);
            netMessage = nullptr;

            if (outResult == -k_EResultLimitExceeded)
            {
                ReleaseConsoleOutput("SendReliableMessage (size = %d) failed with k_EResultLimitExceeded: Resending message\n", writer.m_Offset);
                Timing_Sleep(1'000'000 / 2);
                goto RETRY;
            }
            else if (outResult <= 0)
            {
                ReleaseConsoleOutput("SendReliableMessage (size = %d) failed with error code '%lld': Disconnecting\n", writer.m_Offset, outResult);
                m_OnlineState = EOnlineState::Disconnecting;
            }
            else
            {
                ReleaseConsoleOutput("SendReliableMessage (size = %d) succeeded\n", writer.m_Offset);
            }
        }

        void ReceiveReliableMessage(SteamNetworkingMessage_t* netMessage, int clientIndex, CInstance* instance)
        {
            ReleaseConsoleOutput("ReceiveReliableMessage (size = %d)\n", netMessage->m_cbSize);

            ReliableMessage reliableMessage;
            Reader reader(static_cast<uint8*>(netMessage->m_pData), netMessage->m_cbSize);
            reliableMessage.Serialize(reader);

            if (reliableMessage.m_Type == EReliableMessageType::File || reliableMessage.m_Type == EReliableMessageType::FileDoesNotExist)
            {
                auto fileData = FindFileData(reliableMessage.m_Filename, clientIndex);

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
                auto& clientData = m_ClientData[clientIndex];
                auto& checksumBuffer = clientData.m_ChecksumData[reliableMessage.m_Frame & (countof(clientData.m_ChecksumData) - 1)];

                checksumBuffer.Reset();
                checksumBuffer.Serialize(reader);
                assert(checksumBuffer.m_Frame == reliableMessage.m_Frame);
                checksumBuffer.m_HasBuffer = true;

                CheckChecksum(checksumBuffer, instance);
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

    PFO g_pfo;
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

    g_GmlChecksumBuffer1 = CreateBuffer(sizeof(ChecksumBuffer::m_GmlBuffer), eBuffer_Format_Fixed, 1);
    g_GmlChecksumBuffer2 = CreateBuffer(sizeof(ChecksumBuffer::m_GmlBuffer), eBuffer_Format_Fixed, 1);

    g_SteamNetworkingUtils->InitRelayNetworkAccess();
    g_SteamNetworkingSockets->InitAuthentication();
    g_SteamNetworkingUtils->SetGlobalConfigValueInt32(k_ESteamNetworkingConfig_TimeoutConnected, 60 * 1000);

    struct SteamCallbacks
    {
        STEAM_CALLBACK(SteamCallbacks, OnNetConnectionStatusChanged, SteamNetConnectionStatusChangedCallback_t)
        {
            g_pfo.OnNetConnectionStatusChanged(pParam);
        }

        STEAM_CALLBACK(SteamCallbacks, OnLobbyDataUpdate, LobbyDataUpdate_t)
        {
            auto map = CreateDsMap(0, 0);
            DsMapAddString(map, "event_type", "lobby_data_update");
            DsMapAddDouble(map, "success", pParam->m_bSuccess);
            DsMapAddInt64(map, "lobby_id", static_cast<int64>(pParam->m_ulSteamIDLobby));
            DsMapAddInt64(map, "member_id", static_cast<int64>(pParam->m_ulSteamIDMember));
            CreateAsyncEventWithDSMap(map, 69);
        }

        STEAM_CALLBACK(SteamCallbacks, OnLobbyGameCreated, LobbyGameCreated_t)
        {
            auto map = CreateDsMap(0, 0);
            DsMapAddString(map, "event_type", "lobby_game_created");
            DsMapAddInt64(map, "lobby_id", static_cast<int64>(pParam->m_ulSteamIDLobby));
            DsMapAddInt64(map, "server_id", static_cast<int64>(pParam->m_ulSteamIDGameServer));
            DsMapAddDouble(map, "ip", pParam->m_unIP);
            DsMapAddDouble(map, "port", pParam->m_usPort);
            CreateAsyncEventWithDSMap(map, 69);
        }
    };
    new SteamCallbacks();

    DebugConsoleOutput("PFO 50 YYExtensionInitialise CONFIGURED\n");
}

YYEXPORT void pfo_update(RValue& result, CInstance* selfinst, CInstance* otherinst, int argc, RValue* arg)
{
    assert(argc == 0 || argc == 1);

    bool advanceFrame = argc > 0 ? YYGetBool(arg, 0) : true;

    g_pfo.Update(selfinst, advanceFrame);
    init_bool(result, true);
}

YYEXPORT void pfo_player_get_input(RValue& result, CInstance* selfinst, CInstance* otherinst, int argc, RValue* arg)
{
    assert(argc == 1);

    int playerIndex = YYGetInt32(arg, 0);
    init_int64(result, g_pfo.PlayerGetInput(selfinst, playerIndex));
}

YYEXPORT void pfo_is_online(RValue& result, CInstance* selfinst, CInstance* otherinst, int argc, RValue* arg)
{
    assert(argc == 0);

    init_bool(result, g_pfo.m_OnlineState >= EOnlineState::InGame);
}

YYEXPORT void pfo_client_get_input_delay(RValue& result, CInstance* selfinst, CInstance* otherinst, int argc, RValue* arg)
{
    assert(argc <= 1);

    int clientIndex = argc > 0 ? YYGetInt32(arg, 0) : g_pfo.m_ClientIndex;
    assert(clientIndex >= 0 && clientIndex < countof(g_pfo.m_ClientData));

    init_real(result, g_pfo.m_ClientData[clientIndex].m_InputDelay);
}

YYEXPORT void pfo_client_set_input_delay(RValue& result, CInstance* selfinst, CInstance* otherinst, int argc, RValue* arg)
{
    assert(argc > 0 && argc <= 2);

    int delay = YYGetInt32(arg, 0);
    assert(delay >= 0 && delay <= c_MaxInputDelay * 2 + c_FavoredModeExtraInputDelay);

    int clientIndex = argc > 1 ? YYGetInt32(arg, 1) : g_pfo.m_ClientIndex;
    assert(clientIndex >= 0 && clientIndex < countof(g_pfo.m_ClientData));

    if (g_pfo.m_InputDelayMode != EInputDelayMode::Automatic)
    {
        g_pfo.m_ClientData[clientIndex].m_InputDelay = delay;
    }
}

YYEXPORT void pfo_get_input_delay_mode(RValue& result, CInstance* selfinst, CInstance* otherinst, int argc, RValue* arg)
{
    assert(argc == 0);

    init_int64(result, static_cast<int64>(g_pfo.m_InputDelayMode));
}

YYEXPORT void pfo_set_input_delay_mode(RValue& result, CInstance* selfinst, CInstance* otherinst, int argc, RValue* arg)
{
    assert(argc == 1);

    auto mode = YYGetInt64(arg, 0);
    assert(mode >= 0 && mode < static_cast<int64>(EInputDelayMode::COUNT));

    auto newInputDelayMode = static_cast<EInputDelayMode>(mode);
    if (g_pfo.m_InputDelayMode != newInputDelayMode)
    {
        g_pfo.m_InputDelayMode = newInputDelayMode;
        g_pfo.UpdateInputDelay();
        g_pfo.RequestAutomaticInputDelayChange();
    }
}

YYEXPORT void pfo_get_input_delay_favored_client_index(RValue& result, CInstance* selfinst, CInstance* otherinst, int argc, RValue* arg)
{
    assert(argc == 0);

    init_real(result, g_pfo.m_InputDelayFavoredClientIndex);
}

YYEXPORT void pfo_set_input_delay_favored_client_index(RValue& result, CInstance* selfinst, CInstance* otherinst, int argc, RValue* arg)
{
    assert(argc == 1);

    int clientIndex = YYGetInt32(arg, 0);
    assert(clientIndex >= -1 && clientIndex < static_cast<int>(countof(g_pfo.m_ClientData)));

    if (g_pfo.m_AssignedClientsCount > 1 && g_pfo.m_ClientData[clientIndex].m_PlayerData.m_PlayerIndex >= 0 && g_pfo.m_InputDelayFavoredClientIndex != clientIndex)
    {
        g_pfo.m_InputDelayFavoredClientIndex = clientIndex;
        g_pfo.UpdateInputDelay();
    }
}

YYEXPORT void pfo_get_min_automatic_input_delay(RValue& result, CInstance* selfinst, CInstance* otherinst, int argc, RValue* arg)
{
    assert(argc == 0);

    init_real(result, g_pfo.m_MinAutomaticInputDelay);
}

YYEXPORT void pfo_set_min_automatic_input_delay(RValue& result, CInstance* selfinst, CInstance* otherinst, int argc, RValue* arg)
{
    assert(argc == 1);

    int delay = YYGetInt32(arg, 0);
    delay = min(delay, c_MaxInputDelay);
    delay = max(delay, 0);

    if (g_pfo.m_MinAutomaticInputDelay != delay)
    {
        g_pfo.m_MinAutomaticInputDelay = delay;
        g_pfo.UpdateInputDelay();
    }

    init_real(result, g_pfo.m_MinAutomaticInputDelay);
}

YYEXPORT void pfo_get_max_automatic_input_delay(RValue& result, CInstance* selfinst, CInstance* otherinst, int argc, RValue* arg)
{
    assert(argc == 0);

    init_real(result, g_pfo.m_MaxAutomaticInputDelay);
}

YYEXPORT void pfo_set_max_automatic_input_delay(RValue& result, CInstance* selfinst, CInstance* otherinst, int argc, RValue* arg)
{
    assert(argc == 1);

    int delay = static_cast<int>(YYGetInt32(arg, 0));
    delay = min(delay, c_MaxInputDelay);
    delay = max(delay, 0);

    if (g_pfo.m_MaxAutomaticInputDelay != delay)
    {
        g_pfo.m_MaxAutomaticInputDelay = delay;
        g_pfo.UpdateInputDelay();
    }

    init_real(result, g_pfo.m_MaxAutomaticInputDelay);
}

YYEXPORT void pfo_get_frame(RValue& result, CInstance* selfinst, CInstance* otherinst, int argc, RValue* arg)
{
    assert(argc == 0);

    init_real(result, g_pfo.m_Frame);
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

YYEXPORT void pfo_set_players(RValue& result, CInstance* selfinst, CInstance* otherinst, int argc, RValue* arg)
{
    assert(argc == 1);

    int map[countof(g_pfo.m_PlayerIndexToClientIndexMap)];

    assert(KIND_RValue(&arg[0]) == VALUE_ARRAY);
    int count = YYArrayGetLength(&arg[0]);
    assert(count >= 0 && count <= countof(map));

    for (int i = 0; i < countof(map); i++)
    {
        if (i < count)
        {
            RValue val = {};
            GET_RValue(&val, &arg[0], nullptr, i);
            map[i] = YYGetInt32(&val, 0);
        }
        else
        {
            map[i] = -1;
        }
    }

    g_pfo.SetPlayers(map);
}

YYEXPORT void pfo_get_client_index(RValue& result, CInstance* selfinst, CInstance* otherinst, int argc, RValue* arg)
{
    assert(argc == 0);

    init_real(result, g_pfo.m_ClientIndex);
}

YYEXPORT void pfo_client_get_player_index(RValue& result, CInstance* selfinst, CInstance* otherinst, int argc, RValue* arg)
{
    assert(argc == 0 || argc == 1);

    int clientIndex = argc > 0 ? YYGetInt32(arg, 0) : g_pfo.m_ClientIndex;
    assert(clientIndex >= 0 && clientIndex < countof(g_pfo.m_ClientData));

    init_real(result, g_pfo.m_ClientData[clientIndex].m_PlayerData.m_PlayerIndex);
}

YYEXPORT void pfo_player_get_client_index(RValue& result, CInstance* selfinst, CInstance* otherinst, int argc, RValue* arg)
{
    assert(argc == 1);

    int playerIndex = YYGetInt32(arg, 0);
    assert(playerIndex >= 0 && playerIndex < countof(g_pfo.m_PlayerIndexToClientIndexMap));

    init_real(result, g_pfo.m_PlayerIndexToClientIndexMap[playerIndex]);
}

YYEXPORT void pfo_get_assigned_clients_count(RValue& result, CInstance* selfinst, CInstance* otherinst, int argc, RValue* arg)
{
    assert(argc == 0);

    init_real(result, g_pfo.m_PreviousFrameAssignedClientsCount); // TODO: this is a temporary hack
}

YYEXPORT void pfo_file_exists(RValue& result, CInstance* selfinst, CInstance* otherinst, int argc, RValue* arg)
{
    assert(argc > 0 && argc <= 2);

    int clientIndex = argc > 1 ? YYGetInt32(arg, 1) : 0;

    g_pfo.FileExists(result, selfinst, arg, clientIndex);
}

YYEXPORT void pfo_buffer_load(RValue& result, CInstance* selfinst, CInstance* otherinst, int argc, RValue* arg)
{
    assert(argc == 1);

    g_pfo.BufferLoad(result, selfinst, arg);
}

YYEXPORT void pfo_buffer_save(RValue& result, CInstance* selfinst, CInstance* otherinst, int argc, RValue* arg)
{
    assert(argc == 2);
    assert(KIND_RValue(&arg[0]) == VALUE_REF);
    assert(GET_REF_TYPE(arg[0].v64) == REFID_BUFFER);

    g_pfo.BufferSave(result, selfinst, arg);
}

YYEXPORT void pfo_file_delete(RValue& result, CInstance* selfinst, CInstance* otherinst, int argc, RValue* arg)
{
    assert(argc == 1);

    g_pfo.FileDelete(result, selfinst, arg);
}

YYEXPORT void pfo_file_copy(RValue& result, CInstance* selfinst, CInstance* otherinst, int argc, RValue* arg)
{
    assert(argc == 2);

    g_pfo.FileCopy(result, selfinst, arg);
}

YYEXPORT void pfo_file_status(RValue& result, CInstance* selfinst, CInstance* otherinst, int argc, RValue* arg)
{
    assert(argc == 1);

    g_pfo.FileStatus(result, YYGetString(arg, 0));
}

YYEXPORT void pfo_quit(RValue& result, CInstance* selfinst, CInstance* otherinst, int argc, RValue* arg)
{
    assert(argc == 0);

    g_pfo.Quit();
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

    g_pfo.m_ClientIndex = 0;

    if (g_pfo.m_ListenSocket != k_HSteamListenSocket_Invalid)
    {
        g_SteamNetworkingSockets->CloseListenSocket(g_pfo.m_ListenSocket);
        g_pfo.m_ListenSocket = k_HSteamListenSocket_Invalid;
    }

    g_pfo.m_ListenSocket = g_SteamNetworkingSockets->CreateListenSocketP2P(0, 0, nullptr);
    if (g_pfo.m_ListenSocket == k_HSteamListenSocket_Invalid) YYError("Listen socket invalid");
}

YYEXPORT void pfo_connect(RValue& result, CInstance* selfinst, CInstance* otherinst, int argc, RValue* arg)
{
    assert(argc == 1);
    SteamNetworkingIdentity identity;
    identity.SetSteamID64(static_cast<uint64>(YYGetInt64(arg, 0)));

    g_pfo.m_ClientIndex = 1;

    auto hConn = g_SteamNetworkingSockets->ConnectP2P(identity, 0, 0, nullptr);
    if (hConn != k_HSteamNetConnection_Invalid)
    {
        g_pfo.AddConnection(0, hConn);
    }
    else
    {
        YYError("Connect socket invalid");
    }
}

YYEXPORT void pfo_client_get_ping(RValue& result, CInstance* selfinst, CInstance* otherinst, int argc, RValue* arg)
{
    assert(argc == 0);

    int ping = -2;
    for (int i = 0; i < countof(g_pfo.m_ClientData); i++)
    {
        auto& clientData = g_pfo.m_ClientData[i];
        if (clientData.m_ConnectSocket != k_HSteamNetConnection_Invalid)
        {
            SteamNetConnectionRealTimeStatus_t status;
            g_SteamNetworkingSockets->GetConnectionRealTimeStatus(clientData.m_ConnectSocket, &status, 0, nullptr);
            ping = status.m_nPing;
            break;
        }
    }

    init_real(result, ping);
}
