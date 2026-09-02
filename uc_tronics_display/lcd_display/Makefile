TATGET     := display
CLI_TARGET := display-cli
CC         := gcc

OBJ := obj

mkfile_path := $(shell pwd)/$(lastword $(MAKEFILE_LIST))
dir=$(shell dirname $(mkfile_path))
$(shell mkdir -p $(dir)/$(OBJ))

SRCDIRS :=  project/ \
			hardware/rpiInfo \
			hardware/st7735  

SRCS := $(foreach dir, $(SRCDIRS), $(wildcard $(dir)/*.c))
NOT_DIR :=$(notdir $(SRCS))
OBJS := $(patsubst %.c, $(OBJ)/%.o, $(NOT_DIR))

# Each binary brings its own main(), so neither may be linked into the
# other. Everything else is shared.
MAIN_OBJS   := $(OBJ)/display.o $(OBJ)/display_cli.o
COMMON_OBJS := $(filter-out $(MAIN_OBJS), $(OBJS))

INCLUDE := $(patsubst %, -I %, $(SRCDIRS))

VPATH := $(SRCDIRS)

all: $(TATGET) $(CLI_TARGET)

$(TATGET):$(OBJ)/display.o $(COMMON_OBJS)
	$(CC) -o $@ $^

# The CLI only touches the message file, so it needs none of the display
# or font code.
$(CLI_TARGET):$(OBJ)/display_cli.o $(OBJ)/message.o
	$(CC) -o $@ $^

# -MMD -MP has the compiler record which headers each object used, so that
# editing a header rebuilds what depends on it. Without this a header
# change leaves stale objects behind and the binary silently keeps the old
# value compiled into it.
$(OBJS) : obj/%.o : %.c
	$(CC) -c $(INCLUDE) -MMD -MP -o $@ $<

-include $(OBJS:.o=.d)


# No sudo: make builds these as the invoking user, so removing them needs
# no more privilege than creating them did. It also failed silently where
# sudo needs a password and nothing was reading the exit status, which left
# a "clean" build using objects that were never rebuilt.
clean:
	rm -rf $(OBJ)
	rm -rf $(TATGET) $(CLI_TARGET)

.PHONY: all clean