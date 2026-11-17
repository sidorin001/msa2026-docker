# Лицензии

Исходники проекта распространяются под GPL-3.0-only (LICENSE). Выбор обусловлен использованием Qt HTTP Server, доступного в открытой поставке Qt 6.4 под GPLv3; коммерческая лицензия Qt здесь не используется.

- Qt Core, Network, SQL, Concurrent: LGPLv3/GPLv3; Qt HTTP Server: GPLv3. Библиотеки подключены динамически. Copyright и тексты лицензий пакетов Debian находятся в `/usr/share/doc/libqt6*/copyright`; LICENSE проекта включён в runtime. Исходники соответствующих версий доступны через Debian source packages и https://code.qt.io/qt/.
- PostgreSQL/libpq: PostgreSQL License и лицензии зависимостей Debian.
- Python: PSF License. FastAPI, Starlette, Pydantic, Uvicorn и остальные Python-зависимости сохраняют свои лицензии в metadata установленного virtualenv. Psycopg: LGPLv3; psycopg-binary включает license notices своих зависимостей.
- Caddy: Apache-2.0; pgAdmin: PostgreSQL License. Образы сохраняют лицензии поставщиков.

Версии Python-зависимостей закреплены в `service2/requirements.txt`. Системные версии Qt/libpq управляются Debian bookworm; сборка сохраняет license notices установленных пакетов. Этот перечень не заменяет полные тексты лицензий зависимостей.
