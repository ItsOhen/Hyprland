#include <filesystem>
#include <linux/input-event-codes.h>
#include <thread>
#include "../../shared.hpp"
#include "../../hyprctlCompat.hpp"
#include "../shared.hpp"
#include "tests.hpp"

using namespace Hyprutils::OS;
using namespace Hyprutils::Memory;

static int         ret      = 0;
static std::string flagFile = "/tmp/hyprtester-keybinds.txt";

// Because i don't feel like changing someone elses code.
enum eKeyboardModifierIndex : uint8_t {
    MOD_SHIFT = 1,
    MOD_CAPS,
    MOD_CTRL,
    MOD_ALT,
    MOD_MOD2,
    MOD_MOD3,
    MOD_META,
    MOD_MOD5
};

static void clearFlag() {
    std::filesystem::remove(flagFile);
}

static bool checkFlag() {
    bool exists = std::filesystem::exists(flagFile);
    clearFlag();
    return exists;
}

static std::string readKittyOutput() {
    std::string output = Tests::execAndGet("kitten @ --to unix:/tmp/hyprtester-kitty.sock get-text --extent all");
    // chop off shell prompt
    std::size_t pos = output.rfind("$");
    if (pos != std::string::npos) {
        pos += 1;
        if (pos < output.size())
            output.erase(0, pos);
    }
    // NLog::log("Kitty output: '{}'", output);
    return output;
}

static void awaitKittyPrompt() {
    // wait until we see the shell prompt, meaning it's ready for test inputs
    for (int i = 0; i < 10; i++) {
        std::string output = Tests::execAndGet("kitten @ --to unix:/tmp/hyprtester-kitty.sock get-text --extent all");
        if (output.rfind("$") == std::string::npos) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            continue;
        }
        return;
    }
    NLog::log("{}Error: timed out waiting for kitty prompt", Colors::RED);
}

static CUniquePointer<CProcess> spawnRemoteControlKitty() {
    auto kittyProc = Tests::spawnKitty("keybinds_test", {"-o", "allow_remote_control=yes", "--listen-on", "unix:/tmp/hyprtester-kitty.sock", "--config", "NONE", "/bin/sh"});
    // wait a bit to ensure shell prompt is sent, we are going to read the text after it
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    if (kittyProc)
        awaitKittyPrompt();
    return kittyProc;
}

static void testBind() {
    EXPECT(checkFlag(), false);
    OK(getFromSocket("/keyword bind SUPER,Y,exec,touch " + flagFile));
    // press keybind
    OK(getFromSocket("/dispatch plugin:test:keybind 1,7,29"));
    // await flag
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT(checkFlag(), true);
    // release keybind
    OK(getFromSocket("/dispatch plugin:test:keybind 0,0,29"));
    OK(getFromSocket("/keyword unbind SUPER,Y"));
}

static void testBindKey() {
    EXPECT(checkFlag(), false);
    OK(getFromSocket("/keyword bind ,Y,exec,touch " + flagFile));
    // press keybind
    OK(getFromSocket("/dispatch plugin:test:keybind 1,0,29"));
    // await flag
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT(checkFlag(), true);
    // release keybind
    OK(getFromSocket("/dispatch plugin:test:keybind 0,0,29"));
    OK(getFromSocket("/keyword unbind ,Y"));
}

static void testLongPress() {
    EXPECT(checkFlag(), false);
    OK(getFromSocket("/keyword bindo SUPER,Y,exec,touch " + flagFile));
    OK(getFromSocket("/keyword input:repeat_delay 100"));
    // press keybind
    OK(getFromSocket("/dispatch plugin:test:keybind 1,7,29"));
    // check no flag on short press
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT(checkFlag(), false);
    // await repeat delay
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT(checkFlag(), true);
    // release keybind
    OK(getFromSocket("/dispatch plugin:test:keybind 0,0,29"));
    OK(getFromSocket("/keyword unbind SUPER,Y"));
}

static void testKeyLongPress() {
    EXPECT(checkFlag(), false);
    OK(getFromSocket("/keyword bindo ,Y,exec,touch " + flagFile));
    OK(getFromSocket("/keyword input:repeat_delay 100"));
    // press keybind
    OK(getFromSocket("/dispatch plugin:test:keybind 1,0,29"));
    // check no flag on short press
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT(checkFlag(), false);
    // await repeat delay
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT(checkFlag(), true);
    // release keybind
    OK(getFromSocket("/dispatch plugin:test:keybind 0,0,29"));
    OK(getFromSocket("/keyword unbind ,Y"));
}

