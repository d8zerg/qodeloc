COMPOSE_FILE := infra/docker-compose.yml
COMPOSE ?= docker compose
PROJECT ?= qodeloc
NPM ?= npm
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
MCP_IMAGE ?= qodeloc/mcp-adapter:dev
CORE_COMPOSE_FILE ?= infra/core/docker-compose.yml
MCP_DIR ?= mcp-adapter
MCP_CONFIG ?= $(MCP_DIR)/config.json
MCP_CONFIG_ENV ?= QODELOC_MCP_CONFIG=$(MCP_CONFIG)
TESTDATA_REPO_NAME ?= fmt
TESTDATA_REPO_URL ?= https://github.com/fmtlib/fmt.git
TESTDATA_REPO_REF ?= master
TESTDATA_REPO_DIR ?= testdata/repos/$(TESTDATA_REPO_NAME)
TESTDATA_REPO_DEPTH ?= 1
TESTDATA_REPO_FETCHER ?= scripts/fetch-testdata-repo.py
E2E_REPO_NAME ?= catch2
E2E_REPO_URL ?= https://github.com/catchorg/Catch2.git
E2E_REPO_REF ?= v3.13.0
E2E_REPO_DIR ?= tests/e2e/fixtures/repos/$(E2E_REPO_NAME)
E2E_REPO_DEPTH ?= 1
E2E_REPO_FETCHER ?= scripts/fetch-testdata-repo.py
E2E_RUNNER ?= tests/e2e/run_e2e.py
LITELLM_LOAD_TEST ?= tests/e2e/load_litellm.py
E2E_LOAD_WORKERS ?= 3
E2E_LOAD_DURATION_SECONDS ?= 120
E2E_LOAD_MIN_INTERVAL_SECONDS ?= 5
E2E_LOAD_MAX_INTERVAL_SECONDS ?= 10
E2E_LOAD_MAX_TOKENS ?= 32
QODELOC_LLAMA_MODEL_DIR ?= models/downloads/llama31-8b
QODELOC_LLAMA_MODEL_FILE ?= Meta-Llama-3.1-8B-Instruct-Q4_K_M.gguf
LLAMA_MODEL_DIR := $(QODELOC_LLAMA_MODEL_DIR)
LLAMA_MODEL_FILE := $(LLAMA_MODEL_DIR)/$(QODELOC_LLAMA_MODEL_FILE)
JINA_MODEL_DIR ?= models/downloads/jina-code
MODEL_INSTALLER ?= scripts/install-models.py
MODEL_NAMES := jina-code llama31-8b codestral2 qwen3-14b qwen3-30b-a3b
MODEL_TARGETS := $(addprefix install-models-,$(MODEL_NAMES))

.PHONY: up down logs reset status fmt lint build test release up-core down-core \
	install-models-all download-testdata-repo download-testdata-fmt \
	download-e2e-fixture download-e2e-catch2 $(MODEL_TARGETS) \
	mcp-install mcp-build mcp-test mcp-lint mcp-dev mcp-bundle mcp-start \
	e2e-test e2e-load-test

up:
	@if [ ! -d "$(JINA_MODEL_DIR)" ]; then \
		echo "Missing $(JINA_MODEL_DIR). Run make install-models-jina-code first."; \
		exit 1; \
	fi
	@if [ ! -f "$(LLAMA_MODEL_FILE)" ]; then \
		echo "Missing $(LLAMA_MODEL_FILE). Run make install-models-llama31-8b first."; \
		exit 1; \
	fi
	$(COMPOSE) --env-file $(COMPOSE_ENV_FILE) -f $(COMPOSE_FILE) -p $(PROJECT) up -d --build --wait --remove-orphans

down:
	$(COMPOSE) --env-file $(COMPOSE_ENV_FILE) -f $(COMPOSE_FILE) -p $(PROJECT) down --remove-orphans

logs:
	$(COMPOSE) --env-file $(COMPOSE_ENV_FILE) -f $(COMPOSE_FILE) -p $(PROJECT) logs -f --tail=200

