#include "layout_detector.h"

#include "word_index.h"

#include <string>
#include <unordered_set>

namespace {

[[nodiscard]] bool is_latin(wchar_t ch) noexcept {
    return (ch >= L'A' && ch <= L'Z') || (ch >= L'a' && ch <= L'z');
}

[[nodiscard]] bool is_cyrillic(wchar_t ch) noexcept {
    return (ch >= 0x0400 && ch <= 0x04FF);
}

[[nodiscard]] wchar_t fold_char(wchar_t ch) noexcept {
    if (ch >= L'A' && ch <= L'Z') {
        return static_cast<wchar_t>(ch - L'A' + L'a');
    }
    if (ch >= L'А' && ch <= L'Я') {
        return static_cast<wchar_t>(ch - L'А' + L'а');
    }
    if (ch == L'Ё') {
        return L'е';
    }
    if (ch == L'ё') {
        return L'е';
    }
    return ch;
}

[[nodiscard]] wchar_t proto_fold_char(wchar_t ch) noexcept {
    if (ch >= L'A' && ch <= L'Z') {
        return static_cast<wchar_t>(ch - L'A' + L'a');
    }
    if (ch >= L'А' && ch <= L'Я') {
        return static_cast<wchar_t>(ch - L'А' + L'а');
    }
    if (ch == L'Ё') {
        return L'ё';
    }
    return ch;
}

[[nodiscard]] std::wstring letter_core(std::wstring_view text) {
    std::wstring core;
    core.reserve(text.size());
    for (const wchar_t ch : text) {
        if (is_latin(ch) || is_cyrillic(ch)) {
            core.push_back(fold_char(ch));
        }
    }
    return core;
}

constexpr const wchar_t* kEnglish[] = {
    L"able", L"about", L"above", L"accept", L"access", L"account", L"across", L"action",
    L"actually", L"add", L"address", L"after", L"again", L"against", L"ago", L"agree",
    L"ahead", L"air", L"all", L"allow", L"almost", L"alone", L"along", L"already",
    L"also", L"always", L"among", L"and", L"animal", L"another", L"answer", L"any",
    L"anyone", L"anything", L"app", L"appear", L"apple", L"apply", L"area", L"argument",
    L"around", L"arrive", L"art", L"ask", L"attack", L"attention", L"available", L"away",
    L"baby", L"back", L"bad", L"bag", L"ball", L"bank", L"bar", L"base", L"be", L"beat",
    L"beautiful", L"because", L"become", L"bed", L"been", L"before", L"begin", L"behind",
    L"being", L"believe", L"below", L"best", L"better", L"between", L"big", L"bill",
    L"bit", L"black", L"block", L"blood", L"blue", L"board", L"body", L"book", L"both",
    L"box", L"boy", L"break", L"bring", L"brother", L"brown", L"build", L"building",
    L"business", L"but", L"buy", L"call", L"came", L"can", L"car", L"card", L"care",
    L"carry", L"case", L"catch", L"cause", L"cell", L"center", L"certain", L"change",
    L"character", L"charge", L"check", L"child", L"children", L"choice", L"choose",
    L"city", L"class", L"clean", L"clear", L"click", L"close", L"code", L"cold",
    L"college", L"color", L"come", L"comment", L"common", L"community", L"company",
    L"compare", L"complete", L"computer", L"condition", L"consider", L"continue",
    L"control", L"cook", L"copy", L"correct", L"cost", L"could", L"country", L"couple",
    L"course", L"cover", L"create", L"cross", L"culture", L"current", L"cut", L"dark",
    L"data", L"date", L"daughter", L"day", L"dead", L"deal", L"death", L"debug",
    L"decide", L"deep", L"default", L"delete", L"describe", L"design", L"develop",
    L"did", L"die", L"difference", L"different", L"difficult", L"dinner", L"direction",
    L"discover", L"discuss", L"do", L"doctor", L"does", L"dog", L"done", L"dont",
    L"door", L"down", L"download", L"draw", L"dream", L"drive", L"drop", L"during",
    L"each", L"early", L"earth", L"easy", L"eat", L"economic", L"edge", L"education",
    L"effect", L"effort", L"eight", L"either", L"else", L"email", L"end", L"energy",
    L"english", L"enough", L"enter", L"environment", L"error", L"especially", L"even",
    L"evening", L"event", L"ever", L"every", L"everyone", L"everything", L"example",
    L"exist", L"expect", L"experience", L"explain", L"extra", L"eye", L"face", L"fact",
    L"fail", L"fall", L"family", L"far", L"fast", L"father", L"fear", L"feature",
    L"feel", L"few", L"field", L"fight", L"figure", L"file", L"fill", L"film", L"final",
    L"finally", L"find", L"fine", L"finger", L"finish", L"fire", L"first", L"fish",
    L"fit", L"five", L"floor", L"fly", L"focus", L"follow", L"food", L"foot", L"for",
    L"force", L"foreign", L"forget", L"form", L"former", L"forward", L"four", L"free",
    L"friend", L"from", L"front", L"full", L"function", L"future", L"game", L"garden",
    L"gas", L"general", L"get", L"girl", L"give", L"glass", L"go", L"goal", L"going",
    L"good", L"got", L"government", L"great", L"green", L"ground", L"group", L"grow",
    L"guess", L"gun", L"guy", L"had", L"hair", L"half", L"hand", L"hang", L"happen",
    L"happy", L"hard", L"has", L"have", L"he", L"head", L"health", L"hear", L"heart",
    L"heat", L"heavy", L"hello", L"help", L"her", L"here", L"herself", L"hey", L"hi",
    L"high", L"him", L"himself", L"his", L"history", L"hit", L"hold", L"home", L"hope",
    L"hospital", L"hot", L"hotel", L"hour", L"house", L"how", L"however", L"human",
    L"hundred", L"husband", L"idea", L"identify", L"if", L"image", L"imagine", L"impact",
    L"implement", L"important", L"in", L"include", L"including", L"increase", L"indeed",
    L"indicate", L"individual", L"industry", L"info", L"information", L"inside",
    L"instead", L"interest", L"international", L"internet", L"into", L"involve",
    L"issue", L"it", L"item", L"its", L"itself", L"job", L"join", L"just", L"keep",
    L"key", L"kid", L"kill", L"kind", L"kitchen", L"know", L"knowledge", L"land",
    L"language", L"large", L"last", L"late", L"later", L"laugh", L"law", L"lay",
    L"lead", L"learn", L"least", L"leave", L"left", L"leg", L"legal", L"less", L"let",
    L"letter", L"level", L"library", L"lie", L"life", L"light", L"like", L"likely",
    L"line", L"link", L"list", L"listen", L"little", L"live", L"local", L"lock",
    L"login", L"long", L"look", L"lose", L"loss", L"lot", L"love", L"low", L"machine",
    L"main", L"major", L"make", L"man", L"manage", L"many", L"map", L"market", L"marry",
    L"match", L"material", L"matter", L"may", L"maybe", L"me", L"mean", L"measure",
    L"media", L"medical", L"meet", L"member", L"memory", L"mention", L"message",
    L"method", L"middle", L"might", L"military", L"million", L"mind", L"minute",
    L"miss", L"model", L"modern", L"moment", L"money", L"month", L"more", L"morning",
    L"most", L"mother", L"mouse", L"mouth", L"move", L"movement", L"movie", L"much",
    L"music", L"must", L"my", L"myself", L"name", L"nation", L"national", L"natural",
    L"nature", L"near", L"nearly", L"necessary", L"need", L"network", L"never", L"new",
    L"news", L"next", L"nice", L"night", L"nine", L"no", L"none", L"nor", L"north",
    L"not", L"note", L"nothing", L"notice", L"now", L"number", L"object", L"occur",
    L"of", L"off", L"offer", L"office", L"often", L"oh", L"oil", L"ok", L"okay", L"old",
    L"on", L"once", L"one", L"only", L"onto", L"open", L"operation", L"opportunity",
    L"option", L"or", L"order", L"organization", L"other", L"others", L"our", L"out",
    L"outside", L"over", L"own", L"page", L"paint", L"paper", L"parent", L"part",
    L"particular", L"partner", L"party", L"pass", L"password", L"past", L"path",
    L"patient", L"pattern", L"pay", L"peace", L"people", L"per", L"perform", L"perhaps",
    L"period", L"person", L"personal", L"phone", L"photo", L"physical", L"pick",
    L"picture", L"piece", L"place", L"plan", L"plant", L"play", L"player", L"please",
    L"point", L"police", L"policy", L"political", L"poor", L"popular", L"population",
    L"position", L"positive", L"possible", L"power", L"practice", L"prepare", L"present",
    L"president", L"press", L"pressure", L"pretty", L"prevent", L"price", L"private",
    L"probably", L"problem", L"process", L"produce", L"product", L"professional",
    L"professor", L"program", L"project", L"property", L"protect", L"prove", L"provide",
    L"public", L"pull", L"purpose", L"push", L"put", L"quality", L"question", L"quickly",
    L"quite", L"race", L"radio", L"raise", L"range", L"rate", L"rather", L"reach",
    L"read", L"ready", L"real", L"reality", L"realize", L"really", L"reason", L"receive",
    L"recent", L"recently", L"recognize", L"record", L"red", L"reduce", L"reflect",
    L"region", L"relate", L"relationship", L"religious", L"remain", L"remember",
    L"remove", L"report", L"represent", L"require", L"research", L"resource", L"respond",
    L"response", L"rest", L"result", L"return", L"reveal", L"rich", L"right", L"rise",
    L"risk", L"road", L"rock", L"role", L"room", L"rule", L"run", L"safe", L"same",
    L"save", L"say", L"scene", L"school", L"science", L"scientist", L"score", L"screen",
    L"search", L"season", L"seat", L"second", L"section", L"security", L"see", L"seek",
    L"seem", L"sell", L"send", L"senior", L"sense", L"series", L"serious", L"serve",
    L"service", L"set", L"setting", L"seven", L"several", L"sex", L"shake", L"share",
    L"she", L"shoot", L"short", L"shot", L"should", L"shoulder", L"show", L"side",
    L"sign", L"significant", L"similar", L"simple", L"simply", L"since", L"sing",
    L"single", L"sister", L"sit", L"site", L"situation", L"six", L"size", L"skill",
    L"skin", L"small", L"smile", L"so", L"social", L"society", L"software", L"soldier",
    L"some", L"somebody", L"someone", L"something", L"sometimes", L"son", L"song",
    L"soon", L"sorry", L"sort", L"sound", L"source", L"south", L"space", L"speak",
    L"special", L"specific", L"speech", L"spend", L"sport", L"spring", L"staff",
    L"stage", L"stand", L"standard", L"star", L"start", L"state", L"statement",
    L"station", L"stay", L"step", L"still", L"stock", L"stop", L"store", L"story",
    L"strategy", L"street", L"strong", L"structure", L"student", L"study", L"stuff",
    L"style", L"subject", L"success", L"successful", L"such", L"suddenly", L"suffer",
    L"suggest", L"summer", L"support", L"sure", L"surface", L"system", L"table",
    L"take", L"talk", L"task", L"tax", L"teach", L"teacher", L"team", L"technology",
    L"television", L"tell", L"ten", L"term", L"test", L"text", L"than", L"thank",
    L"thanks", L"that", L"the", L"their", L"them", L"themselves", L"then", L"theory",
    L"there", L"these", L"they", L"thing", L"think", L"third", L"this", L"those",
    L"though", L"thought", L"thousand", L"threat", L"three", L"through", L"throw",
    L"thus", L"time", L"to", L"today", L"together", L"tomorrow", L"tonight", L"too",
    L"top", L"total", L"touch", L"toward", L"town", L"trade", L"traditional", L"training",
    L"travel", L"treat", L"tree", L"trial", L"trip", L"trouble", L"true", L"truth",
    L"try", L"turn", L"two", L"type", L"under", L"understand", L"unit", L"until", L"up",
    L"upon", L"us", L"use", L"user", L"usually", L"value", L"various", L"very",
    L"victim", L"view", L"violence", L"visit", L"voice", L"vote", L"wait", L"walk",
    L"wall", L"want", L"war", L"was", L"watch", L"water", L"way", L"we", L"weapon",
    L"wear", L"week", L"weight", L"welcome", L"well", L"went", L"were", L"west",
    L"what", L"whatever", L"when", L"where", L"whether", L"which", L"while", L"white",
    L"who", L"whole", L"whom", L"whose", L"why", L"wide", L"wife", L"will", L"win",
    L"wind", L"window", L"wish", L"with", L"within", L"without", L"woman", L"women",
    L"wonder", L"word", L"work", L"worker", L"world", L"worry", L"would", L"write",
    L"writer", L"wrong", L"yeah", L"year", L"yes", L"yesterday", L"yet", L"you",
    L"young", L"your", L"yourself", L"lol", L"omg", L"btw", L"imo", L"idk", L"asap",
    L"github", L"gitlab", L"cmake", L"cursor", L"switch", L"layout", L"keyboard",
};

constexpr const wchar_t* kEnglishShort[] = {
    L"am", L"an", L"as", L"at", L"be", L"by", L"do", L"go", L"he", L"hi", L"if", L"in",
    L"is", L"it", L"me", L"my", L"no", L"of", L"ok", L"on", L"or", L"so", L"to", L"up",
    L"us", L"we",
};

constexpr const wchar_t* kRussian[] = {
    L"без", L"более", L"больше", L"будет", L"буду", L"будем", L"будете", L"будешь",
    L"будто", L"быть", L"важно", L"вам", L"вас", L"вдруг", L"ведь", L"вечер", L"вид",
    L"видеть", L"вниз", L"вовсе", L"воздух", L"война", L"вокруг", L"вообще", L"вопрос",
    L"восем", L"восемь", L"вот", L"впрочем", L"время", L"все", L"всегда", L"всего",
    L"всех", L"всю", L"вся", L"второй", L"вчера", L"вы", L"выбор", L"выглядит",
    L"высокий", L"вышел", L"где", L"главный", L"глаз", L"глаза", L"говорить", L"говорят",
    L"год", L"года", L"году", L"голова", L"голос", L"город", L"городе", L"гость",
    L"готов", L"готово", L"группа", L"давай", L"давать", L"давно", L"даже", L"далее",
    L"далеко", L"данные", L"дать", L"два", L"две", L"дверь", L"дворец", L"дело",
    L"день", L"деньги", L"для", L"договор", L"должен", L"должна", L"должно", L"дом",
    L"дома", L"дорога", L"достаточно", L"дочь", L"друг", L"другое", L"другой",
    L"думать", L"его", L"ее", L"если", L"есть", L"еще", L"жалко", L"жаль", L"ждать",
    L"же", L"жена", L"женщина", L"жизнь", L"жить", L"завтра", L"зачем", L"здесь",
    L"знать", L"значит", L"знаю", L"и", L"игра", L"идея", L"идти", L"из", L"или",
    L"именно", L"иметь", L"имя", L"иначе", L"иногда", L"искать", L"искусство",
    L"использовать", L"история", L"итак", L"их", L"кабинет", L"каждый", L"как",
    L"какая", L"какие", L"какое", L"какой", L"кажется", L"картина", L"квартира",
    L"книга", L"код", L"когда", L"конец", L"конечно", L"который", L"кроме", L"круто",
    L"кто", L"куда", L"ладно", L"легко", L"ли", L"либо", L"лицо", L"лишь", L"лучше",
    L"любить", L"любой", L"люди", L"мало", L"мама", L"между", L"меня", L"место",
    L"минута", L"мир", L"мне", L"много", L"мог", L"могла", L"могло", L"могу",
    L"мое", L"мой", L"может", L"можно", L"момент", L"москва", L"мочь", L"моя", L"мы",
    L"надо", L"назад", L"название", L"найти", L"например", L"народ", L"начать",
    L"начало", L"наш", L"наша", L"наше", L"наши", L"небо", L"него", L"нее", L"нельзя",
    L"немного", L"необходимо", L"несколько", L"нет", L"нибудь", L"никогда", L"никто",
    L"ничего", L"но", L"новый", L"нога", L"номер", L"нормально", L"ночь", L"ну",
    L"нужно", L"нужен", L"о", L"оба", L"обычно", L"один", L"однако", L"однажды",
    L"ожидать", L"ок", L"окно", L"около", L"он", L"она", L"они", L"оно", L"опять",
    L"особенно", L"остаться", L"ответить", L"отец", L"оттуда", L"очень", L"папа",
    L"первый", L"перед", L"писать", L"письмо", L"план", L"плохо", L"по", L"победа",
    L"погода", L"под", L"подумать", L"пожалуйста", L"пока", L"показать", L"получить",
    L"помнить", L"понял", L"поняла", L"понятно", L"пора", L"после", L"последний",
    L"потому", L"почему", L"почти", L"поэтому", L"правда", L"право", L"представить",
    L"прежде", L"привет", L"прийти", L"пример", L"принять", L"проблема", L"просто",
    L"против", L"процесс", L"прямо", L"путь", L"пять", L"работа", L"работать", L"рад",
    L"ради", L"раз", L"разве", L"развитие", L"разговор", L"рядом", L"ранний", L"ребенок",
    L"результат", L"решение", L"решить", L"россия", L"рука", L"руки", L"русский",
    L"ряд", L"рядом", L"сам", L"сама", L"сами", L"самый", L"свет", L"свое", L"свой",
    L"своя", L"сделать", L"себе", L"себя", L"сегодня", L"сейчас", L"семья", L"сердце",
    L"сказать", L"сколько", L"скоро", L"слишком", L"слово", L"случай", L"смотреть",
    L"сначала", L"снова", L"собой", L"совсем", L"согласен", L"солнце", L"сомнение",
    L"сообщение", L"сорок", L"спасибо", L"спрашивать", L"сразу", L"среди", L"стал",
    L"стала", L"стать", L"статья", L"сторона", L"стоять", L"страна", L"страх",
    L"строить", L"стул", L"суд", L"судьба", L"счет", L"сюда", L"такой", L"там",
    L"твой", L"твоя", L"твое", L"текст", L"тело", L"тем", L"теперь", L"тебя", L"то",
    L"тогда", L"того", L"тоже", L"той", L"только", L"том", L"тому", L"тот", L"точно",
    L"три", L"ту", L"туда", L"тут", L"ты", L"уже", L"улица", L"увидеть", L"утро",
    L"уходить", L"файл", L"фильм", L"хорошо", L"хотеть", L"хоть", L"хотя", L"хочу",
    L"царь", L"цвет", L"цел", L"цель", L"час", L"часто", L"часть", L"человек", L"чем",
    L"через", L"черный", L"четыре", L"число", L"что", L"чтобы", L"чуть", L"шаг",
    L"шесть", L"школа", L"это", L"этого", L"этой", L"этом", L"этот", L"эту", L"я",
    L"язык", L"ясно", L"лол", L"ага", L"угу", L"щас", L"норм", L"спс",
    L"раскладка", L"клавиатура", L"переключить", L"настройка", L"приложение",
};

constexpr const wchar_t* kRussianShort[] = {
    L"бы", L"во", L"да", L"до", L"же", L"за", L"из", L"ли", L"на", L"не", L"ни", L"ну",
    L"об", L"ок", L"он", L"от", L"по", L"то", L"ты", L"уж",
};

// Impossible bigrams from xneur (GPL-2.0): share/languages/{en,ru}/proto
// https://github.com/linuxbuh/xneur
constexpr const wchar_t* kEnglishProto[] = {
    L"bq", L"cj", L"cx", L"dx", L"fq", L"fv", L"fx", L"fz", L"gx", L"hx", L"jb", L"jd",
    L"jf", L"jh", L"jl", L"jm", L"jp", L"jq", L"jt", L"jv", L"jw", L"jx", L"jz", L"kx",
    L"kz", L"mx", L"mz", L"pq", L"px", L"qc", L"qd", L"qe", L"qf", L"qg", L"qh", L"qj",
    L"qk", L"ql", L"qm", L"qn", L"qo", L"qp", L"qs", L"qv", L"qx", L"qy", L"qz", L"sx",
    L"tq", L"tx", L"vc", L"vf", L"vj", L"vm", L"vp", L"vq", L"vw", L"vx", L"vz", L"wq",
    L"wv", L"wx", L"xd", L"xj", L"xk", L"yq", L"zf", L"zj", L"zx",
};

constexpr const wchar_t* kRussianProto[] = {
    L"аъ", L"аы", L"аь", L"бй", L"вй", L"гй", L"гх", L"гц", L"гщ", L"гъ", L"гы", L"гь",
    L"гя", L"дй", L"еъ", L"еы", L"еь", L"жй", L"жх", L"жщ", L"жы", L"жя", L"зй", L"зх",
    L"зщ", L"иъ", L"иы", L"иь", L"йй", L"йъ", L"йы", L"йь", L"йэ", L"йё", L"кй", L"кщ",
    L"къ", L"кя", L"лй", L"лъ", L"мй", L"мъ", L"нй", L"оъ", L"оы", L"оь", L"пг", L"пж",
    L"пз", L"пй", L"пъ", L"рй", L"сй", L"тй", L"уъ", L"уы", L"уь", L"фж", L"фй", L"фх",
    L"фц", L"фъ", L"фэ", L"хй", L"хы", L"хю", L"хя", L"хё", L"цж", L"цй", L"цф", L"цх",
    L"цч", L"цщ", L"цъ", L"ць", L"цэ", L"ця", L"цё", L"чг", L"чз", L"чй", L"чп", L"чф",
    L"чщ", L"чъ", L"чы", L"чэ", L"чю", L"чя", L"шг", L"шд", L"шж", L"шз", L"шй", L"шш",
    L"шщ", L"шъ", L"шы", L"шэ", L"шя", L"щб", L"щг", L"щд", L"щж", L"щз", L"щй", L"щк",
    L"щл", L"щп", L"щс", L"щт", L"щф", L"щх", L"щц", L"щч", L"щш", L"щщ", L"щъ", L"щы",
    L"щэ", L"щю", L"щя", L"ъа", L"ъб", L"ъв", L"ъг", L"ъд", L"ъж", L"ъз", L"ъи", L"ъй",
    L"ък", L"ъл", L"ъм", L"ън", L"ъо", L"ъп", L"ър", L"ъс", L"ът", L"ъу", L"ъф", L"ъх",
    L"ъц", L"ъч", L"ъш", L"ъщ", L"ъъ", L"ъы", L"ъь", L"ъэ", L"ыа", L"ыо", L"ыф", L"ыъ",
    L"ыы", L"ыь", L"ыэ", L"ыю", L"ыё", L"ьа", L"ьй", L"ьр", L"ьу", L"ьъ", L"ьы", L"ьь",
    L"эа", L"эе", L"эи", L"эч", L"эщ", L"эъ", L"эы", L"эь", L"ээ", L"эю", L"эё", L"юу",
    L"юъ", L"юы", L"юь", L"яа", L"яъ", L"яы", L"яь", L"яэ", L"яё", L"ёа", L"ёе", L"ёи",
    L"ёй", L"ёо", L"ёу", L"ёф", L"ёъ", L"ёы", L"ёь", L"ёэ", L"ёю", L"ёя", L"ёё",
};

template <std::size_t N>
[[nodiscard]] std::unordered_set<std::wstring> make_set(const wchar_t* const (&words)[N]) {
    std::unordered_set<std::wstring> set;
    set.reserve(N);
    for (const wchar_t* word : words) {
        set.emplace(word);
    }
    return set;
}

[[nodiscard]] bool contains(const std::unordered_set<std::wstring>& set, const std::wstring& word) {
    return set.find(word) != set.end();
}

std::unordered_set<std::wstring> g_extra_en;
std::unordered_set<std::wstring> g_extra_ru;
std::unordered_set<std::wstring> g_exceptions;
WordIndex g_en_index;
WordIndex g_ru_index;
bool g_indexes_ready = false;

void ensure_indexes() {
    if (g_indexes_ready) {
        return;
    }
    for (const wchar_t* word : kEnglish) {
        g_en_index.add(word);
    }
    for (const wchar_t* word : kEnglishShort) {
        g_en_index.add(word);
    }
    for (const wchar_t* word : kRussian) {
        g_ru_index.add(word);
    }
    for (const wchar_t* word : kRussianShort) {
        g_ru_index.add(word);
    }
    g_en_index.finalize();
    g_ru_index.finalize();
    g_indexes_ready = true;
}

[[nodiscard]] WordIndex& index_for(Translator::Layout layout) {
    ensure_indexes();
    return (layout == Translator::Layout::Ru) ? g_ru_index : g_en_index;
}

[[nodiscard]] const std::unordered_set<std::wstring>& extra_for(Translator::Layout layout) {
    return (layout == Translator::Layout::Ru) ? g_extra_ru : g_extra_en;
}

[[nodiscard]] bool extra_has_prefix(const std::unordered_set<std::wstring>& extra, std::wstring_view prefix) {
    for (const auto& word : extra) {
        if (word.size() >= prefix.size() && word.compare(0, prefix.size(), prefix.data(), prefix.size()) == 0) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] const std::unordered_set<std::wstring>& english_proto() {
    static const auto set = make_set(kEnglishProto);
    return set;
}

[[nodiscard]] const std::unordered_set<std::wstring>& russian_proto() {
    static const auto set = make_set(kRussianProto);
    return set;
}

[[nodiscard]] std::wstring proto_core(std::wstring_view text) {
    std::wstring core;
    core.reserve(text.size());
    for (const wchar_t ch : text) {
        if (is_latin(ch) || is_cyrillic(ch)) {
            core.push_back(proto_fold_char(ch));
        }
    }
    return core;
}

[[nodiscard]] std::wstring fold_token(std::wstring_view text) {
    std::wstring folded;
    folded.reserve(text.size());
    for (const wchar_t ch : text) {
        folded.push_back(proto_fold_char(ch));
    }
    return folded;
}

[[nodiscard]] bool starts_with(std::wstring_view text, std::wstring_view prefix) noexcept {
    return text.size() >= prefix.size() && text.compare(0, prefix.size(), prefix) == 0;
}

[[nodiscard]] bool contains_text(std::wstring_view text, std::wstring_view needle) noexcept {
    return text.find(needle) != std::wstring_view::npos;
}

[[nodiscard]] int proto_hits(std::wstring_view word, Translator::Layout layout) {
    const auto& proto = (layout == Translator::Layout::Ru) ? russian_proto() : english_proto();
    int hits = 0;
    for (std::size_t i = 0; i + 1 < word.size(); ++i) {
        const std::wstring bigram{word[i], word[i + 1]};
        if (contains(proto, bigram)) {
            ++hits;
        }
    }
    return hits;
}

[[nodiscard]] bool looks_like_ipv4(std::wstring_view text) noexcept {
    int dots = 0;
    int group = 0;
    for (const wchar_t ch : text) {
        if (ch == L'.') {
            if (group == 0) {
                return false;
            }
            ++dots;
            group = 0;
            continue;
        }
        if (ch < L'0' || ch > L'9') {
            return false;
        }
        ++group;
        if (group > 3) {
            return false;
        }
    }
    return dots == 3 && group > 0;
}

[[nodiscard]] bool looks_like_mac(std::wstring_view text) noexcept {
    if (text.size() != 17) {
        return false;
    }
    for (std::size_t i = 0; i < text.size(); ++i) {
        const wchar_t ch = text[i];
        if ((i + 1) % 3 == 0) {
            if (ch != L':') {
                return false;
            }
        } else {
            const bool hex = (ch >= L'0' && ch <= L'9') || (ch >= L'a' && ch <= L'f') ||
                             (ch >= L'A' && ch <= L'F');
            if (!hex) {
                return false;
            }
        }
    }
    return true;
}

[[nodiscard]] bool matches_xneur_exception(std::wstring_view text, Translator::Layout layout) {
    const std::wstring folded = fold_token(text);
    const std::wstring core = letter_core(text);

    if (layout == Translator::Layout::En) {
        if (starts_with(folded, L"http") || starts_with(folded, L"ftp") || starts_with(folded, L"www")) {
            return true;
        }
        if (contains_text(folded, L"xneur") || looks_like_ipv4(text) || looks_like_mac(text)) {
            return true;
        }
    }

    if (layout == Translator::Layout::Ru) {
        if (core == L"а" || core == L"в" || core == L"и" || core == L"к" || core == L"о" ||
            core == L"у" || core == L"я" || core == L"ну") {
            return true;
        }
        if (contains_text(folded, L".рф")) {
            return true;
        }
    }

    return false;
}

[[nodiscard]] bool matches_user_exception(std::wstring_view text) {
    const std::wstring folded = fold_token(text);
    const std::wstring core = letter_core(text);
    return contains(g_exceptions, folded) || (!core.empty() && contains(g_exceptions, core));
}

[[nodiscard]] bool is_known(const std::wstring& word, Translator::Layout layout, bool) {
    return contains(extra_for(layout), word) || index_for(layout).contains(word);
}

[[nodiscard]] bool is_dictionary_prefix(const std::wstring& prefix, Translator::Layout layout) {
    if (prefix.size() < 3) {
        return false;
    }
    if (extra_has_prefix(extra_for(layout), prefix)) {
        return true;
    }
    return index_for(layout).has_prefix(prefix);
}

[[nodiscard]] std::wstring utf8_to_wide(std::string_view text) {
    if (text.empty()) {
        return {};
    }
    const int needed = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (needed <= 0) {
        return {};
    }
    std::wstring wide(static_cast<std::size_t>(needed), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), needed);
    return wide;
}

[[nodiscard]] bool looks_like_url_or_email(std::wstring_view text) noexcept {
    bool has_at = false;
    bool has_colon = false;
    bool has_slash = false;
    bool has_dot = false;
    int digits = 0;
    int letters = 0;

    for (const wchar_t ch : text) {
        has_at = has_at || (ch == L'@');
        has_colon = has_colon || (ch == L':');
        has_slash = has_slash || (ch == L'/' || ch == L'\\');
        has_dot = has_dot || (ch == L'.');
        digits += static_cast<int>(ch >= L'0' && ch <= L'9');
        letters += static_cast<int>(is_latin(ch) || is_cyrillic(ch));
    }

    if (has_at || (has_colon && has_slash)) {
        return true;
    }

    if (has_dot && letters >= 3) {
        const auto folded = letter_core(text);
        static constexpr const wchar_t* kTlds[] = {
            L"com", L"net", L"org", L"ru", L"io", L"dev", L"app", L"info", L"edu",
        };
        for (const wchar_t* tld : kTlds) {
            const std::wstring suffix = tld;
            if (folded.size() > suffix.size() &&
                folded.compare(folded.size() - suffix.size(), suffix.size(), suffix) == 0) {
                return true;
            }
        }
    }

    return letters >= 6 && digits >= 3 && digits * 2 >= letters;
}

}  // namespace

