<p align="center">
  <img src="docs/banner.png" alt="MixLauncher" width="860">
</p>

<h1 align="center">MixLauncher</h1>

<p align="center">
  Быстрый лаунчер для Windows 10/11.
</p>

<p align="center">
  <img src="https://img.shields.io/badge/Windows-10%20%7C%2011-0b0b0d?style=flat-square" alt="Windows 10 | 11">
  <img src="https://img.shields.io/badge/размер-~550%20КБ-0b0b0d?style=flat-square" alt="~550 КБ">
  <img src="https://img.shields.io/badge/без%20сети%20и%20телеметрии-0b0b0d?style=flat-square" alt="Без сети и телеметрии">
</p>

## Возможности

- **Вместо Пуска.** Открывается по `Win` или любой другой клавише, даже поверх игр.
- **Приложения.** Всё из меню Пуск и Microsoft Store. Частые - выше.
- **Файлы.** Поиск по всему диску через встроенный [Everything](https://www.voidtools.com/). Если у вас свой Everything, используется он.
- **Не та раскладка.** `rfkr` найдёт Калькулятор, `сщву` - VS Code.
- **Английские имена.** `notepad`, `cmd`, `calc` находят Блокнот, Командную строку и Калькулятор.
- **Команды.** Блокировка, сон, перезагрузка, выключение, выход, корзина.
- **Кнопка Пуск.** Свой значок на кнопке Пуск Windows 11, клик открывает лаунчер.
- **Сейчас играет.** Обложка, название и кнопки управления любым плеером на панели задач.
- **Стили.** Четыре вида лаунчера: Стандарт, Raycast, Windows 11 и Компактный. Совпадения с запросом выделяются жирным.
- **Без ИИ, без сети, без телеметрии.**

## Установка

Скачайте со страницы [Releases](https://github.com/mel1x/mixlauncher/releases):

- `MixLauncher-Setup.exe` - установщик, Everything уже внутри.
- `MixLauncher-portable.exe` - без установки, для поиска файлов нужен установленный Everything.

## Горячие клавиши

| Клавиши | Действие |
|---|---|
| `Win` | открыть или закрыть |
| `↑` `↓`, `PgUp` `PgDn` | выбор |
| `Tab` | следующая секция |
| `Enter` | открыть |
| `Ctrl+Enter` | показать в папке |
| `Ctrl+Shift+Enter` | от имени администратора |
| `Alt+Enter` | свойства |
| `Ctrl+C` | копировать путь |
| `Esc` | очистить запрос, повторно - закрыть |
| `Ctrl+,` | настройки |

## Настройки

`Ctrl+,`, кнопка в углу лаунчера или меню значка в трее. Хранятся в `%LOCALAPPDATA%\MixLauncher\config.ini`.

## Сборка

Нужен MinGW-w64 (MSYS2) или Visual Studio Build Tools, для установщика - [Inno Setup 7](https://jrsoftware.org/isdl.php).

- `build.bat` - `build\mixlauncher.exe`; `build.bat dev` - без запроса прав администратора.
- `release.bat` - лаунчер и `build\MixLauncher-Setup.exe`. Версия берётся из `res\mixlauncher.rc`.
