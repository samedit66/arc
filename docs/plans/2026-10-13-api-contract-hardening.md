# Исправление контрактов API, жизненного цикла и индексов

Дата: 2026-10-13
Статус: реализовано; macOS проверена, Linux/Windows CI ожидается
Основание: аудит API и дизайна; пользователь запросил исправление всех шести замечаний.
Цели: macOS, Linux, Windows MSYS2 UCRT64.

## Подход и совместимость

Исправить привязку output к текущему каталогу, неограниченную рекурсию ссылок, потерю ошибок capabilities и очистки, неполную документацию writer и квадратичные индексы. Сохранить владельцев состояния: writer.c, reader.c, unpack.c; libarchive остается единственным backend. Новые production-зависимости не нужны.

Публичные структуры, сигнатуры, enum и биты не менять. Изменения наблюдаемого поведения: discard удаляет исходный output после последовательной смены cwd; capabilities возвращает реальную ошибку проверки вместо ложной недоступности; диагностика writer/extraction сохраняет вторичные сбои в пределах 256 байт; длинные цепочки ссылок обрабатываются без роста стека C. Все существующие ограничения selection, metadata, no-overwrite, trusted parents и partial output сохраняются.

Предположения:
- Последовательная смена cwd между writer-вызовами поддерживается. Конкурентная смена process cwd во время операции не поддерживается; это следует явно документировать.
- Смена cwd не меняет объект output, но относительные source_path по-прежнему вычисляются на момент соответствующего add-вызова.
- Абсолютный путь не защищает от переименования родителей или подмены output. Существующее обязательство не заменять пути остается.
- Reader close сохраняет точный первый stream result; visitor failure возвращается неизмененным. Это намеренные исключения из добавления вторичных диагностик, без изменения ABI.
- Улучшение индексов входит в принятую задачу. Замеры определяют параметры проверки, а не возможность оставить квадратичный поиск.

## 1. Закрепить контракты и добавить исходные regression-сценарии

Файлы: arc.h, docs/2026-10-13-arc-design.md, justfile.

В заголовке описать Ready / Entry open / Failed, правило перехода в Failed после любой ошибочной content-операции, сохранение первой ошибки и потребление указателя close/discard. Для NULL writer нет объекта, состояние которого можно изменить. Уточнить lifetime output path, правила cwd и приоритеты ошибок очистки.

В дизайн добавить ожидаемую сложность внутренних индексов и отсутствие рекурсивного C-stack обхода ссылок. Ограничение backend fatal termination не расширять на собственный код.

В existing temporary suite justfile добавить regression-сценарии и малый тестовый executable для fault injection. Он генерируется вне дерева исходников, как существующие API executables. Замещать нужные filesystem/backend/locale символы только при тестовой компиляции; не добавлять runtime dispatch или production fault hooks.

Проверка: failed begin/add/write/end отравляют writer, дальнейшие content-вызовы дают INVALID_STATE, close сохраняет первую ошибку и обнуляет указатель; discard удаляет output. Invalid arguments reader не отравляют reader. Зафиксировать исходные результаты замеров больших наборов без жестких wall-clock порогов CI.

## 2. Привязать writer output к исходному каталогу

Файлы: internal/file_system.h, internal/file_system_unix.c, internal/file_system_windows.c, internal/writer.c; при необходимости существующие platform_text helpers.

Добавить private fallible helper получения owned абсолютного UTF-8 пути. В arc_create_impl вычислить его до filesystem effects, использовать один сохраненный путь для создания, fs_info, cleanup и discard. Публикацию writer по-прежнему выполнять только после успешного завершения операции и восстановления locale.

POSIX: getcwd с проверяемым ростом буфера и ar_join; не использовать realpath для еще не созданного output и не сворачивать относительные компоненты через symlink. Windows: переиспользовать существующую long-path Unicode конверсию, не создавать вторую реализацию GetFullPathNameW. Проверить drive-relative, rooted и UNC пути. Валидация UTF-8 и ошибки памяти происходят до создания output.

Проверка: в изолированном temporary API process создать A/output.tar, перейти в B с одноименным sentinel, вызвать discard. A/output.tar удален, B/sentinel неизменен. Отдельно проверить успешный close после смены cwd, source-path resolution после смены cwd, output identity exclusion, absolute/Unicode пути, отсутствующих родителей и existing output. На Windows включить длинный путь. Restore cwd выполняет тестовый клиент.

## 3. Исправить capabilities и классификацию backend ошибок

Файлы: internal/libarchive_adapter.c, arc.c, justfile.

Разделить успешную регистрацию, подтвержденную недоступность и технический сбой probe. ENOMEM и иные возвращенные технические ошибки не превращать в отсутствие битов. Проверить результаты RAR5 регистрации и освобождения probe handles. Ошибка не публикует частичный output mask. Read-only форматы остаются успешными без CAN_CREATE; отсутствие writer callback является подтвержденной недоступностью записи.

