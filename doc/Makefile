# conda install -c conda-forge r-tinytex
# conda install python=3.7

all:
	pandoc spec.md -o cpphdl.pdf --pdf-engine=lualatex -H code-style.tex --lua-filter=use_minted.lua --pdf-engine-opt=-shell-escape -V geometry:margin=2.5cm -V fontsize=12pt
