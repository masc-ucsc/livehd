#!/usr/bin/perl

# program to convert the floorplan file and FIG-like output 
# of HotSpot into a FIG file

#use warnings;

#params
#$maxx = 12600;
#$maxy = 9600;
$maxx = 9600;
$maxy = 12600;
$res = 1200;
#aspect ratio of blocks to give vertical labels
$skinny = 3;

sub usage () {
    print("usage: tofig.pl [-a <area ratio>] [-f <fontsize>] [-s <nskip>] <file>\n");
	print("converts the floorplan file into 'FIG' format and prints to stdout\n");
    print("[-a <area ratio>] -- approx page occupancy by figure (default 0.95)\n");
    print("[-f <fontsize>]   -- font size to be used (default 10)\n");
    print("[-s <nskip>]      -- no. of entries to be skipped in input (default 0)\n");
    print("<file>            -- input file (eg: ev6.flp or output of print_flp_fig function)\n");
    exit(1);
}

&usage() if (@ARGV > 7 || !@ARGV % 2 || ! -f $ARGV[@ARGV-1]);

$occupancy = 0.95;
$fontsize = 10;
$nskip = 0;

for($i=0; $i < @ARGV-1; $i+=2) {
	if ($ARGV[$i] eq "-a") {
		$occupancy=$ARGV[$i+1];
		next;
	}

	if ($ARGV[$i] eq "-f") {
		$fontsize=$ARGV[$i+1];
		next;
	}

	if ($ARGV[$i] eq "-s") {
		$nskip=$ARGV[$i+1];
		next;
	}

	&usage();
	exit(1);
}

open (FILE, "<$ARGV[@ARGV-1]") || die("error: file $ARGV[@ARGV-1] could not be opened\n");
$maxfig = -inf;
$minfig = inf;
$figinput = 0;
while (<FILE>) {
	if (/FIG starts/) {
		$figinput = 1;
		last;
	}
}

# This is a HotSpot floorplan file and not an output of print_flp_fig. 
# So let us generate the print_flp_fig output ourselves and save it in a 
# temporary file.
if (!$figinput) {
	seek(FILE, 0, 0);
	$timestamp = time();
	$file = "$ARGV[@ARGV-1].$timestamp";
	open(NEWFILE, ">$file") || die("error: file $file could not be created\n");
	print(NEWFILE "FIG starts\n");
	while (<FILE>) {
		# skip comments and empty lines
		next if (/^\s*#|^\s*$/);
		chomp;
		@strs = split(/\s+/);
 		if (@strs != 3 && @strs != 5) {
			unlink($file);
			die ("error: wrong floorplan input format\n");
		}	
		# skip connectivity information
		next if (@strs == 3);
		($name, $width, $height, $leftx, $bottomy) = @strs;
		$rightx = $leftx + $width;
		$topy = $bottomy + $height;
		printf(NEWFILE "%.16f %.16f %.16f %.16f %.16f %.16f %.16f %.16f %.16f %.16f\n", 
			    $leftx, $bottomy, $leftx, $topy, $rightx, $topy, $rightx, $bottomy, 
				$leftx, $bottomy);
		printf(NEWFILE "%s\n", $name);
	}
	print(NEWFILE "FIG ends\n");
	close(NEWFILE);
	close(FILE);
	open(FILE, "<$file") || die("error: file $file could not be opened\n");
	while (<FILE>) {
		last if (/FIG starts/);
	}	
}

$pos=tell(FILE);
$j=0; 
while (<FILE>) {
	last if (/FIG ends/);
	next if (/[a-zA-Z_]|^\s*$/);
	if ($j < $nskip) {
		$j++;
		next;
	}
	chomp;
	@nums = split(/\s+/);
	for ($i=0; $i < @nums; $i++) {
		$maxfig = $nums[$i] if ($nums[$i] > $maxfig);
		$minfig = $nums[$i] if ($nums[$i] < $minfig);
	}
}

$maxfig-=$minfig;
$scale = (($maxx < $maxy)?$maxx:$maxy) / $maxfig * sqrt($occupancy);
$xorig = ($maxx-$maxfig*$scale)/2;
$yorig = ($maxy-$maxfig*$scale)/2;

print "#FIG 3.1\nPortrait\nCenter\nInches\n$res 2\n";
seek(FILE, $pos, 0);

$j=0;
while (<FILE>) {
	last if (/FIG ends/);
	if ($j < $nskip * 2) {
		$j++;
		next;
	}	
	chomp;
	@coords = split(/\s+/);
	next if ($#coords == -1);
	@coords = map($_-$minfig, @coords);
	@coords = map($_*$scale, @coords);
	$leftx = $rightx = $coords[0];
	$bottomy = $topy = $coords[1];
	for($i=2; $i < @coords; $i++) {
		if ($i % 2) {
			$bottomy = $coords[$i] if ($coords[$i] < $bottomy);
			$topy = $coords[$i] if ($coords[$i] > $topy);
		} else {
			$leftx = $coords[$i] if ($coords[$i] < $leftx);
			$rightx = $coords[$i] if ($coords[$i] > $rightx);
		}
	}
	for ($i=0; $i < @coords; $i++) {
		if ($i % 2) {
			$coords[$i] = int($maxy - $coords[$i] - $yorig);
		}	
		else {
			$coords[$i] = int($coords[$i] + $xorig);
		}	
	}
	# Changed by GGF on 4/25/2012 to make lines of width 2.
	printf("2 2 0 2 -1 7 0 0 -1 0.000 0 0 0 0 0 %d\n", int (@coords)/2);
	print "\t@coords\n";
	$xpos = int($xorig + ($leftx + $rightx + 0.5) / 2.0);
	$ypos = int($maxy - (($bottomy + $topy + 0.5) / 2.0) - $yorig);
	$angle = 0;
	# Figure out if the text is vertical.
	# If so, change the angle.
	# Changed by GGF on 4/25/2012 to do the pos fixups.
	# While we're at it, make the start positions take the font height into account.
	if (($topy - $bottomy) > $skinny * ($rightx - $leftx))
	{
	    $angle = 1.5708;
	    $xpos += int($res*($fontsize/288.0) + 0.5);
	}
	else
	{
	    $ypos += int($res*($fontsize/288.0) + 0.5);
	}
	$name = <FILE>;
	chomp($name);
	# Changed by GGF on 4/25/2012 to make font bold.
	print "4 1 -1 0 0 2 $fontsize $angle 4 -1 -1 $xpos $ypos $name\\001\n";
}
close(FILE);

# delete the temporary file created
unlink ($file) if (!$figinput); 
