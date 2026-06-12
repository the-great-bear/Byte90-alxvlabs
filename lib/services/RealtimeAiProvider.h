/**
 * RealtimeAiProvider.h
 *
 * Compile-time selection of the realtime AI voice provider.
 *
 * BYTE-90 ships one provider per firmware build (mirroring how the project
 * ships separate b90-openai / b90-xiaozhi images). Both provider clients
 * (OpenAIWebsocket and GeminiWebsocket) expose an identical public interface,
 * so the rest of the firmware refers to whichever is active through the
 * RealtimeAiClient alias.
 *
 *   Default build        -> OpenAIWebsocket (OpenAI Realtime)
 *   -DAI_PROVIDER_GEMINI  -> GeminiWebsocket (Gemini Live native audio)
 */

#pragma once

#if defined(AI_PROVIDER_GEMINI)
#include "GeminiWebsocket.h"
using RealtimeAiClient = GeminiWebsocket;
#else
#include "OpenAIWebsocket.h"
using RealtimeAiClient = OpenAIWebsocket;
#endif
