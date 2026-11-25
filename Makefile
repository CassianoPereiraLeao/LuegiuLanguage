CC = gcc
TARGET = bin\luegiu.exe

SRC_DIR = src
BUILD_DIR = build

WARNINGS = -Wall -Wextra -Wpedantic

CFLAGS = \
    $(WARNINGS) \
    -MMD -MP

DEBUG_FLAGS = -g -O0
RELEASE_FLAGS = -O3 -DNDEBUG

SRC = \
    $(wildcard src/*.c) \
	$(wildcard src/arena/*.c) \
    $(wildcard src/lexer/*.c) \
    $(wildcard src/parser/*.c) \
    $(wildcard src/helpers/*.c) \
    $(wildcard src/ir/*.c) \
    $(wildcard src/codegen/*.c) \
	$(wildcard src/diagnostics/*.c) \
	$(wildcard src/preprocess/*.c)

OBJ = $(SRC:.c=.o)
OBJ := $(subst src,build,$(OBJ))

DEP = $(OBJ:.o=.d)

all: debug

debug: CFLAGS += $(DEBUG_FLAGS)
debug: $(TARGET)

release: CFLAGS += $(RELEASE_FLAGS)
release: $(TARGET)

$(TARGET): $(OBJ)
	@if not exist bin mkdir bin
	$(CC) $(OBJ) -o $@

build/%.o: src/%.c
	@if not exist build mkdir build
	@if not exist build\lexer mkdir build\lexer
	@if not exist build\arena mkdir build\arena
	@if not exist build\parser mkdir build\parser
	@if not exist build\helpers mkdir build\helpers
	@if not exist build\ir mkdir build\ir
	@if not exist build\diagnostic mkdir build\diagnostic
	@if not exist build\preprocess mkdir build\preprocess
	@if not exist build\codegen mkdir build\codegen

	$(CC) $(CFLAGS) -c $< -o $@

run: debug
	$(TARGET)

clean:
	@if exist build rmdir /S /Q build
	@if exist bin rmdir /S /Q bin

rebuild: clean all

test-preprocess-source: all
	.\bin\luegiu.exe flag-pp .\tests\teste_preprocess.luegiu flag-out .\debug\preprocess.luegiu.preprocess

test-prototype: all
	.\bin\luegiu.exe flag-lex .\tests\teste_prototype.luegiu flag-out .\debug\protytype.teste.lex

test-lexer-keywords: all
	.\bin\luegiu.exe flag-lex .\tests\teste_keywords.luegiu flag-out .\debug\verify_keywords.luegiu.lex

test-lexer-operators: all
	.\bin\luegiu.exe flag-lex .\tests\teste_operators.luegiu flag-out .\debug\verify_operators.luegiu.lex

test-parser: all
	.\bin\luegiu.exe flag-parse .\tests\teste_parser.luegiu flag-out .\debug\verify_parser.luegiu.parser

test-check-only-program: all
	.\bin\luegiu.exe flag-check .\tests\teste_typecheck.luegiu

test-ir-only-program: all
	.\bin\luegiu.exe flag-ir .\tests\teste_ir.luegiu

test-all-tags-program: all
	.\bin\luegiu.exe -bp -pp -lex -parse -check -ir .\tests\teste_all.luegiu

test-no-debug: all
	.\bin\luegiu.exe .\tests\teste_all.luegiu -out .\debug\out.asm

test-print: all
	.\bin\luegiu.exe -bp -preprocess .\bootstrap\src\main.luegiu .\bootstrap\src\lexer\lexer.luegiu

lib-test: all
	.\bin\luegiu.exe .\bootstrap\src\main.luegiu .\bootstrap\lib\mmemory.luegiu

v-test: all
	.\bin\luegiu.exe .\uses\main.luegiu .\bin\stdlib\io.luegiu .\bin\stdlib\string.luegiu .\bin\stdlib\memory.luegiu

bootstrap: all
	.\bin\luegiu.exe .\bootstrap\parser\fold.luegiu .\bootstrap\helpers\string_helper.luegiu .\bootstrap\parser\parser.luegiu .\bootstrap\lexer\lexer.luegiu .\bootstrap\main.luegiu .\bootstrap\preprocess\preprocess.luegiu .\bootstrap\arena\arena.luegiu .\bootstrap\diagnostic\diagnostic.luegiu flag-out .\bootstrap\bin\luegiu.exe

testing-bootstrap: bootstrap
	.\bootstrap\bin\luegiu.exe .\bootstrap\main.luegiu

parser-asm-testing: all
	.\bin\luegiu.exe .\bootstrap\diagnostic\diagnostic.luegiu .\tests\sla.luegiu .\bootstrap\preprocess\preprocess.luegiu .\bootstrap\helpers\string_helper.luegiu .\bootstrap\arena\arena.luegiu .\bootstrap\lexer\lexer.luegiu flag-codegen .\bootstrap\parser\parser.luegiu

-include $(DEP)

.PHONY: all debug release run clean rebuild