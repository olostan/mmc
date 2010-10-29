package Conf;

# Configuration defaults

$Conf::char='/';		# command char, don't forget to update
				# Parser::cmdre if you change this at runtime
$Conf::sep=';';			# Command separator, cannot be changed at runtime
$Conf::defprompt="\003Cmmc> ";	# default prompt when the client is not
				# connected to a server
$Conf::incolor=11;		# user input color, be sure to call
				# CL::set_iattr() if you change this at runtime
$Conf::iccolor=15;		# control chars color
$Conf::icbg=1;			# control chars background
$Conf::statusbg=4;		# status line background
$Conf::statusfg=2;		# status line default foreground
$Conf::send_verbose=1;		# display all text that gets sent to the server
$Conf::verbose=1;		# display various sucky messages
$Conf::status_type=2;		# status line position
$Conf::status_height=1;		# number of status lines
$Conf::save_stuff=1;		# automatically save triggers, aliases, keybindings,
				# variables
$Conf::ansi_log=0;		# write ansi escapes into logs if true
$Conf::speedwalk_delay=500;	# delay for 5 rooms
$Conf::logsub=1;		# log lines _after_ substitutions take place
$Conf::skipws=0;		# ignore whitespace at start of command when searching
				# for aliases
$Conf::timedlog=0;		# timestamp each logged line
$Conf::prefixall=0;		# prefix ALL commands, even from triggers and aliases
$Conf::hideinput=0;		# don't copy input line to main window when processing newline
$Conf::fullinput=0;		# don't truncate the input line and prompt when copying to main win

1;