reset:
	$(COMPOSE) --env-file $(COMPOSE_ENV_FILE) -f $(COMPOSE_FILE) -p $(PROJECT) down -v --remove-orphans
	@if [ ! -d "$(JINA_MODEL_DIR)" ]; then \
		echo "Missing $(JINA_MODEL_DIR). Run make install-models-jina-code first."; \
		exit 1; \
	fi
	@if [ ! -f "$(LLAMA_MODEL_FILE)" ]; then \
		echo "Missing $(LLAMA_MODEL_FILE). Run make install-models-llama31-8b first."; \
		exit 1; \
	fi
	$(COMPOSE) --env-file $(COMPOSE_ENV_FILE) -f $(COMPOSE_FILE) -p $(PROJECT) up -d --build --wait --remove-orphans

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

download-e2e-fixture:
	$(PYTHON) $(E2E_REPO_FETCHER) --url "$(E2E_REPO_URL)" --ref "$(E2E_REPO_REF)" \
		--dest "$(E2E_REPO_DIR)" --depth "$(E2E_REPO_DEPTH)"

download-e2e-catch2: E2E_REPO_NAME = catch2
download-e2e-catch2: E2E_REPO_URL = https://github.com/catchorg/Catch2.git
download-e2e-catch2: E2E_REPO_REF = v3.13.0
download-e2e-catch2: E2E_REPO_DIR = tests/e2e/fixtures/repos/catch2
download-e2e-catch2: download-e2e-fixture

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
		docker build -t $(CORE_IMAGE) -f core/Dockerfile .; \
	else \
		echo "Core Docker image is not ready yet. Step 1.1 will add core/Dockerfile."; \
	fi

mcp-release:
	@if [ -f mcp-adapter/Dockerfile ]; then \
		docker build -t $(MCP_IMAGE) -f mcp-adapter/Dockerfile .; \
	else \
		echo "MCP Adapter Docker image is not ready yet. Step 3.1 will add mcp-adapter/Dockerfile."; \
	fi

mcp-install:
	$(NPM) --prefix $(MCP_DIR) install

mcp-build:
	$(NPM) --prefix $(MCP_DIR) run build

mcp-test:
	$(MCP_CONFIG_ENV) $(NPM) --prefix $(MCP_DIR) test

mcp-lint:
	$(NPM) --prefix $(MCP_DIR) run lint

mcp-dev:
	$(MCP_CONFIG_ENV) $(NPM) --prefix $(MCP_DIR) run dev

mcp-bundle:
	$(NPM) --prefix $(MCP_DIR) run bundle

mcp-start:
	$(MCP_CONFIG_ENV) $(NPM) --prefix $(MCP_DIR) start

up-core:
	$(COMPOSE) --env-file $(COMPOSE_ENV_FILE) -f $(COMPOSE_FILE) -p $(PROJECT) up -d --build --wait --remove-orphans

down-core:
	$(COMPOSE) --env-file $(COMPOSE_ENV_FILE) -f $(COMPOSE_FILE) -p $(PROJECT) down --remove-orphans

e2e-test:
	$(PYTHON) $(E2E_RUNNER)

e2e-load-test:
	$(PYTHON) $(LITELLM_LOAD_TEST) --workers $(E2E_LOAD_WORKERS) \
		--duration-seconds $(E2E_LOAD_DURATION_SECONDS) \
		--min-interval-seconds $(E2E_LOAD_MIN_INTERVAL_SECONDS) \
		--max-interval-seconds $(E2E_LOAD_MAX_INTERVAL_SECONDS) \
		--max-tokens $(E2E_LOAD_MAX_TOKENS)

install-models-all:
	$(PYTHON) $(MODEL_INSTALLER) all

define INSTALL_MODEL_TARGET
install-models-$(1):
	$(PYTHON) $(MODEL_INSTALLER) $(1)
endef

$(foreach model,$(MODEL_NAMES),$(eval $(call INSTALL_MODEL_TARGET,$(model))))
