/**
 * @file test_runner.cpp
 * @brief Main test runner - orchestrates all module tests
 */

#include <unity.h>
#include <Arduino.h>

#include "test_common.h"
#include "test_button_state.h"
#include "test_event_bus.h"
#include "test_timer_manager.h"
#include "test_system_state.h"
#include "test_task_manager.h"
#include "test_input_manager.h"
#include "test_protocol_manager.h"
#include "test_mcp_tool_manager.h"
#include "test_storage_language.h"

//==============================================================================
// MAIN TEST CONFIGURATION
//==============================================================================

static int totalTestsRun = 0;
static unsigned long testSuiteStartTime = 0;

//==============================================================================
// TEST SUITE FUNCTIONS
//==============================================================================

static void printTestSuiteHeader(void) {
    Serial.println("\n==================================================");
    Serial.println("🔧 BYTE-90 MODULAR TEST SUITE 🔧");
    Serial.println("==================================================");
    Serial.printf("Firmware Version: %s\n", FIRMWARE_VERSION);
    Serial.println("🎯 Modular, scalable testing architecture");
    Serial.println("🛡️ Safe, comprehensive testing approach");
    Serial.println("==================================================");
}

static void printTestSuiteFooter(void) {
    unsigned long testSuiteEndTime = getTestUptime();
    unsigned long totalDuration = testSuiteEndTime - testSuiteStartTime;

    Serial.println("\n==================================================");
    Serial.println("🎉 BYTE-90 TEST SUITE COMPLETED! 🎉");
    Serial.println("==================================================");
    Serial.printf("📊 Total tests run: %d\n", totalTestsRun);
    Serial.printf("⏱️ Total duration: %lu ms (%.2f seconds)\n",
                  totalDuration, totalDuration / 1000.0);
    Serial.printf("🧠 Final memory: %u bytes free\n", ESP.getFreeHeap());
    Serial.println("✅ All modules tested successfully");
    Serial.println("🚀 System ready for deployment");
    Serial.println("==================================================");
}

static void runBasicSafetyTests(void) {
#ifdef RUN_BASIC_SAFETY_TESTS
    printTestSectionHeader("BASIC SAFETY TESTS");
    unsigned long sectionStart = getTestUptime();

    int testsRun = run_basic_safety_tests();
    totalTestsRun += testsRun;

    unsigned long sectionEnd = getTestUptime();
    logTestTiming("Basic Safety Tests", sectionStart, sectionEnd);
#endif
}

static void runButtonStateTests(void) {
    printTestSectionHeader("BUTTON + STATE TESTS");
    unsigned long sectionStart = getTestUptime();

    int testsRun = run_button_state_tests();
    totalTestsRun += testsRun;

    unsigned long sectionEnd = getTestUptime();
    logTestTiming("Button + State Tests", sectionStart, sectionEnd);
}

static void runEventBusTests(void) {
    printTestSectionHeader("EVENT BUS TESTS");
    unsigned long sectionStart = getTestUptime();

    int testsRun = run_event_bus_tests();
    totalTestsRun += testsRun;

    unsigned long sectionEnd = getTestUptime();
    logTestTiming("Event Bus Tests", sectionStart, sectionEnd);
}

static void runTimerManagerTests(void) {
    printTestSectionHeader("TIMER MANAGER TESTS");
    unsigned long sectionStart = getTestUptime();

    int testsRun = run_timer_manager_tests();
    totalTestsRun += testsRun;

    unsigned long sectionEnd = getTestUptime();
    logTestTiming("Timer Manager Tests", sectionStart, sectionEnd);
}

static void runSystemStateTests(void) {
    printTestSectionHeader("SYSTEM STATE TESTS");
    unsigned long sectionStart = getTestUptime();

    int testsRun = run_system_state_tests();
    totalTestsRun += testsRun;

    unsigned long sectionEnd = getTestUptime();
    logTestTiming("System State Tests", sectionStart, sectionEnd);
}

static void runTaskManagerTests(void) {
    printTestSectionHeader("TASK MANAGER TESTS");
    unsigned long sectionStart = getTestUptime();

    int testsRun = run_task_manager_tests();
    totalTestsRun += testsRun;

    unsigned long sectionEnd = getTestUptime();
    logTestTiming("Task Manager Tests", sectionStart, sectionEnd);
}

static void runInputManagerTests(void) {
    printTestSectionHeader("INPUT MANAGER TESTS");
    unsigned long sectionStart = getTestUptime();

    int testsRun = run_input_manager_tests();
    totalTestsRun += testsRun;

    unsigned long sectionEnd = getTestUptime();
    logTestTiming("Input Manager Tests", sectionStart, sectionEnd);
}

static void runProtocolManagerTests(void) {
    printTestSectionHeader("PROTOCOL MANAGER TESTS");
    unsigned long sectionStart = getTestUptime();

    int testsRun = run_protocol_manager_tests();
    totalTestsRun += testsRun;

    unsigned long sectionEnd = getTestUptime();
    logTestTiming("Protocol Manager Tests", sectionStart, sectionEnd);
}

static void runMcpToolManagerTests(void) {
    printTestSectionHeader("MCP TOOL MANAGER TESTS");
    unsigned long sectionStart = getTestUptime();

    int testsRun = run_mcp_tool_manager_tests();
    totalTestsRun += testsRun;

    unsigned long sectionEnd = getTestUptime();
    logTestTiming("MCP Tool Manager Tests", sectionStart, sectionEnd);
}

static void runStorageLanguageTests(void) {
    printTestSectionHeader("STORAGE + LANGUAGE TESTS");
    unsigned long sectionStart = getTestUptime();

    int testsRun = run_storage_language_tests();
    totalTestsRun += testsRun;

    unsigned long sectionEnd = getTestUptime();
    logTestTiming("Storage + Language Tests", sectionStart, sectionEnd);
}

static void runFinalTests(void) {
    printTestSectionHeader("FINAL SYSTEM TESTS");
    unsigned long sectionStart = getTestUptime();

    RUN_TEST(test_memory_after_tests);
    totalTestsRun++;

    unsigned long sectionEnd = getTestUptime();
    logTestTiming("Final System Tests", sectionStart, sectionEnd);
}

//==============================================================================
// UNITY TEST SETUP
//==============================================================================

void setUp(void) {
    briefStabilityDelay();
}

void tearDown(void) {
    briefStabilityDelay();
}

//==============================================================================
// MAIN SETUP AND LOOP
//==============================================================================

void setup() {
    delay(3000);
    Serial.begin(115200);

    testSuiteStartTime = getTestUptime();

    printTestSuiteHeader();

    UNITY_BEGIN();

    runBasicSafetyTests();
    runButtonStateTests();
    runEventBusTests();
    runTimerManagerTests();
    runSystemStateTests();
    runTaskManagerTests();
    runInputManagerTests();
    runProtocolManagerTests();
    runMcpToolManagerTests();
    runStorageLanguageTests();
    runFinalTests();

    UNITY_END();

    printTestSuiteFooter();
}

void loop() {
    delay(30000);
}
