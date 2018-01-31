.PHONY: clean

DIR=$(CURDIR)
INCDIR=$(DIR)/include
LIBDIR=$(DIR)/lib
OBJDIR=$(DIR)/build

CC=g++
CFLAGS=-std=c++1y -O2 -I$(INCDIR) -Wall -Wextra

SRCS=$(DIR)/lmb_c.cpp $(shell find $(LIBDIR) -name '*.cpp')
OBJS=$(SRCS:$(DIR)%.cpp=$(OBJDIR)%.o)
RTO=$(OBJDIR)/lib/runtime.o

LMBC=$(OBJDIR)/lmb_c

$(LMBC): $(OBJS)
	$(CC) $(CFLAGS) $^ -o $@

$(OBJDIR)/%.o: $(DIR)/%.cpp
	@mkdir -p `dirname "$@"`
	$(CC) $(CFLAGS) -c $< -o $@

%.prog.cpp: %.lmb $(LMBC)
	$(LMBC) $< $@

%: %.prog.cpp $(RTO)
	$(CC) $(CFLAGS) $^ -o $@

clean:
	rm -rf $(LMBC) $(OBJS)