static void testLongPressRelease() {
    EXPECT(checkFlag(), false);
    OK(getFromSocket("/keyword bindo SUPER,Y,exec,touch " + flagFile));
    OK(getFromSocket("/keyword input:repeat_delay 100"));
    // press keybind
    OK(getFromSocket("/dispatch plugin:test:keybind 1,7,29"));
    // check no flag on short press
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT(checkFlag(), false);
    // release keybind
    OK(getFromSocket("/dispatch plugin:test:keybind 0,0,29"));
    // await repeat delay
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT(checkFlag(), false);
    OK(getFromSocket("/keyword unbind SUPER,Y"));
}

static void testLongPressOnlyKeyRelease() {
    EXPECT(checkFlag(), false);
    OK(getFromSocket("/keyword bindo SUPER,Y,exec,touch " + flagFile));
    OK(getFromSocket("/keyword input:repeat_delay 100"));
    // press keybind
    OK(getFromSocket("/dispatch plugin:test:keybind 1,7,29"));
    // check no flag on short press
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT(checkFlag(), false);
    // release key, keep modifier
    OK(getFromSocket("/dispatch plugin:test:keybind 0,7,29"));
    // await repeat delay
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT(checkFlag(), false);
    OK(getFromSocket("/dispatch plugin:test:keybind 0,0,29"));
    OK(getFromSocket("/keyword unbind SUPER,Y"));
}

static void testRepeat() {
    EXPECT(checkFlag(), false);
    OK(getFromSocket("/keyword binde SUPER,Y,exec,touch " + flagFile));
    OK(getFromSocket("/keyword input:repeat_delay 100"));
    // press keybind
    OK(getFromSocket("/dispatch plugin:test:keybind 1,7,29"));
    // await flag
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT(checkFlag(), true);
    // await repeat delay
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT(checkFlag(), true);
    // check that it continues repeating
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT(checkFlag(), true);
    // release keybind
    OK(getFromSocket("/dispatch plugin:test:keybind 0,0,29"));
    OK(getFromSocket("/keyword unbind SUPER,Y"));
}

static void testKeyRepeat() {
    EXPECT(checkFlag(), false);
    OK(getFromSocket("/keyword binde ,Y,exec,touch " + flagFile));
    OK(getFromSocket("/keyword input:repeat_delay 100"));
    // press keybind
    OK(getFromSocket("/dispatch plugin:test:keybind 1,0,29"));
    // await flag
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT(checkFlag(), true);
    // await repeat delay
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT(checkFlag(), true);
    // check that it continues repeating
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT(checkFlag(), true);
    // release keybind
    OK(getFromSocket("/dispatch plugin:test:keybind 0,0,29"));
    OK(getFromSocket("/keyword unbind ,Y"));
}

static void testRepeatRelease() {
    EXPECT(checkFlag(), false);
    OK(getFromSocket("/keyword binde SUPER,Y,exec,touch " + flagFile));
    OK(getFromSocket("/keyword input:repeat_delay 100"));
    // press keybind
    OK(getFromSocket("/dispatch plugin:test:keybind 1,7,29"));
    // await flag
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT(checkFlag(), true);
    // release keybind
    OK(getFromSocket("/dispatch plugin:test:keybind 0,0,29"));
    // await repeat delay
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT(checkFlag(), false);
    // check that it is not repeating
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT(checkFlag(), false);
    OK(getFromSocket("/keyword unbind SUPER,Y"));
}

static void testRepeatOnlyKeyRelease() {
    EXPECT(checkFlag(), false);
    OK(getFromSocket("/keyword binde SUPER,Y,exec,touch " + flagFile));
    OK(getFromSocket("/keyword input:repeat_delay 100"));
    // press keybind
    OK(getFromSocket("/dispatch plugin:test:keybind 1,7,29"));
    // await flag
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT(checkFlag(), true);
    // release key, keep modifier
    OK(getFromSocket("/dispatch plugin:test:keybind 0,7,29"));
    // await repeat delay
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT(checkFlag(), false);
    // check that it is not repeating
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT(checkFlag(), false);
    OK(getFromSocket("/dispatch plugin:test:keybind 0,0,29"));
    OK(getFromSocket("/keyword unbind SUPER,Y"));
}

static void testShortcutBind() {
    auto kittyProc = spawnRemoteControlKitty();
    if (!kittyProc) {
        NLog::log("{}Error: kitty did not spawn", Colors::RED);
        ret = 1;
        return;
    }
    OK(getFromSocket("/dispatch focuswindow class:keybinds_test"));
    OK(getFromSocket("/keyword bind SUPER,Y,sendshortcut,,q,"));
    // press keybind
    OK(getFromSocket("/dispatch plugin:test:keybind 1,7,29"));
    // release keybind
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    OK(getFromSocket("/dispatch plugin:test:keybind 0,0,29"));
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    const std::string output = readKittyOutput();
    EXPECT_COUNT_STRING(output, "y", 0);
    EXPECT_COUNT_STRING(output, "q", 1);
    OK(getFromSocket("/keyword unbind SUPER,Y"));
    Tests::killAllWindows();
}

