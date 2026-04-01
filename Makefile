COMPOSE_FILE := infra/docker-compose.yml
COMPOSE ?= docker compose
PROJECT ?= qodeloc
-include .env
COMPOSE_ENV_FILE ?= .env
FMT_SOURCES := $(shell find core testdata \( -type d \( -name build -o -name repos \) -prune \) -o -type f \( -name '*.cpp' -o -name '*.cc' -o -name '*.cxx' -o -name '*.hpp' -o -name '*.h' \) -print 2>/dev/null)
LINT_SOURCES := $(shell find core \( -type d \( -name build -o -name tests -o -name repos \) -prune \) -o -type f \( -name '*.cpp' -o -name '*.cc' -o -name '*.cxx' -o -name '*.hpp' -o -name '*.h' \) -print 2>/dev/null)
CLANG_FORMAT ?= clang-format
CLANG_TIDY ?= clang-tidy
CLANG_TIDY_QUIET ?= --quiet
PYTHON ?= python3
CORE_BUILD_PRESET ?= debug
CORE_BUILD_DIR ?= core/build/$(CORE_BUILD_PRESET)
CORE_BUILD_TYPE ?= Debug
CORE_CPPSTD ?= 23
CORE_IMAGE ?= qodeloc/core:dev
CORE_COMPOSE_FILE ?= infra/core/docker-compose.yml
TESTDATA_REPO_NAME ?= fmt
TESTDATA_REPO_URL ?= https://github.com/fmtlib/fmt.git
TESTDATA_REPO_REF ?= master
TESTDATA_REPO_DIR ?= testdata/repos/$(TESTDATA_REPO_NAME)
TESTDATA_REPO_DEPTH ?= 1
TESTDATA_REPO_FETCHER ?= scripts/fetch-testdata-repo.py
QODELOC_LLAMA_MODEL_DIR ?= models/downloads/llama31-8b
QODELOC_LLAMA_MODEL_FILE ?= Meta-Llama-3.1-8B-Instruct-Q4_K_M.gguf
LLAMA_MODEL_DIR := $(QODELOC_LLAMA_MODEL_DIR)
LLAMA_MODEL_FILE := $(LLAMA_MODEL_DIR)/$(QODELOC_LLAMA_MODEL_FILE)
MODEL_INSTALLER ?= scripts/install-models.py
MODEL_NAMES := jina-code llama31-8b codestral2 qwen3-14b qwen3-30b-a3b
MODEL_TARGETS := $(addprefix install-models-,$(MODEL_NAMES))

.PHONY: up down logs reset status fmt lint build test release up-core down-core \
	install-models-all download-testdata-repo download-testdata-fmt $(MODEL_TARGETS)

up:
	@if [ ! -f "$(LLAMA_MODEL_FILE)" ]; then \
		echo "Missing $(LLAMA_MODEL_FILE). Run make install-models-llama31-8b first."; \
		exit 1; \
	fi
	$(COMPOSE) --env-file $(COMPOSE_ENV_FILE) -f $(COMPOSE_FILE) -p $(PROJECT) up -d --wait --remove-orphans

down:
	$(COMPOSE) --env-file $(COMPOSE_ENV_FILE) -f $(COMPOSE_FILE) -p $(PROJECT) down --remove-orphans

logs:
	$(COMPOSE) --env-file $(COMPOSE_ENV_FILE) -f $(COMPOSE_FILE) -p $(PROJECT) logs -f --tail=200

reset:
	$(COMPOSE) --env-file $(COMPOSE_ENV_FILE) -f $(COMPOSE_FILE) -p $(PROJECT) down -v --remove-orphans
	@if [ ! -f "$(LLAMA_MODEL_FILE)" ]; then \
		echo "Missing $(LLAMA_MODEL_FILE). Run make install-models-llama31-8b first."; \
		exit 1; \
	fi
	$(COMPOSE) --env-file $(COMPOSE_ENV_FILE) -f $(COMPOSE_FILE) -p $(PROJECT) up -d --wait --remove-orphans

status:
	$(COMPOSE) --env-file $(COMPOSE_ENV_FILE) -f $(COMPOSE_FILE) -p $(PROJECT) ps

