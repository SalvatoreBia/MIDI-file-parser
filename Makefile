CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -Iinclude
LDFLAGS = 

SRCDIR = src
INCDIR = include
OBJDIR = obj

SOURCES = main.c $(SRCDIR)/midi_parser.c $(SRCDIR)/json_generator.c
OBJECTS = $(SOURCES:.c=.o)

TARGET = midi_parser

all: $(TARGET)

$(OBJDIR):
	mkdir -p $(OBJDIR)

$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) -o $(TARGET) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJECTS) $(TARGET)

rebuild: clean all

install: $(TARGET)
	cp $(TARGET) /usr/local/bin/

uninstall:
	rm -f /usr/local/bin/$(TARGET)

help:
	@echo "Available targets:"
	@echo "  all      - Build the executable (default)"
	@echo "  clean    - Remove object files and executable"
	@echo "  rebuild  - Clean and build"
	@echo "  install  - Install to /usr/local/bin/"
	@echo "  uninstall- Remove from /usr/local/bin/"
	@echo "  help     - Show this help message"

.PHONY: all clean rebuild install uninstall help