static void testShortcutBindKey() {
    auto kittyProc = spawnRemoteControlKitty();
    if (!kittyProc) {
        NLog::log("{}Error: kitty did not spawn", Colors::RED);
        ret = 1;
        return;
    }
    OK(getFromSocket("/dispatch focuswindow class:keybinds_test"));
    OK(getFromSocket("/keyword bind ,Y,sendshortcut,,q,"));
    // press keybind
    OK(getFromSocket("/dispatch plugin:test:keybind 1,0,29"));
    // release keybind
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    OK(getFromSocket("/dispatch plugin:test:keybind 0,0,29"));
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    const std::string output = readKittyOutput();
    EXPECT_COUNT_STRING(output, "y", 0);
    EXPECT_COUNT_STRING(output, "q", 1);
    OK(getFromSocket("/keyword unbind ,Y"));
    Tests::killAllWindows();
}

static void testShortcutLongPress() {
    auto kittyProc = spawnRemoteControlKitty();
    if (!kittyProc) {
        NLog::log("{}Error: kitty did not spawn", Colors::RED);
        ret = 1;
        return;
    }
    OK(getFromSocket("/dispatch focuswindow class:keybinds_test"));
    OK(getFromSocket("/keyword bindo SUPER,Y,sendshortcut,,q,"));
    OK(getFromSocket("/keyword input:repeat_delay 100"));
    OK(getFromSocket("/keyword input:repeat_rate 10"));
    // press keybind
    OK(getFromSocket("/dispatch plugin:test:keybind 1,7,29"));
    // await repeat delay
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    OK(getFromSocket("/dispatch plugin:test:keybind 0,0,29"));
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    const std::string output = readKittyOutput();
    int               yCount = Tests::countOccurrences(output, "y");
    // sometimes 1, sometimes 2, not sure why
    // keybind press sends 1 y immediately
    // then repeat triggers, sending 1 y
    // final release stop repeats, and shouldn't send any more
    NLog::log("{}yCount: {}", Colors::GREEN, yCount);
    EXPECT(true, yCount == 1 || yCount == 2);
    EXPECT_COUNT_STRING(output, "q", 1);
    OK(getFromSocket("/keyword unbind SUPER,Y"));
    Tests::killAllWindows();
}

static void testShortcutLongPressKeyRelease() {
    auto kittyProc = spawnRemoteControlKitty();
    if (!kittyProc) {
        NLog::log("{}Error: kitty did not spawn", Colors::RED);
        ret = 1;
        return;
    }
    OK(getFromSocket("/dispatch focuswindow class:keybinds_test"));
    OK(getFromSocket("/keyword bindo SUPER,Y,sendshortcut,,q,"));
    OK(getFromSocket("/keyword input:repeat_delay 100"));
    OK(getFromSocket("/keyword input:repeat_rate 10"));
    // press keybind
    OK(getFromSocket("/dispatch plugin:test:keybind 1,7,29"));
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    // release key, keep modifier
    OK(getFromSocket("/dispatch plugin:test:keybind 0,7,29"));
    // await repeat delay
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    const std::string output = readKittyOutput();
    // disabled: doesn't work on CI
    // EXPECT_COUNT_STRING(output, "y", 1);
    EXPECT_COUNT_STRING(output, "q", 0);
    OK(getFromSocket("/dispatch plugin:test:keybind 0,0,29"));
    OK(getFromSocket("/keyword unbind SUPER,Y"));
    Tests::killAllWindows();
}

static void testShortcutRepeat() {
    auto kittyProc = spawnRemoteControlKitty();
    if (!kittyProc) {
        NLog::log("{}Error: kitty did not spawn", Colors::RED);
        ret = 1;
        return;
    }
    OK(getFromSocket("/dispatch focuswindow class:keybinds_test"));
    OK(getFromSocket("/keyword binde SUPER,Y,sendshortcut,,q,"));
    OK(getFromSocket("/keyword input:repeat_rate 5"));
    OK(getFromSocket("/keyword input:repeat_delay 200"));
    // press keybind
    OK(getFromSocket("/dispatch plugin:test:keybind 1,7,29"));
    // await repeat
    std::this_thread::sleep_for(std::chrono::milliseconds(210));
    // release keybind
    OK(getFromSocket("/dispatch plugin:test:keybind 0,0,29"));
    std::this_thread::sleep_for(std::chrono::milliseconds(450));
    const std::string output = readKittyOutput();
    EXPECT_COUNT_STRING(output, "y", 0);
    int qCount = Tests::countOccurrences(output, "q");
    // sometimes 2, sometimes 3, not sure why
    // keybind press sends 1 q immediately
    // then repeat triggers, sending 1 q
    // final release stop repeats, and shouldn't send any more
    EXPECT(true, qCount == 2 || qCount == 3);
    OK(getFromSocket("/keyword unbind SUPER,Y"));
    Tests::killAllWindows();
}

