/**
 * McpToolRegistry.h
 *
 * Declarations for McpToolRegistry.
 */

#pragma once

// Project includes
#include "WebsocketClient.h"

// Forward declarations
/**
 * @brief AudioCodec.
 */
class AudioCodec;
/**
 * @brief AudioService.
 */
class AudioService;
/**
 * @brief AXP2101.
 */
class AXP2101;
/**
 * @brief McpServer.
 */
class McpServer;

/**
 * McpToolRegistry - MCP tool registration
 *
 * Features:
 * - Centralized tool registration
 * - Audio control tools
 * - Display control tools
 * - Device status tools
 * - Network status tools
 *
 * Registers all MCP (Model Context Protocol) tools that allow AI assistants
 * to interact with device hardware and state.
 *
 * Tool Categories:
 * - Audio tools: Volume control, gain settings
 * - Display tools: Brightness control
 * - Status tools: Device status (audio, display, battery, network, websocket)
 */
class McpToolRegistry {
public:
    /**
     * @brief Register all MCP tools with the server
     *
     * @param mcp Pointer to McpServer instance for tool registration
     */
    static void buildDeviceToolRegistry(McpServer* mcp);

private:
    /**
     * @brief Register audio control tools
     *
     * @param mcp Pointer to McpServer instance
     */
    static void registerAudioTools(McpServer* mcp);

    /**
     * @brief Register display control tools
     *
     * @param mcp Pointer to McpServer instance
     */
    static void registerDisplayTools(McpServer* mcp);
    static void registerEffectsTools(McpServer* mcp);

    /**
     * @brief Register device status tools
     *
     * @param mcp Pointer to McpServer instance
     */
    static void registerStatusTools(McpServer* mcp);

    /**
     * @brief Register timer tools
     *
     * @param mcp Pointer to McpServer instance
     */
    static void registerTimerTools(McpServer* mcp);

    /**
     * @brief Register user-only tools
     *
     * @param mcp Pointer to McpServer instance
     */
};
