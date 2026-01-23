#include "pch.h"

#define __YYDEFINE_EXTENSION_FUNCTIONS__
#include "Extension_Interface.h"
#include "Ref.h"
#include "YYRValue.h"
#define YYEXPORT __declspec(dllexport)

//#pragma warning(default: 4365)
#pragma warning(default: 4388)
#pragma warning(default: 4800)
//#pragma warning(default: 4820)

#include "../version.h"

YYRunnerInterface* g_pYYRunnerInterface;

namespace
{
    enum class LogLevel
    {
        Verbose,
        Debug,
        Info,
        Warning,
        Error,
    };

    #if !defined(NDEBUG)
    constexpr LogLevel c_LogLevel = LogLevel::Debug;
    #else
    constexpr LogLevel c_LogLevel = LogLevel::Info;
    #endif

    char g_TempBuffer[0x2000];
    const char* g_LogFileName = "pfo.log";

    void handle_assert(const char* assertion, const char* file, const char* function, int line);

    #if !defined(NDEBUG) || 1
    #define breakpoint() (IsDebuggerPresent() && (__debugbreak(), false))
    #define assert(a) if (!(a)) [[unlikely]] { handle_assert(#a, __FILE__, __FUNCTION__, __LINE__); __debugbreak(); __assume(false); } else {}
    #else
    #define breakpoint() (false)
    #define assert(a) __assume(a)
    #endif

    #define countof(a) static_cast<ptrdiff_t>(sizeof(a) / sizeof((a)[0]))
    #define ssizeof(a) static_cast<ptrdiff_t>(sizeof(a))

    template<typename T>
    FORCEINLINE constexpr T min(T a, T b)
    {
        return a < b ? a : b;
    }
    template<typename T>
    FORCEINLINE constexpr T max(T a, T b)
    {
        return a > b ? a : b;
    }

    template<typename T, typename A>
    FORCEINLINE constexpr T narrow_cast(A a)
    {
        if constexpr (sizeof(T) < sizeof(A))
        {
            assert(static_cast<A>(static_cast<T>(a)) == a);
        }
        else if constexpr (std::is_signed<A>::value != std::is_signed<T>::value && (std::is_signed<A>::value || sizeof(T) == sizeof(A)))
        {
            if constexpr (std::is_signed<A>::value)
            {
                assert(a >= 0);
            }
            else
            {
                assert(static_cast<T>(a) >= 0);
            }
        }

        return static_cast<T>(a);
    }

    template<typename T, typename A>
    FORCEINLINE constexpr T bitwise_cast(A a)
    {
        if constexpr (sizeof(T) < sizeof(A))
        {
            assert(static_cast<A>(static_cast<T>(a)) == a);
        }

        return static_cast<T>(a);
    }

    #if !defined(NDEBUG)
    #define Log(fmt, ...) ReleaseConsoleOutput(fmt, ##__VA_ARGS__)
    #else
    #define Log(fmt, ...) log(fmt, ##__VA_ARGS__)
    #endif

    #define LogVerbose(fmt, ...) if constexpr (c_LogLevel <= LogLevel::Verbose) { Log(fmt, ##__VA_ARGS__); }
    #define LogDebug(fmt, ...) if constexpr (c_LogLevel <= LogLevel::Debug) { Log(fmt, ##__VA_ARGS__); }
    #define LogInfo(fmt, ...) if constexpr (c_LogLevel <= LogLevel::Info) { Log(fmt, ##__VA_ARGS__); }

    #if !defined(NDEBUG)
    #define LogError(fmt, ...) { YYError(fmt, ##__VA_ARGS__); }
    #else
    #define LogError(fmt, ...) { YYError(fmt, ##__VA_ARGS__); LogInfo("ERROR!!! " fmt "\n", ##__VA_ARGS__); }
    #endif

