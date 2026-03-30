COMPOSE_FILE := infra/docker-compose.yml
COMPOSE ?= docker compose
PROJECT ?= qodeloc
CPP_SOURCES := $(shell find core tests testdata -type f \( -name '*.cpp' -o -name '*.cc' -o -name '*.cxx' -o -name '*.hpp' -o -name '*.h' \) 2>/dev/null)
CLANG_FORMAT ?= clang-format
CLANG_TIDY ?= clang-tidy
CORE_BUILD_DIR ?= build/core
CORE_BUILD_TYPE ?= Debug
CORE_IMAGE ?= qodeloc/core:dev
CORE_COMPOSE_FILE ?= infra/core/docker-compose.yml

.PHONY: up down logs reset status fmt lint build test release up-core down-core

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
		if [ -f build/compile_commands.json ]; then \
			for file in $(CPP_SOURCES); do \
				$(CLANG_TIDY) "$$file" -p build; \
			done; \
		else \
			echo "Skipping clang-tidy because build/compile_commands.json is missing."; \
		fi; \
	else \
		echo "No C++ sources found yet."; \
	fi

build:
	@if [ -f core/CMakeLists.txt ]; then \
		cmake -S core -B $(CORE_BUILD_DIR) -G Ninja -DCMAKE_BUILD_TYPE=$(CORE_BUILD_TYPE); \
		cmake --build $(CORE_BUILD_DIR); \
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
