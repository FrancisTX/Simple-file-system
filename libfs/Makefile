# Target library
lib := libfs.a

o_file := fs.o disk.o

CC := gcc
CFALGS := -Wall -wextra -Werror


# Include dependencies
deps := $(patsubst %.o,%.d,$(o_file))
-include $(deps)

DEPFLAGS = -MMD -MF $(@:.o=.d)

all: $(lib)

$(lib): $(o_file)
	ar rcs $@ $^

%.o: %.c
	@echo "CC    $@"
	@$(CC) $(CFLAGS) -c -o $@ $< $(DEPFLAGS)


clean:
	@echo "CLEAN"
	@rm -f $(lib) $(o_file) $(deps)