bool LayoutDetector::is_technical_token(std::wstring_view text) noexcept {
    return looks_like_url_or_email(text);
}

void LayoutDetector::load_bundled_dictionaries() {
    ensure_indexes();

    wchar_t path[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        return;
    }

    std::wstring dir = path;
    const auto slash = dir.find_last_of(L"\\/");
    if (slash == std::wstring::npos) {
        return;
    }
    dir.resize(slash);

    const bool en = g_en_index.load_utf8_file(dir + L"\\dict\\en.txt");
    const bool ru = g_ru_index.load_utf8_file(dir + L"\\dict\\ru.txt");
    if (en || ru) {
        g_en_index.finalize();
        g_ru_index.finalize();
    }
}

void LayoutDetector::set_user_words(const std::vector<std::string>& extra_en, const std::vector<std::string>& extra_ru) {
    g_extra_en.clear();
    g_extra_ru.clear();
    for (const auto& word : extra_en) {
        const std::wstring core = letter_core(utf8_to_wide(word));
        if (!core.empty()) {
            g_extra_en.insert(core);
        }
    }
    for (const auto& word : extra_ru) {
        const std::wstring core = letter_core(utf8_to_wide(word));
        if (!core.empty()) {
            g_extra_ru.insert(core);
        }
    }
}

