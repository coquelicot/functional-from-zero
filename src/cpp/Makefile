.PHONY: clean

DIR=$(CURDIR)
INCDIR=$(DIR)/include
LIBDIR=$(DIR)/lib
OBJDIR=$(DIR)/build

CC=g++
CFLAGS=-std=c++11 -I$(INCDIR) -Wall -Wextra -O2

SRCS=$(DIR)/lmb_c.cpp $(shell find $(LIBDIR) -name '*.cpp')
OBJS=$(SRCS:$(DIR)%.cpp=$(OBJDIR)%.o)

LMBC=$(OBJDIR)/lmb_c

$(LMBC): $(OBJS)
	$(CC) $(CFLAGS) $^ -o $@

$(OBJDIR)/%.o: $(DIR)/%.cpp
	@mkdir -p `dirname "$@"`
	$(CC) $(CFLAGS) -c $< -o $@

%.prog.cpp: %.lmb $(LMBC)
	$(LMBC) $< $@

%: %.prog.cpp
	$(CC) -fno-rtti $(CFLAGS) $^ -o $@

clean:
	rm -rf $(LMBC) $(OBJS)
