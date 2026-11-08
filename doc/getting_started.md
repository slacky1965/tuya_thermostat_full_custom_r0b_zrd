# Термостат: железо и прошивка

### Данный проект подходит к термостатам.

- С оригинальной прошивкой они определяются в **zigbee2mqtt** так
	- "_TZE204_8byfmxdv"
	- "_TZE204_jrtvxwvy"
- Внешний вид
	- <img src="images/model0b.png"/>
- Плата
	- Смотри в разделе [Железо](#1-железо)
	
## Содержание

- [1. Железо](#1-железо)
- [2. Сборка проекта](#2-сборка-проекта)
- [3. Как обновить](#3-как-обновить)
  - [Этап 1. OTA-обновление ZTU](#этап-1-ota-обновление-zt3l)
  - [Этап 2. Прошивка CA51F253L2](#этап-2-прошивка-ca51f253l2)

---

## 1. Железо

### Схема термостата

Схема слаботочной части взята вот [отсюда](https://blog.softdev.online/index.php?controller=post&action=view&id_post=64).

<div align="center">
<img src="../doc/images/schematic_low.jpg" alt="Схема слаботочной части термостата" width="45%" height="auto" />
<p align="center"><em>Схема слаботочной части термостата</em></p>
</div>

### Слаботочная плата термостата с экраном

<div align="center">
<img src="../doc/images/board_top.jpg" alt="Слаботочная плата, вид сверху" width="20%" height="auto"> <img src="../doc/images/board_bottom.jpg" alt="Слаботочная плата, вид снизу" width="20%" height="auto" />
<p align="center"><em>Слаботочная плата: вид сверху и вид снизу</em></p>
</div>

---

## 2. Сборка проекта

### Инструменты

Для сборки проекта под Windows нужно скачать несколько инструментов.

| Инструмент | Обязательность |
| --- | --- |
| [clang](https://github.com/devbis/llvm-mcs51/releases) | обязательно |
| [SDCC](https://sdcc.sourceforge.net/snap.php) | опционально, по умолчанию сборка для **CA51F253L2** сделана на clang |
| [Telink IoT Studio](https://doc.telink-semi.cn/tools/telink_iot_studio/TelinkIoTStudio_V2025.2.zip) | обязательно |
| [Git for Windows](https://git-scm.com/install/windows?ref=faun) | рекомендуется |

В системе должен быть инсталлирован **python**.

Если нужно только прошить **CA51F253L2**, то **SDCC** и **Telink IoT Studio** можно не скачивать.

### Сборка

После установки всех компонентов запускаем **Git Bash** и переходим в папку проекта. Далее выполняем

```text
$ make bootloader clean all
```

Проверяем вывод компилятора. Должно собраться три основных файла - **bootloader** для **TLSR8258**, и две **firmware** для **TLSR8258** и **CA51F253L2**. Попутно еще создаются образы **ОТА**. Все будет лежать в директории **bin**.

---

## 3. Как обновить

Сразу скажу, обновить **CA51F253L2** по ОТА из оригинальной прошивки в кастомную нельзя. В протоколе Tuya есть такой механизм, но проектировщики этого термостата не реализовали такую возможность. Но с другой стороны, для перепрошивки **CA51F253L2** не нужен какой-то отдельный программатор. Достаточно обычного **USB-UART** адаптера.

Итак. Обновление будет происходить в два этапа. Сначала по **ОТА** обновляем zigbee-модуль ZTU. А затем мы уже прошьем проводами **CA51F253L2**.

### Этап 1. OTA-обновление ZTU

#### Шаг 1. Добавление оригинального термостата в zigbee2mqtt

У вас должно получиться что-то типа вот такого

<div align="center">
<img src="../doc/images/z2m_about_orig.jpg" alt="Оригинальный термостат в zigbee2mqtt"/>
<p align="center"><em>Оригинальный термостат в zigbee2mqtt</em></p>
</div>

#### Шаг 2. Загрузка OTA-образа и запуск обновления

Далее жмем на синее облачко и выбираем **custom firmware** и добавляем **ОТА** образ для обновления - [1141-d3a3-1111114b-osrctherm_r0b_zrd_ZT3L.zigbee](https://github.com/slacky1965/tuya_thermostat_full_custom_r0b_zrd/raw/refs/heads/main/bin/1141-d3a3-1111114b-osrctherm_r0b_zrd_ZT3L.zigbee) и жмем обновить.

<div align="center">
<img src="../doc/images/z2m_update_ztu_orig.jpg" alt="Добавление OTA-образа в zigbee2mqtt"/>
<p align="center"><em>Добавление OTA-образа в zigbee2mqtt</em></p>
</div>

#### Шаг 3. Проверка лога и результата

Далее в логе можно наблюдать отчеты об обновлении.

<div align="center">
<img src="../doc/images/z2m_ota_update_log1.jpg" alt="Лог: обновление началось"/>
<p align="center"><em>Лог: обновление началось</em></p>
</div>

В процессе обновления в логе также можно увидеть информацию о кол-ве оставшихся секунд и процент загрузки.

<div align="center">
<img src="../doc/images/z2m_ota_update_log2.jpg" alt="Лог: оставшееся время и процент загрузки"/>
<p align="center"><em>Лог: оставшееся время и процент загрузки</em></p>
</div>

Также в логе можно наблюдать окончание обновления.

<div align="center">
<img src="../doc/images/z2m_ota_update_log3.jpg" alt="Лог: обновление завершено"/>
<p align="center"><em>Лог: обновление завершено</em></p>
</div>

После успешного обновления нужно разрешить добавление новых устройств в zigbee2mqtt и термостат сам подключится к сети уже с новым именем и адресом. Старую версию термостата принудительно удаляем. У вас должно получиться что-то в этом роде.

<div align="center">
<img src="../doc/images/z2m_about.jpg" alt="Термостат после обновления в zigbee2mqtt"/>
<p align="center"><em>Термостат после обновления в zigbee2mqtt</em></p>
</div>

### Этап 2. Прошивка CA51F253L2

Для этого нужно обесточить термостат.

> [!WARNING]
> Внимание!!! При отсоединенной слаботочной плате нельзя подавать питание на силовую часть термостата!!!

Нужно аккуратно тонкой шлитцевой отвертой вынуть экран из общего корпуса (он на разъеме). Затем открутить четыре маленьких винтика по углам и вынуть из корпуса слаботочную плату с экраном. На ней нужно найти место с нераспаянным разъемом без маркировки. Далее подключаем к **USB-UART** адаптеру согласно надписям на фото ниже - контакт **RX** на плате нужно подключать к **RX** адаптера, контакт **TX** на плате - к **TX** адаптера. Также подключаем **GND**, а вот **3v3** пока не подключаем.

<div align="center">
<img src="../doc/images/board_uart.jpg" alt="Подключение USB-UART к разъему программирования"/>
<p align="center"><em>Подключение <b>USB-UART</b> к разъему программирования</em></p>
</div>

Предварительно проверяем, на каком порту сидит **USB-UART** адаптер и редактируем в корне проекта в **makefile** параметр **CA51F2_PORT** на тот, который у вас.

<div align="center">
<img src="../doc/images/makefile_port_ca51f2.jpg" alt="Параметр CA51F2_PORT в makefile"/>
<p align="center"><em>Параметр <b>CA51F2_PORT</b> в <b>makefile</b></em></p>
</div>

Далее заходим в **Git Bash**, переходим в директорию проекта. И даем такую команду.

```text
$ make flash-cachip
```

> [!NOTE]
> Запускается небольшая программа на **python**'е, которая и прошивает **CA51F253L2**. Программа немного модифицирована под себя. Оригинал - [https://github.com/MrdUkk/cachip-ispuartflasher](https://github.com/MrdUkk/cachip-ispuartflasher)

Тут есть два варианта.

1. Чип прошьется.
2. Программа напишет, что она ожидает 15 секунд для сброса (**reset**) или подачи питания и будет выводить счетчик секунд.

Если у нас второй вариант, то в эти 15 секунд мы подаем питание **3v3** (мы же помним, что его не подавали). В конце концов должно получиться следующее.

<details>
<summary>Пример вывода команды <b>make flash-cachip</b></summary>

```text
py ./tools/cachip-isp-serial_new.py COM4 upload ../bin/osrctherm_r0b_v1.01_CA51F253L2.bin
using embedded phase1 bootloader (642 bytes)
opening serial port COM4
trying ISP-mode activation, waiting up to 15 seconds (if power is already applied, pulse RESET pin 2 low->high !)
still waiting for the MCU (1s of 15s) - apply power or pulse RESET now
still waiting for the MCU (2s of 15s) - apply power or pulse RESET now
MCU accepted connection
sending some magic cmd
got mcu success response
switching port speed
sending next magic cmd
got mcu response, asserting values
valid
sending our bootloader
got mcu success response
got mcu success response
got mcu success response
got mcu success response
got mcu success response
got mcu success response
reading hidden 0x2B area to identify the chip
chip identified: CA51F253L2, flash size 32768 bytes
requesting verify chipID for next phases
got mcu success response
requesting access to flash with designated chipID
got mcu success response
requesting onchip flash memory partial 243 sectors erase
got mcu success response
uploading from ../bin/osrctherm_r0b_v1.01_CA51F253L2.bin to onchip memory
written 31351/31351 bytes (100%)
upload done: 31351 bytes written to onchip flash
sending session end cmd
got mcu success response
end, remove power from MCU
```

</details>

Все. На этом можно термостат собирать обратно.