fmt:
	@if [ -n "$(FMT_SOURCES)" ]; then \
		echo "$(FMT_SOURCES)" | tr ' ' '\n' | xargs $(CLANG_FORMAT) -i; \
	else \
		echo "No C++ sources found yet."; \
	fi

lint:
	@if [ -n "$(LINT_SOURCES)" ]; then \
		if [ -f $(CORE_BUILD_DIR)/compile_commands.json ]; then \
			set -- $(LINT_SOURCES); total=$$#; i=0; \
			for file in "$$@"; do \
				i=$$((i + 1)); \
				printf '[%d/%d] %s\n' "$$i" "$$total" "$$file"; \
				tmp_file=$$(mktemp); \
				$(CLANG_TIDY) $(CLANG_TIDY_QUIET) "$$file" -p $(CORE_BUILD_DIR) > /dev/null 2>"$$tmp_file"; \
				status=$$?; \
				awk 'index($$0, "warnings generated.") == 0 && index($$0, "Suppressed ") == 0' "$$tmp_file" >&2; \
				rm -f "$$tmp_file"; \
				if [ $$status -ne 0 ]; then \
					exit $$status; \
				fi; \
			done; \
		else \
			echo "Skipping clang-tidy because $(CORE_BUILD_DIR)/compile_commands.json is missing."; \
		fi; \
	else \
		echo "No C++ sources found yet."; \
	fi

download-testdata-repo:
	$(PYTHON) $(TESTDATA_REPO_FETCHER) --url "$(TESTDATA_REPO_URL)" --ref "$(TESTDATA_REPO_REF)" \
		--dest "$(TESTDATA_REPO_DIR)" --depth "$(TESTDATA_REPO_DEPTH)"

download-testdata-fmt: TESTDATA_REPO_NAME = fmt
download-testdata-fmt: TESTDATA_REPO_URL = https://github.com/fmtlib/fmt.git
download-testdata-fmt: TESTDATA_REPO_REF = master
download-testdata-fmt: TESTDATA_REPO_DIR = testdata/repos/fmt
download-testdata-fmt: download-testdata-repo

build:
	@if [ -f core/CMakeLists.txt ]; then \
		conan install core -of $(CORE_BUILD_DIR) -s build_type=$(CORE_BUILD_TYPE) -s compiler.cppstd=$(CORE_CPPSTD) -g CMakeDeps -g CMakeToolchain --build=missing; \
		(cd core && cmake --preset $(CORE_BUILD_PRESET)); \
		(cd core && cmake --build --preset $(CORE_BUILD_PRESET)); \
	else \
		echo "Core scaffold is not ready yet. Step 1.1 will add core/CMakeLists.txt."; \
	fi

test:
	@if [ -d $(CORE_BUILD_DIR) ] && [ -f $(CORE_BUILD_DIR)/CTestTestfile.cmake ]; then \
		ctest --test-dir $(CORE_BUILD_DIR) --output-on-failure; \
	else \
		echo "Core tests are not wired yet. Build the core project first."; \
	fi

release:
	@if [ -f core/Dockerfile ]; then \
		docker build -t $(CORE_IMAGE) -f core/Dockerfile core; \
	else \
		echo "Core Docker image is not ready yet. Step 1.1 will add core/Dockerfile."; \
	fi

up-core:
	@if [ -f $(CORE_COMPOSE_FILE) ]; then \
		$(COMPOSE) -f $(CORE_COMPOSE_FILE) -p $(PROJECT)-core up -d --wait --remove-orphans; \
	else \
		echo "Core compose stack is not ready yet. Step 1.1 will add $(CORE_COMPOSE_FILE)."; \
	fi

down-core:
	@if [ -f $(CORE_COMPOSE_FILE) ]; then \
		$(COMPOSE) -f $(CORE_COMPOSE_FILE) -p $(PROJECT)-core down --remove-orphans; \
	else \
		echo "Core compose stack is not ready yet."; \
	fi

install-models-all:
	$(PYTHON) $(MODEL_INSTALLER) all

define INSTALL_MODEL_TARGET
install-models-$(1):
	$(PYTHON) $(MODEL_INSTALLER) $(1)
endef

$(foreach model,$(MODEL_NAMES),$(eval $(call INSTALL_MODEL_TARGET,$(model))))