В backend_error дать ENOMEM приоритет перед текстовыми эвристиками. Сопоставление текста оставить узким fallback для случаев без достаточных backend сигналов. Для каждого probe сохранить результат и диагностические данные до free; не интерпретировать все ARCHIVE_WARN как обычную недоступность без анализа операции.

Проверка: test-only stubs возвращают allocation/setup/free failures, unsupported configuration и RAR5 failure. Проверить код ошибки, неизменный mask sentinel и освобождение каждого handle. Повторить реальные round trips всех advertised creation formats, RAR read-only и неизвестные identifiers. Недоступная запись не создает output. Test stubs не доказывают свойства реальной libarchive: реальные проверки обязательны отдельно.

## 4. Упорядочить обработку ошибок очистки

Файлы: internal/support.h/.c, internal/writer.c, internal/unpack.c, internal/libarchive_adapter.c, arc.c, internal/platform_text_unix.c, internal/platform_text_windows.c.

Добавить небольшой private helper объединения primary result и secondary cleanup diagnostic без heap allocation. Если primary успешен, возвращать cleanup error; иначе сохранять primary code и начало его сообщения, добавляя ограниченную вторичную диагностику. Текст остается нестабильным API.

Применить к writer create failure, writer close/discard, extraction file close, обычному reader_release в verify/unpack и locale restoration. Все cleanup действия выполнять независимо от предыдущего сбоя. Не заменять первую cleanup error последующей ошибкой удаления; явно указывать невозможность удаления incomplete output.

Reader close и visitor failure требуют исключения: возвратить оригинальный result байт-в-байт в соответствии с контрактом. Разделить сбор cleanup outcome и выбор возвращаемого результата внутри текущих модулей; не добавлять публичный callback диагностики. Первую writer failure хранить независимо от дополнительного сообщения, формируемого при close.

Проверка: сочетания read/write error + close error, backend close + free error, discard cleanup + unlink error, locale restoration + operation error. Проверить primary code, видимую вторичную диагностику, NUL termination при truncation и полное освобождение handles. Отдельно exact-result проверки reader close и visitor-stop. Fault injection закрывает реальные ресурсы перед возвратом синтетического сбоя там, где это необходимо для корректного теста.

## 5. Заменить линейные индексы

Файлы: internal/archive_paths.h/.c, internal/platform_text.h и обе реализации, internal/writer.c, internal/unpack.c, internal/file_system.h и обе реализации при добавлении identity comparison.

Добавить private ordered name comparison, сохраняющий текущую семантику: strcmp на POSIX, CompareStringOrdinal с ignore-case на Windows. Equal-only API недостаточно для ordered index; не использовать Unicode lowercase hashing, если его равенство не доказано эквивалентным текущему сравнению.

Рекомендуемый индекс путей — типизированное сбалансированное дерево в существующем archive_paths модуле. Запись содержит owned normalized name, entry type и size_t номер записи владельца. Это общий владелец правил поиска/конфликта путей, а не generic void* collection. Writer/extraction metadata и lifecycle остаются в их собственных массивах.

Проверять exact duplicate, non-directory ancestors по component boundaries и существующих descendants для нового non-directory через ordered range lookup. Сохранить разрешение implicit directories, повторных согласованных extraction directory records и запрет повторных explicit writer additions. Обход records для metadata finalization остается в исходном порядке.

Writer identity map сделать отдельным типизированным индексом FsIdentity -> номер первого data-bearing record. Добавить native identity ordering helper вместо знания об устройстве key в writer. Synthetic entries не попадают в identity map. Выбранные aliases сохраняют текущий контракт TAR и отказ ZIP.

Использовать номера записей вместо указателей, которые realloc может инвалидировать. Указать владельца каждой строки и освобождать записи/узлы ровно один раз. При allocation failure writer становится Failed; extraction возвращает ошибку с возможным partial output.

Цель: exact lookup O(log N), path-conflict lookup O(D log N), identity lookup O(log N), где D — число компонентов. Полный проход не должен иметь скрытого попарного сравнения. Случайные и adversarial порядки имен не ухудшают дерево до линейной высоты.

Проверка: в temporary harness сравнить результаты нового индекса с независимым простым попарным oracle на малых сгенерированных наборах. Включить file/directory prefix conflicts, Unicode, разные регистры Windows, UTF-8 POSIX, implicit/explicit directories, selection byte-exact и source aliases across calls. Для 1k/10k/100k плоских записей и вложенных путей измерить время и число сравнений тестовой сборки. В CI ограничивать рост сравнений, не время машины. Память остается пропорциональной числу имен и их длине.

## 6. Убрать рекурсию разрешения ссылок

Файлы: internal/unpack.c: hard_link, resolve_symbolic, resolve_path; использовать индекс шага 5.

Hard links: итеративный обход цепочки с explicit heap stack и состояниями unseen/visiting/resolved. После достижения data-bearing file создавать aliases при обратном проходе. Сохранить metadata equality, archive-root-relative targets, forward links, missing/cyclic target classification и SELECTION_ERROR для absent/excluded dependency в selective mode. Нельзя связывать alias с pre-existing filesystem объектом.

