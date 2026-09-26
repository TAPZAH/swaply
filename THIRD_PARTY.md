# Открытые данные и сторонние материалы

Swaply использует только перечисленные ниже открытые источники.
Исходный код самой программы — авторства Tap3ah, 2026.

## Эвристики раскладки — xneur

- Проект: [linuxbuh/xneur](https://github.com/linuxbuh/xneur)
- Сайт: <http://www.xneur.ru>
- Лицензия: GNU GPL версии 2 или более поздней
- Что взято:
  - невозможные биграммы (`share/languages/en/proto`, `share/languages/ru/proto`);
  - правила словаря исключений (`share/languages/en/dictionary`, `share/languages/ru/dictionary`):
    `http` / `ftp` / `www`, `xneur`, IPv4, MAC, `.рф`, частицы `а в и к ну о у я`;
  - идея проверки языка: сначала словарь, затем proto.

Файлы с этими правилами встроены в `src/layout_detector.cpp`.

## Частотные словари — FrequencyWords

- Проект: [hermitdave/FrequencyWords](https://github.com/hermitdave/FrequencyWords)
- Лицензия содержимого: [CC-BY-SA-4.0](https://creativecommons.org/licenses/by-sa/4.0/)
- Корпус: [OpenSubtitles 2018](https://opus.nlpl.eu/OpenSubtitles2018.php) (OPUS)
- Что взято: топ-30 000 буквенных словоформ английского и русского языков
- Файлы: `assets/dict/en.txt`, `assets/dict/ru.txt`
- Пояснение: `assets/dict/ATTRIBUTION.txt`

Списки урезаны до алфавитных токенов длиной 2–32 символа и используются
для распознавания слов и префиксов при автопереключении раскладки.

## Совместимость лицензий

xneur распространяется как GPL-2.0-or-later. Словари FrequencyWords —
CC-BY-SA-4.0, которая односторонне совместима с GNU GPL версии 3.
Поэтому **Swaply публикуется под GNU GPL версии 3 или более поздней**:
это закрывает оба источника и сохраняет copyleft.
