# Shell RC — everything runs in Docker by default (devkitPro image for the
# Switch app, python image for the Home Assistant tests).
#   make                      = make test nx
#   make nx                   build nx/shellrc.nro
#   make deploy SWITCH_IP=…   send it to hbmenu's netloader (Y); its log streams back here
#   make deploy-ha HA=…       copy custom_components/shell_rc to HA OS over SSH, restart HA
#   make test                 C core tests (gcc) + Home Assistant integration tests
#   make test-c | test-ha     only one of them
#   make ui-preview           render the Switch screens to PNG (host: gcc, python3, node+playwright)
#   make release              dist/shellrc.nro + dist/shell_rc-ha.zip
# DOCKER=0 runs the same commands on the host. Research tools: research/README.md.

DEVKIT    ?= devkitpro/devkita64:20260219
PYIMAGE   ?= python:3.13.17-slim-bookworm
DOCKER    ?= 1
SWITCH_IP ?=
HA        ?=
HA_USER   ?= root
APP_NRO   := nx/shellrc.nro
PREVIEW   ?= build-host/ui-preview

ifeq ($(DOCKER),1)
  RUN_DKP = docker run --rm -t -v "$(CURDIR)":/work -w /work --user $$(id -u):$$(id -g) $(DEVKIT)
  # nxlink talks both ways (UDP discovery + TCP log server): host network.
  RUN_NET = docker run --rm -it -v "$(CURDIR)":/work -w /work --network host $(DEVKIT)
  RUN_PY  = docker run --rm -t -v "$(CURDIR)":/work -w /work -v shellrc-pip-cache:/root/.cache/pip $(PYIMAGE)
  PIP     = pip install -q -r tests-ha/requirements.txt &&
else
  RUN_DKP =
  RUN_NET =
  RUN_PY  =
  PIP     =
endif

.PHONY: all nx deploy deploy-ha test test-c test-ha ui-preview release clean help
all: test nx

help:
	@sed -n '2,13p' Makefile

# --- Switch app ---------------------------------------------------------------

nx:
	$(RUN_DKP) make -C nx
	@ls -lh $(APP_NRO)

deploy: nx
	@echo ""
	@echo "Na Switchu: przytrzymaj R przy starcie gry (tryb aplikacji), w hbmenu nacisnij Y."
	@echo "Aplikacja zapisze sie na karcie jako switch/shellrc/shellrc.nro."
	@echo ""
	$(RUN_NET) nxlink $(if $(SWITCH_IP),-a $(SWITCH_IP)) -p switch/shellrc/shellrc.nro -s $(APP_NRO)

# --- Home Assistant -------------------------------------------------------------
# Needs the "Terminal & SSH" add-on with your key (or a password), port 22.

deploy-ha:
	@test -n "$(HA)" || { echo "Podaj adres HA: make deploy-ha HA=192.168.x.y"; exit 1; }
	ssh $(HA_USER)@$(HA) 'mkdir -p /config/custom_components'
	scp -r custom_components/shell_rc $(HA_USER)@$(HA):/config/custom_components/
	ssh $(HA_USER)@$(HA) 'rm -rf /config/custom_components/shell_rc/__pycache__; ha core restart'
	@echo "Gotowe. HA restartuje sie (ok. 1 min)."

# --- tests ----------------------------------------------------------------------

test: test-c test-ha

test-c:
	$(RUN_DKP) sh -c 'mkdir -p build-host && gcc -std=c11 -O2 -Wall -Wextra -Werror \
		-fsanitize=address,undefined -o build-host/test_core tests-c/test_core.c core/*.c -lm \
		&& ./build-host/test_core'

test-ha:
	$(RUN_PY) sh -c '$(PIP) python3 -m pytest -c tests-ha/pytest.ini --rootdir tests-ha tests-ha -q -p no:cacheprovider'

# --- UI preview (host tools) ------------------------------------------------------

ui-preview:
	mkdir -p $(PREVIEW)
	gcc -std=c11 -Wall -Wextra -Werror -o $(PREVIEW)/preview tools/ui-preview/preview.c
	$(PREVIEW)/preview $(PREVIEW)
	python3 tools/ui-preview/render.py $(PREVIEW)
	NODE_PATH=$$(npm root -g) node tools/ui-preview/shoot.cjs $(PREVIEW)

# --- release ------------------------------------------------------------------------

release: nx
	mkdir -p dist
	# committed files only (no __pycache__), shell_rc/ at the zip root
	git archive --format=zip -o dist/shell_rc-ha.zip HEAD:custom_components shell_rc
	cp $(APP_NRO) dist/shellrc.nro
	@ls -lh dist

clean:
	rm -rf build-host dist
	$(RUN_DKP) make -C nx clean
