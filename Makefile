CC = gcc
CFLAGS = -Wall -O2
LIBS = -lm
TARGET = main
SOURCE = main.c

.PHONY: all clean setup install test

all: $(TARGET)

$(TARGET): $(SOURCE)
	$(CC) $(CFLAGS) -o $(TARGET) $(SOURCE) $(LIBS)

setup: $(TARGET)
	pip3 install --user -r requirements.txt

install: setup
	@echo "Installation complete!"
	@echo "Usage: ./$(TARGET) file.mid && python3 midi_importer.py file.json"

clean:
	rm -f $(TARGET)

help:
	@echo "Available targets:"
	@echo "  all     - Compile the C parser"
	@echo "  setup   - Compile and install Python dependencies"
	@echo "  install - Complete setup with instructions"
	@echo "  clean   - Remove compiled binary"
	@echo "  help    - Show this help"
