# Modern MacOS Native AppleSoft BASIC

I'd like to run AppleSoft BASIC, but on my modern MacOS computer.

I want it to run as a native, compiled application - NOT emulated.

I want:
    an editor window, where I can edit the source code.
    a run window, where the running program's input/output goes to/comes from.

syntax checking and highlighting is automatically performed.

When I run a program, its output appears in another window, which behaves like the Apple II text screen, and uses the Apple II font for text - but which also supports GR and HGR graphics modes. More on this below.

This appears to be a disassembly of the 1978 version. There were some very minor changes made later, such as to add lowercase support. This will be your golden bible:
https://github.com/cmosher01/Apple-II-Source/blob/master/src/system/applesoft/applesoft.m4

The code window will have RUN, PAUSE, and STOP buttons, as well as single-step debugger.


## Editor

Code shall be tokenized exactly like AppleSoft BASIC does it, each time a line is edited. 

new line numbers gapped by 10 automatically generated.

Renumbering on user command.

For each program line, keep the tokenized form. When a line starts to be edited, convert it to ASCII, but retokenize each character changed - this drives the syntax highlighting and live-syntax-checking.

When the program is run, the interpreter does the following:
    creates a hash table of all GOSUB And GOTO targets for fast line lookups;
    otherwise walks the program line by line.

You can recommend a data structure but I'm thinking a linked list.

Each line has a toggle to the left of the line number, that is "set breakpoint";

Open, Close, Save, Save As menu items. File is saved to disk in tokenized form only, AppleSoft BASIC compatible so that the exact file could run on an Apple II.

## Variables

AppleSoft supports string; char (type of string? clarify); integer; and floating point variables, with appropriate suffixes (e.g. A% is integer, A$ is string). Variables can only be two characters. For now, limit to 2 but do not require the backend variable storage mechanism.

Since there are at most 1000-ish variables of each type, we can either have a hash, or, just pre-define a lookup table for each possible variable name and type.


### Syntax highlighting

highlight reserved words in one color;
variables in another color;
everything else in plain black.
A line that is a GOSUB or GOTO target will be highlighted in some way.

## Output / Run Window

## Graphics Output

support GR; HGR keywords and associated color and plot.
These will render using modern graphics. E.g. hgr is 280x192. (Scaled to whatever the run window size is).

Now, we will also have: GR(40,40); GR(80x80); HGR(640x400); 
These modes both support the same 16-color palette.
(Normal legacy GR is 16 colors; legacy HGR is 8 "colors");

Text in split mode is rendered using Apple II font, with screen split just like an Apple II into top graphics portion and bottom text portion.

Graphics are drawn clean, without Apple II weird artifacts. Implement the spirit of the modes, not the gory 1977 details.


## Peek / Poke Support

Peek and Poke will either have to be:
1) ignored;
or 2) "decoded" perhaps for certain well-known .

CALL commands will be tokenized but throw errors.

# Build System

Build with clang (present on system).
We'll edit here in Cursor.
