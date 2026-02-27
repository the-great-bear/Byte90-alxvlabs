#include <unity.h>
#include <cstring>

#include "LanguageManager.h"
#include "LittlefsManager.h"
#include "NvsStorage.h"
#include "../test_storage_language.h"

static void test_nvs_wifi_credentials_roundtrip() {
    NVSStorage storage;
    if (!storage.begin()) {
        TEST_IGNORE_MESSAGE("NVS not available in this test context");
    }

    TEST_ASSERT_TRUE(storage.saveWiFiCredentials("unit_test_ssid", "unit_test_password"));

    wifi_credentials_t credentials{};
    TEST_ASSERT_TRUE(storage.loadWiFiCredentials(&credentials));
    TEST_ASSERT_EQUAL_STRING("unit_test_ssid", credentials.ssid);
    TEST_ASSERT_EQUAL_STRING("unit_test_password", credentials.password);

    TEST_ASSERT_TRUE(storage.clearWiFiCredentials());
}

static void test_nvs_system_settings_roundtrip() {
    NVSStorage storage;
    if (!storage.begin()) {
        TEST_IGNORE_MESSAGE("NVS not available in this test context");
    }

    system_settings_t expected{};
    strncpy(expected.language, "en-US", sizeof(expected.language) - 1);
    expected.wifi_enabled = true;
    expected.brightness = 77;
    expected.effects_scanlines_enabled = true;
    expected.effects_glitch_enabled = false;
    expected.effects_dot_matrix_enabled = true;
    expected.effects_tint_enabled = true;
    expected.effects_tint_color = 0xF800;

    TEST_ASSERT_TRUE(storage.saveSystemSettings(&expected));

    system_settings_t actual{};
    TEST_ASSERT_TRUE(storage.loadSystemSettings(&actual));
    TEST_ASSERT_EQUAL_STRING(expected.language, actual.language);
    TEST_ASSERT_EQUAL_UINT8(expected.brightness, actual.brightness);
    TEST_ASSERT_EQUAL(expected.effects_scanlines_enabled, actual.effects_scanlines_enabled);
    TEST_ASSERT_EQUAL(expected.effects_dot_matrix_enabled, actual.effects_dot_matrix_enabled);
    TEST_ASSERT_EQUAL(expected.effects_tint_enabled, actual.effects_tint_enabled);
    TEST_ASSERT_EQUAL_HEX16(expected.effects_tint_color, actual.effects_tint_color);
}

static void test_language_manager_boot_and_fallback_lookup() {
    LittleFSManager filesystem;
    FSStatus fs_status = filesystem.begin();
    if (fs_status != FSStatus::FS_SUCCESS) {
        TEST_IGNORE_MESSAGE("LittleFS assets partition not available for language tests");
    }

    LanguageManager language_manager(&filesystem);
    LangStatus status = language_manager.begin();
    if (status != LangStatus::LANG_SUCCESS) {
        filesystem.end();
        TEST_IGNORE_MESSAGE("Language assets missing; upload filesystem to run this test");
    }

    const char* missing_key = "__unit_test_missing_key__";
    const char* value = language_manager.getString(missing_key);
    TEST_ASSERT_EQUAL_STRING(missing_key, value);

    TEST_ASSERT_FALSE(language_manager.setLanguage("invalid-language"));

    filesystem.end();
}

int run_storage_language_tests(void) {
    int test_count = 0;

    RUN_TEST(test_nvs_wifi_credentials_roundtrip);
    test_count++;
    RUN_TEST(test_nvs_system_settings_roundtrip);
    test_count++;
    RUN_TEST(test_language_manager_boot_and_fallback_lookup);
    test_count++;

    return test_count;
}
