# cpphdl

C++HDL Hardware Description Language

Slides: https://github.com/user-attachments/files/24297702/cpphdl.pdf

Spec: https://github.com/67d1dfe4-fefb-4882-8ef9-47f06e643818

# build

Win64 requires the following to be done:
 - Install msys2-x86_64-20240727.exe, Miniconda3-py39_24.7.1-0-Windows-x86_64.exe, run MSYS2 MSYS console
 - git clone https://github.com/mirekez/cpphdl; cd cpphdl

And for Linux:
 - git clone ssh://github.com/mirekez/cpphdl; cd cpphdl
 - wget https://repo.anaconda.com/miniconda/Miniconda3-latest-Linux-x86_64.sh; ./Miniconda3-latest-Linux-x86_64.sh
 - source ~/miniconda3/bin/activate; conda init

Then for both Win&Lin:
 - conda create -p ./.conda; source activate base; conda activate ./.conda; conda env update --file requirements.yaml
 - mkdir build; cd build; cmake -DCMAKE_BUILD_TYPE=Debug -G "Unix Makefiles" ..; make

# author

This software is developed by Mike Reznikov (https://www.linkedin.com/in/mike-reznikov) based on the results of own research.

This work is not subsidized or paid.

# development plan

- VCD file dumping
- asynchronous resets
