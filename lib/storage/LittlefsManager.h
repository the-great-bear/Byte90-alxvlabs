/**
 * LittlefsManager.h
 *
 * Declarations for LittlefsManager.
 */

#pragma once

// System includes
#include <Arduino.h>
#include <FS.h>
#include <LittleFS.h>

/**
 * Filesystem status codes
 *
 * Status codes for LittleFS operations (inspired by FSStatus pattern)
 */
enum class FSStatus {
    FS_SUCCESS,          // Operation successful
    FS_NOT_MOUNTED,      // Filesystem not mounted
    FS_MOUNT_FAILED,     // Mount operation failed
    FS_FORMAT_FAILED,    // Format operation failed
    FS_FILE_MISSING      // Required file missing
};

/**
 * Storage statistics structure
 */
struct StorageStats {
    size_t totalBytes;    // Total storage capacity
    size_t usedBytes;     // Used storage space
    size_t freeBytes;     // Free storage space
    float usedPercent;    // Percentage used
};

/**
 * LittleFSManager - Centralized filesystem management
 *
 * Features:
 * - LittleFS initialization and mounting
 * - Status tracking and validation
 * - Error handling and recovery
 * - Storage statistics
 *
 * Architecture:
 * - Provides interface for LittleFS operations
 * - Allows multiple components to share filesystem access
 * - Prevents duplicate initialization logic
 * - Uses simple instantiation pattern for clarity
 */
class LittleFSManager {
public:
    /**
     * @brief Construct LittleFS manager instance
     */
    LittleFSManager();

    /**
     * @brief Destroy LittleFS manager and unmount filesystem
     */
    ~LittleFSManager();

    // Initialization

    /**
     * @brief Initialize and mount LittleFS
     *
     * @return FSStatus indicating success or failure reason
     */
    FSStatus begin();

    /**
     * @brief Unmount filesystem and cleanup
     */
    void end();

    // Status queries

    /**
     * @brief Check if filesystem is mounted
     *
     * @return true if mounted, false otherwise
     */
    bool isMounted() const { return _mounted; }

    /**
     * @brief Check if filesystem manager is ready
     *
     * @return true if mounted and ready, false otherwise
     */
    bool isReady() const { return _mounted; }

    // File operations (convenience wrappers)

    /**
     * @brief Check if file exists
     *
     * @param path File path to check
     * @return true if file exists, false otherwise
     */
    bool exists(const char* path);

    /**
     * @brief Open file for reading or writing
     *
     * @param path File path to open
     * @param mode File mode ("r" for read, "w" for write, "a" for append)
     * @return File object
     */
    File open(const char* path, const char* mode = "r");

private:
    // State
    bool _mounted;
};
