#include "GameFlow/GfChunkedNdjson.h"

// Parse a hex chunk-size string (e.g. "1a") into an int32. Stops at the first
// non-hex character (handles a trailing ';' chunk-extension); negative on none.
static int32 ParseHexSize(const FString& Hex)
{
    int32 Value = 0;
    bool bAny = false;
    for (const TCHAR C : Hex)
    {
        const TCHAR L = FChar::ToLower(C);
        int32 Digit;
        if (C >= '0' && C <= '9')        { Digit = C - '0'; }
        else if (L >= 'a' && L <= 'f')   { Digit = 10 + (L - 'a'); }
        else                             { break; }
        Value = (Value << 4) + Digit;
        bAny = true;
    }
    return bAny ? Value : -1;
}

void FGfChunkedNdjson::EmitLine(int32 NlIndex, TArray<FString>& OutLines)
{
    // Decode LineBuf[0..NlIndex) as UTF-8 (the '\n' itself is dropped), then
    // shift the remainder down so the next line starts at index 0.
    if (NlIndex > 0)
    {
        // Strip a trailing '\r' (the sidecar uses bare '\n', but tolerate CRLF).
        int32 ByteLen = NlIndex;
        if (LineBuf[ByteLen - 1] == '\r')
        {
            --ByteLen;
        }
        const FUTF8ToTCHAR Conv(reinterpret_cast<const ANSICHAR*>(LineBuf.GetData()), ByteLen);
        // Construct from an explicit (length, ptr) — the (ptr, int) overload
        // treats the int as slack, not length, and Conv.Get() is not NUL-terminated.
        OutLines.Emplace(FString::ConstructFromPtrSize(Conv.Get(), Conv.Length()));
    }
    else
    {
        OutLines.Emplace(); // empty line
    }
    LineBuf.RemoveAt(0, NlIndex + 1, EAllowShrinking::No);
}

void FGfChunkedNdjson::PushBodyByte(uint8 Byte, TArray<FString>& OutLines)
{
    LineBuf.Add(Byte);
    if (Byte == '\n')
    {
        EmitLine(LineBuf.Num() - 1, OutLines);
    }
}

void FGfChunkedNdjson::Feed(const uint8* Data, int32 Len, TArray<FString>& OutLines)
{
    for (int32 i = 0; i < Len; ++i)
    {
        const uint8 Byte = Data[i];

        switch (State)
        {
        case EState::ChunkSize:
        {
            // Accumulate the hex size line until the terminating "\r\n". We
            // ignore '\r' and treat '\n' as the line terminator; any chunk
            // extensions (";name=val") would sit before the ';' — the sidecar
            // never sends them, so a plain hex parse suffices.
            if (Byte == '\n')
            {
                const int32 Size = ParseHexSize(SizeLine.TrimStartAndEnd());
                SizeLine.Reset();
                if (Size <= 0)
                {
                    State = EState::Done; // 0-sized chunk terminates the body
                }
                else
                {
                    Remaining = Size;
                    State = EState::ChunkData;
                }
            }
            else if (Byte != '\r')
            {
                SizeLine.AppendChar(static_cast<TCHAR>(Byte));
            }
            break;
        }

        case EState::ChunkData:
        {
            PushBodyByte(Byte, OutLines);
            if (--Remaining == 0)
            {
                CrlfSeen = 0;
                State = EState::ChunkCrlf;
            }
            break;
        }

        case EState::ChunkCrlf:
        {
            // Swallow the "\r\n" that trails each chunk's data, then loop back
            // for the next chunk-size line. Tolerate a lone '\n'.
            if (++CrlfSeen >= 2 || Byte == '\n')
            {
                State = EState::ChunkSize;
            }
            break;
        }

        case EState::Done:
        default:
            return; // body finished; ignore the trailing "0\r\n\r\n" bytes
        }
    }
}