Symlinks: стек кадров содержит номер link record, current base, оставшиеся компоненты target и continuation после вложенной ссылки. Возобновлять разбор после полного разрешения symlink, сохраняя смысл link/../file. visiting выявляет цикл, resolved memoization сохраняет конечный путь и kind. Не сворачивать .. лексически до разрешения ссылок. Сохраняются rejection existing redirections, external targets и Windows unknown target type.

Рост обоих стеков проверять на size overflow и allocation failure. Не вводить произвольную публичную квоту глубины. directory/parents также перевести на последовательный обход компонентов, чтобы обход extraction paths не оставлял собственную неограниченную рекурсию.

Проверка: forward/reverse chains из десятков тысяч коротких link records, длинный цикл, diamond/shared tails, mixed relative targets, link/../file, escape, dangling и missing/excluded dependencies. Проверять hard-link identity, symlink stored target text и классификацию ошибок. На POSIX запускать resolver harness при уменьшенном stack limit; он проверяет разрешение длинной symlink цепочки, не чтение файла ОС через всю цепочку. Windows filesystem/privilege failures проверять отдельно от успешного graph resolution, не скрывая произвольный сбой как отсутствие привилегий.

## 7. Интеграционная проверка и запись результатов

Файлы: justfile, docs/2026-10-13-arc-design.md, этот план. Существующая .github/workflows/ci.yml остается входом для трех платформ; обновлять только если новым проверкам действительно нужен отдельный вызов.

Команды:
- just format
- just check
- CC=clang ARC_SANITIZE=1 just verify с отдельным ARC_BUILD_DIR
- Отдельная compiler-проверка declarations/prototypes с -Wstrict-prototypes -Wmissing-prototypes и -Werror для production units.

Все новые harnesses/fixtures/binaries генерировать вне source tree. Проверить cleanup и allocation failures под ASan/UBSan, реальные CLI/API round trips, selection, sparse content, locale restoration, no-overwrite и existing-directory metadata. Проверить, что явный object list включает полный набор выбранных платформенных модулей и не содержит test stubs.

Проверки macOS не считать доказательством Linux/Windows. Записать фактическую версию libarchive, платформу, команды, результаты и замеры; cross-platform completion требует исполнения существующей CI matrix. В этой workspace нет .git, поэтому историю и удаленный CI статус нельзя выводить из наличия workflow файла.

## Порядок и критерий завершения

1 -> 2 -> 3 -> 4 -> 5 -> 6 -> 7. Правки документации финализируются после проверки реализации. Индекс нужен до масштабных chain fixtures, иначе проверки рекурсии упрутся в квадратичную стоимость поиска.

Завершение: каждый из шести замечаний закрыт соответствующей regression-проверкой, публичная совместимость сохранена, failures освобождают ресурсы, большие наборы не требуют попарного сканирования или рекурсивного C-stack обхода. Все заявленные результаты воспроизводимы на указанных платформах.

Главные риски: изменение Windows name equality при оптимизации, ошибочное возобновление symlink resolution после .., потеря исходного result при cleanup aggregation и изменение semantics относительных POSIX путей. Эти риски проверяются отдельными сценариями выше. Passwords, quotas, rollback, atomic publication и generic I/O не входят в работу.


## Результаты реализации

Все шесть замечаний исправлены. Реализация сохраняет публичные сигнатуры, структуры и значения enum/feature flags. Новых production-зависимостей и модулей нет: типизированный AVL индекс путей добавлен в archive_paths, identity AVL остается в writer, filesystem владеет anchoring/identity comparison, unpack использует heap stacks и последовательное создание родителей.

Проверки на macOS/libarchive 3.8.9:

- Исходный just check: 154 CLI/API проверки.
- Итоговый just check: 160 проверок, форматирование и сборка успешны.
- CC=clang ARC_SANITIZE=1 ARC_BUILD_DIR=/tmp/arc-hardening-sanitize just verify: 160 проверок без ASan/UBSan ошибок.
- Production syntax-only: -Wstrict-prototypes -Wmissing-prototypes -Werror, успешно.
- Path index: 6 275 733 сравнений для 100k плоских записей в обоих порядках; 9 348 056 для вложенных путей. Identity index: 3 137 892 для 100k записей в обоих порядках.
- Цепочки/циклы 20k ссылок и ошибки выделения памяти проходят. Обычный graph executable запускается со стеком 256 КБ.
- Возврат к старому относительному output в test-only filesystem варианте воспроизводит удаление sentinel в отдельной временной папке и ожидаемо ломает тот же regression-тест; исправленная сборка его проходит.

Уточнения плана: Python resource.setrlimit не смог установить малый стек в этой macOS среде; launcher использует работающий Bash ulimit. Исходные timings до изменений не снимались, вместо них используются независимый pairwise oracle и проверяемые границы числа сравнений. Счётчик 160 включает запуск каждого нового harness, а не каждое его внутреннее утверждение. Строгие объявления проверены отдельной командой, а не новым рецептом сборки. Linux/Windows здесь не запускались; новая native Windows проверка long Unicode/drive-relative/UNC путей выполняется существующей CI matrix.
