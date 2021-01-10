PROG	 = ArchFP
OBJS     = Main.o Floorplan.o MathUtil.o
GPP	 = g++ -Wall
CFLAGS	 = -O3 -g
#LDFLAGS = -lmcheck

$(PROG): $(OBJS) 
	$(GPP) $(LDFLAGS) -o $@ $(OBJS) $(LIBS)


%.o: %.cc Floorplan.hh MathUtil.hh
	$(GPP) -c $< $(CFLAGS) -o $@

clean:
	rm -f $(PROG) *.o