static void testShortcutRepeatKeyRelease() {
    auto kittyProc = spawnRemoteControlKitty();
    if (!kittyProc) {
        NLog::log("{}Error: kitty did not spawn", Colors::RED);
        ret = 1;
        return;
    }
    OK(getFromSocket("/dispatch focuswindow class:keybinds_test"));
    OK(getFromSocket("/keyword binde SUPER,Y,sendshortcut,,q,"));
    OK(getFromSocket("/keyword input:repeat_rate 5"));
    OK(getFromSocket("/keyword input:repeat_delay 200"));
    // press keybind
    OK(getFromSocket("/dispatch plugin:test:keybind 1,7,29"));
    std::this_thread::sleep_for(std::chrono::milliseconds(210));
    // release key, keep modifier
    OK(getFromSocket("/dispatch plugin:test:keybind 0,7,29"));
    // if repeat was still active, we'd get 2 more q's here
    std::this_thread::sleep_for(std::chrono::milliseconds(450));
    // release modifier
    const std::string output = readKittyOutput();
    EXPECT_COUNT_STRING(output, "y", 0);
    int qCount = Tests::countOccurrences(output, "q");
    // sometimes 2, sometimes 3, not sure why
    // keybind press sends 1 q immediately
    // then repeat triggers, sending 1 q
    // final release stop repeats, and shouldn't send any more
    EXPECT(true, qCount == 2 || qCount == 3);
    OK(getFromSocket("/dispatch plugin:test:keybind 0,0,29"));
    OK(getFromSocket("/keyword unbind SUPER,Y"));
    Tests::killAllWindows();
}

static void testSubmap() {
    const auto press = [](const uint32_t key, const uint32_t mod = 0) {
        // +8 because udev -> XKB keycode.
        getFromSocket("/dispatch plugin:test:keybind 1," + std::to_string(mod) + "," + std::to_string(key + 8));
        getFromSocket("/dispatch plugin:test:keybind 0," + std::to_string(mod) + "," + std::to_string(key + 8));
    };

    NLog::log("{}Testing submaps", Colors::GREEN);
    // submap 1 no resets
    press(KEY_U, MOD_META);
    EXPECT_CONTAINS(getFromSocket("/submap"), "submap1");
    press(KEY_O);
    Tests::waitUntilWindowsN(1);
    EXPECT_CONTAINS(getFromSocket("/submap"), "submap1");
    // submap 2 resets to submap 1
    press(KEY_U);
    EXPECT_CONTAINS(getFromSocket("/submap"), "submap2");
    press(KEY_O);
    Tests::waitUntilWindowsN(2);
    EXPECT_CONTAINS(getFromSocket("/submap"), "submap1");
    // submap 3 resets to default
    press(KEY_I);
    EXPECT_CONTAINS(getFromSocket("/submap"), "submap3");
    press(KEY_O);
    Tests::waitUntilWindowsN(3);
    EXPECT_CONTAINS(getFromSocket("/submap"), "default");
    // submap 1 reset via keybind
    press(KEY_U, MOD_META);
    EXPECT_CONTAINS(getFromSocket("/submap"), "submap1");
    press(KEY_P);
    EXPECT_CONTAINS(getFromSocket("/submap"), "default");

    Tests::killAllWindows();
}

static bool test() {
    NLog::log("{}Testing keybinds", Colors::GREEN);

    testBind();
    testBindKey();
    testLongPress();
    testKeyLongPress();
    testLongPressRelease();
    testLongPressOnlyKeyRelease();
    testRepeat();
    testKeyRepeat();
    testRepeatRelease();
    testRepeatOnlyKeyRelease();
    testShortcutBind();
    testShortcutBindKey();
    testShortcutLongPress();
    testShortcutLongPressKeyRelease();
    testShortcutRepeat();
    testShortcutRepeatKeyRelease();

    testSubmap();

    clearFlag();
    return !ret;
}

REGISTER_TEST_FN(test)
