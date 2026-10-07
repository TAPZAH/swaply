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
    expect(Translator::char_from_vk('J', false, false, Translator::Layout::En) == L'j', "vk_j_en");
    expect(Translator::char_from_vk('J', false, false, Translator::Layout::Ru) == L'о', "vk_j_ru");
    expect(Translator::opposite(Translator::Layout::En) == Translator::Layout::Ru, "opposite");

    const auto key_ru_p = Translator::key_from_char(L'п', Translator::Layout::Ru);
    expect(key_ru_p.has_value() && key_ru_p->vk == 'G' && !key_ru_p->shift, "key_from_char_ru_p");
    const auto key_ru_P = Translator::key_from_char(L'П', Translator::Layout::Ru);
    expect(key_ru_P.has_value() && key_ru_P->vk == 'G' && key_ru_P->shift, "key_from_char_ru_P");
    const auto key_en_g = Translator::key_from_char(L'g', Translator::Layout::En);
    expect(key_en_g.has_value() && key_en_g->vk == 'G' && !key_en_g->shift, "key_from_char_en_g");

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

    expect_eq(Translator::convert(L"j,yjdb"), L"обнови", "en_j_to_ru_o");
    expect_eq(Translator::convert(L"jib,rf"), L"ошибка", "en_jib_to_oshibka");
    expect_eq(Translator::convert(L"ошибка"), L"jib,rf", "ru_oshibka_to_en");
    expect_eq(Translator::convert(L"xnj-nj"), L"что-то", "en_xnj_nj_to_chto_to");
    expect(LayoutDetector::should_switch(L"jlby", L"один", Translator::Layout::En), "en_j_as_ru_o");
    expect(LayoutDetector::should_switch(L"xnj-nj", L"что-то", Translator::Layout::En), "hyphen_chto_to");
    expect(LayoutDetector::should_switch(L"jib,rf", L"ошибка", Translator::Layout::En), "comma_oshibka");
    expect(LayoutDetector::should_switch(L"htfkbpjdfyf", L"реализована", Translator::Layout::En),
           "inflected_realizovana");
    expect(!LayoutDetector::should_switch(L"well-known", Translator::convert(L"well-known"), Translator::Layout::En),
           "keep_hyphen_en");
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
    expect(auto_tracker.on_key(key_event('C', en)) == TextTracker::Action::DiscardUndo, "early_c_waits");
    expect(auto_tracker.on_key(key_event('L', en)) == TextTracker::Action::DiscardUndo, "early_cl_waits");
    expect(auto_tracker.on_key(key_event('T', en)) == TextTracker::Action::AutoConvert, "early_clt_converts");
    expect_eq(auto_tracker.current_word(), L"clt", "early_clt_word_kept");
    expect_eq(auto_tracker.converted_word(), L"сде", "early_clt_converted");

    TextTracker keep_tracker(auto_config);
    expect(keep_tracker.on_key(key_event('H', en)) == TextTracker::Action::DiscardUndo, "keep_en_h");
    expect(keep_tracker.on_key(key_event('E', en)) == TextTracker::Action::DiscardUndo, "keep_en_he");
    expect(keep_tracker.on_key(key_event('L', en)) == TextTracker::Action::DiscardUndo, "keep_en_hel");
    expect(keep_tracker.on_key(key_event('L', en)) == TextTracker::Action::DiscardUndo, "keep_en_hell");
    expect(keep_tracker.on_key(key_event('O', en)) == TextTracker::Action::DiscardUndo, "keep_en_hello");

    TextTracker hyphen_tracker(auto_config);
    expect(hyphen_tracker.on_key(key_event('X', en)) == TextTracker::Action::DiscardUndo, "hyphen_x");
    expect(hyphen_tracker.on_key(key_event('N', en)) == TextTracker::Action::DiscardUndo, "hyphen_xn");
    expect(hyphen_tracker.on_key(key_event('J', en)) == TextTracker::Action::AutoConvert,
           "hyphen_xnj_converts_early");

    // Single-letter particles still wait for a delimiter instead of switching on
    // the first keystroke.
    TextTracker particle_tracker(auto_config);
    expect(particle_tracker.on_key(key_event('C', en)) == TextTracker::Action::DiscardUndo, "particle_c_waits");
    expect(particle_tracker.on_key(key_event(VK_SPACE, en)) == TextTracker::Action::AutoConvert,
           "particle_c_converts_on_space");

    TextTracker hotkey_tracker(auto_config);
    expect(hotkey_tracker.on_key(key_event(VK_PAUSE, en)) == TextTracker::Action::ConvertSelection,
           "pause_without_word_converts_selection");
    auto selection_hotkey = key_event(VK_PAUSE, en);
    selection_hotkey.ctrl = true;
    expect(hotkey_tracker.on_key(selection_hotkey) == TextTracker::Action::ConvertSelection,
           "ctrl_pause_converts_selection");
    expect(hotkey_tracker.on_key(key_event('C', en)) == TextTracker::Action::DiscardUndo, "hotkey_then_type");
    expect(hotkey_tracker.on_key(key_event(VK_PAUSE, en)) == TextTracker::Action::ConvertWord,
           "pause_with_word_converts_word");

    expect(LayoutDetector::should_switch(L"d", L"в", Translator::Layout::En), "particle_d_to_ve");
    expect(LayoutDetector::should_switch(L"b", L"и", Translator::Layout::En), "particle_b_to_i");
    expect(LayoutDetector::should_switch(L"r", L"к", Translator::Layout::En), "particle_r_to_ka");
    expect(LayoutDetector::should_switch(L"c", L"с", Translator::Layout::En), "particle_c_to_es");
    expect(LayoutDetector::should_switch(L"j", L"о", Translator::Layout::En), "particle_j_to_o");
    expect(LayoutDetector::should_switch(L"e", L"у", Translator::Layout::En), "particle_e_to_u");
    expect(LayoutDetector::should_switch(L"f", L"а", Translator::Layout::En), "particle_f_to_a");
    expect(LayoutDetector::should_switch(L"z", L"я", Translator::Layout::En), "particle_z_to_ya");
    expect(!LayoutDetector::should_switch(L"в", L"d", Translator::Layout::Ru), "particle_keep_correct_ru");
    expect(!LayoutDetector::should_switch(L"и", L"b", Translator::Layout::Ru), "particle_keep_correct_ru_i");
    expect(!LayoutDetector::should_switch(L"a", L"ф", Translator::Layout::En), "particle_keep_en_article");
    expect(!LayoutDetector::should_switch(L"i", L"ш", Translator::Layout::En), "particle_keep_en_pronoun");
    expect(LayoutDetector::should_switch(L"ш", L"i", Translator::Layout::Ru), "particle_ru_to_en_i");

    expect(!LayoutDetector::should_switch(L"awb", Translator::convert(L"awb"), Translator::Layout::En),
           "term_awb_known_en");
    expect(!LayoutDetector::should_switch(L"вэд", Translator::convert(L"вэд"), Translator::Layout::Ru),
           "term_ved_known_ru");
    expect(!LayoutDetector::should_switch(L"ндс", Translator::convert(L"ндс"), Translator::Layout::Ru),
           "term_nds_known_ru");
    expect(!LayoutDetector::should_switch(L"еаэс", Translator::convert(L"еаэс"), Translator::Layout::Ru),
           "term_eaes_known_ru");
    expect(!LayoutDetector::should_switch(L"гост", Translator::convert(L"гост"), Translator::Layout::Ru),
           "term_gost_known_ru");
    expect(!LayoutDetector::should_switch(L"тн", Translator::convert(L"тн"), Translator::Layout::Ru),
           "term_tn_known_ru");
    expect(!LayoutDetector::should_switch(L"И.И.", L"B.B.", Translator::Layout::Ru), "initials_ru_kept");
    expect(!LayoutDetector::should_switch(L"b.b", L"и.и", Translator::Layout::En), "initials_en_kept");
    expect(!LayoutDetector::should_switch(L"мрт", Translator::convert(L"мрт"), Translator::Layout::Ru),
           "term_mrt_known_ru");
    expect(!LayoutDetector::should_switch(L"егэ", Translator::convert(L"егэ"), Translator::Layout::Ru),
           "term_ege_known_ru");
    expect(!LayoutDetector::should_switch(L"жкх", Translator::convert(L"жкх"), Translator::Layout::Ru),
           "term_zhkh_known_ru");
    expect(!LayoutDetector::should_switch(L"снип", Translator::convert(L"снип"), Translator::Layout::Ru),
           "term_snip_known_ru");
    expect(!LayoutDetector::should_switch(L"ооо", Translator::convert(L"ооо"), Translator::Layout::Ru),
           "term_ooo_known_ru");

    if (g_failed != 0) {
        std::cerr << g_failed << " test(s) failed\n";
        return 1;
    }
    std::cout << "all tests passed\n";
    return 0;
}
