"""Read ELF diagnostics that can include raw bytes from vendor note sections."""
import subprocess


def readelf_output(tool, *arguments, env=None):
    command = list(map(str, (tool, *arguments)))
    result = subprocess.run(command, env=env, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, encoding='utf-8', errors='replace',
                            timeout=120)
    if result.returncode:
        raise RuntimeError(f'{command}:\n{result.stdout}')
    return result.stdout
