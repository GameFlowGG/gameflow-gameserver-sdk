#pragma once

#include "CoreMinimal.h"

/**
 * Streaming decoder for an HTTP/1.1 chunked-transfer body that carries NDJSON.
 *
 * The GameFlow sidecar serves GET /watch/gameserver by writing JSON lines with
 * no Content-Length, so Node emits them as `Transfer-Encoding: chunked`: the
 * wire bytes are `<hex-size>\r\n<data>\r\n<hex-size>\r\n<data>...` ending in a
 * `0\r\n\r\n` terminator. Inside the de-chunked payload, frames are NDJSON —
 * complete JSON objects separated by '\n'.
 *
 * Feed() is fed raw post-header body bytes as they arrive off the socket; the
 * boundaries are arbitrary (a chunk-size line, a chunk body, or a JSON line may
 * each be split across calls). The decoder is a state machine that buffers
 * across calls and appends every complete '\n'-terminated line (decoded UTF-8)
 * to the caller's array. Pure: no socket, no engine ticking — unit-testable.
 */
class FGfChunkedNdjson
{
public:
    /**
     * Feed Len raw chunked-body bytes; append every newly-completed NDJSON line
     * (without its trailing '\n', decoded UTF-8 -> FString) to OutLines.
     */
    void Feed(const uint8* Data, int32 Len, TArray<FString>& OutLines);

private:
    enum class EState : uint8
    {
        ChunkSize,   // reading the hex chunk-size line up to "\r\n"
        ChunkData,   // reading exactly Remaining body bytes of the current chunk
        ChunkCrlf,   // consuming the "\r\n" that trails each chunk's data
        Done,        // saw the 0-sized chunk; ignore any trailing bytes
    };

    EState State = EState::ChunkSize;

    /** Accumulates the hex size line while State == ChunkSize. */
    FString SizeLine;

    /** Body bytes still owed for the current chunk while State == ChunkData. */
    int32 Remaining = 0;

    /** Index into the 2-byte "\r\n" trailer while State == ChunkCrlf. */
    int32 CrlfSeen = 0;

    /** De-chunked UTF-8 bytes not yet terminated by a '\n'. */
    TArray<uint8> LineBuf;

    /** Move the buffered UTF-8 bytes up to (not including) the '\n' at NlIndex. */
    void EmitLine(int32 NlIndex, TArray<FString>& OutLines);

    /** Append one de-chunked body byte, emitting a line on '\n'. */
    void PushBodyByte(uint8 Byte, TArray<FString>& OutLines);
};
