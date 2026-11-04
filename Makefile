# =============================================================================
#  OpenMP scheduling-algorithm selection: student reproduction
#  Run `make help` for the list of targets.
# =============================================================================
SYSTEM  ?=
PROFILE ?= full
ARGS    ?=

CXX      ?= c++
CXXFLAGS ?= -O2 -g
CXXFLAGS += -std=c++17 -Wall -Wextra -pthread -Iruntime/include -Iruntime/src
LDFLAGS  += -pthread

BUILD   := build
LIB_SRC := $(filter-out runtime/src/main.cpp,$(wildcard runtime/src/*.cpp runtime/src/apps/*.cpp))
LIB_OBJ := $(patsubst runtime/%.cpp,$(BUILD)/%.o,$(LIB_SRC))
HEADERS := $(wildcard runtime/include/lb/*.h runtime/src/*.h)

PY := .venv/bin/python

.PHONY: help build test setup smoke campaign trace aggregate plots check clean require-system

help:
	@echo "One-time:"
	@echo "  make setup                    create .venv and install Python deps"
	@echo "  make build                    compile build/lb_bench and build/lb_tests"
	@echo "Develop:"
	@echo "  make test                     correctness checks (PASS / TODO / FAIL)"
	@echo "Experiments (run on YOUR machine, SYSTEM = your machine label):"
	@echo "  make smoke    SYSTEM=alice-m2 tiny 1-minute campaign to verify everything works"
	@echo "  make campaign SYSTEM=alice-m2 full campaign (resumable; re-run to continue)"
	@echo "  make trace    SYSTEM=alice-m2 chunk traces for Fig. 1 / Fig. 2"
	@echo "  make check    SYSTEM=alice-m2 completeness report of your results"
	@echo "Analysis (anyone, after pulling everybody's results):"
	@echo "  make aggregate                build results/processed/*.csv"
	@echo "  make plots                    build figures/ours/*.png"
	@echo "Pass extra flags with ARGS=\"...\", e.g. ARGS=\"--only fixed --apps tc\""

build: $(BUILD)/lb_bench $(BUILD)/lb_tests

$(BUILD)/%.o: runtime/%.cpp $(HEADERS)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/lb_bench: $(LIB_OBJ) $(BUILD)/src/main.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

$(BUILD)/lb_tests: $(LIB_OBJ) $(BUILD)/tests/test_main.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

test: $(BUILD)/lb_tests
	./$(BUILD)/lb_tests

setup:
	python3 -m venv .venv
	$(PY) -m pip install --upgrade pip -q
	$(PY) -m pip install -r requirements.txt -q
	@echo "Python environment ready (.venv)."

require-system:
	@if [ -z "$(SYSTEM)" ]; then echo "error: pass SYSTEM=<your-machine-label>, e.g. make $(MAKECMDGOALS) SYSTEM=alice-m2"; exit 1; fi

smoke: build require-system
	$(PY) scripts/run_campaign.py --system $(SYSTEM) --profile smoke $(ARGS)

campaign: build require-system
	$(PY) scripts/run_campaign.py --system $(SYSTEM) --profile $(PROFILE) $(ARGS)

trace: build require-system
	$(PY) scripts/run_campaign.py --system $(SYSTEM) --profile $(PROFILE) --only chunks $(ARGS)

check: require-system
	$(PY) scripts/check_results.py --system $(SYSTEM) --profile $(PROFILE)

aggregate:
	$(PY) scripts/aggregate.py $(ARGS)

plots: aggregate
	$(PY) scripts/plots/make_all.py $(ARGS)

clean:
	rm -rf $(BUILD)
