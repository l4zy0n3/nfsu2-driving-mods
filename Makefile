CC = gcc
MINGW = i686-w64-mingw32-gcc
CFLAGS = -std=c11 -O2 -Wall -Wextra -Werror
LDFLAGS = -shared -static-libgcc -Wl,--no-insert-timestamp,--disable-auto-image-base
OUT = build
TESTS = handbrake car_profile dualsense rumble steering_guard clutch_work manual coeff policy bindings
TEST_BINS = $(addprefix $(OUT)/test_,$(TESTS))
ASIS = $(OUT)/NFSU2.AnalogClutch.asi $(OUT)/NFSU2.RightStickLook.asi $(OUT)/NFSU2.MirrorPanels.asi

.PHONY: all test sanitize package verify
all: $(ASIS) $(OUT)/NFSU2.GearGrind.wav

$(OUT):
	mkdir -p $(OUT)

$(OUT)/test_%: src/clutch/test_%.c $(wildcard src/clutch/*.h) | $(OUT)
	$(CC) $(CFLAGS) $< -lm -o $@

$(OUT)/clutch-selftest: src/clutch/analog-clutch.c $(wildcard src/clutch/*.h) | $(OUT)
	$(CC) $(CFLAGS) -DSELF_TEST $< -lm -o $@

$(OUT)/look-selftest: src/camera/right-stick-look.c | $(OUT)
	$(CC) $(CFLAGS) -DSELF_TEST $< -lm -o $@

$(OUT)/mirror-selftest: src/camera/mirror-panels.c | $(OUT)
	$(CC) $(CFLAGS) -DSELF_TEST $< -lm -o $@

$(OUT)/NFSU2.AnalogClutch.asi: src/clutch/analog-clutch.c $(wildcard src/clutch/*.h src/common/*.h) Makefile | $(OUT)
	$(MINGW) $(CFLAGS) $(LDFLAGS) $< -ldinput8 -ldxguid -lole32 -luser32 -lwinmm -ladvapi32 -lm -o $@

$(OUT)/NFSU2.RightStickLook.asi: src/camera/right-stick-look.c $(wildcard src/common/*.h) Makefile | $(OUT)
	$(MINGW) $(CFLAGS) $(LDFLAGS) $< -luser32 -lm -o $@

$(OUT)/NFSU2.MirrorPanels.asi: src/camera/mirror-panels.c Makefile | $(OUT)
	$(MINGW) $(CFLAGS) $(LDFLAGS) $< -ld3d9 -ld3dx9 -ldxguid -luser32 -lm -o $@

$(OUT)/NFSU2.GearGrind.wav: tools/grind_sound.py | $(OUT)
	python3 $< $@

test: all $(TEST_BINS) $(OUT)/clutch-selftest $(OUT)/look-selftest $(OUT)/mirror-selftest
	@set -e; for test in $(TEST_BINS) $(OUT)/clutch-selftest $(OUT)/look-selftest $(OUT)/mirror-selftest; do ./$$test; done
	python3 -m unittest discover -s companion -p 'test_*.py'
	python3 -m unittest discover -s tests -p 'test_*.py'

sanitize: | $(OUT)
	@set -e; for src in src/clutch/test_*.c src/clutch/analog-clutch.c src/camera/*.c; do \
	  $(CC) -std=c11 -g -Wall -Wextra -Werror -DSELF_TEST -fsanitize=address,undefined $$src -lm -o $(OUT)/sanitized; \
	  ASAN_OPTIONS=detect_leaks=0 ./$(OUT)/sanitized; \
	done

package: all test
	python3 tools/package.py

verify: all test sanitize
