COMPOSE_FILE := infra/docker-compose.yml
COMPOSE ?= docker compose
PROJECT ?= qodeloc
CPP_SOURCES := $(shell find core tests testdata \( -type d -name build -prune \) -o -type f \( -name '*.cpp' -o -name '*.cc' -o -name '*.cxx' -o -name '*.hpp' -o -name '*.h' \) -print 2>/dev/null)
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
MODEL_INSTALLER ?= scripts/install-models.py
MODEL_NAMES := jina-code llama31-8b codestral2 qwen3-14b qwen3-30b-a3b
MODEL_TARGETS := $(addprefix install-models-,$(MODEL_NAMES))

.PHONY: up down logs reset status fmt lint build test release up-core down-core install-models-all $(MODEL_TARGETS)

up:
	$(COMPOSE) -f $(COMPOSE_FILE) -p $(PROJECT) up -d --wait --remove-orphans

down:
	$(COMPOSE) -f $(COMPOSE_FILE) -p $(PROJECT) down --remove-orphans

logs:
	$(COMPOSE) -f $(COMPOSE_FILE) -p $(PROJECT) logs -f --tail=200

reset:
	$(COMPOSE) -f $(COMPOSE_FILE) -p $(PROJECT) down -v --remove-orphans
	$(COMPOSE) -f $(COMPOSE_FILE) -p $(PROJECT) up -d --wait --remove-orphans

status:
	$(COMPOSE) -f $(COMPOSE_FILE) -p $(PROJECT) ps

fmt:
	@if [ -n "$(CPP_SOURCES)" ]; then \
		echo "$(CPP_SOURCES)" | tr ' ' '\n' | xargs $(CLANG_FORMAT) -i; \
	else \
		echo "No C++ sources found yet."; \
	fi

lint:
	@if [ -n "$(CPP_SOURCES)" ]; then \
		if [ -f $(CORE_BUILD_DIR)/compile_commands.json ]; then \
			set -- $(CPP_SOURCES); total=$$#; i=0; \
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
