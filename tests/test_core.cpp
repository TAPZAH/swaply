#include "hotkey.h"
#include "layout_detector.h"
#include "text_tracker.h"
#include "translator.h"

#include <iostream>
#include <iterator>
#include <string_view>

namespace {

int g_failed = 0;

void expect(bool ok, const char* name) {
    if (!ok) {
        std::cerr << "FAIL " << name << '\n';
        ++g_failed;
        return;
    }
    std::cout << "OK   " << name << '\n';
}

void expect_eq(std::wstring_view actual, std::wstring_view wanted, const char* name) {
    if (actual != wanted) {
        std::cerr << "FAIL " << name << '\n';
        ++g_failed;
        return;
    }
    std::cout << "OK   " << name << '\n';
}

HookManager::KeyEvent key_event(UINT vk, HKL layout) {
    HookManager::KeyEvent event{};
    event.wparam = WM_KEYDOWN;
    event.info.vkCode = vk;
    event.target_window = reinterpret_cast<HWND>(static_cast<uintptr_t>(0x100));
    event.target_thread = GetCurrentThreadId();
    event.layout = layout;
    return event;
}

}  // namespace

int main() {
    LayoutDetector::load_bundled_dictionaries();

    expect_eq(Translator::convert(L"ghbdtn"), L"привет", "en_keys_to_ru_privet");
    expect_eq(Translator::convert(L"руддщ"), L"hello", "ru_keys_to_en_hello");
    expect_eq(Translator::convert(L"Ghbdtn"), L"Привет", "preserve_case");
    expect_eq(Translator::convert(L"123"), L"123", "digits_unchanged");

    expect(Translator::char_from_vk('G', false, false, Translator::Layout::En) == L'g', "vk_g_en");
    expect(Translator::char_from_vk('G', false, false, Translator::Layout::Ru) == L'п', "vk_g_ru");
    expect(Translator::opposite(Translator::Layout::En) == Translator::Layout::Ru, "opposite");

    expect(LayoutDetector::should_switch(L"ghbdtn", L"привет", Translator::Layout::En), "auto_ru_word");
    expect(LayoutDetector::should_switch(L"руддщ", L"hello", Translator::Layout::Ru), "auto_en_word");
    expect(!LayoutDetector::should_switch(L"hello", L"руддщ", Translator::Layout::En), "keep_known_en");
    expect(!LayoutDetector::should_switch(L"привет", L"ghbdtn", Translator::Layout::Ru), "keep_known_ru");
    expect(!LayoutDetector::should_switch(L"zzqxx", L"яяйчч", Translator::Layout::En), "unknown_both");
    expect(!LayoutDetector::should_switch(L"user@site.com", L"гыук2ышеу0сщь", Translator::Layout::En), "skip_email");
    expect(LayoutDetector::is_technical_token(L"https://example.com"), "url_is_technical");
    expect(!LayoutDetector::is_technical_token(L"привет"), "word_not_technical");

    LayoutDetector::set_user_words({"wxneur"}, {"мояфирма"});
    expect(LayoutDetector::should_switch(L"цчтугк", L"wxneur", Translator::Layout::Ru), "user_en_word");

    expect(!LayoutDetector::should_switch(L"xneur", L"чтугк", Translator::Layout::En), "exception_xneur");
    expect(!LayoutDetector::should_switch(L"wwwtest", L"цццеуые", Translator::Layout::En), "exception_www");
    expect(LayoutDetector::should_switch(L"чтугк", L"xneur", Translator::Layout::Ru), "exception_converted_xneur");
    expect(LayoutDetector::is_exception_word(L"httpabc", Translator::Layout::En), "http_is_exception");
    expect(LayoutDetector::should_switch(L"fqfzjx", L"айаяоч", Translator::Layout::En), "proto_impossible_en");

    expect_eq(Translator::convert(L"cltkfq"), L"сделай", "en_keys_to_ru_sdelay");
    expect_eq(Translator::convert(L"yfghbvth"), L"например", "en_keys_to_ru_naprimer");
    expect(LayoutDetector::should_switch(L"cltkfq", L"сделай", Translator::Layout::En), "dict_sdelay");
    expect(LayoutDetector::should_switch(L"yfghbvth", L"например", Translator::Layout::En), "dict_naprimer");
    expect(LayoutDetector::should_switch(L"clt", L"сде", Translator::Layout::En), "prefix_sde");
    expect(LayoutDetector::should_switch(L"yfg", L"нап", Translator::Layout::En), "prefix_nap");
    expect(!LayoutDetector::should_switch(L"hel", L"руд", Translator::Layout::En), "keep_en_prefix_hel");
    expect(!LayoutDetector::should_switch(L"сделай", L"cltkfq", Translator::Layout::Ru), "keep_sdelay_ru");
    expect(!LayoutDetector::should_switch(L"например", L"yfghbvth", Translator::Layout::Ru), "keep_naprimer_ru");

    LayoutDetector::set_exceptions({"brandword"});
    expect(!LayoutDetector::should_switch(L"brandword", L"икфтвцщкв", Translator::Layout::En), "user_exception");

    const Hotkey pause = Hotkey::parse("Pause");
    expect(pause.vk == VK_PAUSE && !pause.ctrl && !pause.alt, "parse_pause");
    const Hotkey ctrl_pause = Hotkey::parse("Ctrl+Pause");
    expect(ctrl_pause.vk == VK_PAUSE && ctrl_pause.ctrl && !ctrl_pause.alt, "parse_ctrl_pause");
    const Hotkey legacy_ctrl_pause = Hotkey::parse("Ctrl Pause");
    expect(legacy_ctrl_pause.pack() == ctrl_pause.pack(), "parse_legacy_ctrl_pause");
    const Hotkey alt_f12 = Hotkey::parse("Alt+F12");
    expect(alt_f12.vk == VK_F12 && alt_f12.alt && !alt_f12.ctrl, "parse_alt_f12");
    expect(ctrl_pause.matches(VK_CANCEL, true, false, false, false), "pause_cancel_alias");
    expect(ctrl_pause.to_string() == "Ctrl+Pause", "format_ctrl_pause");
    const Hotkey shift_pause = Hotkey::parse("Shift+Pause");
    expect(shift_pause.vk == VK_PAUSE && shift_pause.shift && !shift_pause.ctrl, "parse_shift_pause");
    expect(shift_pause.to_string() == "Shift+Pause", "format_shift_pause");
    expect(Hotkey::key_name(VK_PAUSE) == L"Pause", "key_name_pause");
    expect(Hotkey::key_name('A') == L"A", "key_name_a");
    for (const UINT vk : {VK_SCROLL, VK_CAPITAL, VK_PRIOR, VK_NEXT, VK_SNAPSHOT}) {
        const Hotkey original{vk, true, false, true, false};
        const Hotkey round_trip = Hotkey::parse(original.to_string());
        expect(round_trip.pack() == original.pack(), "hotkey_multiword_round_trip");
    }

    AppConfig tracker_config{};
    tracker_config.auto_switch = false;
    tracker_config.ignore_password_fields = false;
    TextTracker tracker(tracker_config);
    const HKL en = LoadKeyboardLayoutW(L"00000409", KLF_NOTELLSHELL);
    const HKL ru = LoadKeyboardLayoutW(L"00000419", KLF_NOTELLSHELL);
    const UINT sdelay_keys[] = {'C', 'L', 'T', 'K', 'F', 'Q'};
    for (std::size_t i = 0; i < std::size(sdelay_keys); ++i) {
        static_cast<void>(tracker.on_key(key_event(sdelay_keys[i], i < 2 ? ru : en)));
    }
    expect(tracker.source_layout() == Translator::Layout::En, "source_layout_majority");
    expect_eq(tracker.converted_word(), L"сделай", "mixed_layout_converts_uniformly");

    AppConfig auto_config{};
    auto_config.auto_switch = true;
    auto_config.ignore_password_fields = false;
    TextTracker auto_tracker(auto_config);
    expect(auto_tracker.on_key(key_event('C', en)) == TextTracker::Action::DiscardUndo, "prefix_c");
    expect(auto_tracker.on_key(key_event('L', en)) == TextTracker::Action::DiscardUndo, "prefix_cl");
    expect(auto_tracker.on_key(key_event('T', en)) == TextTracker::Action::AutoConvert, "prefix_clt_autoconvert");
    expect_eq(auto_tracker.current_word(), L"clt", "prefix_clt_keeps_full_word");
    expect_eq(auto_tracker.converted_word(), L"сде", "prefix_clt_converted");

    if (en != nullptr) {
        UnloadKeyboardLayout(en);
    }
    if (ru != nullptr) {
        UnloadKeyboardLayout(ru);
    }

    if (g_failed != 0) {
        std::cerr << g_failed << " test(s) failed\n";
        return 1;
    }
    std::cout << "all tests passed\n";
    return 0;
}
