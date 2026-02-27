/**
 * @file test_common.h
 * @brief Common utilities and helpers for all test modules
 */

#ifndef TEST_COMMON_H
#define TEST_COMMON_H

#include <unity.h>
#include <Arduino.h>

//==============================================================================
// COMMON TEST CONFIGURATION
//==============================================================================

// Enable/disable entire test suites
#define RUN_BASIC_SAFETY_TESTS
// Add module flags as needed.

//==============================================================================
// COMMON UTILITY FUNCTIONS
//==============================================================================

void printTestSectionHeader(const char* sectionName);
void printTestResult(const char* testName, bool passed, const char* details = nullptr);
void test_esp32_hardware(void);
void test_memory_before_tests(void);
void test_memory_after_tests(void);
int run_basic_safety_tests(void);

//==============================================================================
// MEMORY TRACKING UTILITIES
//==============================================================================

uint32_t getHeapWithLogging(const char* context);
bool checkMemoryLeak(uint32_t before, uint32_t after, const char* context, uint32_t maxLeak = 1000);

//==============================================================================
// TEST TIMING UTILITIES
//==============================================================================

unsigned long getTestUptime(void);
void logTestTiming(const char* testName, unsigned long startTime, unsigned long endTime);

//==============================================================================
// SAFE DELAY UTILITIES
//==============================================================================

void safeTestDelay(unsigned long ms);
void briefStabilityDelay(void);
void nvsOperationDelay(void);

#endif /* TEST_COMMON_H */