void LayoutDetector::set_exceptions(const std::vector<std::string>& words) {
    g_exceptions.clear();
    for (const auto& word : words) {
        const std::wstring wide = utf8_to_wide(word);
        const std::wstring folded = fold_token(wide);
        const std::wstring core = letter_core(wide);
        if (!folded.empty()) {
            g_exceptions.insert(folded);
        }
        if (!core.empty()) {
            g_exceptions.insert(core);
        }
    }
}

bool LayoutDetector::is_exception_word(std::wstring_view text, Translator::Layout layout) {
    if (layout == Translator::Layout::Other) {
        layout = Translator::infer_layout(text);
    }
    return matches_user_exception(text) || matches_xneur_exception(text, layout);
}

bool LayoutDetector::should_switch(
    std::wstring_view typed,
    std::wstring_view converted,
    Translator::Layout source,
    std::size_t min_length) {
    if (source == Translator::Layout::Other) {
        source = Translator::infer_layout(typed);
    }

    if (is_technical_token(typed) || is_technical_token(converted)) {
        return false;
    }

    if (matches_user_exception(typed) || matches_xneur_exception(typed, source)) {
        return false;
    }

    const std::wstring typed_core = letter_core(typed);
    const std::wstring converted_core = letter_core(converted);
    if (typed_core.empty() || typed_core == converted_core) {
        return false;
    }

    const bool allow_short = typed_core.size() == 2 && min_length <= 3;
    if (typed_core.size() < min_length && !allow_short) {
        return false;
    }

    const auto target = Translator::opposite(source);
    if (matches_xneur_exception(converted, target)) {
        return true;
    }

    const bool typed_known = is_known(typed_core, source, allow_short);
    const bool converted_known = is_known(converted_core, target, allow_short);
    if (typed_known) {
        return false;
    }
    if (converted_known) {
        return true;
    }

    const bool typed_prefix = is_dictionary_prefix(typed_core, source);
    const bool converted_prefix = is_dictionary_prefix(converted_core, target);
    if (typed_core.size() >= min_length) {
        if (typed_prefix && !converted_prefix) {
            return false;
        }
        if (!typed_prefix && converted_prefix) {
            return true;
        }
    }

    const std::wstring typed_proto = proto_core(typed);
    const std::wstring converted_proto = proto_core(converted);
    if (typed_proto.size() < 4) {
        return false;
    }

    const int typed_hits = proto_hits(typed_proto, source);
    const int converted_hits = proto_hits(converted_proto, target);
    return typed_hits >= 2 && converted_hits == 0;
}
