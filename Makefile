# C HTTP Client + Flask Checker Makefile

VENV = .venv
VENV_PYTHON3 = $(VENV)/bin/python3
CC = gcc
CFLAGS = -Wall -Wextra -std=c99
SRC = client.c  parson.c
BIN = client

ADMIN ?= biancaa.mircea:a11aa21e7446
PROGRAM ?= ./$(BIN)

.PHONY: all venv deps build clean run

all: venv deps build

venv: $(VENV_PYTHON3)

$(VENV_PYTHON3):
	python3 -m venv "$(VENV)"

deps: venv
	$(VENV_PYTHON3) -m pip install -r requirements.txt

build:
	$(CC) $(CFLAGS) $(SRC) -o $(BIN)

A ?= --debug --admin "$(ADMIN)"
run:
	$(VENV_PYTHON3) checker.py $(PROGRAM) $(A)

clean:
	rm -rf $(BIN) $(VENV) $(VENV_PYTHON3)