    void log(const char* format, ...)
    {
        va_list args;

        va_start(args, format);
        int length = vsprintf_s(g_TempBuffer, format, args);
        va_end(args);

        HANDLE logFile = CreateFileA(g_LogFileName, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (logFile != INVALID_HANDLE_VALUE)
        {
            if (SetFilePointerEx(logFile, {}, nullptr, FILE_END))
            {
                DWORD bytesToWrite = narrow_cast<DWORD>(length);
                DWORD bytesWritten;
                if (WriteFile(logFile, g_TempBuffer, bytesToWrite, &bytesWritten, nullptr) || bytesWritten != bytesToWrite)
                {
                    // ok
                }
            }

            CloseHandle(logFile);
        }
    }

    void trace(const char* format, ...)
    {
        va_list args;

        va_start(args, format);
        int length = vsprintf_s(g_TempBuffer, format, args);
        va_end(args);

        OutputDebugStringA(g_TempBuffer);
    }

    void handle_assert(const char* assertion, const char* file, const char* function, int line)
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
            LogError("%s", g_TempBuffer);
        }
    }

    constexpr char c_ExtensionName[] = "PFO";
    constexpr char c_ExtensionVersion[] = MOD_VERSION;

    constexpr int64 c_TimeBeforeResendingMessage = 1'000'000 / 60;
    constexpr int64 c_WaitForInputsDelayTime = 1'000'000 / 60;

    constexpr int c_ClientLimit = 8;
    constexpr int c_PlayerLimit = c_ClientLimit;
    constexpr int c_TargetFPS = 60;
    constexpr int c_MaxInputDelay = 20;
    constexpr int c_SamplesNeededBeforeRaisingDelay = 60 * 15;
    constexpr int c_SamplesNeededBeforeLoweringDelay = 60 * 40;
    constexpr int c_PercentSamplesWithinDelayToMaintainDelay = 91;
    constexpr int c_PercentSamplesBelowDelayToLowerDelay = 99;
    constexpr int c_PingDelayCalculationSafetyMargin = 30;
    constexpr int c_FavoredModeExtraInputDelay = 2;
    constexpr int c_MessageInputCountBits = 6;
    constexpr int c_FramesToRunBehindUpperLimit = 70;
    constexpr int c_FramesToRunBehindLowerLimit = 40;

    constexpr int c_CatchupThresholds[] =
    {
        8,
        16,
        32,
    };
    constexpr double c_CatchupThresholdSpeeds[] =
    {
        1.0,
        1.1,
        1.2,
    };

    YYRunnerInterface g_RunnerInterface;

    ISteamUser* g_SteamUser;
    ISteamFriends* g_SteamFriends;
    ISteamMatchmaking* g_SteamMatchmaking;
    ISteamNetworkingSockets* g_SteamNetworkingSockets;
    ISteamNetworkingUtils* g_SteamNetworkingUtils;

    uint8* g_ChecksumRingBuffer;
    constexpr int c_ChecksumRingBufferSize = 0x20000;

    double g_BaseGameSpeedFPS = 0.0;
    double g_FinalGameSpeedFPS = 0.0;

    int g_GMLChecksumBufferMaxSize;
    int g_GMLChecksumBuffer1;
    int g_GMLChecksumBuffer2;

    int g_gml_file_exists;
    int g_gml_buffer_load;
    int g_gml_buffer_save;
    int g_gml_buffer_delete;
    int g_gml_file_delete;
    int g_gml_buffer_seek;
    int g_gml_file_copy;
    int g_gml_game_set_speed;
    int g_gml_display_reset;

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
        dst.~T();
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
            assert(m_Offset + ssizeof(T) <= m_Size);
            memcpy(m_Buffer + m_Offset, &value, sizeof(T));
            m_Offset += sizeof(T);
        }

        void Write(const void* buffer, int size)
        {
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
            assert(m_Offset + ssizeof(T) <= m_Size);
            memcpy(&value, m_Buffer + m_Offset, sizeof(T));
            m_Offset += sizeof(T);
        }

        void Write(void* buffer, int size)
        {
            assert(m_Offset + size <= m_Size);
            memcpy(buffer, m_Buffer + m_Offset, size);
            m_Offset += size;
        }
    };

    template<typename T>
    concept HasAnnotation = requires(T t)
    {
        { t.WriteField(static_cast<const char*>("name")) };
    };

    template<typename T>
    concept HasFullAnnotation = HasAnnotation<T> and requires(T t)
    {
        { t.BeginStruct() };
        { t.EndStruct() };
        { t.BeginArray() };
        { t.EndArray() };
    };

    struct StringWriter
    {
        char* m_String;
        int m_Size;
        int m_Offset = 0;
        int m_Depth = 0;
        bool m_HasValue = false;
        bool m_InArrayAtDepth[43] = {};

        StringWriter() = delete;
        StringWriter(char* str, int count) : m_String(str), m_Size(count) {}

        template<typename T>
        void Write(const T& value)
        {
            const char* fmt;
            if constexpr (std::is_same<T, double>::value)
            {
                fmt = "%f";
            }
            else
            {
                fmt = "%d";
            }

            if (m_HasValue)
            {
                m_Offset += sprintf_s(m_String + m_Offset, m_Size - m_Offset, ", ");
            }
            m_HasValue = true;
            m_Offset += sprintf_s(m_String + m_Offset, m_Size - m_Offset, fmt, value);
        }

        void WriteField(const char* name)
        {
            if (m_HasValue)
            {
                m_Offset += sprintf_s(m_String + m_Offset, m_Size - m_Offset, ", ");
            }
            m_HasValue = false;
            m_Offset += sprintf_s(m_String + m_Offset, m_Size - m_Offset, "%s: ", name);
        }

        void BeginStruct()
        {
            if (m_HasValue)
            {
                m_Offset += sprintf_s(m_String + m_Offset, m_Size - m_Offset, ", ");
            }
            if (m_InArrayAtDepth[m_Depth])
            {
                m_Offset += sprintf_s(m_String + m_Offset, m_Size - m_Offset, "\n");
            }
            m_InArrayAtDepth[++m_Depth] = false;
            m_HasValue = false;
            m_Offset += sprintf_s(m_String + m_Offset, m_Size - m_Offset, "{ ");
        }

        void EndStruct()
        {
            m_Depth--;
            m_HasValue = true;
            m_Offset += sprintf_s(m_String + m_Offset, m_Size - m_Offset, " }");
        }

        void BeginArray()
        {
            m_InArrayAtDepth[++m_Depth] = true;
            if (m_HasValue)
            {
                m_Offset += sprintf_s(m_String + m_Offset, m_Size - m_Offset, ", ");
            }
            m_HasValue = false;
            m_Offset += sprintf_s(m_String + m_Offset, m_Size - m_Offset, "[ ");
        }

        void EndArray()
        {
            m_Depth--;
            m_HasValue = true;
            m_Offset += sprintf_s(m_String + m_Offset, m_Size - m_Offset, " ]");
        }
    };

    template<typename TDiffable>
    struct Differ
    {
        const uint8* m_Buffer1;
        const uint8* m_Buffer2;
        const char* m_Name = nullptr;
        int m_Size;
        int m_Offset = 0;
        int m_TextOffset = 0;
        int m_Differences = 0;

        Differ() = delete;
        Differ(Reader& reader1, Reader& reader2) : m_Buffer1(reader1.m_Buffer), m_Buffer2(reader2.m_Buffer), m_Size(reader1.m_Size)
        {
            assert(reader1.m_Size == reader2.m_Size);
            m_TextOffset += sprintf_s(g_TempBuffer, countof(g_TempBuffer), "Desync detected!\n\nDifference in Session values: [ ");
        }

        template<typename T>
        void Write(const T& value)
        {
            assert(m_Offset + ssizeof(T) <= m_Size);

            if (memcmp(m_Buffer1 + m_Offset, m_Buffer2 + m_Offset, sizeof(T)) != 0)
            {
                if (m_Differences++ > 0)
                {
                    m_TextOffset += sprintf_s(g_TempBuffer + m_TextOffset, countof(g_TempBuffer) - m_TextOffset, ", ");
                }

                m_TextOffset += sprintf_s(g_TempBuffer + m_TextOffset, countof(g_TempBuffer) - m_TextOffset, "\"%s\"", m_Name);
            }

            m_Offset += sizeof(T);
        }

        void WriteField(const char* name)
        {
            m_Name = name;
        }

        void ShowDifferences()
        {
            if (m_Differences != 0)
            {
                auto& diff1 = *new TDiffable();
                auto& diff2 = *new TDiffable();

                Reader reader1(m_Buffer1, m_Size);
                Reader reader2(m_Buffer2, m_Size);
                diff1.Serialize(reader1);
                diff2.Serialize(reader2);

                char* str = g_TempBuffer;
                constexpr int size = countof(g_TempBuffer);

                m_TextOffset += sprintf_s(str + m_TextOffset, size - m_TextOffset, " ]\n\nOurs: ");

                StringWriter stringWriter1(str + m_TextOffset, size - m_TextOffset);
                diff1.Serialize(stringWriter1);
                m_TextOffset += stringWriter1.m_Offset;

                m_TextOffset += sprintf_s(str + m_TextOffset, size - m_TextOffset, "\n\nTheirs: ");

                StringWriter stringWriter2(str + m_TextOffset, size - m_TextOffset);
                diff2.Serialize(stringWriter2);
                m_TextOffset += stringWriter2.m_Offset;

                m_TextOffset += sprintf_s(str + m_TextOffset, size - m_TextOffset, "\n");

                LogInfo("%s", g_TempBuffer);
                ShowMessage(g_TempBuffer);

                breakpoint();

                delete &diff1;
                delete &diff2;
            }
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

    struct ChecksumBuffer
    {
        uint64 m_BufferOffset = 0;
        uint32 m_Frame = 0;
        uint32 m_Checksum = 0;
        int m_SessionBufferSize = 0;
        int m_GMLBufferSize = 0;

        template<typename T>
        constexpr void Serialize(T& writer)
        {
            writer.Write(m_Frame);
            writer.Write(m_Checksum);
            writer.Write(m_SessionBufferSize);
            writer.Write(m_GMLBufferSize);
            writer.Write(g_ChecksumRingBuffer + (m_BufferOffset & (c_ChecksumRingBufferSize - 1)), m_SessionBufferSize + m_GMLBufferSize);
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
        uint64 m_SteamID = 0;
        int64 m_LastSentTime = 0;
        int64 m_LastSentMessageNumber = 0;
        int64 m_LastReceivedMessageNumber = 0;
        int64 m_LastAcknowledgedMessageNumber = 0;
        int64 m_PreviousFrameLastAcknowledgedMessageNumber = 0;
        int64 m_PreviousFrameSampleRTT = 0;
        int64 m_InputFrameSentMessageNumbers[countof(PlayerData::m_InputBuffer)] = {};
        MessageSendInfo m_MessageSendData[64] = { { .m_MessageNumber = 1 } };
        ChecksumBuffer m_ChecksumData[256];
        HSteamNetConnection m_ConnectSocket = k_HSteamNetConnection_Invalid;
        uint32 m_LastInputFrame = 0;
        uint32 m_LastAcknowledgedInputFrame = 0;
        uint32 m_LastSentFrame = 0;
        uint32 m_LastSentInputFrame = 0;
        uint32 m_LastReceivedFrame = 0;
        int m_InputDelay = 0;
        int m_AutomaticInputDelay = 0;
        PlayerData m_PlayerData;
        AutomaticInputDelayChange m_AutomaticInputDelayChanges[countof(m_PlayerData.m_InputBuffer)] = { { .m_InputFrame = 1 } };
        int m_SampleCount = 0;
        int m_SampleHead = 0;
        int m_RaiseDelayHistogram[c_MaxInputDelay + 1] = {};
        int m_LowerDelayHistogram[c_MaxInputDelay + 1] = {};
        uint8 m_Samples[c_SamplesNeededBeforeLoweringDelay] = {};
        bool m_HasReceivedUnacknowledgedInputFrames = false;
        bool m_IsRunningTooFarBehind = false;
    };

    enum class EFileStatus
    {
        None,
        Owned,
        UnownedUnknown,
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
        int64 m_LastReceivedMessageNumber;
        uint32 m_FirstInputFrame;
        uint32 m_Checksum;

        InputFlags_t m_Inputs[1 << c_MessageInputCountBits];
        struct
        {
            uint8 m_InputFrameIndex;
            uint8 m_InputDelay;
        } m_AutomaticInputDelayChanges[countof(m_Inputs)];

        uint8 m_InputFrameCount;
        int8 m_ChecksumFrameDelta;

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
            Message message;

            message.m_InputFrameCount = countof(message.m_Inputs);
            for (int i = 0; i < countof(message.m_AutomaticInputDelayChanges); i++)
            {
                message.m_AutomaticInputDelayChanges[i].m_InputFrameIndex = 0xff;
            }

            SizeGetter size;
            message.Serialize(size);
            return size.m_Size;
        }
    };

    template<int N>
    int CalculatePercentile(int(&buckets)[N], int sampleCount, int percentile)
    {
        assert(percentile >= 0 && percentile <= 100);
        assert(sampleCount > 0);

        int target = sampleCount * percentile / 100;
        int cumulative = 0;

        for (int i = 0; i < countof(buckets); i++)
        {
            if (buckets[i] > 0)
            {
                cumulative += buckets[i];
                if (cumulative >= target)
                {
                    return i;
                }
            }
        }

        assert(false);
    }

    template<int N>
    void CalculateDistributionAt(int(&buckets)[N], int sampleCount, int sample, int* belowPercent, int* belowOrEqualPercent)
    {
        assert(sample < countof(buckets));
        assert(sampleCount > 0);

        int total = 0;
        for (int i = 0; i <= sample; i++)
        {
            total += buckets[i];
        }
        int belowTotal = total - buckets[sample];

        *belowOrEqualPercent = (total * 100 + 50) / sampleCount;
        *belowPercent = (belowTotal * 100 + 50) / sampleCount;
    }

    void SetPowersaveEnabled(bool savePower)
    {
        if (savePower)
        {
            SetThreadExecutionState(ES_CONTINUOUS);
        }
        else
        {
            SetThreadExecutionState(ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED | ES_CONTINUOUS);
        }
    }

    struct Session
    {
        uint64 m_ChecksumRingBufferHead = c_ChecksumRingBufferSize;
        uint64 m_TempChecksumRingBufferHead = 0;
        ClientData m_ClientData[c_ClientLimit];
        int m_PlayerIndexToClientIndexMap[c_PlayerLimit];
        FileData m_FileData[32];

        HSteamListenSocket m_ListenSocket = k_HSteamListenSocket_Invalid;

        EOnlineState m_OnlineState = {};
        uint32 m_Frame = 0;
        uint32 m_LastSentChecksumFrame = 0;
        int m_ClientIndex = -1;
        int m_ClientCount = 0;
        int m_AssignedClientsCount = 0;
        int m_PreviousFrameAssignedClientsCount = 0;

        EInputDelayMode m_InputDelayMode = {};
        uint32 m_RequestedAutomaticInputDelayFrame = 0;
        int m_RequestedAutomaticInputDelay = 0;
        int m_InputDelayFavoredClientIndex = -1;
        int m_MinAutomaticInputDelay = 0;
        int m_MaxAutomaticInputDelay = c_MaxInputDelay;
        int m_CatchupState = 0;

        constexpr Session() : m_PlayerIndexToClientIndexMap{}
        {
            for (int i = 0; i < countof(m_PlayerIndexToClientIndexMap); i++)
            {
                m_PlayerIndexToClientIndexMap[i] = -1;
            }
        }

        template<typename T>
        constexpr void Serialize(T& writer)
        {
#define BEGIN_STRUCT(...) __VA_OPT__(if constexpr (HasAnnotation<T>) { writer.WriteField(__VA_ARGS__); }) if constexpr (HasFullAnnotation<T>) { writer.BeginStruct(); }
#define END_STRUCT() if constexpr (HasFullAnnotation<T>) { writer.EndStruct(); }
#define BEGIN_ARRAY(...) __VA_OPT__(if constexpr (HasAnnotation<T>) { writer.WriteField(__VA_ARGS__); }) if constexpr (HasFullAnnotation<T>) { writer.BeginArray(); }
#define END_ARRAY() if constexpr (HasFullAnnotation<T>) { writer.EndArray(); }
#define WRITE(val, ...) __VA_OPT__(if constexpr (HasAnnotation<T>) { writer.WriteField(__VA_ARGS__); }) writer.Write(val);

            BEGIN_STRUCT()
            {
                WRITE(m_Frame, "Frame");
                WRITE(m_ClientCount, "ClientCount");
                WRITE(m_InputDelayMode, "InputDelayMode");
                WRITE(m_InputDelayFavoredClientIndex, "InputDelayFavoredClientIndex");
                WRITE(m_MinAutomaticInputDelay, "MinAutomaticInputDelay");
                WRITE(m_MaxAutomaticInputDelay, "MaxAutomaticInputDelay");

                BEGIN_ARRAY("PlayerIndexToClientIndexMap")
                {
                    for (int i = 0; i < countof(m_PlayerIndexToClientIndexMap); i++)
                    {
                        WRITE(m_PlayerIndexToClientIndexMap[i]);
                    }
                }
                END_ARRAY()

                BEGIN_ARRAY("ClientData")
                {
                    for (int clientIndex = 0; clientIndex < m_ClientCount; clientIndex++)
                    {
                        BEGIN_STRUCT()
                        {
                            WRITE(m_ClientData[clientIndex].m_InputDelay, "InputDelay");
                            WRITE(m_ClientData[clientIndex].m_AutomaticInputDelay, "AutomaticInputDelay");

                            auto& clientData = m_ClientData[clientIndex];
                            auto& playerData = clientData.m_PlayerData;

                            BEGIN_ARRAY("InputBuffer")
                            {
                                for (int i = 0; i < 2; i++)
                                {
                                    int frame = (m_Frame - i - 1) & (countof(playerData.m_InputBuffer) - 1);
                                    WRITE(playerData.m_InputBuffer[frame]);
                                }
                            }
                            END_ARRAY()

                            BEGIN_STRUCT("AutomaticInputDelayChanges")
                            {
                                int frame = (m_Frame - 1) & (countof(clientData.m_AutomaticInputDelayChanges) - 1);
                                WRITE(clientData.m_AutomaticInputDelayChanges[frame].m_InputFrame, "InputFrame");
                                WRITE(clientData.m_AutomaticInputDelayChanges[frame].m_InputDelay, "InputDelay");
                            }
                            END_STRUCT()
                        }
                        END_STRUCT()
                    }
                }
                END_ARRAY()
            }
            END_STRUCT()

#undef BEGIN_STRUCT
#undef END_STRUCT
#undef BEGIN_ARRAY
#undef END_ARRAY
#undef WRITE
        }

        static consteval int GetMaxSize()
        {
            Session session;
            session.m_ClientCount = countof(m_ClientData);

            SizeGetter size;
            session.Serialize(size);
            return size.m_Size;
        }

        bool Update(CInstance* instance, bool advanceFrame)
        {
            if (m_OnlineState == EOnlineState::StartingGame)
            {
                m_OnlineState = EOnlineState::InGame;
                SetPowersaveEnabled(false);

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
                    ChecksumBuffer tempChecksumBuffer;
                    uint64 checksumRingBufferHead = m_ChecksumRingBufferHead;

                    auto& myClientData = m_ClientData[m_ClientIndex];
                    pChecksumBuffer = &myClientData.m_ChecksumData[m_Frame & (countof(myClientData.m_ChecksumData) - 1)];

                    if (pChecksumBuffer->m_Frame == m_Frame)
                    {
                        pChecksumBuffer = &tempChecksumBuffer;
                    }

                    auto& checksumBuffer = *pChecksumBuffer;
                    reset(checksumBuffer);
                    checksumBuffer.m_Frame = m_Frame;
                    checksumBuffer.m_BufferOffset = checksumRingBufferHead;

                    {
                        Writer writer(g_ChecksumRingBuffer + (checksumRingBufferHead & (c_ChecksumRingBufferSize - 1)), GetMaxSize());
                        Serialize(writer);
                        checksumBuffer.m_SessionBufferSize = writer.m_Offset;
                        checksumRingBufferHead += writer.m_Offset;
                    }

                    {
                        auto ibuffer = BufferGetFromGML(g_GMLChecksumBuffer1);
                        assert(ibuffer != nullptr);

                        buffer_seek(instance, g_GMLChecksumBuffer1, 0, 0);

                        {
                            RValue result = {};
                            RValue arg;
                            init_buffer(arg, g_GMLChecksumBuffer1);
                            Script_Perform(g_gml_Script_getChecksumCallback, instance, instance, 1, &result, &arg);
                        }

                        int dataSize = BufferTELL(ibuffer);
                        assert(dataSize <= g_GMLChecksumBufferMaxSize);
                        uint8* data = BufferGet(ibuffer);

                        memcpy(g_ChecksumRingBuffer + (checksumRingBufferHead & (c_ChecksumRingBufferSize - 1)), data, dataSize);
                        checksumBuffer.m_GMLBufferSize = dataSize;
                        checksumRingBufferHead += dataSize;
                    }

                    if (pChecksumBuffer == &tempChecksumBuffer)
                    {
                        m_TempChecksumRingBufferHead = max(m_TempChecksumRingBufferHead, checksumRingBufferHead);
                    }
                    else
                    {
                        m_ChecksumRingBufferHead = checksumRingBufferHead;
                    }

                    checksumBuffer.m_Checksum = hash_fnv1a32(g_ChecksumRingBuffer + (checksumBuffer.m_BufferOffset & (c_ChecksumRingBufferSize - 1)), checksumBuffer.m_SessionBufferSize + checksumBuffer.m_GMLBufferSize);

                    CheckChecksum(checksumBuffer, m_ClientIndex, instance);
                }

                // handle unasigned players
                if (advanceFrame)
                {
                    auto& myClientData = m_ClientData[m_ClientIndex];
                    auto& myPlayerData = myClientData.m_PlayerData;

                    for (int clientIndex = 0; clientIndex < m_ClientCount; clientIndex++)
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
                            input = bitwise_cast<InputFlags_t>(YYGetInt64(&result, 0));
                        }

                        // if we have more inputs than we need, only keep the input if it's different than the previous
                        bool keepInput = true;
                        if (frame > lastFrame)
                        {
                            InputFlags_t previousInput = myPlayerData.m_InputBuffer[(frame - 1) & (countof(myPlayerData.m_InputBuffer) - 1)];

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
                                    input = bitwise_cast<InputFlags_t>(YYGetInt64(&result, 0));
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

                        for (int clientIndex = 0; clientIndex < m_ClientCount; clientIndex++)
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

                                    if ((netMessage->m_nFlags & k_nSteamNetworkingSend_Reliable) == 0)
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

                                        if (message.m_LastReceivedMessageNumber > clientData.m_LastAcknowledgedMessageNumber)
                                        {
                                            assert(message.m_LastReceivedMessageNumber <= clientData.m_LastSentMessageNumber);
                                            clientData.m_LastAcknowledgedMessageNumber = message.m_LastReceivedMessageNumber;

                                            if (clientData.m_LastAcknowledgedInputFrame < clientData.m_LastSentInputFrame)
                                            {
                                                assert(static_cast<int>(clientData.m_LastSentInputFrame - clientData.m_LastAcknowledgedInputFrame) < countof(clientData.m_InputFrameSentMessageNumbers));

                                                // acknowledge input frames by comparing the received message number against the message numbers we have recorded for each sent input frame
                                                uint32 frame;
                                                for (frame = clientData.m_LastAcknowledgedInputFrame + 1; frame <= clientData.m_LastSentInputFrame; frame++)
                                                {
                                                    auto messageNumber = clientData.m_InputFrameSentMessageNumbers[frame & (countof(clientData.m_InputFrameSentMessageNumbers) - 1)];
                                                    if (message.m_LastReceivedMessageNumber < messageNumber)
                                                    {
                                                        break;
                                                    }
                                                }

                                                clientData.m_LastAcknowledgedInputFrame = frame - 1;
                                            }
                                        }

                                        auto& acknowledgedMessage = clientData.m_MessageSendData[message.m_LastReceivedMessageNumber & (countof(clientData.m_MessageSendData) - 1)];
                                        if (acknowledgedMessage.m_MessageNumber == message.m_LastReceivedMessageNumber)
                                        {
                                            if (acknowledgedMessage.m_MessageAcknowledgeTime == 0)
                                            {
                                                acknowledgedMessage.m_MessageAcknowledgeTime = netMessage->m_usecTimeReceived;
                                            }
                                        }

                                        netMessage->Release();
                                        netMessage = nullptr;

                                        uint32 checksumFrame = message.m_FirstInputFrame + message.m_ChecksumFrameDelta;
                                        assert(message.m_ChecksumFrameDelta >= 0 || checksumFrame < message.m_FirstInputFrame); // detect unsigned overflow
                                        auto& checksumBuffer = clientData.m_ChecksumData[checksumFrame & (countof(clientData.m_ChecksumData) - 1)];

                                        if (checksumBuffer.m_Frame < checksumFrame)
                                        {
                                            // adding 1 because the checksum frame sent is always 1 frame behind the frame the client is actually on
                                            if (clientData.m_LastReceivedFrame < checksumFrame + 1)
                                            {
                                                clientData.m_LastReceivedFrame = checksumFrame + 1;
                                            }

                                            reset(checksumBuffer);
                                            checksumBuffer.m_Frame = checksumFrame;
                                            checksumBuffer.m_Checksum = message.m_Checksum;

                                            CheckChecksum(checksumBuffer, clientIndex, instance);
                                        }

                                        assert(message.m_InputFrameCount <= countof(message.m_Inputs));
                                        if (message.m_InputFrameCount > 0)
                                        {
                                            uint32 lastFrame = message.m_FirstInputFrame + message.m_InputFrameCount - 1;
                                            if (lastFrame > playerData.m_TailInputFrame)
                                            {
                                                uint32 firstFrame = playerData.m_TailInputFrame + 1;
                                                int firstMessageIndex = static_cast<int>(firstFrame - message.m_FirstInputFrame);

                                                // skipping over inputs is possible if they remove themselves as a player before we do, this is fine and we will fill these in with zero as we get to them
                                                if (firstMessageIndex < 0)
                                                {
                                                    firstFrame = message.m_FirstInputFrame;
                                                    firstMessageIndex = 0;
                                                }

                                                int count = static_cast<int>(lastFrame - firstFrame + 1);

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
                                    }
                                    else
                                    {
                                        ReceiveReliableMessage(netMessage, clientIndex, instance);
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

                        for (int clientIndex = 0; clientIndex < m_ClientCount; clientIndex++)
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
                                int countSinceChecksumFrame = static_cast<int>(myClientData.m_LastInputFrame - clientData.m_LastReceivedFrame);
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

                                    uint32 checksumFrame = m_Frame - 1;
                                    auto& checksumBuffer = myClientData.m_ChecksumData[checksumFrame & (countof(myClientData.m_ChecksumData) - 1)];
                                    assert(checksumBuffer.m_Frame == checksumFrame);
                                    int checksumFrameDelta = static_cast<int>(checksumFrame - first);
                                    message.m_ChecksumFrameDelta = narrow_cast<int8>(checksumFrameDelta);
                                    message.m_Checksum = checksumBuffer.m_Checksum;

                                    {
                                        int delayChangeIndex = 0;

                                        if (count > 0)
                                        {
                                            assert(myPlayerData.m_TailInputFrame - first < countof(myPlayerData.m_InputBuffer));
                                            assert(first + count - 1 <= myPlayerData.m_TailInputFrame);

                                            for (int i = 0; i < count; i++)
                                            {
                                                uint32 frame = i + first;

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

                                        if (delayChangeIndex < countof(message.m_AutomaticInputDelayChanges))
                                        {
                                            message.m_AutomaticInputDelayChanges[delayChangeIndex].m_InputFrameIndex = 0;
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
                                    };

                                    // record the first message number we send for each input frame for the purposes of acknowledgement
                                    assert(clientData.m_LastSentInputFrame <= myClientData.m_LastInputFrame);
                                    for (uint32 frame = clientData.m_LastSentInputFrame + 1; frame <= myClientData.m_LastInputFrame; frame++)
                                    {
                                        clientData.m_InputFrameSentMessageNumbers[frame & (countof(clientData.m_InputFrameSentMessageNumbers) - 1)] = messageNumber;
                                    }

                                    clientData.m_LastSentFrame = m_Frame;
                                    clientData.m_LastSentInputFrame = myClientData.m_LastInputFrame;
                                    clientData.m_LastSentTime = time;
                                }
                            }
                        }
                    }

                    if (!advanceFrame)
                    {
                        break;
                    }

                    for (int clientIndex = 0; clientIndex < m_ClientCount; clientIndex++)
                    {
                        auto& clientData = m_ClientData[clientIndex];
                        if (clientData.m_PlayerData.m_TailInputFrame < m_Frame)
                        {
                            canMoveToNextFrame = false;

                            // disconnect if a disconnected client is preventing us from moving to the next frame
                            if (clientData.m_ConnectSocket == k_HSteamNetConnection_Invalid)
                            {
                                assert(clientIndex != m_ClientIndex);
                                assert(clientData.m_PlayerData.m_PlayerIndex >= 0);
                                LogDebug("Ran out of input from disconnected client %d (player %d): Disconnecting\n", clientIndex, clientData.m_PlayerData.m_PlayerIndex);
                                m_OnlineState = EOnlineState::Disconnecting;
                            }
                        }
                    }

                    if (canMoveToNextFrame)
                    {
                        break;
                    }

                    if (m_OnlineState == EOnlineState::InGame)
                    {
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
                            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
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
                    }

                    if (m_OnlineState != EOnlineState::InGame)
                    {
                        m_Frame--;
                        break;
                    }
                }
            }

            if (m_OnlineState == EOnlineState::InGame)
            {
                // update automatic input delay
                if (advanceFrame)
                {
                    // process automatic input delay changes for this frame
                    for (int clientIndex = 0; clientIndex < m_ClientCount; clientIndex++)
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

                    for (int clientIndex = 0; clientIndex < m_ClientCount; clientIndex++)
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
                            if (rtt != 0 && m_AssignedClientsCount > 0 && m_PreviousFrameAssignedClientsCount > 0)
                            {
                                assert(rtt > 0);
                                rtt = min(rtt, static_cast<int64>(INT32_MAX));

                                int delay = static_cast<int>((rtt * c_TargetFPS + 1'000'000 - 1) / 1'000'000);
                                delay = (delay + 1) / 2;
                                uint8 sample = static_cast<uint8>(min(delay, c_MaxInputDelay));

                                assert(sample < countof(clientData.m_LowerDelayHistogram));

                                clientData.m_RaiseDelayHistogram[sample]++;
                                clientData.m_LowerDelayHistogram[sample]++;

                                if (clientData.m_SampleCount >= c_SamplesNeededBeforeRaisingDelay)
                                {
                                    int tail = clientData.m_SampleHead - c_SamplesNeededBeforeRaisingDelay;
                                    if (tail < 0)
                                    {
                                        tail += countof(clientData.m_Samples);
                                    }
                                    int oldSample = clientData.m_Samples[tail];
                                    assert(clientData.m_RaiseDelayHistogram[oldSample] > 0);
                                    clientData.m_RaiseDelayHistogram[oldSample]--;
                                }

                                if (clientData.m_SampleCount >= countof(clientData.m_Samples))
                                {
                                    int oldSample = clientData.m_Samples[clientData.m_SampleHead];
                                    assert(clientData.m_LowerDelayHistogram[oldSample] > 0);
                                    clientData.m_LowerDelayHistogram[oldSample]--;
                                }
                                else
                                {
                                    clientData.m_SampleCount++;
                                }

                                clientData.m_Samples[clientData.m_SampleHead++] = sample;
                                if (clientData.m_SampleHead >= countof(clientData.m_Samples))
                                {
                                    clientData.m_SampleHead = 0;
                                }

                                //trace("Added sample: %d, %d\n", static_cast<int>(rtt), sample);
                            }

                            m_PreviousFrameAssignedClientsCount = m_AssignedClientsCount;
                            clientData.m_PreviousFrameLastAcknowledgedMessageNumber = clientData.m_LastAcknowledgedMessageNumber;
                            clientData.m_PreviousFrameSampleRTT = rtt;

                            if (clientData.m_PlayerData.m_PlayerIndex >= 0)
                            {
                                // prints out some useful info while testing
                                //if (clientData.m_SampleCount >= c_SamplesNeededBeforeRaisingDelay)
                                //{
                                //    int raiseBelowPercent;
                                //    int raiseBelowOrEqualPercent;
                                //    int lowerBelowPercent;
                                //    int lowerBelowOrEqualPercent;
                                //    CalculateDistributionAt(clientData.m_RaiseDelayHistogram, c_SamplesNeededBeforeRaisingDelay, m_RequestedAutomaticInputDelay, &raiseBelowPercent, &raiseBelowOrEqualPercent);
                                //    CalculateDistributionAt(clientData.m_LowerDelayHistogram, clientData.m_SampleCount, m_RequestedAutomaticInputDelay, &lowerBelowPercent, &lowerBelowOrEqualPercent);
                                //    int raiseMedianDelay = CalculatePercentile(clientData.m_RaiseDelayHistogram, c_SamplesNeededBeforeRaisingDelay, 50);
                                //    int lowerMedianDelay = CalculatePercentile(clientData.m_LowerDelayHistogram, clientData.m_SampleCount, 50);

                                //    trace("Raise median: %d, current:%d, <%d%%, <=%d%%, >%d%% | Lower median: %d, current:%d, <%d%%, <=%d%%, >%d%%\n", raiseMedianDelay, m_RequestedAutomaticInputDelay, raiseBelowPercent, raiseBelowOrEqualPercent, 100 - raiseBelowOrEqualPercent, lowerMedianDelay, m_RequestedAutomaticInputDelay, lowerBelowPercent, lowerBelowOrEqualPercent, 100 - lowerBelowOrEqualPercent);
                                //}

                                if (clientData.m_SampleCount >= c_SamplesNeededBeforeRaisingDelay)
                                {
                                    int raiseDelay = CalculatePercentile(clientData.m_RaiseDelayHistogram, c_SamplesNeededBeforeRaisingDelay, c_PercentSamplesWithinDelayToMaintainDelay);
                                    if (raiseDelay >= m_RequestedAutomaticInputDelay)
                                    {
                                        newDelay = max(newDelay, raiseDelay);
                                        hasDelay = true;
                                    }
                                }

                                if (newDelay < m_RequestedAutomaticInputDelay && clientData.m_SampleCount >= c_SamplesNeededBeforeLoweringDelay)
                                {
                                    int lowerDelay = CalculatePercentile(clientData.m_LowerDelayHistogram, clientData.m_SampleCount, c_PercentSamplesBelowDelayToLowerDelay);
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

                // apply gamespeed catchup if we're running too far behind
                if (advanceFrame)
                {
                    int catchupState = 0;

                    if (m_ClientData[m_ClientIndex].m_PlayerData.m_PlayerIndex < 0 && m_AssignedClientsCount > 0)
                    {
                        uint32 lastFrame = UINT32_MAX;
                        for (int clientIndex = 0; clientIndex < m_ClientCount; clientIndex++)
                        {
                            auto& clientData = m_ClientData[clientIndex];
                            if (clientData.m_PlayerData.m_PlayerIndex >= 0)
                            {
                                lastFrame = min(lastFrame, clientData.m_LastInputFrame);
                            }
                        }

                        int framesBehind = static_cast<int>(lastFrame - m_Frame);
                        catchupState = m_CatchupState;

                        while (catchupState < countof(c_CatchupThresholds) - 1 && framesBehind >= c_CatchupThresholds[catchupState + 1])
                        {
                            catchupState++;
                        }

                        while (catchupState > 0 && framesBehind <= c_CatchupThresholds[catchupState - 1])
                        {
                            catchupState--;
                        }

                        //trace("Frames behind: %d, Catchup state: %d\n", framesBehind, catchupState);
                    }

                    if (m_CatchupState != catchupState)
                    {
                        m_CatchupState = catchupState;
                        SetSpeed(g_BaseGameSpeedFPS, instance);
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

                assert(m_OnlineState == EOnlineState::Offline);
            }

            return true;
        }

        void Close()
        {
            for (int clientIndex = 0; clientIndex < countof(m_ClientData); clientIndex++)
            {
                auto& clientData = m_ClientData[clientIndex];
                if (clientData.m_ConnectSocket != k_HSteamNetConnection_Invalid)
                {
                    g_SteamNetworkingSockets->CloseConnection(clientData.m_ConnectSocket, k_ESteamNetConnectionEnd_App_Generic + 1, "Session ended", false);
                    clientData.m_ConnectSocket = k_HSteamNetConnection_Invalid;
                }
            }

            if (m_ListenSocket != k_HSteamListenSocket_Invalid)
            {
                g_SteamNetworkingSockets->CloseListenSocket(m_ListenSocket);
                m_ListenSocket = k_HSteamListenSocket_Invalid;
            }
        }

        void Reset(CInstance* instance)
        {
            Close();

            for (int i = 0; i < countof(m_FileData); i++)
            {
                auto& fileData = m_FileData[i];
                if (fileData.m_Filename != nullptr)
                {
                    YYFree(fileData.m_Filename);
                    fileData.m_Filename = nullptr;
                }
                if (fileData.m_Data != nullptr)
                {
                    YYFree(fileData.m_Data);
                    fileData.m_Data = nullptr;
                }
            }

            reset(*this);

            SetPowersaveEnabled(true);
            SetSpeed(g_BaseGameSpeedFPS, instance);
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

                LogVerbose("Frame %u: Input for player %d = [ %x ]\n", m_Frame, playerIndex, in);
            }

            return in;
        }

        void CheckChecksum(ChecksumBuffer& a, int aClientIndex, CInstance* instance)
        {
            for (int bClientIndex = 0; bClientIndex < m_ClientCount; bClientIndex++)
            {
                auto& clientData = m_ClientData[bClientIndex];
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
                            SendReliableMessage(reliableMessage, instance, nullptr, 0, &mine);
                            m_LastSentChecksumFrame = mine.m_Frame;
                        }
                    }

                    if (a.m_BufferOffset != 0 && b.m_BufferOffset != 0)
                    {
                        if (bClientIndex == m_ClientIndex)
                        {
                            DiffChecksums(b, bClientIndex, a, aClientIndex, instance);
                        }
                        else
                        {
                            DiffChecksums(a, aClientIndex, b, bClientIndex, instance);
                        }

                        // we quit when a checksum failure happens, could change this in the future
                        PostMessageW(nullptr, WM_QUIT, 0, 0);
                        m_OnlineState = EOnlineState::Quitting;
                    }
                }
            }
        }

        void DiffChecksums(ChecksumBuffer& a, int aClientIndex, ChecksumBuffer& b, int bClientIndex, CInstance* instance)
        {
            LogInfo("Diffing checksums (frame=%u): Ours (client=%d), Theirs (client=%d)\n", a.m_Frame, aClientIndex, bClientIndex);

            assert(max(m_ChecksumRingBufferHead, m_TempChecksumRingBufferHead) - a.m_BufferOffset <= c_ChecksumRingBufferSize);
            assert(max(m_ChecksumRingBufferHead, m_TempChecksumRingBufferHead) - b.m_BufferOffset <= c_ChecksumRingBufferSize);

            bool diffInSession = false;
            bool diffInGML = false;

            // compare pfo checksum
            uint8* aSessionBuffer = g_ChecksumRingBuffer + (a.m_BufferOffset & (c_ChecksumRingBufferSize - 1));
            uint8* bSessionBuffer = g_ChecksumRingBuffer + (b.m_BufferOffset & (c_ChecksumRingBufferSize - 1));
            if (a.m_SessionBufferSize != b.m_SessionBufferSize || memcmp(aSessionBuffer, bSessionBuffer, a.m_SessionBufferSize) != 0)
            {
                diffInSession = true;
                Reader readerA(aSessionBuffer, a.m_SessionBufferSize);
                Reader readerB(bSessionBuffer, b.m_SessionBufferSize);

                Differ<Session> differ(readerA, readerB);
                Serialize(differ);
                differ.ShowDifferences();
            }

            // compare gml checksum
            uint8* aGMLBuffer = aSessionBuffer + a.m_SessionBufferSize;
            uint8* bGMLBuffer = bSessionBuffer + b.m_SessionBufferSize;
            if (a.m_GMLBufferSize != b.m_GMLBufferSize || memcmp(aGMLBuffer, bGMLBuffer, a.m_GMLBufferSize) != 0)
            {
                diffInGML = true;
                BufferWriteContent(g_GMLChecksumBuffer1, 0, aGMLBuffer, a.m_GMLBufferSize);
                BufferWriteContent(g_GMLChecksumBuffer2, 0, bGMLBuffer, b.m_GMLBufferSize);

                buffer_seek(instance, g_GMLChecksumBuffer1, 0, 0);
                buffer_seek(instance, g_GMLChecksumBuffer2, 0, 0);

                {
                    RValue result = {};
                    RValue arg[2];
                    init_buffer(arg[0], g_GMLChecksumBuffer1);
                    init_buffer(arg[1], g_GMLChecksumBuffer2);
                    Script_Perform(g_gml_Script_getChecksumCallback, instance, instance, countof(arg), &result, arg);
                }
            }

            assert(diffInSession || diffInGML);
        }

        void OnNetConnectionStatusChanged(SteamNetConnectionStatusChangedCallback_t* pParam)
        {
            auto hConn = pParam->m_hConn;
            auto& info = pParam->m_info;
            auto eOldState = pParam->m_eOldState;

            LogDebug("Connection status changed conn: %u, state %d -> %d, end: %d %s\n", hConn, eOldState, info.m_eState, info.m_eEndReason, info.m_szEndDebug);

            if (info.m_eState == k_ESteamNetworkingConnectionState_ClosedByPeer || info.m_eState == k_ESteamNetworkingConnectionState_ProblemDetectedLocally)
            {
                if (info.m_eState == k_ESteamNetworkingConnectionState_ClosedByPeer)
                {
                    g_SteamNetworkingSockets->CloseConnection(hConn, k_ESteamNetConnectionEnd_App_Generic, "Closed by peer", false);
                }
                else
                {
                    g_SteamNetworkingSockets->CloseConnection(hConn, info.m_eEndReason, info.m_szEndDebug, false);
                }

                for (int clientIndex = 0; clientIndex < m_ClientCount; clientIndex++)
                {
                    auto& clientData = m_ClientData[clientIndex];
                    if (clientData.m_ConnectSocket == hConn)
                    {
                        clientData.m_ConnectSocket = k_HSteamNetConnection_Invalid;

                        {
                            auto map = CreateDsMap(0, 0);
                            DsMapAddDouble(map, "type", 2);
                            DsMapAddDouble(map, "success", info.m_eState == k_ESteamNetworkingConnectionState_ClosedByPeer);
                            DsMapAddDouble(map, "reason", info.m_eEndReason);
                            DsMapAddInt64(map, "steam_id", static_cast<int64>(clientData.m_SteamID));
                            CreateAsyncEventWithDSMap(map, 68);
                        }
                    }
                }

                if (info.m_eState == k_ESteamNetworkingConnectionState_ProblemDetectedLocally &&
                    (info.m_eEndReason == k_ESteamNetConnectionEnd_Remote_Timeout || info.m_eEndReason == k_ESteamNetConnectionEnd_Misc_Timeout))
                {
                    if (m_OnlineState == EOnlineState::InGame)
                    {
                        LogDebug("Connection timed out\n");
                    }
                }

                // disconnect if we're the last player in the session
                if (m_OnlineState == EOnlineState::InGame)
                {
                    int connectedCount = 0;

                    for (int clientIndex = 0; clientIndex < m_ClientCount; clientIndex++)
                    {
                        auto& clientData = m_ClientData[clientIndex];
                        if (clientData.m_ConnectSocket != k_HSteamNetConnection_Invalid)
                        {
                            connectedCount++;
                        }
                    }

                    if (connectedCount == 0)
                    {
                        LogDebug("No more clients connected: Disconnecting\n");
                        m_OnlineState = EOnlineState::Disconnecting;
                    }
                }
            }

            if (info.m_hListenSocket != k_HSteamListenSocket_Invalid &&
                eOldState == k_ESteamNetworkingConnectionState_None &&
                info.m_eState == k_ESteamNetworkingConnectionState_Connecting)
            {
                if (m_OnlineState <= EOnlineState::InGame && m_ClientCount > 0)
                {
                    bool found = false;

                    // accept connections from clients after us
                    for (int clientIndex = m_ClientIndex + 1; clientIndex < m_ClientCount; clientIndex++)
                    {
                        auto& clientData = m_ClientData[clientIndex];
                        if (info.m_identityRemote.GetSteamID64() == clientData.m_SteamID)
                        {
                            if (clientData.m_ConnectSocket != k_HSteamNetConnection_Invalid)
                            {
                                g_SteamNetworkingSockets->CloseConnection(clientData.m_ConnectSocket, k_ESteamNetConnectionEnd_AppException_Generic + 2, "New connection received", false);
                                clientData.m_ConnectSocket = k_HSteamNetConnection_Invalid;
                            }

                            EResult res = g_SteamNetworkingSockets->AcceptConnection(hConn);
                            if (res == k_EResultOK)
                            {
                                AddConnection(clientData, hConn);
                            }
                            else
                            {
                                g_SteamNetworkingSockets->CloseConnection(hConn, k_ESteamNetConnectionEnd_AppException_Generic, "Failed to accept connection", false);
                                LogError("AcceptConnection returned %d", res);
                            }

                            found = true;
                            break;
                        }
                    }

                    if (!found)
                    {
                        g_SteamNetworkingSockets->CloseConnection(hConn, k_ESteamNetConnectionEnd_AppException_Generic + 3, "Not accepting connections from this SteamID", false);
                    }
                }
                else
                {
                    g_SteamNetworkingSockets->CloseConnection(hConn, k_ESteamNetConnectionEnd_AppException_Generic + 1, "Game no longer available", false);
                }
            }

            if (info.m_eState == k_ESteamNetworkingConnectionState_Connected)
            {
                if (m_OnlineState == EOnlineState::Offline && m_ClientCount > 0)
                {
                    bool allClientsConnected = true;
                    SteamNetConnectionRealTimeStatus_t status;

                    for (int clientIndex = 0; clientIndex < m_ClientCount; clientIndex++)
                    {
                        if (clientIndex != m_ClientIndex)
                        {
                            auto& clientData = m_ClientData[clientIndex];
                            if (clientData.m_ConnectSocket == k_HSteamNetConnection_Invalid
                                || g_SteamNetworkingSockets->GetConnectionRealTimeStatus(clientData.m_ConnectSocket, &status, 0, nullptr) != k_EResultOK
                                || status.m_eState != k_ESteamNetworkingConnectionState_Connected)
                            {
                                allClientsConnected = false;
                                break;
                            }
                        }
                    }

                    if (allClientsConnected)
                    {
                        m_OnlineState = EOnlineState::StartingGame;
                    }
                }
            }
        }

        void AddConnection(ClientData& clientData, HSteamNetConnection hConn)
        {
            assert(clientData.m_ConnectSocket == k_HSteamNetConnection_Invalid);

            constexpr int lanePriorities[] = { 1, 1 }; // lowest number drains first
            constexpr uint16 laneWeights[] = { 1, 1 }; // higher number means more relative bandwidth
            g_SteamNetworkingSockets->ConfigureConnectionLanes(hConn, countof(lanePriorities), lanePriorities, laneWeights);
            clientData.m_ConnectSocket = hConn;
        }

        bool SetClients(CSteamID (&steamIDs)[countof(m_ClientData)], int clientCount)
        {
            assert(clientCount >= 2 && clientCount <= countof(steamIDs));
            assert(m_ClientIndex == -1 && m_ClientCount == 0);
            auto mySteamID = g_SteamUser->GetSteamID();

            int myClientIndex = -1;
            for (int clientIndex = 0; clientIndex < clientCount; clientIndex++)
            {
                auto steamID = steamIDs[clientIndex];
                assert(steamID.IsValid());
                
                // make sure each SteamID is unique
                for (int i = 0; i < clientIndex; i++)
                {
                    assert(steamID != steamIDs[i]);
                }

                if (steamID == mySteamID)
                {
                    myClientIndex = clientIndex;
                }
            }

            if (myClientIndex >= 0)
            {
                m_ClientIndex = myClientIndex;
                m_ClientCount = clientCount;

                for (int clientIndex = 0; clientIndex < clientCount; clientIndex++)
                {
                    m_ClientData[clientIndex].m_SteamID = steamIDs[clientIndex].ConvertToUint64();
                }

                // create a listen socket for connecting to all clients after us
                if (m_ClientIndex < m_ClientCount - 1)
                {
                    assert(m_ListenSocket == k_HSteamListenSocket_Invalid);

                    m_ListenSocket = g_SteamNetworkingSockets->CreateListenSocketP2P(0, 0, nullptr);
                    if (m_ListenSocket == k_HSteamListenSocket_Invalid)
                    {
                        LogError("Listen socket invalid");
                    }
                }
            }

            return myClientIndex >= 0;
        }

        void Connect()
        {
            assert(m_ClientIndex >= 0);

            // connect to all clients before us
            for (int clientIndex = 0; clientIndex < m_ClientIndex; clientIndex++)
            {
                auto& clientData = m_ClientData[clientIndex];
                SteamNetworkingIdentity identity;
                identity.SetSteamID64(clientData.m_SteamID);

                auto hConn = g_SteamNetworkingSockets->ConnectP2P(identity, 0, 0, nullptr);
                if (hConn != k_HSteamNetConnection_Invalid)
                {
                    AddConnection(clientData, hConn);
                }
                else
                {
                    LogError("Connect socket invalid");
                }
            }
        }

        void SetPlayers(int (&map)[countof(m_PlayerIndexToClientIndexMap)])
        {
            int anyAssignedClient = -1;
            int oldClientToPlayerIndexMap[countof(m_ClientData)];

            for (int clientIndex = 0; clientIndex < m_ClientCount; clientIndex++)
            {
                auto& playerData = m_ClientData[clientIndex].m_PlayerData;
                oldClientToPlayerIndexMap[clientIndex] = playerData.m_PlayerIndex;
                playerData.m_PlayerIndex = -1;
            }

            for (int playerIndex = countof(map) - 1; playerIndex >= 0; playerIndex--)
            {
                int clientIndex = map[playerIndex];
                assert(clientIndex >= -1 && clientIndex < m_ClientCount);
                if (clientIndex >= 0)
                {
                    auto& playerData = m_ClientData[clientIndex].m_PlayerData;
                    assert(playerData.m_PlayerIndex < 0);
                    playerData.m_PlayerIndex = playerIndex;
                    anyAssignedClient = clientIndex;
                }
                m_PlayerIndexToClientIndexMap[playerIndex] = clientIndex;
            }

            bool changedPlayerAssignment = false;
            m_AssignedClientsCount = 0;
            for (int clientIndex = 0; clientIndex < m_ClientCount; clientIndex++)
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

            if (changedPlayerAssignment)
            {
                LogDebug("We changed the players [ %d, %d, %d, %d, %d, %d, %d, %d ] on frame: %u\n", map[0], map[1], map[2], map[3], map[4], map[5], map[6], map[7], m_Frame);
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
                    for (int clientIndex = 0; clientIndex < m_ClientCount; clientIndex++)
                    {
                        auto& clientData = m_ClientData[clientIndex];
                        if (clientData.m_PlayerData.m_PlayerIndex >= 0)
                        {
                            automaticInputDelay = max(automaticInputDelay, clientData.m_AutomaticInputDelay);
                        }
                    }
                }

                // set input delay based on the current favored player
                for (int clientIndex = 0; clientIndex < m_ClientCount; clientIndex++)
                {
                    int delay = 0;
                    int maxDelay = m_MaxAutomaticInputDelay;
                    int minDelay = m_MinAutomaticInputDelay;

                    if (m_InputDelayFavoredClientIndex < 0)
                    {
                        delay = automaticInputDelay;
                    }
                    else if (m_InputDelayFavoredClientIndex == clientIndex)
                    {
                        delay = 0;
                        minDelay = 0;
                        maxDelay = 0;
                    }
                    else
                    {
                        delay = automaticInputDelay * 2 + c_FavoredModeExtraInputDelay;
                        maxDelay *= 2;
                        minDelay *= 2;
                    }

                    delay = min(delay, maxDelay);
                    delay = max(delay, minDelay);
                    assert(delay >= 0 && delay <= c_MaxInputDelay * 2);

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
                        for (int clientIndex = 0; clientIndex < m_ClientCount; clientIndex++)
                        {
                            auto& clientData = m_ClientData[clientIndex];
                            if (clientData.m_ConnectSocket != k_HSteamNetConnection_Invalid && clientData.m_PlayerData.m_PlayerIndex >= 0)
                            {
                                if (clientData.m_SampleCount >= c_SamplesNeededBeforeRaisingDelay)
                                {
                                    int newDelay = CalculatePercentile(clientData.m_RaiseDelayHistogram, c_SamplesNeededBeforeRaisingDelay, c_PercentSamplesWithinDelayToMaintainDelay);
                                    delay = max(delay, newDelay);
                                }
                            }
                        }

                        if (delay < 0)
                        {
                            SteamNetConnectionRealTimeStatus_t status;

                            for (int clientIndex = 0; clientIndex < m_ClientCount; clientIndex++)
                            {
                                auto& clientData = m_ClientData[clientIndex];
                                if (clientData.m_ConnectSocket != k_HSteamNetConnection_Invalid && clientData.m_PlayerData.m_PlayerIndex >= 0)
                                {
                                    if (g_SteamNetworkingSockets->GetConnectionRealTimeStatus(clientData.m_ConnectSocket, &status, 0, nullptr) == k_EResultOK && status.m_nPing >= 0)
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

        void SetSpeed(double speed, CInstance* instance)
        {
            g_BaseGameSpeedFPS = speed;

            assert(m_CatchupState >= 0 && m_CatchupState < countof(c_CatchupThresholdSpeeds));
            speed *= c_CatchupThresholdSpeeds[m_CatchupState];

            if (g_FinalGameSpeedFPS != speed)
            {
                bool oldVsync = g_FinalGameSpeedFPS <= 60.0;
                bool newVsync = speed <= 60;
                g_FinalGameSpeedFPS = speed;

                if (oldVsync != newVsync)
                {
                    RValue result = {};
                    RValue args[2];
                    init_real(args[0], 0.0);
                    init_bool(args[1], newVsync);
                    Script_Perform(g_gml_display_reset, instance, instance, countof(args), &result, args);
                }

                RValue result = {};
                RValue args[2];
                init_real(args[0], speed);
                init_real(args[1], 0.0);
                Script_Perform(g_gml_game_set_speed, instance, instance, countof(args), &result, args);
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

        void SendReliableMessage(ReliableMessage& reliableMessage, CInstance* instance, void* data, int dataSize, ChecksumBuffer* checksumBuffer = nullptr)
        {
            int messageRetryCount = -1;
            int messageSize = 0;
            bool messageSent[countof(m_ClientData)] = {};

            while (m_OnlineState == EOnlineState::InGame && messageRetryCount != 0)
            {
                if (messageRetryCount > 0)
                {
                    Timing_Sleep(1'000'000 / 2);
                }

                messageRetryCount = 0;
                int messageCount = 0;
                SteamNetworkingMessage_t* netMessages[countof(m_ClientData) - 1];
                int netMessageToClientIndexMap[countof(netMessages)];

                for (int clientIndex = 0; clientIndex < m_ClientCount; clientIndex++)
                {
                    auto& clientData = m_ClientData[clientIndex];
                    if (clientData.m_ConnectSocket != k_HSteamNetConnection_Invalid && !messageSent[clientIndex])
                    {
                        netMessageToClientIndexMap[messageCount] = clientIndex;
                        auto netMessage = netMessages[messageCount++] = g_SteamNetworkingUtils->AllocateMessage(k_cbMaxSteamNetworkingSocketsMessageSizeSend);

                        Writer writer(static_cast<uint8*>(netMessage->m_pData), netMessage->m_cbSize);

                        if (reliableMessage.m_Type == EReliableMessageType::File)
                        {
                            reliableMessage.Serialize(writer);
                            writer.Write(data, dataSize);
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
                        else
                        {
                            assert(false);
                        }

                        netMessage->m_conn = clientData.m_ConnectSocket;
                        netMessage->m_cbSize = writer.m_Offset;
                        netMessage->m_nFlags = k_nSteamNetworkingSend_Reliable;
                        netMessage->m_idxLane = 1;

                        messageSize = netMessage->m_cbSize;
                    }
                }

                if (messageCount > 0)
                {
                    int64 outResults[countof(netMessages)];

                    g_SteamNetworkingSockets->SendMessages(messageCount, netMessages, outResults);
                    for (int i = 0; i < countof(netMessages); i++)
                    {
                        netMessages[i] = nullptr;
                    }

                    for (int i = 0; i < messageCount; i++)
                    {
                        int64 result = outResults[i];
                        assert(!messageSent[netMessageToClientIndexMap[i]]);

                        if (result > 0)
                        {
                            LogDebug("SendReliableMessage (size = %d) succeeded\n", messageSize);
                            messageSent[netMessageToClientIndexMap[i]] = true;
                        }
                        else if (result == -k_EResultLimitExceeded)
                        {
                            LogDebug("SendReliableMessage (size = %d) failed with k_EResultLimitExceeded: Resending message\n", messageSize);
                            messageRetryCount++;
                        }
                        else
                        {
                            LogInfo("SendReliableMessage (size = %d) failed with error code '%lld': Disconnecting\n", messageSize, result);
                            m_OnlineState = EOnlineState::Disconnecting;
                        }
                    }
                }
            }
        }

        void ReceiveReliableMessage(SteamNetworkingMessage_t* netMessage, int clientIndex, CInstance* instance)
        {
            LogDebug("ReceiveReliableMessage (size = %d)\n", netMessage->m_cbSize);

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

                reset(checksumBuffer);
                checksumBuffer.m_BufferOffset = m_ChecksumRingBufferHead;
                checksumBuffer.Serialize(reader);
                m_ChecksumRingBufferHead += checksumBuffer.m_SessionBufferSize + checksumBuffer.m_GMLBufferSize;
                assert(checksumBuffer.m_Frame == reliableMessage.m_Frame);

                CheckChecksum(checksumBuffer, clientIndex, instance);
            }

            netMessage->Release();
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
                    bool exists = BOOL_RValue(&result);

                    if (fileData->m_Status == EFileStatus::None)
                    {
                        fileData->m_Status = EFileStatus::Owned;

                        if (m_OnlineState == EOnlineState::InGame)
                        {
                            ReliableMessage reliableMessage;
                            reliableMessage.m_Type = exists ? EReliableMessageType::File : EReliableMessageType::FileDoesNotExist;
                            strcpy_s(reliableMessage.m_Filename, filename);

                            void* data = nullptr;
                            int dataSize = 0;

                            if (exists)
                            {
                                RValue tmpBuffer = {};
                                Script_Perform(g_gml_buffer_load, instance, instance, 1, &tmpBuffer, arg);
                                assert(KIND_RValue(&tmpBuffer) == VALUE_REF && GET_REF_TYPE(tmpBuffer.v64) == REFID_BUFFER);

                                bool success = BufferGetContent(tmpBuffer.v32, &data, &dataSize);
                                assert(success);
                                assert(data != nullptr);
                                assert(dataSize >= 0);

                                RValue tmpResult = {};
                                Script_Perform(g_gml_buffer_delete, instance, instance, 1, &tmpResult, &tmpBuffer);
                            }

                            reliableMessage.m_Size = dataSize;

                            SendReliableMessage(reliableMessage, instance, data, dataSize);

                            if (data != nullptr)
                            {
                                YYFree(data);
                            }
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
            }
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
                int bufferIndex = CreateBuffer(fileData->m_Size, eBuffer_Format_Fixed, 1);
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
                bool success = BufferGetContent(arg[0].v32, &data, &dataSize);
                assert(success);
                assert(data != nullptr);
                assert(dataSize >= 0);

                fileData->m_Status = EFileStatus::UnownedExists;
                fileData->m_Size = dataSize;
                fileData->m_Data = data;
            } break;
            case EFileStatus::UnownedUnknown:
            {
                // it's ok to attempt to write to an unknown file during shutdown, just do nothing
                assert(m_OnlineState != EOnlineState::InGame);
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

                int dataSize = fileData->m_Size;
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

    Session g_Session;

    void* CreateRingBuffer(size_t bufferSize)
    {
        SYSTEM_INFO sysInfo;
        GetSystemInfo(&sysInfo);
        assert((bufferSize % sysInfo.dwAllocationGranularity) == 0);

        void* placeholder1 = VirtualAlloc2FromApp(nullptr, nullptr, bufferSize * 2, MEM_RESERVE | MEM_RESERVE_PLACEHOLDER, PAGE_NOACCESS, nullptr, 0);
        assert(placeholder1 != nullptr);

        BOOL result = VirtualFree(placeholder1, bufferSize, MEM_RELEASE | MEM_PRESERVE_PLACEHOLDER);
        assert(result);
        void* placeholder2 = static_cast<void*>(static_cast<uint8_t*>(placeholder1) + bufferSize);

        HANDLE section = CreateFileMappingFromApp(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, bufferSize, nullptr);
        assert(section != nullptr);

        void* view1 = MapViewOfFile3FromApp(section, nullptr, placeholder1, 0, bufferSize, MEM_REPLACE_PLACEHOLDER, PAGE_READWRITE, nullptr, 0);
        assert(view1 != nullptr);

        void* view2 = MapViewOfFile3FromApp(section, nullptr, placeholder2, 0, bufferSize, MEM_REPLACE_PLACEHOLDER, PAGE_READWRITE, nullptr, 0);
        assert(view2 != nullptr);

        return view1;
    }
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

    {
        const char* cmd = GetCommandLineA();
        constexpr char option[] = "-output ";
        constexpr char prefix[] = "pfo-";

        while (*cmd != '\0')
        {
            while (*cmd == ' ') cmd++;
            if (strncmp(cmd, option, countof(option) - 1) == 0)
            {
                cmd += countof(option) - 1;
                while (*cmd == ' ') cmd++;
                auto start = cmd;
                while (*cmd != ' ' && *cmd != '\0') cmd++;
                auto len = narrow_cast<size_t>(cmd - start);
                auto size = narrow_cast<int>(len + countof(prefix));
                auto fileName = static_cast<char*>(YYAlloc(size));
                strcpy_s(fileName, len, prefix);
                strncpy_s(fileName + countof(prefix) - 1, size, start, len);
                g_LogFileName = fileName;
                break;
            }
            while (*cmd != ' ' && *cmd != '\0') cmd++;
        }
    }

    auto version = extGetVersion(c_ExtensionName);
    if (strcmp(version, c_ExtensionVersion) != 0)
    {
        sprintf_s(g_TempBuffer, "Error: Version mismatch detected between PFO.dll (v%s) and data.win (v%s).\n\nPlease make sure you have copied PFO.dll into the same folder as data.win, and that both are the from the same version of the mod.", c_ExtensionVersion, version);
        ShowMessage(g_TempBuffer);
        ExitProcess(0);
    }

    auto maxSize = extOptGetString(c_ExtensionName, "checksumBufferMaxSize");
    if (maxSize != nullptr && maxSize[0] != '\0' && strcmp(maxSize, "undefined") != 0)
    {
        char* end;
        g_GMLChecksumBufferMaxSize = strtol(maxSize, &end, 10);
        if (end[0] != '\0')
        {
            LogError("Invalid checksumBufferMaxSize value");
        }
    }

    int requiredChecksumBufferSize = (Session::GetMaxSize() + g_GMLChecksumBufferMaxSize) * (countof(ClientData::m_ChecksumData) + c_ClientLimit * c_ClientLimit + 1);
    assert(requiredChecksumBufferSize <= c_ChecksumRingBufferSize);

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
    g_gml_game_set_speed = get_builtin_function("game_set_speed");
    g_gml_display_reset = get_builtin_function("display_reset");

    g_SteamUser = SteamUser();
    g_SteamFriends = SteamFriends();
    g_SteamMatchmaking = SteamMatchmaking();
    g_SteamNetworkingSockets = SteamNetworkingSockets();
    g_SteamNetworkingUtils = SteamNetworkingUtils();

    g_ChecksumRingBuffer = static_cast<uint8_t*>(CreateRingBuffer(c_ChecksumRingBufferSize));
    g_GMLChecksumBuffer1 = CreateBuffer(g_GMLChecksumBufferMaxSize, eBuffer_Format_Grow, 1);
    g_GMLChecksumBuffer2 = CreateBuffer(g_GMLChecksumBufferMaxSize, eBuffer_Format_Grow, 1);

    g_SteamNetworkingUtils->InitRelayNetworkAccess();
    g_SteamNetworkingSockets->InitAuthentication();
    g_SteamNetworkingUtils->SetGlobalConfigValueInt32(k_ESteamNetworkingConfig_TimeoutConnected, 60 * 1000);
    g_SteamNetworkingUtils->SetGlobalConfigValueInt32(k_ESteamNetworkingConfig_SendBufferSize, 0x100000);

    struct SteamCallbacks
    {
        STEAM_CALLBACK(SteamCallbacks, OnNetConnectionStatusChanged, SteamNetConnectionStatusChangedCallback_t)
        {
            g_Session.OnNetConnectionStatusChanged(pParam);
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
    };
    new SteamCallbacks();

    DebugConsoleOutput("PFO 50 YYExtensionInitialise CONFIGURED\n");
}

YYEXPORT void pfo_update(RValue& result, CInstance* selfinst, CInstance* otherinst, int argc, RValue* arg)
{
    assert(argc == 0 || argc == 1);

    bool advanceFrame = argc > 0 ? YYGetBool(arg, 0) : true;

    bool success = g_Session.Update(selfinst, advanceFrame);
    init_bool(result, success);
}

YYEXPORT void pfo_player_get_input(RValue& result, CInstance* selfinst, CInstance* otherinst, int argc, RValue* arg)
{
    assert(argc == 1);

    int playerIndex = YYGetInt32(arg, 0);
    init_int64(result, g_Session.PlayerGetInput(selfinst, playerIndex));
}

YYEXPORT void pfo_is_online(RValue& result, CInstance* selfinst, CInstance* otherinst, int argc, RValue* arg)
{
    assert(argc == 0);

    init_bool(result, g_Session.m_OnlineState >= EOnlineState::InGame);
}

YYEXPORT void pfo_client_get_input_delay(RValue& result, CInstance* selfinst, CInstance* otherinst, int argc, RValue* arg)
{
    assert(argc <= 1);

    int clientIndex = argc > 0 ? YYGetInt32(arg, 0) : g_Session.m_ClientIndex;
    assert(clientIndex >= 0 && clientIndex < countof(g_Session.m_ClientData));

    init_real(result, g_Session.m_ClientData[clientIndex].m_InputDelay);
}

YYEXPORT void pfo_client_set_input_delay(RValue& result, CInstance* selfinst, CInstance* otherinst, int argc, RValue* arg)
{
    assert(argc > 0 && argc <= 2);

    int delay = YYGetInt32(arg, 0);
    assert(delay >= 0 && delay <= c_MaxInputDelay * 2);

    int clientIndex = argc > 1 ? YYGetInt32(arg, 1) : g_Session.m_ClientIndex;
    assert(clientIndex >= 0 && clientIndex < countof(g_Session.m_ClientData));

    if (g_Session.m_InputDelayMode != EInputDelayMode::Automatic)
    {
        g_Session.m_ClientData[clientIndex].m_InputDelay = delay;
    }
}

YYEXPORT void pfo_get_input_delay_mode(RValue& result, CInstance* selfinst, CInstance* otherinst, int argc, RValue* arg)
{
    assert(argc == 0);

    init_int64(result, static_cast<int64>(g_Session.m_InputDelayMode));
}

YYEXPORT void pfo_set_input_delay_mode(RValue& result, CInstance* selfinst, CInstance* otherinst, int argc, RValue* arg)
{
    assert(argc == 1);

    int64 mode = YYGetInt64(arg, 0);
    assert(mode >= 0 && mode < static_cast<int64>(EInputDelayMode::COUNT));

    auto newInputDelayMode = static_cast<EInputDelayMode>(mode);
    if (g_Session.m_InputDelayMode != newInputDelayMode)
    {
        g_Session.m_InputDelayMode = newInputDelayMode;
        g_Session.UpdateInputDelay();
        g_Session.RequestAutomaticInputDelayChange();
    }
}

YYEXPORT void pfo_get_input_delay_favored_client_index(RValue& result, CInstance* selfinst, CInstance* otherinst, int argc, RValue* arg)
{
    assert(argc == 0);

    init_real(result, g_Session.m_InputDelayFavoredClientIndex);
}

YYEXPORT void pfo_set_input_delay_favored_client_index(RValue& result, CInstance* selfinst, CInstance* otherinst, int argc, RValue* arg)
{
    assert(argc == 1);

    int clientIndex = YYGetInt32(arg, 0);
    assert(clientIndex >= -1 && clientIndex < countof(g_Session.m_ClientData));

    if (g_Session.m_AssignedClientsCount > 1 && g_Session.m_ClientData[clientIndex].m_PlayerData.m_PlayerIndex >= 0 && g_Session.m_InputDelayFavoredClientIndex != clientIndex)
    {
        g_Session.m_InputDelayFavoredClientIndex = clientIndex;
        g_Session.UpdateInputDelay();
    }
}

YYEXPORT void pfo_get_min_automatic_input_delay(RValue& result, CInstance* selfinst, CInstance* otherinst, int argc, RValue* arg)
{
    assert(argc == 0);

    init_real(result, g_Session.m_MinAutomaticInputDelay);
}

YYEXPORT void pfo_set_min_automatic_input_delay(RValue& result, CInstance* selfinst, CInstance* otherinst, int argc, RValue* arg)
{
    assert(argc == 1);

    int delay = YYGetInt32(arg, 0);
    delay = min(delay, c_MaxInputDelay);
    delay = max(delay, 0);

    if (g_Session.m_MinAutomaticInputDelay != delay)
    {
        g_Session.m_MinAutomaticInputDelay = delay;
        g_Session.UpdateInputDelay();
    }

    init_real(result, g_Session.m_MinAutomaticInputDelay);
}

YYEXPORT void pfo_get_max_automatic_input_delay(RValue& result, CInstance* selfinst, CInstance* otherinst, int argc, RValue* arg)
{
    assert(argc == 0);

    init_real(result, g_Session.m_MaxAutomaticInputDelay);
}

YYEXPORT void pfo_set_max_automatic_input_delay(RValue& result, CInstance* selfinst, CInstance* otherinst, int argc, RValue* arg)
{
    assert(argc == 1);

    int delay = YYGetInt32(arg, 0);
    delay = min(delay, c_MaxInputDelay);
    delay = max(delay, 0);

    if (g_Session.m_MaxAutomaticInputDelay != delay)
    {
        g_Session.m_MaxAutomaticInputDelay = delay;
        g_Session.UpdateInputDelay();
    }

    init_real(result, g_Session.m_MaxAutomaticInputDelay);
}

YYEXPORT void pfo_get_frame(RValue& result, CInstance* selfinst, CInstance* otherinst, int argc, RValue* arg)
{
    assert(argc == 0);

    init_real(result, g_Session.m_Frame);
}

YYEXPORT void pfo_init(RValue& result, CInstance* selfinst, CInstance* otherinst, int argc, RValue* arg)
{
    assert(argc == 0);

    constexpr auto get_script_callback = [](const char* optionName) -> int
        {
            auto name = extOptGetString(c_ExtensionName, optionName);
            if (name == nullptr || name[0] == '\0' || strcmp(name, "undefined") == 0)
            {
                LogError("Missing extension option '%s'", optionName);
            }

            int ret = Script_Find_Id(name);
            if (ret == -1)
            {
                LogError("Failed to find script callback '%s' for extension option '%s'", name, optionName);
            }
            return ret;
        };

    g_gml_Script_onlineStateChangedCallback = get_script_callback("onlineStateChangedCallback");
    g_gml_Script_getInputCallback = get_script_callback("getInputCallback");
    g_gml_Script_getChecksumCallback = get_script_callback("getChecksumCallback");

    init_bool(result, 1);
}

YYEXPORT void pfo_set_clients(RValue& result, CInstance* selfinst, CInstance* otherinst, int argc, RValue* arg)
{
    assert(argc == 1);

    CSteamID steamIDs[countof(g_Session.m_ClientData)];

    assert(KIND_RValue(&arg[0]) == VALUE_ARRAY);
    int count = YYArrayGetLength(&arg[0]);
    assert(count >= 0 && count <= countof(steamIDs));

    for (int i = 0; i < count; i++)
    {
        RValue val = {};
        GET_RValue(&val, &arg[0], nullptr, i);
        steamIDs[i] = CSteamID(static_cast<uint64>(YYGetInt64(&val, 0)));
    }

    bool success = g_Session.SetClients(steamIDs, count);
    return init_bool(result, success);
}

YYEXPORT void pfo_set_players(RValue& result, CInstance* selfinst, CInstance* otherinst, int argc, RValue* arg)
{
    assert(argc == 1);

    int map[countof(g_Session.m_PlayerIndexToClientIndexMap)];

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

    g_Session.SetPlayers(map);
}

YYEXPORT void pfo_get_client_index(RValue& result, CInstance* selfinst, CInstance* otherinst, int argc, RValue* arg)
{
    assert(argc == 0);

    init_real(result, g_Session.m_ClientIndex);
}

YYEXPORT void pfo_client_get_player_index(RValue& result, CInstance* selfinst, CInstance* otherinst, int argc, RValue* arg)
{
    assert(argc == 0 || argc == 1);

    int clientIndex = argc > 0 ? YYGetInt32(arg, 0) : g_Session.m_ClientIndex;

    if (clientIndex >= 0)
    {
        assert(clientIndex < countof(g_Session.m_ClientData));
        init_real(result, g_Session.m_ClientData[clientIndex].m_PlayerData.m_PlayerIndex);
    }
    else
    {
        init_real(result, -1);
    }
}

YYEXPORT void pfo_player_get_client_index(RValue& result, CInstance* selfinst, CInstance* otherinst, int argc, RValue* arg)
{
    assert(argc == 0 || argc == 1);

    int playerIndex = argc > 0 ? YYGetInt32(arg, 0) : -1;

    if (playerIndex >= 0)
    {
        assert(playerIndex < countof(g_Session.m_PlayerIndexToClientIndexMap));
        init_real(result, g_Session.m_PlayerIndexToClientIndexMap[playerIndex]);
    }
    else
    {
        init_real(result, g_Session.m_ClientIndex);
    }
}

YYEXPORT void pfo_get_client_count(RValue& result, CInstance* selfinst, CInstance* otherinst, int argc, RValue* arg)
{
    assert(argc == 0);

    init_real(result, g_Session.m_ClientCount);
}

YYEXPORT void pfo_get_assigned_clients_count(RValue& result, CInstance* selfinst, CInstance* otherinst, int argc, RValue* arg)
{
    assert(argc == 0);

    init_real(result, g_Session.m_AssignedClientsCount);
}

YYEXPORT void pfo_file_exists(RValue& result, CInstance* selfinst, CInstance* otherinst, int argc, RValue* arg)
{
    assert(argc > 0 && argc <= 2);

    int clientIndex = argc > 1 ? YYGetInt32(arg, 1) : 0;

    g_Session.FileExists(result, selfinst, arg, clientIndex);
}

YYEXPORT void pfo_buffer_load(RValue& result, CInstance* selfinst, CInstance* otherinst, int argc, RValue* arg)
{
    assert(argc == 1);

    g_Session.BufferLoad(result, selfinst, arg);
}

YYEXPORT void pfo_buffer_save(RValue& result, CInstance* selfinst, CInstance* otherinst, int argc, RValue* arg)
{
    assert(argc == 2);
    assert(KIND_RValue(&arg[0]) == VALUE_REF && GET_REF_TYPE(arg[0].v64) == REFID_BUFFER);

    g_Session.BufferSave(result, selfinst, arg);
}

YYEXPORT void pfo_file_delete(RValue& result, CInstance* selfinst, CInstance* otherinst, int argc, RValue* arg)
{
    assert(argc == 1);

    g_Session.FileDelete(result, selfinst, arg);
}

YYEXPORT void pfo_file_copy(RValue& result, CInstance* selfinst, CInstance* otherinst, int argc, RValue* arg)
{
    assert(argc == 2);

    g_Session.FileCopy(result, selfinst, arg);
}

YYEXPORT void pfo_file_status(RValue& result, CInstance* selfinst, CInstance* otherinst, int argc, RValue* arg)
{
    assert(argc == 1);

    g_Session.FileStatus(result, YYGetString(arg, 0));
}

YYEXPORT void pfo_quit(RValue& result, CInstance* selfinst, CInstance* otherinst, int argc, RValue* arg)
{
    assert(argc == 0);

    g_Session.Close();
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

YYEXPORT void pfo_steam_request_lobby_data(RValue& result, CInstance* selfinst, CInstance* otherinst, int argc, RValue* arg)
{
    assert(argc == 1);
    auto lobbyId = CSteamID(static_cast<uint64>(YYGetInt64(arg, 0)));

    init_bool(result, g_SteamMatchmaking->RequestLobbyData(lobbyId));
}

YYEXPORT void pfo_steam_get_lobby_data(RValue& result, CInstance* selfinst, CInstance* otherinst, int argc, RValue* arg)
{
    assert(argc == 2);
    auto lobbyId = CSteamID(static_cast<uint64>(YYGetInt64(arg, 0)));
    auto key = YYGetString(arg, 1);

    YYCreateString(&result, SteamMatchmaking()->GetLobbyData(lobbyId, key));
}

YYEXPORT void pfo_steam_get_num_lobby_members(RValue& result, CInstance* selfinst, CInstance* otherinst, int argc, RValue* arg)
{
    assert(argc == 1);
    auto lobbyId = CSteamID(static_cast<uint64>(YYGetInt64(arg, 0)));

    init_real(result, SteamMatchmaking()->GetNumLobbyMembers(lobbyId));
}

YYEXPORT void pfo_steam_get_lobby_member_limit(RValue& result, CInstance* selfinst, CInstance* otherinst, int argc, RValue* arg)
{
    assert(argc == 1);
    auto lobbyId = CSteamID(static_cast<uint64>(YYGetInt64(arg, 0)));

    init_real(result, SteamMatchmaking()->GetLobbyMemberLimit(lobbyId));
}

YYEXPORT void pfo_connect(RValue& result, CInstance* selfinst, CInstance* otherinst, int argc, RValue* arg)
{
    assert(argc == 0);

    g_Session.Connect();
}

YYEXPORT void pfo_client_get_ping(RValue& result, CInstance* selfinst, CInstance* otherinst, int argc, RValue* arg)
{
    assert(argc == 0);

    int maxPing = -2;
    int maxAssignedPing = -2;
    SteamNetConnectionRealTimeStatus_t status;

    for (int i = 0; i < g_Session.m_ClientCount; i++)
    {
        auto& clientData = g_Session.m_ClientData[i];
        if (clientData.m_ConnectSocket != k_HSteamNetConnection_Invalid)
        {
            if (g_SteamNetworkingSockets->GetConnectionRealTimeStatus(clientData.m_ConnectSocket, &status, 0, nullptr) == k_EResultOK)
            {
                maxPing = max(maxPing, status.m_nPing);
                if (clientData.m_PlayerData.m_PlayerIndex >= 0)
                {
                    maxAssignedPing = max(maxAssignedPing, status.m_nPing);
                }
            }
        }
    }

    init_real(result, maxAssignedPing > -2 ? maxAssignedPing : maxPing);
}

YYEXPORT void pfo_reset(RValue& result, CInstance* selfinst, CInstance* otherinst, int argc, RValue* arg)
{
    assert(argc == 0);

    g_Session.Reset(selfinst);
}

YYEXPORT void pfo_game_get_speed(RValue& result, CInstance* selfinst, CInstance* otherinst, int argc, RValue* arg)
{
    assert(argc == 1);

    double type = YYGetReal(arg, 0);

    if (type == 0.0)
    {
        init_real(result, g_BaseGameSpeedFPS);
    }
    else
    {
        init_real(result, 1'000'000.0 / g_BaseGameSpeedFPS);
    }
}

YYEXPORT void pfo_game_set_speed(RValue& result, CInstance* selfinst, CInstance* otherinst, int argc, RValue* arg)
{
    assert(argc == 2);

    double speed = YYGetReal(arg, 0);
    double type = YYGetReal(arg, 1);

    bool changedSpeed = false;

    if (type != 0.0)
    {
        speed = 1'000'000.0 / speed;
    }

    g_Session.SetSpeed(speed, selfinst);
}

YYEXPORT void pfo_client_is_connected(RValue& result, CInstance* selfinst, CInstance* otherinst, int argc, RValue* arg)
{
    assert(argc == 1);

    int clientIndex = YYGetInt32(arg, 0);
    assert(clientIndex >= 0 && clientIndex < countof(g_Session.m_ClientData));

    init_real(result, g_Session.m_ClientData[clientIndex].m_ConnectSocket != k_HSteamNetConnection_Invalid || clientIndex == g_Session.m_ClientIndex);
}

YYEXPORT void pfo_show_debug_message(RValue& result, CInstance* selfinst, CInstance* otherinst, int argc, RValue* arg)
{
    assert(argc == 1);

    auto message = YYGetString(arg, 0);
    log("%s\n", message);
}
