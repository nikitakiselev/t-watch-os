# watchOS — сборка и прошивка для LilyGo T-Watch-2020 V3
#
#   make            — собрать и прошить (= make flash)
#   make build      — только собрать
#   make flash      — собрать и залить на часы
#   make fs         — залить ТОЛЬКО файлы из watchos/data/ в SPIFFS (станции и т.п.)
#   make sprites    — собрать sprites/N.png (16×16, ключ — салатовый) → sprites.png + sprites_gen.h
#   make vu         — конвертировать data/img/vu-meter.png → src/programs/vu_gen.h (RGB565)
#   make monitor    — серийный монитор (Serial, 115200)
#   make ports      — список подключённых плат/портов
#   make clean      — очистить кэш сборки arduino-cli
#
# Порт можно переопределить:  make flash PORT=/dev/cu.usbserial-XXXX

SKETCH := watchos
FQBN   := esp32:esp32:twatch
BAUD   := 115200
CLI    := arduino-cli

# Порт определяется автоматически (первый usbserial), но переопределяем при желании.
PORT ?= $(shell ls /dev/cu.usbserial-* 2>/dev/null | head -1)

# Инструменты и раздел SPIFFS (для make fs). Версии подхватываются по маске.
ESP32TOOLS  := $(HOME)/Library/Arduino15/packages/esp32/tools
MKSPIFFS    := $(firstword $(wildcard $(ESP32TOOLS)/mkspiffs/*/mkspiffs))
ESPTOOL     := $(firstword $(wildcard $(ESP32TOOLS)/esptool_py/*/esptool))
SPIFFS_OFF  := 0xc90000
SPIFFS_SIZE := 3538944          # 0x360000 (раздел spiffs в default_16MB)

.PHONY: all build flash upload fs monitor ports clean sprites vu font

all: flash

# Спрайты: sprites/N.png (16x16, ключ прозрачности — салатовый) -> sprites/sprites.png + sprites_gen.h
sprites:
	python3 tools/gen_sprites.py

# Подложка VU-метра: data/img/vu-meter.png (240×130) -> src/programs/vu_gen.h (RGB565)
vu:
	python3 tools/gen_vu.py

# Кириллический VLW-шрифт: TTF -> watchos/notesfont.h (для просмотрщика заметок).
font:
	python3 tools/gen_font.py

# Время RTC актуализируется через NTP (Clock → Sync), а не из времени сборки —
# поэтому build_time.h больше не генерируется при сборке.

build:
	$(CLI) compile --fqbn $(FQBN) ./$(SKETCH)

flash upload: build
	@test -n "$(PORT)" || { echo "Порт не найден. Укажи: make flash PORT=/dev/cu.usbserial-XXXX"; exit 1; }
	$(CLI) upload --fqbn $(FQBN) -p $(PORT) ./$(SKETCH)
	@echo "flashed -> $(PORT)  ($$(date '+%H:%M:%S'))"

# Залить только содержимое watchos/data/ в SPIFFS (быстрое обновление станций).
fs:
	@test -n "$(PORT)" || { echo "Порт не найден. Укажи: make fs PORT=/dev/cu.usbserial-XXXX"; exit 1; }
	@mkdir -p .build
	$(MKSPIFFS) -c $(SKETCH)/data -b 4096 -p 256 -s $(SPIFFS_SIZE) .build/spiffs.bin
	$(ESPTOOL) --chip esp32 --port $(PORT) --baud 460800 --before default_reset --after hard_reset write_flash -z $(SPIFFS_OFF) .build/spiffs.bin
	@echo "spiffs flashed -> $(PORT)"

monitor:
	@test -n "$(PORT)" || { echo "Порт не найден. Укажи: make monitor PORT=/dev/cu.usbserial-XXXX"; exit 1; }
	$(CLI) monitor -p $(PORT) -c baudrate=$(BAUD)

ports:
	$(CLI) board list

clean:
	$(CLI) cache clean
	@rm -f $(SKETCH)/build_time.h
	@echo "cleaned"
